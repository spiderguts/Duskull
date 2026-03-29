// Duskull
// main.cpp
// Created by spiderguts on 3/22/26 at 2:14 AM.
// Copyright © 2026 spiderguts. All rights reserved.

#include <iostream>
#include <array>
#include <string_view>
#include "console.h"
#include "gtl.h"

namespace
{
    enum MainAction
    {
        Exit = 0,
        BuySell = 1,
        Hunt = 2,
        ExpHorde = 3,
    };

    constexpr std::string_view kVersion = "Pre-Alpha v0.0.2";
    const std::array<console::MenuItem, 4> kMainMenuItems{{
        {BuySell, "GTL Buying/Selling"},
        {Hunt, "Pokemon Hunting"},
        {ExpHorde, "EXP Horde Farming"},
        {Exit, "Exit"},
    }};
}

int main()
{
    std::cout << "\nDuskull " << kVersion << "\n"
              << "Copyright (c) 2026 spiderguts. All rights reserved.\n";

    while (true)
    {
        const auto selection = console::promptMenuChoice(
            kMainMenuItems,
            "Choose an operation...",
            "Invalid choice. Try 0-3.");

        if (!selection)
        {
            continue;
        }

        switch (*selection)
        {
        case BuySell:
            gtl::runSniper();
            break;
        case Hunt:
        case ExpHorde:
            std::cout << "\nComing soon...\n";
            break;
        case Exit:
            std::cout << "\nThank you for using Duskull <3\n";
            return 0;
        }
    }
}
