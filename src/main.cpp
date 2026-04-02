// Duskull
// main.cpp
// Created by Duskull Project on 3/22/26 at 2:14 AM.
// Copyright © 2026 Duskull Project. All rights reserved.

#include <iostream>
#include <array>
#include <cstdlib>
#include <string_view>

#include "console.h"
#include "duskull_util.h"
#include "gtl.h"
#include "gtl_match.h"

namespace
{
    enum MainAction
    {
        Exit = 0,
        Buy = 1,
    };

    constexpr std::string_view kVersion = "Alpha v0.2.1";
    constexpr std::string_view kAsciiLogo = R"( ____  _   _ ____  _  ___   _ _     _     
|  _ \| | | / ___|| |/ / | | | |   | |    
| | | | | | \___ \| ' /| | | | |   | |    
| |_| | |_| |___) | . \| |_| | |___| |___ 
|____/ \___/|____/|_|\_\\___/|_____|_____|
)";
    const std::array<console::MenuItem, 2> kMainMenuItems{{
        {Buy, "GTL Sniper"},
        {Exit, "Exit"},
    }};

}

int main()
{
    std::cout << '\n' << console::kPurple << kAsciiLogo << console::kReset
              << "Duskull " << kVersion << "\n"
              << "Copyright (c) 2026 Duskull Project. All rights reserved.\n";

    if (duskull::util::isTruthyEnvValue(std::getenv("DUSKULL_SELF_TEST")))
    {
        const bool testsPassed = gtl::match::runPriceParserSelfTests();
        return testsPassed ? 0 : 1;
    }

    while (true)
    {
        const auto selection = console::promptMenuChoice(
            kMainMenuItems,
            "Main Menu",
            "",
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
            std::cout << '\n'
                      << console::kPurple << "<3 THANK YOU FOR USING DUSKULL! <3" << console::kReset
                      << '\n';
            return 0;
        }
    }
}
