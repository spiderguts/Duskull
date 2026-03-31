// Duskull
// main.cpp
// Created by spiderguts on 3/22/26 at 2:14 AM.
// Copyright © 2026 spiderguts. All rights reserved.

#include <iostream>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string_view>

#include "console.h"
#include "gtl.h"
#include "gtl_match.h"

namespace
{
    enum MainAction
    {
        Exit = 0,
        Buy = 1,
    };

    constexpr std::string_view kVersion = "Alpha v0.1.14";
    const std::array<console::MenuItem, 2> kMainMenuItems{{
        {Buy, "GTL Buying"},
        {Exit, "Exit"},
    }};

    bool isTruthyEnvValue(const char *raw)
    {
        if (raw == nullptr)
        {
            return false;
        }

        std::string lowered;
        for (const char ch : std::string_view(raw))
        {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }

        return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
    }
}

int main()
{
    std::cout << "\nDuskull " << kVersion << "\n"
              << "Copyright (c) 2026 spiderguts. All rights reserved.\n";

    if (isTruthyEnvValue(std::getenv("DUSKULL_SELF_TEST")))
    {
        const bool testsPassed = gtl::match::runPriceParserSelfTests();
        return testsPassed ? 0 : 1;
    }

    while (true)
    {
        const auto selection = console::promptMenuChoice(
            kMainMenuItems,
            "Choose an operation...",
            "Invalid choice. Try 0-1.");

        if (!selection)
        {
            continue;
        }

        switch (*selection)
        {
        case Buy:
            gtl::runSniper();
            break;
        case Exit:
            std::cout << "\nThank you for using Duskull <3\n";
            return 0;
        }
    }
}
