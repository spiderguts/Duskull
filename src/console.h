// Duskull
// console.h
// Created by Duskull Project on 3/29/26 at 1:03 AM.
// Copyright © 2026 Duskull Project. All rights reserved.

#pragma once

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace console
{
    struct MenuItem
    {
        int value;
        std::string label;
    };

    inline constexpr std::string_view kGreen = "\033[32m";
    inline constexpr std::string_view kYellow = "\033[33m";
    inline constexpr std::string_view kOrange = "\033[38;5;208m";
    inline constexpr std::string_view kBlue = "\033[94m";
    inline constexpr std::string_view kRed = "\033[31m";
    inline constexpr std::string_view kPurple = "\033[35m";
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
    std::string buildChoiceLabel(const Range &items)
    {
        if (items.begin() == items.end())
        {
            return std::string();
        }

        std::vector<int> values;
        values.reserve(static_cast<std::size_t>(std::distance(items.begin(), items.end())));
        for (const auto &item : items)
        {
            values.push_back(item.value);
        }

        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());

        bool contiguous = true;
        auto it = values.begin();
        int minValue = *it;
        int maxValue = *it;
        int previous = *it;
        ++it;

        for (; it != values.end(); ++it)
        {
            minValue = std::min(minValue, *it);
            maxValue = std::max(maxValue, *it);
            if (*it != previous + 1)
            {
                contiguous = false;
            }
            previous = *it;
        }

        if (contiguous)
        {
            return std::to_string(minValue) + "-" + std::to_string(maxValue);
        }

        std::ostringstream choiceList;
        bool first = true;
        for (const int value : values)
        {
            if (!first)
            {
                choiceList << ", ";
            }
            choiceList << value;
            first = false;
        }

        return choiceList.str();
    }

    template <typename Range>
    void printMenu(const Range &items,
                   std::string_view menuTitle,
                   std::string_view menuContext)
    {
        std::cout << '\n'
                  << kPurple << "~~~" << menuTitle << "~~~" << kReset << '\n';

        if (!menuContext.empty())
        {
            std::cout << menuContext << '\n';
        }

        for (const auto &item : items)
        {
            std::cout << '[' << item.value << "] " << item.label << '\n';
        }

        std::cout << '\n'
                  << "Choose " << buildChoiceLabel(items) << ": ";
    }

    template <typename Range>
    std::optional<int> promptMenuChoice(const Range &items,
                                        std::string_view menuTitle,
                                        std::string_view menuContext,
                                        std::string_view invalidChoiceMessage)
    {
        printMenu(items, menuTitle, menuContext);

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
