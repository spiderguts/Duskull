// Duskull
// gtl.cpp
// Created by spiderguts on 3/22/26 at 12:41 PM.
// Copyright © 2026 spiderguts. All rights reserved.

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>
#include "console.h"
#include "gtl.h"

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
        };

        constexpr std::string_view kCsvHeader = "Pokemon,Nature,HP,Atk,Def,SpAtk,SpDef,Spd,MinBuy,MaxBuy,Sell\n";
        constexpr std::string_view kCsvDirectory = "CSV_Files";
        constexpr std::string_view kIllegalChars = " \\/:*?\"<>|";
        const std::array<console::MenuItem, 3> kGtlMenuItems{{
            {CreateCsv, "Create CSV file"},
            {SelectCsv, "Select CSV file"},
            {Exit, "Back to Main Menu"},
        }};
        const std::array<console::MenuItem, 2> kStartMenuItems{{
            {1, "Start"},
            {0, "Back"},
        }};

        struct ExternalProgramSnapshot
        {
            std::vector<std::string> detectedTexts;
            std::string sourceApplication;
        };

        std::string trim(const std::string &in)
        {
            const auto start = std::find_if_not(in.begin(), in.end(), [](unsigned char ch)
                                                { return std::isspace(ch); });
            const auto end = std::find_if_not(in.rbegin(), in.rend(), [](unsigned char ch)
                                              { return std::isspace(ch); })
                                 .base();
            return (start < end) ? std::string(start, end) : std::string();
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

        std::vector<std::string> parseCsvLine(const std::string &line)
        {
            std::vector<std::string> fields;
            std::string currentField;
            bool inQuotes = false;

            for (std::size_t i = 0; i < line.size(); ++i)
            {
                const char ch = line[i];

                if (ch == '"')
                {
                    if (inQuotes && (i + 1) < line.size() && line[i + 1] == '"')
                    {
                        currentField.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = !inQuotes;
                    }
                }
                else if (ch == ',' && !inQuotes)
                {
                    fields.push_back(trim(currentField));
                    currentField.clear();
                }
                else
                {
                    currentField.push_back(ch);
                }
            }

            fields.push_back(trim(currentField));
            return fields;
        }

        bool isMeaningfulCsvRow(const std::vector<std::string> &fields)
        {
            return std::any_of(fields.begin(), fields.end(), [](const std::string &field)
                               { return !field.empty(); });
        }

        std::vector<std::string> loadCsvRows(const fs::path &csvPath)
        {
            std::ifstream csvFile(csvPath);
            std::vector<std::string> rows;

            if (!csvFile)
            {
                console::printError("Error opening file '" + csvPath.string() + "'.");
                return rows;
            }

            std::string line;
            bool skippedHeader = false;

            while (std::getline(csvFile, line))
            {
                if (!skippedHeader)
                {
                    skippedHeader = true;
                    continue;
                }

                if (trim(line).empty())
                {
                    continue;
                }

                const auto fields = parseCsvLine(line);
                if (fields.size() < 11)
                {
                    console::printError("Skipping malformed CSV row: " + line);
                    continue;
                }

                if (isMeaningfulCsvRow(fields))
                {
                    rows.push_back(line);
                }
            }

            return rows;
        }

        ExternalProgramSnapshot captureExternalProgramText()
        {
            std::string sourceApplication;
            std::cout << "\nEnter source application name (optional): ";
            std::getline(std::cin, sourceApplication);

            std::cout << "\nEnter detected external text lines exactly as shown in the external program.\n"
                      << "Submit an empty line when done.\n";

            std::vector<std::string> detectedLines;
            while (true)
            {
                std::string line;
                std::getline(std::cin, line);

                if (line.empty())
                {
                    break;
                }

                detectedLines.push_back(line);
            }

            if (detectedLines.empty())
            {
                detectedLines.push_back("[placeholder: external text detection pending]");
            }

            return ExternalProgramSnapshot{
                detectedLines,
                trim(sourceApplication).empty() ? "[placeholder: target application not wired yet]" : trim(sourceApplication)};
        }

        void emitPlaceholderInputSequence(std::string_view csvRow, std::string_view detectedText)
        {
            std::cout << "  Keyboard output: [placeholder]\n"
                      << "  Mouse output: [placeholder]\n"
                      << "  Context: csvRow='" << csvRow << "', matchedText='" << detectedText << "'\n";
        }

        const std::string *findExactCsvLine(const std::vector<std::string> &rows, std::string_view csvLine)
        {
            const auto match = std::find(rows.begin(), rows.end(), csvLine);
            return match == rows.end() ? nullptr : &*match;
        }

        void startSniper(const fs::path &csvPath)
        {
            const auto rows = loadCsvRows(csvPath);

            if (rows.empty())
            {
                console::printError("No data rows found in '" + csvPath.filename().string() + "'.");
                return;
            }

            console::printSuccess("Loaded " + std::to_string(rows.size()) + " CSV data row(s) from '" + csvPath.filename().string() + "'.");

            const auto snapshot = captureExternalProgramText();
            if (snapshot.detectedTexts.empty())
            {
                console::printError("No text detected in the external program.");
                return;
            }

            std::cout << "\nDetected " << snapshot.detectedTexts.size() << " external text candidate(s) from '"
                      << snapshot.sourceApplication << "'.\n";

            std::size_t matchCount = 0;
            for (const auto &detectedText : snapshot.detectedTexts)
            {
                std::cout << "\nDetected text: '" << detectedText << "'\n";

                const auto *matchedRow = findExactCsvLine(rows, detectedText);
                if (matchedRow == nullptr)
                {
                    std::cout << "  CSV match: none\n";
                    continue;
                }

                ++matchCount;
                std::cout << "  CSV match: exact row '" << *matchedRow << "'"
                          << ", source='" << snapshot.sourceApplication << "'\n";
                emitPlaceholderInputSequence(*matchedRow, detectedText);
            }

            if (matchCount == 0)
            {
                console::printError("No detected text matched any CSV row.");
            }
            else
            {
                console::printSuccess("Matched " + std::to_string(matchCount) + " external line(s) to CSV row(s).");
            }
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

        bool ensureCsvDirectory()
        {
            const fs::path dirPath(kCsvDirectory);
            return fs::exists(dirPath) || fs::create_directories(dirPath);
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
                    const auto startSelection = console::promptMenuChoice(
                        kStartMenuItems,
                        "Choose an operation...",
                        "Invalid choice. Try 0-1.");

                    if (!startSelection)
                    {
                        continue;
                    }

                    switch (*startSelection)
                    {
                    case 1:
                        startSniper(selectedCsv);
                        break;
                    case 0:
                        break;
                    default:
                        console::printError("Invalid choice. Try 0-1.");
                        continue;
                    }

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

            std::ofstream csvFile(csvPath);
            if (!csvFile)
            {
                console::printError("Error creating file '" + csvPath.string() + "'. Please try again.");
                return;
            }

            csvFile << kCsvHeader;
            console::printSuccess("CSV file '" + csvPath.string() + "' created successfully.");
        }
    }

    void runSniper()
    {
        while (true)
        {
            const auto selection = console::promptMenuChoice(
                kGtlMenuItems,
                "Choose an operation...",
                "Invalid choice. Try 0-2.");

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
            case Exit:
                return;
            }
        }
    }
}
