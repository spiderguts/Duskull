// Duskull
// gtl_calibration.cpp
// Created by spiderguts on 3/29/26 at 11:58 PM.
// Copyright © 2026 spiderguts. All rights reserved.

#include "gtl_calibration.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#include "console.h"
#include "macos_native.h"

namespace gtl::calibration
{
    namespace
    {
        std::string trim(const std::string &in)
        {
            const auto start = std::find_if_not(in.begin(), in.end(), [](unsigned char ch)
                                                { return std::isspace(ch); });
            const auto end = std::find_if_not(in.rbegin(), in.rend(), [](unsigned char ch)
                                              { return std::isspace(ch); })
                                 .base();
            return (start < end) ? std::string(start, end) : std::string();
        }

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

            clampPointX(coords.refreshX);
            clampPointY(coords.refreshY);
            clampPointX(coords.buyX);
            clampPointY(coords.buyY);
            clampPointX(coords.cropX);
            clampPointY(coords.cropY);

            const long long maxCropWidth = std::max(1LL, metrics.widthPoints - coords.cropX);
            const long long maxCropHeight = std::max(1LL, metrics.heightPoints - coords.cropY);

            const long long originalCropWidth = coords.cropWidth;
            const long long originalCropHeight = coords.cropHeight;
            coords.cropWidth = clamp(coords.cropWidth, 1LL, maxCropWidth);
            coords.cropHeight = clamp(coords.cropHeight, 1LL, maxCropHeight);
            adjusted = adjusted || (originalCropWidth != coords.cropWidth) || (originalCropHeight != coords.cropHeight);

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
                return coords.refreshX > metrics.widthPoints ||
                       coords.buyX > metrics.widthPoints ||
                       coords.cropX > metrics.widthPoints ||
                       coords.refreshY > metrics.heightPoints ||
                       coords.buyY > metrics.heightPoints ||
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

            coords.refreshX = scaleDown(coords.refreshX);
            coords.refreshY = scaleDown(coords.refreshY);
            coords.buyX = scaleDown(coords.buyX);
            coords.buyY = scaleDown(coords.buyY);
            coords.cropX = scaleDown(coords.cropX);
            coords.cropY = scaleDown(coords.cropY);
            coords.cropWidth = scaleDown(coords.cropWidth);
            coords.cropHeight = scaleDown(coords.cropHeight);

            note = "Retina scale detected. Converted calibration from pixel-style values to display points.";
        }

        bool writeFileAtomically(const std::filesystem::path &targetPath,
                                 const std::string &content,
                                 std::string &error)
        {
            const std::filesystem::path parent = targetPath.parent_path();
            if (!parent.empty())
            {
                std::error_code dirError;
                std::filesystem::create_directories(parent, dirError);
                if (dirError)
                {
                    error = "Failed to create directory: " + dirError.message();
                    return false;
                }
            }

            std::error_code statusError;
            const auto targetStatus = std::filesystem::symlink_status(targetPath, statusError);
            if (!statusError && std::filesystem::exists(targetStatus) && std::filesystem::is_symlink(targetStatus))
            {
                error = "Refusing to write through symlinked target: '" + targetPath.string() + "'.";
                return false;
            }

            std::random_device rd;
            std::stringstream suffix;
            suffix << ".tmp." << std::hex << rd();
            const std::filesystem::path tempPath = targetPath.string() + suffix.str();

            {
                std::ofstream tempFile(tempPath, std::ios::out | std::ios::trunc);
                if (!tempFile)
                {
                    error = "Failed to open temp file for writing: '" + tempPath.string() + "'.";
                    return false;
                }

                tempFile << content;
                if (!tempFile.good())
                {
                    std::filesystem::remove(tempPath);
                    error = "Failed while writing temp file: '" + tempPath.string() + "'.";
                    return false;
                }
            }

            std::error_code replaceError;
            std::filesystem::rename(tempPath, targetPath, replaceError);
            if (!replaceError)
            {
                return true;
            }

            std::error_code removeError;
            std::filesystem::remove(targetPath, removeError);
            std::error_code secondRenameError;
            std::filesystem::rename(tempPath, targetPath, secondRenameError);
            if (!secondRenameError)
            {
                return true;
            }

            std::filesystem::remove(tempPath);
            error = "Failed to atomically replace file '" + targetPath.string() + "': " + secondRenameError.message();
            return false;
        }
    }

    std::filesystem::path defaultCoordinateConfigPath()
    {
        return std::filesystem::path("CSV_Files") / "duskull_coords.cfg";
    }

    bool loadCoordinatesFromEnv(CoordinateSet &coords, std::string &diagnostic)
    {
        const auto parseInt = [](const char *name) -> std::optional<long long>
        {
            const char *value = std::getenv(name);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            try
            {
                return std::stoll(value);
            }
            catch (...)
            {
                return std::nullopt;
            }
        };

        std::vector<std::pair<const char *, long long *>> requiredCoords{
            {"DUSKULL_REFRESH_X", &coords.refreshX},
            {"DUSKULL_REFRESH_Y", &coords.refreshY},
            {"DUSKULL_BUY_X", &coords.buyX},
            {"DUSKULL_BUY_Y", &coords.buyY},
            {"DUSKULL_FIRST_ROW_CROP_X", &coords.cropX},
            {"DUSKULL_FIRST_ROW_CROP_Y", &coords.cropY},
            {"DUSKULL_FIRST_ROW_CROP_W", &coords.cropWidth},
            {"DUSKULL_FIRST_ROW_CROP_H", &coords.cropHeight},
        };

        for (const auto &[envName, coordPtr] : requiredCoords)
        {
            const auto value = parseInt(envName);
            if (!value)
            {
                diagnostic += std::string(envName) + " not set or invalid. ";
                return false;
            }
            *coordPtr = *value;
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

        bool sawRefreshX = false;
        bool sawRefreshY = false;
        bool sawBuyX = false;
        bool sawBuyY = false;
        bool sawCropX = false;
        bool sawCropY = false;
        bool sawCropW = false;
        bool sawCropH = false;

        std::string line;
        while (std::getline(configFile, line))
        {
            const std::string cleaned = trim(line);
            if (cleaned.empty() || cleaned.front() == '#')
            {
                continue;
            }

            const auto equalsPos = cleaned.find('=');
            if (equalsPos == std::string::npos)
            {
                continue;
            }

            const std::string key = trim(cleaned.substr(0, equalsPos));
            const std::string valueText = trim(cleaned.substr(equalsPos + 1));
            long long parsedValue = 0;
            try
            {
                parsedValue = std::stoll(valueText);
            }
            catch (...)
            {
                diagnostic = "Invalid number for key '" + key + "' in config file.";
                return false;
            }

            if (key == "refresh_x")
            {
                coords.refreshX = parsedValue;
                sawRefreshX = true;
            }
            else if (key == "refresh_y")
            {
                coords.refreshY = parsedValue;
                sawRefreshY = true;
            }
            else if (key == "buy_x")
            {
                coords.buyX = parsedValue;
                sawBuyX = true;
            }
            else if (key == "buy_y")
            {
                coords.buyY = parsedValue;
                sawBuyY = true;
            }
            else if (key == "crop_x")
            {
                coords.cropX = parsedValue;
                sawCropX = true;
            }
            else if (key == "crop_y")
            {
                coords.cropY = parsedValue;
                sawCropY = true;
            }
            else if (key == "crop_w")
            {
                coords.cropWidth = parsedValue;
                sawCropW = true;
            }
            else if (key == "crop_h")
            {
                coords.cropHeight = parsedValue;
                sawCropH = true;
            }
        }

        if (!(sawRefreshX && sawRefreshY && sawBuyX && sawBuyY &&
              sawCropX && sawCropY && sawCropW && sawCropH))
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
        content << "refresh_x=" << coords.refreshX << "\n";
        content << "refresh_y=" << coords.refreshY << "\n";
        content << "buy_x=" << coords.buyX << "\n";
        content << "buy_y=" << coords.buyY << "\n";
        content << "crop_x=" << coords.cropX << "\n";
        content << "crop_y=" << coords.cropY << "\n";
        content << "crop_w=" << coords.cropWidth << "\n";
        content << "crop_h=" << coords.cropHeight << "\n";

        return writeFileAtomically(configPath, content.str(), error);
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

        if (!captureCursorPoint("1. Hover over the Refresh button", coords.refreshX, coords.refreshY))
            return;
        if (!captureCursorPoint("2. Hover over the Buy button", coords.buyX, coords.buyY))
            return;
        if (!captureCursorPoint("3. Hover over first-row OCR region TOP-LEFT", coords.cropX, coords.cropY))
            return;
        if (!captureCursorPoint("4. Hover over first-row OCR region BOTTOM-RIGHT", cropRightX, cropBottomY))
            return;

        if (cropRightX <= coords.cropX || cropBottomY <= coords.cropY)
        {
            console::printError("Invalid crop region. Bottom-right must be lower-right of top-left.");
            coords = CoordinateSet{};
            return;
        }

        coords.cropWidth = cropRightX - coords.cropX;
        coords.cropHeight = cropBottomY - coords.cropY;

        console::printSuccess("\nCapture complete! Values below can be used as environment overrides if needed:\n");
        std::cout << "export DUSKULL_REFRESH_X=" << coords.refreshX << "\n";
        std::cout << "export DUSKULL_REFRESH_Y=" << coords.refreshY << "\n";
        std::cout << "export DUSKULL_BUY_X=" << coords.buyX << "\n";
        std::cout << "export DUSKULL_BUY_Y=" << coords.buyY << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_X=" << coords.cropX << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_Y=" << coords.cropY << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_W=" << coords.cropWidth << "\n";
        std::cout << "export DUSKULL_FIRST_ROW_CROP_H=" << coords.cropHeight << "\n\n";
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
