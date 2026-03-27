// Duskull
// main.cpp
// Created by spiderguts on 3/22/26 at 2:14 AM.
// Copyright © 2026 spiderguts. All rights reserved.

#include <iostream>
#include <limits>
#include <string>
#include "gtl.h"

static constexpr int kExit = 0;
static constexpr int kBuySell = 1;
static constexpr int kHunt = 2;
static constexpr int kExpHorde = 3;
static constexpr const char *kColorRed = "\033[31m";
static constexpr const char *kColorReset = "\033[0m";
static const std::string kSemVer = "Pre-Alpha v0.0.2";

void mainMenu()
{
    std::cout << "\n[1] GTL Buying/Selling\n"
              << "[2] Pokémon Hunting\n"
              << "[3] EXP Horde Farming\n"
              << "[0] Exit\n"
              << "\nChoose an operation...\n";
}

int main()
{
    int mainOpSelect = -1;
    std::cout << "\nDuskull " << kSemVer << "\n"
              << "Copyright © 2026 spiderguts. All rights reserved.\n";

    while (true)
    {
        mainMenu();
        if (!(std::cin >> mainOpSelect))
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << kColorRed << "\nInvalid input; enter an integer from menu.\n"
                      << kColorReset;
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        switch (mainOpSelect)
        {
        case kBuySell:
            runGtlSniper();
            break;
        case kHunt:
        case kExpHorde:
            std::cout << "\nComing soon...\n";
            break;
        case kExit:
            std::cout << "\nThank you for using Duskull <3\n";
            return EXIT_SUCCESS;
        default:
            std::cout << kColorRed << "\nInvalid choice. Try 0-3.\n"
                      << kColorReset;
            break;
        }
    }
}
