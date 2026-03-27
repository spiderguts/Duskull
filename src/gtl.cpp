// Duskull
// gtl.cpp
// Created by spiderguts on 3/22/26 at 12:41 PM.
// Copyright © 2026 spiderguts. All rights reserved.

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <limits>
#include "gtl.h"

static constexpr int kExit = 0;
static constexpr int kCreateCsv = 1;
static constexpr int kSelectCsv = 2;
static constexpr const char *kCsvHeader = "Pokemon,Nature,HP,Atk,Def,SpAtk,SpDef,Spd,MinBuy,MaxBuy,Sell\n";
static constexpr const char *kCsvDirectory = "CSV_Files";
static constexpr const char *kColorGreen = "\033[32m";
static constexpr const char *kColorRed = "\033[31m";
static constexpr const char *kColorReset = "\033[0m";
static constexpr std::string_view kIllegalChars = " \\/:*?\"<>|";

static std::string trim(const std::string &in)
{
    const auto start = std::find_if_not(in.begin(), in.end(), [](unsigned char ch)
                                        { return std::isspace(ch); });
    const auto end = std::find_if_not(in.rbegin(), in.rend(), [](unsigned char ch)
                                      { return std::isspace(ch); })
                         .base();
    return (start < end) ? std::string(start, end) : std::string();
}

static bool isValidFileName(const std::string_view name)
{
    if (name.empty())
        return false;
    return std::none_of(name.begin(), name.end(),
                        [](unsigned char ch)
                        {
                            return kIllegalChars.find(static_cast<char>(ch)) != std::string_view::npos;
                        });
}

void gtlMenu()
{
    std::cout << "\n[1] Create CSV file\n"
              << "[2] Select CSV file\n"
              << "[0] Back to Main Menu\n"
              << "\nChoose an operation...\n";
}

void createGtlCsv()
{
    std::string baseName;
    std::cout << "\nEnter a name for the CSV file (without extension): ";
    std::getline(std::cin >> std::ws, baseName);
    const std::string baseNameTrimmed = trim(baseName);

    if (!isValidFileName(baseNameTrimmed))
    {
        std::cerr << kColorRed << "\nInvalid name. Use a non-empty filename without characters: space \\ / : * ? \" < > |\n"
                  << kColorReset;
        return;
    }

    const std::filesystem::path dirPath(kCsvDirectory);
    if (!std::filesystem::exists(dirPath))
    {
        if (!std::filesystem::create_directories(dirPath))
        {
            std::cerr << kColorRed << "\nError creating directory '" << dirPath.string() << "'.\n"
                      << kColorReset;
            return;
        }
    }

    const std::filesystem::path csvPath = dirPath / (baseNameTrimmed + ".csv");

    if (std::filesystem::exists(csvPath))
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
        std::cerr << kColorRed << "\nError creating file '" << csvPath.string() << "'. Please try again.\n"
                  << kColorReset;
        return;
    }

    csvFile << kCsvHeader;
    std::cout << kColorGreen << "\nCSV file '" << csvPath.string() << "' created successfully.\n"
              << kColorReset;
}

void runGtlSniper()
{
    while (true)
    {
        gtlMenu();
        int gtlOpSelect = -1;

        if (!(std::cin >> gtlOpSelect))
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << kColorRed << "\nInvalid input; enter an integer from menu.\n"
                      << kColorReset;
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        switch (gtlOpSelect)
        {
        case kCreateCsv:
            createGtlCsv();
            break;
        case kSelectCsv:
            std::cout << "\nComing soon...\n";
            break;
        case kExit:
            return;
        default:
            std::cout << kColorRed << "\nInvalid choice. Try 0-2.\n"
                      << kColorReset;
            break;
        }
    }
}
