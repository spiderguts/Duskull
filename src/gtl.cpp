// Duskull
// gtl.cpp
// Created by Duskull Project on 3/22/26 at 12:41 PM.
// Copyright © 2026 Duskull Project. All rights reserved.

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "console.h"
#include "duskull_util.h"
#include "gtl.h"
#include "gtl_calibration.h"
#include "gtl_csv.h"
#include "gtl_match.h"
#include "macos_native.h"

namespace gtl
{
    namespace
    {
        namespace fs = std::filesystem;

        enum MainMenuAction
        {
            Start = 1,
            Settings = 2,
            Back = 0,
        };

        enum SettingsMenuAction
        {
            SelectCsv = 1,
            CreateCsv = 2,
            RecalibrateCoordinates = 3,
            SettingsBack = 0,
        };

        constexpr std::string_view kCsvHeader = "Pokemon,Nature,HP,Atk,Def,SpAtk,SpDef,Spd,Item,MaxBuy,MinIV\n";
        constexpr std::string_view kCsvDirectory = "CSV_Files";
        constexpr std::string_view kSelectedCsvFile = "selected_csv.cfg";
        constexpr std::string_view kIllegalChars = " \\/:*?\"<>|";

        const std::array<console::MenuItem, 3> kGtlMenuItems{{
            {Start, "Start"},
            {Settings, "Settings"},
            {Back, "Back"},
        }};

        const std::array<console::MenuItem, 4> kSettingsMenuItems{{
            {SelectCsv, "Select CSV"},
            {CreateCsv, "Create CSV"},
            {RecalibrateCoordinates, "Recalibrate Coordinates"},
            {SettingsBack, "Back"},
        }};

        bool isValidFileName(std::string_view name)
        {
            if (name.empty())
            {
                return false;
            }

            return std::none_of(name.begin(), name.end(),
                                [](unsigned char ch)
                                {
                                    return kIllegalChars.find(static_cast<char>(ch)) != std::string_view::npos;
                                });
        }

        bool ensureCsvDirectory()
        {
            const fs::path dirPath(kCsvDirectory);
            return fs::exists(dirPath) || fs::create_directories(dirPath);
        }

        std::string commaFormat(long long value)
        {
            const std::string raw = std::to_string(value);
            std::string out;
            out.reserve(raw.size() + raw.size() / 3);
            const std::size_t offset = raw.size() % 3;
            for (std::size_t i = 0; i < raw.size(); ++i)
            {
                if (i > 0 && i % 3 == offset)
                {
                    out.push_back(',');
                }
                out.push_back(raw[i]);
            }
            return out;
        }

        std::string colorizePriceByMaxBuy(long long listingPrice, long long maxBuy)
        {
            if (maxBuy <= 0)
            {
                return "$" + commaFormat(listingPrice);
            }

            const double ratio = static_cast<double>(listingPrice) / static_cast<double>(maxBuy);
            std::string_view color = console::kGreen;

            // Near max-buy is high (orange), mid-range is medium (yellow), low is good (green).
            if (ratio >= 0.90)
            {
                color = console::kOrange;
            }
            else if (ratio >= 0.70)
            {
                color = console::kYellow;
            }

            return std::string(color) + "$" + commaFormat(listingPrice) + std::string(console::kReset);
        }

        std::string colorizeExactThirtyOneTokens(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + 16);

            for (std::size_t i = 0; i < text.size();)
            {
                const bool isThirtyOne = i + 1 < text.size() && text[i] == '3' && text[i + 1] == '1';
                if (!isThirtyOne)
                {
                    out.push_back(text[i]);
                    ++i;
                    continue;
                }

                const bool hasLeftDigit = i > 0 && std::isdigit(static_cast<unsigned char>(text[i - 1]));
                const bool hasRightDigit = i + 2 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 2]));
                if (hasLeftDigit || hasRightDigit)
                {
                    out.push_back(text[i]);
                    ++i;
                    continue;
                }

                out.append(console::kGreen);
                out.append("31");
                out.append(console::kReset);
                i += 2;
            }

            return out;
        }

        struct IvParseResult
        {
            std::array<int, 6> ivs{0, 0, 0, 0, 0, 0};
            std::size_t detectedCount = 0;
        };

        IvParseResult extractIvStatsFromDetectedTexts(const std::vector<std::string> &detectedTexts)
        {
            std::vector<int> ivs;
            ivs.reserve(6);

            bool expectLevelNumber = false;
            std::string token;

            const auto flushToken = [&]()
            {
                if (token.empty())
                {
                    return;
                }

                const bool hasComma = token.find(',') != std::string::npos;
                if (!hasComma)
                {
                    try
                    {
                        const long long value = std::stoll(token);
                        if (expectLevelNumber)
                        {
                            expectLevelNumber = false;
                        }
                        else if (ivs.size() < 6)
                        {
                            ivs.push_back(static_cast<int>(value));
                        }
                    }
                    catch (const std::invalid_argument &)
                    {
                        // Ignore malformed token.
                    }
                    catch (const std::out_of_range &)
                    {
                        // Ignore malformed token.
                    }
                }

                token.clear();
            };

            for (const auto &line : detectedTexts)
            {
                for (std::size_t i = 0; i < line.size(); ++i)
                {
                    const char ch = line[i];

                    const bool isLvPrefix = std::tolower(static_cast<unsigned char>(ch)) == 'l' &&
                                            i + 1 < line.size() &&
                                            std::tolower(static_cast<unsigned char>(line[i + 1])) == 'v';
                    if (isLvPrefix)
                    {
                        expectLevelNumber = true;
                    }

                    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == ',')
                    {
                        token.push_back(ch);
                    }
                    else
                    {
                        flushToken();
                    }
                }

                flushToken();

                if (ivs.size() == 6)
                {
                    break;
                }
            }

            IvParseResult result;
            result.detectedCount = ivs.size();
            for (std::size_t i = 0; i < ivs.size() && i < result.ivs.size(); ++i)
            {
                result.ivs[i] = ivs[i];
            }

            return result;
        }

        std::string formatParsedIvSummary(const IvParseResult &ivData)
        {
            static constexpr std::array<std::string_view, 6> kIvLabels{
                "HP", "Atk", "Def", "SpAtk", "SpDef", "Spd"};

            std::ostringstream out;
            out << "IVs ";

            for (std::size_t i = 0; i < kIvLabels.size(); ++i)
            {
                if (i > 0)
                {
                    out << " ";
                }

                out << kIvLabels[i] << "=";
                if (ivData.ivs[i] == 31)
                {
                    out << console::kGreen << ivData.ivs[i] << console::kReset;
                }
                else
                {
                    out << ivData.ivs[i];
                }
            }

            return out.str();
        }

        std::string extractNatureFromDetectedTexts(const std::vector<std::string> &detectedTexts)
        {
            static const std::unordered_set<std::string> kNatures{
                "hardy", "lonely", "brave", "adamant", "naughty",
                "bold", "docile", "relaxed", "impish", "lax",
                "timid", "hasty", "serious", "jolly", "naive",
                "modest", "mild", "quiet", "bashful", "rash",
                "calm", "gentle", "sassy", "careful", "quirky"};

            for (const auto &line : detectedTexts)
            {
                // Extract each whitespace-delimited word from the token.
                std::string word;
                for (std::size_t i = 0; i <= line.size(); ++i)
                {
                    const bool end = i == line.size() || std::isspace(static_cast<unsigned char>(line[i]));
                    if (!end)
                    {
                        word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(line[i]))));
                    }
                    else if (!word.empty())
                    {
                        if (kNatures.count(word))
                        {
                            // Capitalize first letter for display.
                            word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
                            return word;
                        }
                        word.clear();
                    }
                }
            }

            return std::string();
        }

        std::string extractLevelFromDetectedTexts(const std::vector<std::string> &detectedTexts)
        {
            for (const auto &line : detectedTexts)
            {
                for (std::size_t i = 0; i + 1 < line.size(); ++i)
                {
                    const bool isLv = (line[i] == 'L' || line[i] == 'l') &&
                                      (line[i + 1] == 'v' || line[i + 1] == 'V');
                    if (!isLv)
                    {
                        continue;
                    }

                    std::size_t j = i + 2;
                    while (j < line.size() && (line[j] == '.' || line[j] == ' '))
                    {
                        ++j;
                    }

                    std::string digits;
                    while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j])))
                    {
                        digits.push_back(line[j++]);
                    }

                    if (!digits.empty())
                    {
                        return "Lv." + digits;
                    }
                }
            }

            return std::string();
        }

        std::string formatIvSlash(const IvParseResult &ivData)
        {
            std::ostringstream out;
            for (std::size_t i = 0; i < 6; ++i)
            {
                if (i > 0)
                {
                    out << "/";
                }

                if (ivData.ivs[i] == 31)
                {
                    out << console::kGreen << ivData.ivs[i] << console::kReset;
                }
                else
                {
                    out << ivData.ivs[i];
                }
            }
            return out.str();
        }

        std::string formatIvParseWarning(std::size_t detectedCount)
        {
            if (detectedCount >= 6)
            {
                return std::string();
            }

            std::ostringstream out;
            out << console::kYellow << "Warning: detected " << detectedCount
                << "/6 IV values before price; check OCR crop/order." << console::kReset;
            return out.str();
        }

        std::string summarizeDetectedDetails(const std::vector<std::string> &detectedTexts)
        {
            std::ostringstream out;
            bool wroteAny = false;

            for (const auto &text : detectedTexts)
            {
                const std::string cleaned = duskull::util::trim(text);
                if (cleaned.empty())
                {
                    continue;
                }

                if (wroteAny)
                {
                    out << " | ";
                }

                out << colorizeExactThirtyOneTokens(cleaned);
                wroteAny = true;
            }

            if (!wroteAny)
            {
                return std::string("n/a");
            }

            return out.str();
        }

        class ScopedAbortListener
        {
        public:
            bool start(std::string &error)
            {
                active_ = macos_native::startEmergencyAbortListener(error);
                if (active_)
                {
                    macos_native::clearEmergencyAbortRequested();
                }
                return active_;
            }

            ~ScopedAbortListener()
            {
                if (active_)
                {
                    macos_native::stopEmergencyAbortListener();
                }
            }

        private:
            bool active_ = false;
        };

        std::string readExpectedFrontmostAppOwner()
        {
            const char *raw = std::getenv("DUSKULL_EXPECTED_APP_OWNER");
            if (raw == nullptr)
            {
                raw = std::getenv("DUSKULL_EXPECTED_BUNDLE_ID"); // Backward-compatible alias.
            }
            if (raw == nullptr)
            {
                return std::string();
            }

            return duskull::util::trim(raw);
        }

        std::mt19937_64 &sniperRng()
        {
            static thread_local std::mt19937_64 generator([]()
                                                          {
                std::random_device rd;
                const auto now = static_cast<std::uint64_t>(
                    std::chrono::high_resolution_clock::now().time_since_epoch().count());
                return static_cast<std::uint64_t>(rd()) ^
                       (static_cast<std::uint64_t>(rd()) << 1ULL) ^
                       (now << 7ULL); }());
            return generator;
        }

        int nextRefreshDelayMs()
        {
            // Keep auto-refresh cadence human-like around 2s with mild jitter.
            std::uniform_int_distribution<int> delayMs(1850, 2350);
            return delayMs(sniperRng());
        }

        std::pair<long long, long long> jitterClickTarget(const calibration::ClickBounds &bounds,
                                                          const macos_native::DisplayMetrics &display,
                                                          std::string_view regionTag)
        {
            const long long maxXDisplay = std::max(0LL, display.widthPoints - 1);
            const long long maxYDisplay = std::max(0LL, display.heightPoints - 1);

            const auto clampToVisibleBounds = [&](long long x, long long y)
            {
                const long long clampedX = std::clamp(x, bounds.minX, bounds.maxX);
                const long long clampedY = std::clamp(y, bounds.minY, bounds.maxY);
                return std::make_pair(std::clamp(clampedX, 0LL, maxXDisplay),
                                      std::clamp(clampedY, 0LL, maxYDisplay));
            };

            const auto randomInRegion = [&]()
            {
                // Draw uniformly then nudge ~33% toward the region center — loose
                // center preference without locking tightly to the middle.
                std::uniform_int_distribution<long long> distX(bounds.minX, bounds.maxX);
                std::uniform_int_distribution<long long> distY(bounds.minY, bounds.maxY);
                const long long midX = (bounds.minX + bounds.maxX) / 2;
                const long long midY = (bounds.minY + bounds.maxY) / 2;
                const long long rawX = distX(sniperRng());
                const long long rawY = distY(sniperRng());
                const long long x = rawX + (midX - rawX) / 3;
                const long long y = rawY + (midY - rawY) / 3;
                return clampToVisibleBounds(x, y);
            };

            struct RegionAnchor
            {
                long long x = 0;
                long long y = 0;
                bool initialized = false;
            };

            static std::unordered_map<std::string, RegionAnchor> anchorsByRegion;
            static bool hasLastClick = false;
            static long long lastClickX = 0;
            static long long lastClickY = 0;
            static std::string lastRegionTag;

            if (bounds.minX == bounds.maxX && bounds.minY == bounds.maxY)
            {
                const auto fixedPoint = clampToVisibleBounds(bounds.minX, bounds.minY);
                hasLastClick = true;
                lastClickX = fixedPoint.first;
                lastClickY = fixedPoint.second;
                return fixedPoint;
            }

            RegionAnchor &anchor = anchorsByRegion[std::string(regionTag)];

            constexpr double kLongHopThresholdPx = 80.0;
            // Refresh clicks use 1-in-11 micro-drift during repeated clicks in the same region.
            const int rareJitterOdds = 11;
            constexpr int kRareJitterMaxPx = 2;

            const bool isConsecutiveSameRegion = hasLastClick && (lastRegionTag == regionTag);
            bool shouldResampleAnchor = !anchor.initialized || !isConsecutiveSameRegion;
            if (hasLastClick)
            {
                const double centerX = (static_cast<double>(bounds.minX) + static_cast<double>(bounds.maxX)) * 0.5;
                const double centerY = (static_cast<double>(bounds.minY) + static_cast<double>(bounds.maxY)) * 0.5;
                const double hopDistance = std::hypot(static_cast<double>(lastClickX) - centerX,
                                                      static_cast<double>(lastClickY) - centerY);
                if (hopDistance >= kLongHopThresholdPx)
                {
                    shouldResampleAnchor = true;
                }
            }

            if (shouldResampleAnchor)
            {
                const auto sampled = randomInRegion();
                anchor.x = sampled.first;
                anchor.y = sampled.second;
                anchor.initialized = true;
            }

            // Stay exactly on the anchor unless rare same-region jitter fires.
            long long jitteredX = anchor.x;
            long long jitteredY = anchor.y;
            if (isConsecutiveSameRegion)
            {
                std::uniform_int_distribution<int> oddsRoll(1, rareJitterOdds);
                if (oddsRoll(sniperRng()) == 1)
                {
                    std::uniform_int_distribution<int> jitterDist(-kRareJitterMaxPx, kRareJitterMaxPx);
                    jitteredX += static_cast<long long>(jitterDist(sniperRng()));
                    jitteredY += static_cast<long long>(jitterDist(sniperRng()));
                }
            }
            const auto finalPoint = clampToVisibleBounds(jitteredX, jitteredY);

            hasLastClick = true;
            lastRegionTag = std::string(regionTag);
            lastClickX = finalPoint.first;
            lastClickY = finalPoint.second;
            return finalPoint;
        }

        bool frontmostAppMatchesExpected(const std::string &expectedAppOwner,
                                         bool quietMode,
                                         const std::string &action)
        {
            if (expectedAppOwner.empty())
            {
                return true;
            }

            std::string activeBundleId;
            std::string appError;
            if (!macos_native::getFrontmostApplicationBundleId(activeBundleId, appError))
            {
                if (!quietMode)
                {
                    console::printError("Skipping " + action + ": could not read frontmost app bundle id: " + appError);
                }
                return false;
            }

            if (activeBundleId != expectedAppOwner)
            {
                if (!quietMode)
                {
                    console::printError("Skipping " + action + ": frontmost app is '" + activeBundleId +
                                        "', expected '" + expectedAppOwner + "'.");
                }
                return false;
            }

            return true;
        }

        std::vector<std::string> captureFirstRowRegion(const calibration::CoordinateSet &coords, bool quietMode)
        {
            std::vector<std::string> detectedLines;
            std::string ocrError;
            detectedLines = macos_native::recognizeTextInRegion(coords.cropX,
                                                                 coords.cropY,
                                                                 coords.cropWidth,
                                                                 coords.cropHeight,
                                                                 ocrError);
            if (!ocrError.empty() && detectedLines.empty() && !quietMode)
            {
                console::printError(ocrError);
            }
            return detectedLines;
        }

        std::vector<fs::path> listCsvFiles(const fs::path &dirPath)
        {
            std::vector<fs::path> csvFiles;

            if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
            {
                return csvFiles;
            }

            for (const auto &entry : fs::directory_iterator(dirPath))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".csv")
                {
                    csvFiles.push_back(entry.path());
                }
            }

            std::sort(csvFiles.begin(), csvFiles.end());
            return csvFiles;
        }

        std::vector<console::MenuItem> buildCsvMenuItems(const std::vector<fs::path> &csvFiles)
        {
            std::vector<console::MenuItem> items;
            items.reserve(csvFiles.size() + 1);

            for (std::size_t i = 0; i < csvFiles.size(); ++i)
            {
                items.push_back({static_cast<int>(i + 1), csvFiles[i].filename().string()});
            }

            items.push_back({0, "Back"});
            return items;
        }

        fs::path selectedCsvConfigPath()
        {
            return fs::path(kCsvDirectory) / kSelectedCsvFile;
        }

        std::optional<fs::path> loadPersistedSelectedCsv()
        {
            const fs::path selectedPath = selectedCsvConfigPath();
            if (!fs::exists(selectedPath))
            {
                return std::nullopt;
            }

            std::ifstream selectedFile(selectedPath);
            if (!selectedFile)
            {
                return std::nullopt;
            }

            std::string selectedCsvName;
            std::getline(selectedFile, selectedCsvName);

            selectedCsvName = duskull::util::trim(selectedCsvName);
            if (selectedCsvName.empty())
            {
                return std::nullopt;
            }

            const fs::path csvPath = fs::path(kCsvDirectory) / selectedCsvName;
            if (!fs::exists(csvPath) || !fs::is_regular_file(csvPath) || csvPath.extension() != ".csv")
            {
                return std::nullopt;
            }

            return csvPath;
        }

        bool persistSelectedCsv(const fs::path &csvPath)
        {
            if (!ensureCsvDirectory())
            {
                return false;
            }

            const fs::path configPath = selectedCsvConfigPath();
            std::string writeError;
            return duskull::util::writeTextFileAtomically(configPath,
                                                          csvPath.filename().string() + "\n",
                                                          writeError);
        }

        std::string buildGtlMainContext()
        {
            const auto selectedCsv = loadPersistedSelectedCsv();
            const std::string selectedCsvLabel = selectedCsv
                                                     ? selectedCsv->filename().string()
                                                     : std::string("None selected");
            return "Selected CSV: " + selectedCsvLabel;
        }

        void createCsv();

        void startSniper(const fs::path &csvPath)
        {
            ScopedAbortListener abortListener;
            std::string abortListenerError;
            if (!abortListener.start(abortListenerError))
            {
                console::printError("Emergency Escape abort is unavailable for this run: " + abortListenerError);
            }

            std::string fatalError;
            const auto preparedRules = csv::loadPreparedRules(csvPath, fatalError);
            if (!fatalError.empty())
            {
                console::printError(fatalError);
                return;
            }

            if (preparedRules.empty())
            {
                console::printError("No valid CSV rows were available for matching.");
                return;
            }

            std::vector<std::string> startupMessages;
            startupMessages.push_back("Loaded " + std::to_string(preparedRules.size()) +
                                      " valid CSV rule(s) from '" + csvPath.filename().string() + "'.");

            calibration::CoordinateSet coords;
            const fs::path configPath = calibration::defaultCoordinateConfigPath();

            std::string envDiagnostic;
            bool hasCoordinates = calibration::loadCoordinatesFromEnv(coords, envDiagnostic);
            if (!hasCoordinates)
            {
                std::string configDiagnostic;
                hasCoordinates = calibration::loadCoordinatesFromConfig(configPath, coords, configDiagnostic);
                if (hasCoordinates)
                {
                    startupMessages.push_back("Loaded saved calibration from '" + configPath.string() + "'.");
                }
                else
                {
                    console::printError("No usable env/config calibration found. Starting one-time setup.");
                    if (!envDiagnostic.empty())
                    {
                        console::printError("Env: " + envDiagnostic);
                    }
                    if (!configDiagnostic.empty())
                    {
                        console::printError("Config: " + configDiagnostic);
                    }

                    calibration::promptCaptureCoordinates(coords);
                    if (!coords.isComplete())
                    {
                        console::printError("Coordinate capture was incomplete. Cannot start sniper.");
                        return;
                    }

                    std::string saveError;
                    if (!calibration::saveCoordinatesToConfig(configPath, coords, saveError))
                    {
                        console::printError("Captured coordinates were valid, but saving failed: " + saveError);
                    }
                    else
                    {
                        console::printSuccess("Calibration saved to '" + configPath.string() + "'. Future runs auto-load it.");
                    }
                }
            }

            if (!calibration::prepareCoordinatesForCurrentDisplay(coords, true, configPath))
            {
                return;
            }

            const bool quietMode = match::readQuietMode();
            const bool debugOcrLogging = match::readDebugOcrLoggingMode();
            const bool verboseMode = match::readVerboseMode();
            const std::string expectedAppOwner = readExpectedFrontmostAppOwner();

            macos_native::DisplayMetrics display;
            std::string displayError;
            if (!macos_native::getPrimaryDisplayMetrics(display, displayError))
            {
                display.widthPoints = 20000;
                display.heightPoints = 20000;
                if (!quietMode)
                {
                    console::printError("Could not read display bounds for click randomization; using fallback bounds: " + displayError);
                }
            }

            startupMessages.push_back("Active sniper started with auto-refresh mode.");
            startupMessages.push_back("Quit: Press Escape (global) or Ctrl+C to stop.");

            std::cout << "\n";
            for (const auto &message : startupMessages)
            {
                std::cout << console::kGreen << "- " << message << console::kReset << "\n";
            }
            std::cout << "\n";

            if (verboseMode)
            {
                std::cout << "Refresh button bounds: X[" << coords.refreshMinX << ", " << coords.refreshMaxX
                          << "] Y[" << coords.refreshMinY << ", " << coords.refreshMaxY << "]\n";
                std::cout << "Buy button bounds: X[" << coords.buyMinX << ", " << coords.buyMaxX
                          << "] Y[" << coords.buyMinY << ", " << coords.buyMaxY << "]\n";
                std::cout << "First row region: (" << coords.cropX << ", " << coords.cropY << ") size "
                          << coords.cropWidth << "x" << coords.cropHeight << "\n";
                std::cout << "Quiet mode: " << (quietMode ? "ON" : "OFF") << " (set DUSKULL_QUIET=1 to enable)\n";
                std::cout << "Raw OCR logging: " << (debugOcrLogging ? "ON" : "OFF")
                          << " (set DUSKULL_DEBUG_OCR=1 to enable)\n";
                if (debugOcrLogging)
                {
                    std::cout << "Warning: raw OCR logging can expose sensitive text in terminal logs.\n";
                }
                if (!expectedAppOwner.empty())
                {
                    std::cout << "Click safety: enforcing frontmost app owner '" << expectedAppOwner << "'.\n";
                }
                else
                {
                    std::cout << "Click safety: no frontmost app restriction (set DUSKULL_EXPECTED_APP_OWNER to enable).\n";
                }
                std::cout << "Click randomization bounds (calibrated): Refresh X[" << coords.refreshMinX << "," << coords.refreshMaxX
                          << "] Y[" << coords.refreshMinY << "," << coords.refreshMaxY
                          << "] Buy X[" << coords.buyMinX << "," << coords.buyMaxX
                          << "] Y[" << coords.buyMinY << "," << coords.buyMaxY << "].\n";
            }

            std::unordered_set<std::string> previousMatches;

            while (true)
            {
                if (macos_native::isEmergencyAbortRequested())
                {
                    console::printError("Emergency abort requested (Escape). Returning to menu.");
                    macos_native::clearEmergencyAbortRequested();
                    return;
                }

                std::string refreshClickError;
                const auto refreshClickTarget = jitterClickTarget(coords.refreshBounds(), display, "refresh");
                if (frontmostAppMatchesExpected(expectedAppOwner, quietMode, "refresh click") &&
                    !macos_native::clickAt(refreshClickTarget.first, refreshClickTarget.second, refreshClickError) && !quietMode)
                {
                    console::printError("Failed to click refresh button: " + refreshClickError);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(nextRefreshDelayMs()));

                const auto detectedTexts = captureFirstRowRegion(coords, quietMode);
                if (detectedTexts.empty())
                {
                    if (!quietMode)
                    {
                        console::printError("No text detected in first row region; skipping this cycle.");
                    }

                    if (macos_native::isEmergencyAbortRequested())
                    {
                        console::printError("Emergency abort requested (Escape). Returning to menu.");
                        macos_native::clearEmergencyAbortRequested();
                        return;
                    }

                    continue;
                }

                if (debugOcrLogging)
                {
                    std::cout << "\nScanned first row: ";
                    for (const auto &text : detectedTexts)
                    {
                        std::cout << "[" << duskull::util::sanitizeForTerminal(text) << "] ";
                    }
                    std::cout << "\n";
                }

                std::unordered_set<std::string> currentMatches;
                for (const auto &preparedRule : preparedRules)
                {
                    if (!match::rowMatchesDetectedText(preparedRule.rule, detectedTexts))
                    {
                        continue;
                    }

                    const auto matchedPrice = match::findDetectedPriceAtOrBelowMaxBuy(detectedTexts,
                                                                                       preparedRule.rule.maxBuy,
                                                                                       preparedRule.rule.primaryMatchText);
                    const std::string listingPrice = matchedPrice ? std::to_string(*matchedPrice) : std::string("n/a");
                    const std::string matchKey = preparedRule.rawLine + "|" + listingPrice;
                    currentMatches.insert(matchKey);

                    if (previousMatches.find(matchKey) != previousMatches.end())
                    {
                        continue;
                    }

                    const IvParseResult ivData = extractIvStatsFromDetectedTexts(detectedTexts);
                    const std::string ivWarning = formatIvParseWarning(ivData.detectedCount);

                    if (preparedRule.rule.minIV > 0)
                    {
                        if (ivData.detectedCount < 6)
                        {
                            continue;
                        }

                        int ivSum = 0;
                        for (const auto v : ivData.ivs)
                        {
                            ivSum += v;
                        }
                        if (ivSum < preparedRule.rule.minIV)
                        {
                            continue;
                        }
                    }

                    if (verboseMode)
                    {
                        console::printSuccess("MATCH FOUND!");
                        std::cout << "  Row: " << preparedRule.rawLine << "\n";
                        std::cout << "  " << formatParsedIvSummary(ivData) << "\n";
                        if (!ivWarning.empty())
                        {
                            std::cout << "  " << ivWarning << "\n";
                        }
                        std::cout << "  Details: " << summarizeDetectedDetails(detectedTexts) << "\n";
                        if (matchedPrice)
                        {
                            std::cout << "  Listing Price: "
                                      << colorizePriceByMaxBuy(*matchedPrice, preparedRule.rule.maxBuy)
                                      << "\n";
                        }
                        else
                        {
                            std::cout << "  Listing Price: " << listingPrice << "\n";
                        }
                    }
                    else
                    {
                        const std::string level = extractLevelFromDetectedTexts(detectedTexts);
                        const std::string &name = preparedRule.rule.primaryMatchText.empty()
                                                      ? std::string("Unknown")
                                                      : preparedRule.rule.primaryMatchText;
                        const std::string levelName = level.empty() ? name : (level + " " + name);
                        const std::string detectedNature = extractNatureFromDetectedTexts(detectedTexts);

                        std::cout << console::kGreen << "[BOUGHT]" << console::kReset << " " << levelName;
                        if (!detectedNature.empty())
                        {
                            std::cout << " | " << console::kBlue << detectedNature << console::kReset;
                        }
                        int totalIv = 0;
                        for (const int value : ivData.ivs)
                        {
                            totalIv += value;
                        }

                        std::cout << " | " << formatIvSlash(ivData)
                                  << " | TotalIV: " << totalIv << " | ";
                        if (matchedPrice)
                        {
                            std::cout << colorizePriceByMaxBuy(*matchedPrice, preparedRule.rule.maxBuy);
                        }
                        else
                        {
                            std::cout << "n/a";
                        }
                        std::cout << "\n";
                        if (!ivWarning.empty())
                        {
                            std::cout << "  " << ivWarning << "\n";
                        }
                    }

                    if (matchedPrice)
                    {
                        const auto buyClickTarget = jitterClickTarget(coords.buyBounds(), display, "buy");
                        if (verboseMode)
                        {
                            std::cout << "  Clicking buy button in bounds X[" << coords.buyMinX << ", " << coords.buyMaxX
                                      << "] Y[" << coords.buyMinY << ", " << coords.buyMaxY
                                      << "] at (" << buyClickTarget.first << ", " << buyClickTarget.second << ")...\n";
                        }
                        std::string buyClickError;
                        if (frontmostAppMatchesExpected(expectedAppOwner, quietMode, "buy click") &&
                            !macos_native::clickAt(buyClickTarget.first, buyClickTarget.second, buyClickError))
                        {
                            const std::string prefix = verboseMode ? "  " : "";
                            console::printError(prefix + "Buy click failed: " + buyClickError);
                        }
                    }
                }

                previousMatches = std::move(currentMatches);

                if (macos_native::isEmergencyAbortRequested())
                {
                    console::printError("Emergency abort requested (Escape). Returning to menu.");
                    macos_native::clearEmergencyAbortRequested();
                    return;
                }
            }
        }

        void recalibrateCoordinatesFlow()
        {
            if (!ensureCsvDirectory())
            {
                console::printError("Error creating directory '" + std::string(kCsvDirectory) + "'.");
                return;
            }

            calibration::CoordinateSet coords;
            calibration::promptCaptureCoordinates(coords);

            if (!coords.isComplete())
            {
                console::printError("Coordinate capture was incomplete. Recalibration cancelled.");
                return;
            }

            const fs::path configPath = calibration::defaultCoordinateConfigPath();
            if (!calibration::prepareCoordinatesForCurrentDisplay(coords, false, configPath))
            {
                return;
            }

            std::string saveError;
            if (!calibration::saveCoordinatesToConfig(configPath, coords, saveError))
            {
                console::printError("Failed to save recalibrated coordinates: " + saveError);
                return;
            }

            console::printSuccess("Recalibration saved to '" + configPath.string() + "'.");
        }

        void selectCsvFlow()
        {
            const fs::path dirPath(kCsvDirectory);
            const auto csvFiles = listCsvFiles(dirPath);

            if (csvFiles.empty())
            {
                console::printError("No CSV files found in '" + dirPath.string() + "'. Create one first.");
                return;
            }

            const auto csvMenuItems = buildCsvMenuItems(csvFiles);

            while (true)
            {
                if (macos_native::isEmergencyAbortRequested())
                {
                    console::printError("Emergency abort requested (Escape). Returning to menu.");
                    macos_native::clearEmergencyAbortRequested();
                    return;
                }

                const auto csvSelection = console::promptMenuChoice(
                    csvMenuItems,
                    "Select CSV",
                    "",
                    "Invalid choice. Select one of the listed CSV files.");

                if (!csvSelection)
                {
                    continue;
                }

                if (*csvSelection == 0)
                {
                    return;
                }

                if (*csvSelection < 1 || static_cast<std::size_t>(*csvSelection) > csvFiles.size())
                {
                    console::printError("Invalid choice. Select one of the listed CSV files.");
                    continue;
                }

                const auto &selectedCsv = csvFiles[static_cast<std::size_t>(*csvSelection - 1)];
                if (!persistSelectedCsv(selectedCsv))
                {
                    console::printError("Failed to save selected CSV.");
                    return;
                }

                console::printSuccess("Selected CSV saved: " + selectedCsv.filename().string());
                return;
            }
        }

        void settingsFlow()
        {
            while (true)
            {
                if (macos_native::isEmergencyAbortRequested())
                {
                    console::printError("Emergency abort requested (Escape). Returning to menu.");
                    macos_native::clearEmergencyAbortRequested();
                    return;
                }

                const auto selection = console::promptMenuChoice(
                    kSettingsMenuItems,
                    "Settings",
                    "",
                    "Invalid choice. Try 0-3.");

                if (!selection)
                {
                    continue;
                }

                switch (*selection)
                {
                case SelectCsv:
                    selectCsvFlow();
                    break;
                case CreateCsv:
                    createCsv();
                    break;
                case RecalibrateCoordinates:
                    recalibrateCoordinatesFlow();
                    break;
                case SettingsBack:
                    return;
                }
            }
        }

        void createCsv()
        {
            std::string baseName;
            std::cout << "\nEnter a name for the CSV file (without extension): ";
            std::getline(std::cin >> std::ws, baseName);
            const std::string baseNameTrimmed = duskull::util::trim(baseName);

            if (!isValidFileName(baseNameTrimmed))
            {
                console::printError("Invalid name. Use a non-empty filename without characters: space \\ / : * ? \" < > |");
                return;
            }

            if (!ensureCsvDirectory())
            {
                console::printError("Error creating directory '" + std::string(kCsvDirectory) + "'.");
                return;
            }

            const fs::path csvPath = fs::path(kCsvDirectory) / (baseNameTrimmed + ".csv");

            std::error_code statusError;
            const auto csvStatus = fs::symlink_status(csvPath, statusError);
            if (!statusError && fs::exists(csvStatus) && fs::is_symlink(csvStatus))
            {
                console::printError("Refusing to overwrite symlinked CSV target: '" + csvPath.string() + "'.");
                return;
            }

            if (fs::exists(csvPath))
            {
                std::string answer;
                std::cout << "File '" << csvPath.string() << "' already exists. Overwrite? (y/N): ";
                std::getline(std::cin, answer);
                if (answer.empty() || (std::tolower(static_cast<unsigned char>(answer[0])) != 'y'))
                {
                    std::cout << "Operation cancelled.\n";
                    return;
                }
            }

            std::string writeError;
            if (!duskull::util::writeTextFileAtomically(csvPath, kCsvHeader, writeError))
            {
                console::printError(writeError);
                return;
            }
            console::printSuccess("CSV file '" + csvPath.string() + "' created successfully.");
        }
    }

    void runSniper()
    {
        while (true)
        {
            if (macos_native::isEmergencyAbortRequested())
            {
                console::printError("Emergency abort requested (Escape). Returning to main menu.");
                macos_native::clearEmergencyAbortRequested();
                return;
            }

            const auto selection = console::promptMenuChoice(
                kGtlMenuItems,
                "GTL Sniper",
                buildGtlMainContext(),
                "Invalid choice. Try 0-2.");

            if (!selection)
            {
                continue;
            }

            switch (*selection)
            {
            case Start:
            {
                const auto selectedCsv = loadPersistedSelectedCsv();
                if (!selectedCsv)
                {
                    console::printError("No CSV selected. Open Settings and choose Select CSV before starting.");
                    break;
                }

                startSniper(*selectedCsv);
                break;
            }
            case Settings:
                settingsFlow();
                break;
            case Back:
                return;
            }
        }
    }
}
