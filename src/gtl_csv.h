// Duskull
// gtl_csv.h
// Created by spiderguts on 3/29/26 at 11:58 PM.
// Copyright © 2026 spiderguts. All rights reserved.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gtl::csv
{
    struct CsvMatchRule
    {
        std::vector<std::string> requiredFields;
        long long maxBuy = 0;
        std::string mode;
        std::string primaryMatchText;
        std::string nature;
        int minIV = 0;
    };

    struct PreparedCsvRule
    {
        CsvMatchRule rule;
        std::string rawLine;
    };

    std::vector<PreparedCsvRule> loadPreparedRules(const std::filesystem::path &csvPath, std::string &fatalError);
}
