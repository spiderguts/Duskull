// Duskull
// gtl.cpp
// Created by Duskull Project on 3/22/26 at 12:41 PM.
// Copyright © 2026 Duskull Project. All rights reserved.

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "console.h"
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

        enum MenuAction
        {
            Exit = 0,
            CreateCsv = 1,
            SelectCsv = 2,
            RecalibrateCoordinates = 3,
        };

        constexpr std::string_view kCsvHeader = "Pokemon,Nature,HP,Atk,Def,SpAtk,SpDef,Spd,Item,MaxBuy,MinIV\n";
        constexpr std::string_view kCsvDirectory = "CSV_Files";
        constexpr std::string_view kIllegalChars = " \\/:*?\"<>|";
        constexpr std::string_view kAnsiGreen = "\033[32m";
        constexpr std::string_view kAnsiYellow = "\033[33m";
        constexpr std::string_view kAnsiOrange = "\033[38;5;208m";
        constexpr std::string_view kAnsiBlue = "\033[94m";
        constexpr std::string_view kAnsiReset = "\033[0m";

        const std::array<console::MenuItem, 4> kGtlMenuItems{{
            {CreateCsv, "Create CSV file"},
            {SelectCsv, "Select CSV file"},
            {RecalibrateCoordinates, "Recalibrate Coordinates"},
            {Exit, "Back to Main Menu"},
        }};

        const std::array<console::MenuItem, 2> kStartMenuItems{{
            {1, "Start"},
            {0, "Back"},
        }};

        std::string trim(const std::string &in)
        {
            const auto start = std::find_if_not(in.begin(), in.end(), [](unsigned char ch)
                                                { return std::isspace(ch); });
            const auto end = std::find_if_not(in.rbegin(), in.rend(), [](unsigned char ch)
                                              { return std::isspace(ch); })
                                 .base();
            return (start < end) ? std::string(start, end) : std::string();
        }

        std::string sanitizeForTerminal(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const unsigned char ch : text)
            {
                const bool keep = ch == '\t' || ch == '\n' || (ch >= 32 && ch != 127);
                out.push_back(keep ? static_cast<char>(ch) : '?');
            }
            return out;
        }

        bool existingPathContainsSymlink(const fs::path &path, fs::path &symlinkPath)
        {
            if (path.empty())
            {
                return false;
            }

            const auto normalized = path.lexically_normal();
            fs::path current = normalized.root_path();

            for (const auto &part : normalized.relative_path())
            {
                current /= part;

                std::error_code statusError;
                const auto status = fs::symlink_status(current, statusError);
                if (statusError)
                {
                    continue;
                }

                if (!fs::exists(status))
                {
                    break;
                }

                if (fs::is_symlink(status))
                {
                    symlinkPath = current;
                    return true;
                }
            }

            return false;
        }

        bool setOwnerOnlyPermissions(const fs::path &targetPath, std::string &error)
        {
            std::error_code permissionsError;
            fs::permissions(targetPath,
                            fs::perms::owner_read | fs::perms::owner_write,
                            fs::perm_options::replace,
                            permissionsError);
            if (permissionsError)
            {
                error = "Error setting secure permissions on '" + targetPath.string() + "': " + permissionsError.message();
                return false;
            }

            return true;
        }

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
            std::string_view color = kAnsiGreen;

            // Near max-buy is highest urgency (red), mid-range is caution (yellow), low is good (green).
            if (ratio >= 0.90)
            {
                color = kAnsiOrange;
            }
            else if (ratio >= 0.70)
            {
                color = kAnsiYellow;
            }

            return std::string(color) + "$" + commaFormat(listingPrice) + std::string(kAnsiReset);
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

                out.append(kAnsiGreen);
                out.append("31");
                out.append(kAnsiReset);
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
                    out << kAnsiGreen << ivData.ivs[i] << kAnsiReset;
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
                    out << kAnsiGreen << ivData.ivs[i] << kAnsiReset;
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
            out << kAnsiYellow << "Warning: detected " << detectedCount
                << "/6 IV values before price; check OCR crop/order." << kAnsiReset;
            return out.str();
        }

        std::string summarizeDetectedDetails(const std::vector<std::string> &detectedTexts)
        {
            std::ostringstream out;
            bool wroteAny = false;

            for (const auto &text : detectedTexts)
            {
                const std::string cleaned = trim(text);
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

        bool writeTextFileAtomically(const fs::path &targetPath,
                                     const std::string &content,
                                     std::string &error)
        {
            const fs::path parent = targetPath.parent_path();
            if (!parent.empty())
            {
                fs::path symlinkPath;
                if (existingPathContainsSymlink(parent, symlinkPath))
                {
                    error = "Refusing to write through symlinked directory component: '" + symlinkPath.string() + "'.";
                    return false;
                }

                std::error_code dirError;
                fs::create_directories(parent, dirError);
                if (dirError)
                {
                    error = "Error creating directory '" + parent.string() + "': " + dirError.message();
                    return false;
                }

                if (existingPathContainsSymlink(parent, symlinkPath))
                {
                    error = "Refusing to write through symlinked directory component: '" + symlinkPath.string() + "'.";
                    return false;
                }
            }

            std::error_code statusError;
            const auto targetStatus = fs::symlink_status(targetPath, statusError);
            if (!statusError && fs::exists(targetStatus) && fs::is_symlink(targetStatus))
            {
                error = "Refusing to write through symlinked target: '" + targetPath.string() + "'.";
                return false;
            }

            std::random_device rd;
            std::stringstream suffix;
            suffix << ".tmp." << std::hex << rd();
            const fs::path tempPath = targetPath.string() + suffix.str();

            {
                std::ofstream tempFile(tempPath, std::ios::out | std::ios::trunc);
                if (!tempFile)
                {
                    error = "Error creating temp file '" + tempPath.string() + "'.";
                    return false;
                }

                tempFile << content;
                if (!tempFile.good())
                {
                    fs::remove(tempPath);
                    error = "Error writing temp file '" + tempPath.string() + "'.";
                    return false;
                }
            }

            std::error_code renameError;
            fs::rename(tempPath, targetPath, renameError);
            if (!renameError)
            {
                return setOwnerOnlyPermissions(targetPath, error);
            }

            std::error_code secondStatusError;
            const auto secondStatus = fs::symlink_status(targetPath, secondStatusError);
            if (!secondStatusError && fs::exists(secondStatus) && fs::is_symlink(secondStatus))
            {
                fs::remove(tempPath);
                error = "Refusing to replace symlinked target after rename retry: '" + targetPath.string() + "'.";
                return false;
            }

            std::error_code removeError;
            fs::remove(targetPath, removeError);
            std::error_code secondRenameError;
            fs::rename(tempPath, targetPath, secondRenameError);
            if (!secondRenameError)
            {
                return setOwnerOnlyPermissions(targetPath, error);
            }

            fs::remove(tempPath);
            error = "Error replacing file '" + targetPath.string() + "': " + secondRenameError.message();
            return false;
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

            return trim(raw);
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

            console::printSuccess("Loaded " + std::to_string(preparedRules.size()) + " valid CSV rule(s) from '" +
                                  csvPath.filename().string() + "'.");

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
                    console::printSuccess("Loaded saved calibration from '" + configPath.string() + "'.");
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
                    if (coords.refreshX == 0 || coords.buyX == 0 || coords.cropWidth == 0)
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

            console::printSuccess("\nActive sniper started with auto-refresh mode.");
            std::cout << "Press Escape (global) or Ctrl+C to stop.\n";
            if (verboseMode)
            {
                std::cout << "Refresh button: (" << coords.refreshX << ", " << coords.refreshY << ")\n";
                std::cout << "Buy button: (" << coords.buyX << ", " << coords.buyY << ")\n";
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
                if (frontmostAppMatchesExpected(expectedAppOwner, quietMode, "refresh click") &&
                    !macos_native::clickAt(coords.refreshX, coords.refreshY, refreshClickError) && !quietMode)
                {
                    console::printError("Failed to click refresh button: " + refreshClickError);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(150));

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
                        std::cout << "[" << sanitizeForTerminal(text) << "] ";
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

                        std::cout << kAnsiGreen << "[BOUGHT]" << kAnsiReset << " " << levelName;
                        if (!detectedNature.empty())
                        {
                            std::cout << " | " << kAnsiBlue << detectedNature << kAnsiReset;
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
                        if (verboseMode)
                        {
                            std::cout << "  Clicking buy button at (" << coords.buyX << ", " << coords.buyY << ")...\n";
                        }
                        std::string buyClickError;
                        if (frontmostAppMatchesExpected(expectedAppOwner, quietMode, "buy click") &&
                            !macos_native::clickAt(coords.buyX, coords.buyY, buyClickError))
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

            if (coords.refreshX == 0 || coords.buyX == 0 || coords.cropWidth == 0)
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
                    "Choose a CSV file...",
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
                console::printSuccess("Selected CSV: " + selectedCsv.filename().string());

                while (true)
                {
                    if (macos_native::isEmergencyAbortRequested())
                    {
                        console::printError("Emergency abort requested (Escape). Returning to menu.");
                        macos_native::clearEmergencyAbortRequested();
                        return;
                    }

                    const auto startSelection = console::promptMenuChoice(
                        kStartMenuItems,
                        "Choose an operation...",
                        "Invalid choice. Try 0-1.");

                    if (!startSelection)
                    {
                        continue;
                    }

                    if (*startSelection == 0)
                    {
                        break;
                    }

                    startSniper(selectedCsv);
                    break;
                }
            }
        }

        void createCsv()
        {
            std::string baseName;
            std::cout << "\nEnter a name for the CSV file (without extension): ";
            std::getline(std::cin >> std::ws, baseName);
            const std::string baseNameTrimmed = trim(baseName);

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
            if (!writeTextFileAtomically(csvPath, std::string(kCsvHeader), writeError))
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
                "Choose an operation...",
                "Invalid choice. Try 0-3.");

            if (!selection)
            {
                continue;
            }

            switch (*selection)
            {
            case CreateCsv:
                createCsv();
                break;
            case SelectCsv:
                selectCsvFlow();
                break;
            case RecalibrateCoordinates:
                recalibrateCoordinatesFlow();
                break;
            case Exit:
                return;
            }
        }
    }
}
