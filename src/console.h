// Duskull
// console.h
// Created by Duskull Project on 3/29/26 at 1:03 AM.
// Copyright © 2026 Duskull Project. All rights reserved.

#pragma once

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace console
{
    struct MenuItem
    {
        int value;
        std::string label;
    };

    inline constexpr std::string_view kGreen = "\033[32m";
    inline constexpr std::string_view kRed = "\033[31m";
    inline constexpr std::string_view kReset = "\033[0m";

    inline void printColored(std::string_view message, std::string_view color)
    {
        std::cout << color << '\n'
                  << message << '\n'
                  << kReset;
    }

    inline void printError(std::string_view message)
    {
        printColored(message, kRed);
    }

    inline void printSuccess(std::string_view message)
    {
        printColored(message, kGreen);
    }

    inline std::optional<int> readMenuChoice()
    {
        int choice = 0;
        if (!(std::cin >> choice))
        {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return std::nullopt;
        }

        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return choice;
    }

    template <typename Range>
    bool isValidChoice(const Range &items, int choice)
    {
        return std::any_of(items.begin(), items.end(), [choice](const auto &item)
                           { return item.value == choice; });
    }

    template <typename Range>
    void printMenu(const Range &items, std::string_view prompt)
    {
        std::cout << '\n';
        for (const auto &item : items)
        {
            std::cout << '[' << item.value << "] " << item.label << '\n';
        }

        std::cout << '\n'
                  << prompt << '\n';
    }

    template <typename Range>
    std::optional<int> promptMenuChoice(const Range &items,
                                        std::string_view prompt,
                                        std::string_view invalidChoiceMessage)
    {
        printMenu(items, prompt);

        const auto selection = readMenuChoice();
        if (!selection)
        {
            printError("Invalid input; enter an integer from menu.");
            return std::nullopt;
        }

        if (!isValidChoice(items, *selection))
        {
            printError(invalidChoiceMessage);
            return std::nullopt;
        }

        return selection;
    }
}
