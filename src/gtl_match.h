// Duskull
// gtl_match.h
// Created by spiderguts on 3/29/26 at 11:58 PM.
// Copyright © 2026 spiderguts. All rights reserved.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gtl::csv
{
    struct CsvMatchRule;
}

namespace gtl::match
{
    [[nodiscard]] int readLiveRefreshIntervalMs();
    [[nodiscard]] bool readQuietMode();
    [[nodiscard]] bool readDebugOcrLoggingMode();
    [[nodiscard]] bool readVerboseMode();
    [[nodiscard]] bool runPriceParserSelfTests();

    [[nodiscard]] std::optional<long long> findDetectedPriceAtOrBelowMaxBuy(const std::vector<std::string> &detectedTexts,
                                                                             long long maxBuy,
                                                                             std::string_view preferredContextText);

    [[nodiscard]] bool rowMatchesDetectedText(const csv::CsvMatchRule &rule,
                                              const std::vector<std::string> &detectedTexts);
}
