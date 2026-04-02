// Duskull
// gtl_calibration.cpp
// Created by Duskull Project on 3/29/26 at 11:58 PM.
// Copyright © 2026 Duskull Project. All rights reserved.

#include "gtl_calibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "console.h"
#include "duskull_util.h"
#include "macos_native.h"

namespace gtl::calibration
{
    namespace
    {
        struct CoordinateFieldDescriptor
        {
            const char *envName;
            const char *configKey;
            long long CoordinateSet::*member;
        };

        constexpr std::array<CoordinateFieldDescriptor, 12> kCoordinateFields{{
            {"DUSKULL_REFRESH_MIN_X", "refresh_min_x", &CoordinateSet::refreshMinX},
            {"DUSKULL_REFRESH_MAX_X", "refresh_max_x", &CoordinateSet::refreshMaxX},
            {"DUSKULL_REFRESH_MIN_Y", "refresh_min_y", &CoordinateSet::refreshMinY},
            {"DUSKULL_REFRESH_MAX_Y", "refresh_max_y", &CoordinateSet::refreshMaxY},
            {"DUSKULL_BUY_MIN_X", "buy_min_x", &CoordinateSet::buyMinX},
            {"DUSKULL_BUY_MAX_X", "buy_max_x", &CoordinateSet::buyMaxX},
            {"DUSKULL_BUY_MIN_Y", "buy_min_y", &CoordinateSet::buyMinY},
            {"DUSKULL_BUY_MAX_Y", "buy_max_y", &CoordinateSet::buyMaxY},
            {"DUSKULL_FIRST_ROW_CROP_X", "crop_x", &CoordinateSet::cropX},
            {"DUSKULL_FIRST_ROW_CROP_Y", "crop_y", &CoordinateSet::cropY},
            {"DUSKULL_FIRST_ROW_CROP_W", "crop_w", &CoordinateSet::cropWidth},
            {"DUSKULL_FIRST_ROW_CROP_H", "crop_h", &CoordinateSet::cropHeight},
        }};

        bool validateAndClampCoordinates(CoordinateSet &coords,
                                         const macos_native::DisplayMetrics &metrics,
                                         std::string &diagnostic)
        {
            if (metrics.widthPoints <= 0 || metrics.heightPoints <= 0)
            {
                diagnostic = "Primary display dimensions are invalid.";
                return false;
            }

            if (coords.cropWidth <= 0 || coords.cropHeight <= 0)
            {
                diagnostic = "Crop width/height must be positive.";
                return false;
            }

            const auto clamp = [](long long value, long long minValue, long long maxValue)
            {
                return std::max(minValue, std::min(value, maxValue));
            };

            bool adjusted = false;

            auto clampPointX = [&](long long &value)
            {
                const long long before = value;
                value = clamp(value, 0LL, metrics.widthPoints - 1);
                adjusted = adjusted || (before != value);
            };

            auto clampPointY = [&](long long &value)
            {
                const long long before = value;
                value = clamp(value, 0LL, metrics.heightPoints - 1);
                adjusted = adjusted || (before != value);
            };

            clampPointX(coords.refreshMinX);
            clampPointX(coords.refreshMaxX);
            clampPointY(coords.refreshMinY);
            clampPointY(coords.refreshMaxY);
            clampPointX(coords.buyMinX);
            clampPointX(coords.buyMaxX);
            clampPointY(coords.buyMinY);
            clampPointY(coords.buyMaxY);
            clampPointX(coords.cropX);
            clampPointY(coords.cropY);

            const long long maxCropWidth = std::max(1LL, metrics.widthPoints - coords.cropX);
            const long long maxCropHeight = std::max(1LL, metrics.heightPoints - coords.cropY);

            const long long originalCropWidth = coords.cropWidth;
            const long long originalCropHeight = coords.cropHeight;
            coords.cropWidth = clamp(coords.cropWidth, 1LL, maxCropWidth);
            coords.cropHeight = clamp(coords.cropHeight, 1LL, maxCropHeight);
            adjusted = adjusted || (originalCropWidth != coords.cropWidth) || (originalCropHeight != coords.cropHeight);

            if (coords.refreshMinX > coords.refreshMaxX ||
                coords.refreshMinY > coords.refreshMaxY ||
                coords.buyMinX > coords.buyMaxX ||
                coords.buyMinY > coords.buyMaxY)
            {
                diagnostic = "Click bounds are invalid. Ensure min coordinates do not exceed max coordinates.";
                return false;
            }

            if (adjusted)
            {
                diagnostic = "Coordinates were outside display bounds and were clamped automatically.";
            }

            return true;
        }

        void normalizeCoordinatesForDisplay(CoordinateSet &coords,
                                            const macos_native::DisplayMetrics &metrics,
                                            std::string &note)
        {
            const auto exceedsPointSpace = [&coords, &metrics]()
            {
                return coords.refreshMinX > metrics.widthPoints ||
                       coords.refreshMaxX > metrics.widthPoints ||
                       coords.buyMinX > metrics.widthPoints ||
                       coords.buyMaxX > metrics.widthPoints ||
                       coords.cropX > metrics.widthPoints ||
                       coords.refreshMinY > metrics.heightPoints ||
                       coords.refreshMaxY > metrics.heightPoints ||
                       coords.buyMinY > metrics.heightPoints ||
                       coords.buyMaxY > metrics.heightPoints ||
                       coords.cropY > metrics.heightPoints ||
                       (coords.cropX + coords.cropWidth) > metrics.widthPoints ||
                       (coords.cropY + coords.cropHeight) > metrics.heightPoints;
            };

            const bool looksPixelBased = exceedsPointSpace() &&
                                         metrics.scaleFactor > 1.1 &&
                                         coords.cropWidth <= metrics.widthPixels &&
                                         coords.cropHeight <= metrics.heightPixels;

            if (!looksPixelBased)
            {
                return;
            }

            const auto scaleDown = [scale = metrics.scaleFactor](long long value) -> long long
            {
                return static_cast<long long>(std::llround(static_cast<double>(value) / scale));
            };

            coords.refreshMinX = scaleDown(coords.refreshMinX);
            coords.refreshMaxX = scaleDown(coords.refreshMaxX);
            coords.refreshMinY = scaleDown(coords.refreshMinY);
            coords.refreshMaxY = scaleDown(coords.refreshMaxY);
            coords.buyMinX = scaleDown(coords.buyMinX);
            coords.buyMaxX = scaleDown(coords.buyMaxX);
            coords.buyMinY = scaleDown(coords.buyMinY);
            coords.buyMaxY = scaleDown(coords.buyMaxY);
            coords.cropX = scaleDown(coords.cropX);
            coords.cropY = scaleDown(coords.cropY);
            coords.cropWidth = scaleDown(coords.cropWidth);
            coords.cropHeight = scaleDown(coords.cropHeight);

            note = "Retina scale detected. Converted calibration from pixel-style values to display points.";
        }
    }

    std::filesystem::path defaultCoordinateConfigPath()
    {
        return std::filesystem::path("CSV_Files") / "duskull_coords.cfg";
    }

    bool loadCoordinatesFromEnv(CoordinateSet &coords, std::string &diagnostic)
    {
        for (const auto &field : kCoordinateFields)
        {
            const auto value = duskull::util::parseSignedInteger(duskull::util::readTrimmedEnv(field.envName));
            if (!value)
            {
                diagnostic += std::string(field.envName) + " not set or invalid. ";
                return false;
            }
            coords.*(field.member) = *value;
        }

        return true;
    }

    bool loadCoordinatesFromConfig(const std::filesystem::path &configPath,
                                   CoordinateSet &coords,
                                   std::string &diagnostic)
    {
        std::ifstream configFile(configPath);
        if (!configFile)
        {
            diagnostic = "Config file not found at '" + configPath.string() + "'.";
            return false;
        }

        std::array<bool, kCoordinateFields.size()> sawField{};

        std::string line;
        while (std::getline(configFile, line))
        {
            const std::string cleaned = duskull::util::trim(line);
            if (cleaned.empty() || cleaned.front() == '#')
            {
                continue;
            }

            const auto equalsPos = cleaned.find('=');
            if (equalsPos == std::string::npos)
            {
                continue;
            }

            const std::string key = duskull::util::trim(cleaned.substr(0, equalsPos));
            const std::string valueText = duskull::util::trim(cleaned.substr(equalsPos + 1));
            const auto parsedValue = duskull::util::parseSignedInteger(valueText);
            if (!parsedValue)
            {
                diagnostic = "Invalid number for key '" + key + "' in config file.";
                return false;
            }

            for (std::size_t index = 0; index < kCoordinateFields.size(); ++index)
            {
                if (key != kCoordinateFields[index].configKey)
                {
                    continue;
                }

                coords.*(kCoordinateFields[index].member) = *parsedValue;
                sawField[index] = true;
                break;
            }
        }

        if (std::any_of(sawField.begin(), sawField.end(), [](bool seen)
                        { return !seen; }))
        {
            diagnostic = "Config file is missing one or more required keys.";
            return false;
        }

        return true;
    }

    bool saveCoordinatesToConfig(const std::filesystem::path &configPath,
                                 const CoordinateSet &coords,
                                 std::string &error)
    {
        std::ostringstream content;
        content << "# Duskull coordinate calibration\n";
        for (const auto &field : kCoordinateFields)
        {
            content << field.configKey << "=" << coords.*(field.member) << "\n";
        }

        return duskull::util::writeTextFileAtomically(configPath, content.str(), error);
    }

    void promptCaptureCoordinates(CoordinateSet &coords)
    {
        console::printSuccess("\nCoordinate Capture Mode\n");
        console::printSuccess("Hover your mouse over each target, then press Enter to capture coordinates.\n");

        auto captureCursorPoint = [](std::string_view label, long long &x, long long &y) -> bool
        {
            std::cout << label << "\n"
                      << "  Place cursor and press Enter...";

            std::string line;
            if (!std::getline(std::cin, line))
            {
                console::printError("Input stream closed. Capture aborted.");
                return false;
            }

            std::string error;
            if (!macos_native::getCursorPosition(x, y, error))
            {
                console::printError("Could not capture cursor position: " + error);
                return false;
            }

            std::cout << "  Captured: (" << x << ", " << y << ")\n";
            return true;
        };

        long long cropRightX = 0;
        long long cropBottomY = 0;

        if (!captureCursorPoint("1. Hover over Refresh button TOP-LEFT corner", coords.refreshMinX, coords.refreshMinY))
            return;
        if (!captureCursorPoint("2. Hover over Refresh button BOTTOM-RIGHT corner", coords.refreshMaxX, coords.refreshMaxY))
            return;
        if (!captureCursorPoint("3. Hover over Buy button TOP-LEFT corner", coords.buyMinX, coords.buyMinY))
            return;
        if (!captureCursorPoint("4. Hover over Buy button BOTTOM-RIGHT corner", coords.buyMaxX, coords.buyMaxY))
            return;
        if (!captureCursorPoint("5. Hover over first-row OCR region TOP-LEFT", coords.cropX, coords.cropY))
            return;
        if (!captureCursorPoint("6. Hover over first-row OCR region BOTTOM-RIGHT", cropRightX, cropBottomY))
            return;

        if (coords.refreshMaxX <= coords.refreshMinX || coords.refreshMaxY <= coords.refreshMinY)
        {
            console::printError("Invalid refresh button bounds. Bottom-right must be lower-right of top-left.");
            coords = CoordinateSet{};
            return;
        }

        if (coords.buyMaxX <= coords.buyMinX || coords.buyMaxY <= coords.buyMinY)
        {
            console::printError("Invalid buy button bounds. Bottom-right must be lower-right of top-left.");
            coords = CoordinateSet{};
            return;
        }

        if (cropRightX <= coords.cropX || cropBottomY <= coords.cropY)
        {
            console::printError("Invalid crop region. Bottom-right must be lower-right of top-left.");
            coords = CoordinateSet{};
            return;
        }

        coords.cropWidth = cropRightX - coords.cropX;
        coords.cropHeight = cropBottomY - coords.cropY;

        console::printSuccess("\nCapture complete! Values below can be used as environment overrides if needed:\n");
#if defined(_WIN32)
        std::cout << "set DUSKULL_REFRESH_MIN_X=" << coords.refreshMinX << "\n";
        std::cout << "set DUSKULL_REFRESH_MAX_X=" << coords.refreshMaxX << "\n";
        std::cout << "set DUSKULL_REFRESH_MIN_Y=" << coords.refreshMinY << "\n";
        std::cout << "set DUSKULL_REFRESH_MAX_Y=" << coords.refreshMaxY << "\n";
        std::cout << "set DUSKULL_BUY_MIN_X=" << coords.buyMinX << "\n";
        std::cout << "set DUSKULL_BUY_MAX_X=" << coords.buyMaxX << "\n";
        std::cout << "set DUSKULL_BUY_MIN_Y=" << coords.buyMinY << "\n";
        std::cout << "set DUSKULL_BUY_MAX_Y=" << coords.buyMaxY << "\n";
        std::cout << "set DUSKULL_FIRST_ROW_CROP_X=" << coords.cropX << "\n";
        std::cout << "set DUSKULL_FIRST_ROW_CROP_Y=" << coords.cropY << "\n";
        std::cout << "set DUSKULL_FIRST_ROW_CROP_W=" << coords.cropWidth << "\n";
        std::cout << "set DUSKULL_FIRST_ROW_CROP_H=" << coords.cropHeight << "\n\n";
#else
        std::cout << "export DUSKULL_REFRESH_MIN_X=" << coords.refreshMinX << "\n";
        std::cout << "export DUSKULL_REFRESH_MAX_X=" << coords.refreshMaxX << "\n";
        std::cout << "export DUSKULL_REFRESH_MIN_Y=" << coords.refreshMinY << "\n";
        std::cout << "export DUSKULL_REFRESH_MAX_Y=" << coords.refreshMaxY << "\n";
        std::cout << "export DUSKULL_BUY_MIN_X=" << coords.buyMinX << "\n";
        std::cout << "export DUSKULL_BUY_MAX_X=" << coords.buyMaxX << "\n";
        std::cout << "export DUSKULL_BUY_MIN_Y=" << coords.buyMinY << "\n";
        std::cout << "export DUSKULL_BUY_MAX_Y=" << coords.buyMaxY << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_X=" << coords.cropX << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_Y=" << coords.cropY << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_W=" << coords.cropWidth << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_H=" << coords.cropHeight << "\n\n";
#endif
    }

    bool prepareCoordinatesForCurrentDisplay(CoordinateSet &coords,
                                             bool saveAdjustedCoordinates,
                                             const std::filesystem::path &configPath)
    {
        macos_native::DisplayMetrics metrics;
        std::string displayError;
        if (!macos_native::getPrimaryDisplayMetrics(metrics, displayError))
        {
            console::printError("Could not read display metrics for calibration checks: " + displayError);
            return false;
        }

        std::string scaleNote;
        normalizeCoordinatesForDisplay(coords, metrics, scaleNote);
        if (!scaleNote.empty())
        {
            console::printSuccess(scaleNote);
        }

        std::string clampDiagnostic;
        if (!validateAndClampCoordinates(coords, metrics, clampDiagnostic))
        {
            console::printError("Calibration invalid: " + clampDiagnostic);
            return false;
        }

        if (!clampDiagnostic.empty())
        {
            console::printError(clampDiagnostic);
            if (saveAdjustedCoordinates)
            {
                std::string saveError;
                if (!saveCoordinatesToConfig(configPath, coords, saveError))
                {
                    console::printError("Adjusted coordinates could not be saved: " + saveError);
                }
            }
        }

        return true;
    }
}
