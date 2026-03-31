// Duskull
// gtl_match.cpp
// Created by spiderguts on 3/29/26 at 11:58 PM.
// Copyright © 2026 spiderguts. All rights reserved.

#include "gtl_match.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>

#include "console.h"
#include "gtl_csv.h"

namespace gtl::match
{
    namespace
    {
        std::string trim(const std::string &in)
        {
            const auto start = std::find_if_not(in.begin(), in.end(), [](unsigned char ch)
                                                { return std::isspace(ch); });
            const auto end = std::find_if_not(in.rbegin(), in.rend(), [](unsigned char ch)
                                              { return std::isspace(ch); })
                                 .base();
            return (start < end) ? std::string(start, end) : std::string();
        }

        bool equalsIgnoreCase(std::string_view left, std::string_view right)
        {
            if (left.size() != right.size())
            {
                return false;
            }

            return std::equal(left.begin(), left.end(), right.begin(),
                              [](unsigned char lch, unsigned char rch)
                              {
                                  return std::tolower(lch) == std::tolower(rch);
                              });
        }

        std::string toLowerAscii(std::string_view text)
        {
            std::string lowered;
            lowered.reserve(text.size());
            for (const char ch : text)
            {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            return lowered;
        }

        bool containsTextIgnoreCaseInLine(std::string_view detected, std::string_view value)
        {
            const std::string detectedLower = toLowerAscii(detected);
            const std::string valueLower = toLowerAscii(value);
            return detectedLower == valueLower || detectedLower.find(valueLower) != std::string::npos;
        }

        bool containsTextIgnoreCase(const std::vector<std::string> &detectedTexts, std::string_view value)
        {
            return std::any_of(detectedTexts.begin(), detectedTexts.end(), [value](const std::string &detected)
                               { return containsTextIgnoreCaseInLine(detected, value); });
        }

        std::optional<long long> parseWholeNumber(std::string_view text)
        {
            std::string digitsOnly;
            digitsOnly.reserve(text.size());

            for (const char ch : text)
            {
                if (std::isdigit(static_cast<unsigned char>(ch)))
                {
                    digitsOnly.push_back(ch);
                    continue;
                }

                if (ch != ',')
                {
                    return std::nullopt;
                }
            }

            if (digitsOnly.empty())
            {
                return std::nullopt;
            }

            try
            {
                return std::stoll(digitsOnly);
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        std::vector<long long> extractNumbersFromLine(std::string_view line)
        {
            std::vector<long long> numbers;
            std::string token;

            const auto flushToken = [&numbers, &token]()
            {
                if (token.empty())
                {
                    return;
                }

                const auto parsed = parseWholeNumber(token);
                if (parsed)
                {
                    numbers.push_back(*parsed);
                }

                token.clear();
            };

            for (const char ch : line)
            {
                if (std::isdigit(static_cast<unsigned char>(ch)) || ch == ',')
                {
                    token.push_back(ch);
                }
                else
                {
                    flushToken();
                }
            }

            flushToken();
            return numbers;
        }

        // Removes "Lv." (or "Lv ") followed by 1-3 digits from a line, case-insensitively,
        // so the Pokemon level is never mistaken for a listing price.
        std::string stripLevelFromLine(std::string_view line)
        {
            std::string result(line);
            for (std::size_t i = 0; i + 1 < result.size(); )
            {
                if (std::tolower(static_cast<unsigned char>(result[i])) != 'l' ||
                    std::tolower(static_cast<unsigned char>(result[i + 1])) != 'v')
                {
                    ++i;
                    continue;
                }

                std::size_t j = i + 2;

                // Require '.' or ' ' after "lv"
                if (j >= result.size() ||
                    (result[j] != '.' && !std::isspace(static_cast<unsigned char>(result[j]))))
                {
                    ++i;
                    continue;
                }
                ++j;

                // Skip any additional whitespace
                while (j < result.size() && std::isspace(static_cast<unsigned char>(result[j])))
                {
                    ++j;
                }

                // Require 1-3 digits (Lv.1 to Lv.100)
                const std::size_t digitStart = j;
                while (j < result.size() && std::isdigit(static_cast<unsigned char>(result[j])))
                {
                    ++j;
                }

                const std::size_t digitCount = j - digitStart;
                if (digitCount == 0 || digitCount > 3)
                {
                    ++i;
                    continue;
                }

                result.erase(i, j - i);
                // restart from same position after erasure
            }

            return result;
        }

        std::vector<long long> extractCommaFormattedNumbers(std::string_view line)
        {
            std::vector<long long> numbers;
            std::string token;

            const auto flushToken = [&numbers, &token]()
            {
                if (token.empty())
                {
                    return;
                }

                const bool hasComma = token.find(',') != std::string::npos;
                if (hasComma)
                {
                    const auto parsed = parseWholeNumber(token);
                    if (parsed)
                    {
                        numbers.push_back(*parsed);
                    }
                }

                token.clear();
            };

            for (const char ch : line)
            {
                if (std::isdigit(static_cast<unsigned char>(ch)) || ch == ',')
                {
                    token.push_back(ch);
                }
                else
                {
                    flushToken();
                }
            }

            flushToken();
            return numbers;
        }

        // Extracts numbers preceded by '$', with optional whitespace between '$' and digits
        // (e.g. "$600,000" or "$ 600,000" -> 600000).
        std::vector<long long> extractDollarPrefixedNumbers(std::string_view line)
        {
            std::vector<long long> numbers;
            const std::size_t len = line.size();

            for (std::size_t i = 0; i < len; ++i)
            {
                if (line[i] != '$')
                {
                    continue;
                }

                // Skip optional whitespace after '$'
                std::size_t j = i + 1;
                while (j < len && std::isspace(static_cast<unsigned char>(line[j])))
                {
                    ++j;
                }

                // Collect the digit/comma run
                std::string token;
                for (; j < len; ++j)
                {
                    const char ch = line[j];
                    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == ',')
                    {
                        token.push_back(ch);
                    }
                    else
                    {
                        break;
                    }
                }

                const auto parsed = parseWholeNumber(token);
                if (parsed)
                {
                    numbers.push_back(*parsed);
                }
            }

            return numbers;
        }

        bool optionalEquals(const std::optional<long long> &left, const std::optional<long long> &right)
        {
            return left.has_value() == right.has_value() && (!left.has_value() || *left == *right);
        }
    }

    int readLiveRefreshIntervalMs()
    {
        constexpr int kDefaultRefreshMs = 1000;
        const char *envValue = std::getenv("DUSKULL_REFRESH_MS");
        if (envValue == nullptr)
        {
            return kDefaultRefreshMs;
        }

        const auto parsed = parseWholeNumber(envValue);
        if (!parsed || *parsed <= 0 || *parsed > 10000)
        {
            console::printError("Invalid DUSKULL_REFRESH_MS; using default 1000ms.");
            return kDefaultRefreshMs;
        }

        return static_cast<int>(*parsed);
    }

    bool readQuietMode()
    {
        const char *raw = std::getenv("DUSKULL_QUIET");
        if (raw == nullptr)
        {
            return false;
        }

        const std::string value = trim(raw);
        return equalsIgnoreCase(value, "1") ||
               equalsIgnoreCase(value, "true") ||
               equalsIgnoreCase(value, "yes") ||
               equalsIgnoreCase(value, "on");
    }

    bool readDebugOcrLoggingMode()
    {
        const char *raw = std::getenv("DUSKULL_DEBUG_OCR");
        if (raw == nullptr)
        {
            return false;
        }

        const std::string value = trim(raw);
        return equalsIgnoreCase(value, "1") ||
               equalsIgnoreCase(value, "true") ||
               equalsIgnoreCase(value, "yes") ||
               equalsIgnoreCase(value, "on");
    }

    bool readVerboseMode()
    {
        const char *raw = std::getenv("DUSKULL_VERBOSE");
        if (raw == nullptr)
        {
            return false;
        }

        const std::string value = trim(raw);
        return equalsIgnoreCase(value, "1") ||
               equalsIgnoreCase(value, "true") ||
               equalsIgnoreCase(value, "yes") ||
               equalsIgnoreCase(value, "on");
    }

    bool runPriceParserSelfTests()
    {
        struct PriceParserTestCase
        {
            std::string name;
            std::vector<std::string> detectedTexts;
            long long maxBuy = 0;
            std::string preferredContextText;
            std::optional<long long> expected;
        };

        const std::vector<PriceParserTestCase> testCases{
            {"Dollar after level",
             {"Lv.45 Duskull $600,000"},
             700000,
             "Duskull",
             600000},
            {"Comma fallback without dollar",
             {"LV. 100 Duskull 600,000"},
             700000,
             "Duskull",
             600000},
            {"Only level should not parse as price",
             {"Lv.45 Duskull Adamant"},
             700000,
             "Duskull",
             std::nullopt},
            {"Dollar with whitespace",
             {"Lv.1 Duskull $ 12,345"},
             20000,
             "Duskull",
             12345},
            {"Max buy filter applies",
             {"Lv.55 Duskull $600,000"},
             500000,
             "Duskull",
             std::nullopt},
            {"Preferred context line selection",
             {"Lv.44 Mismagius $700,000", "Lv.33 Duskull $300,000"},
             800000,
             "Duskull",
             300000},
        };

        std::cout << "\nRunning price parser self-tests...\n";

        int passed = 0;
        for (const auto &testCase : testCases)
        {
            const auto actual = findDetectedPriceAtOrBelowMaxBuy(testCase.detectedTexts,
                                                                 testCase.maxBuy,
                                                                 testCase.preferredContextText);
            const bool ok = optionalEquals(actual, testCase.expected);
            if (ok)
            {
                ++passed;
                std::cout << "  [PASS] " << testCase.name << "\n";
                continue;
            }

            const std::string expectedText = testCase.expected ? std::to_string(*testCase.expected) : "n/a";
            const std::string actualText = actual ? std::to_string(*actual) : "n/a";
            std::cout << "  [FAIL] " << testCase.name
                      << " (expected " << expectedText
                      << ", got " << actualText << ")\n";
        }

        std::cout << "Self-test summary: " << passed << "/" << testCases.size() << " passed.\n";
        return passed == static_cast<int>(testCases.size());
    }

    std::optional<long long> findDetectedPriceAtOrBelowMaxBuy(const std::vector<std::string> &detectedTexts,
                                                               long long maxBuy,
                                                               std::string_view preferredContextText)
    {
        auto selectBestPriceFromLines = [maxBuy](const std::vector<std::string> &lines) -> std::optional<long long>
        {
            std::optional<long long> bestCurrencyTaggedMatch;
            std::optional<long long> bestMatch;

            for (const auto &lineRaw : lines)
            {
                const std::string line = stripLevelFromLine(lineRaw);
                // Only numbers directly preceded by '$' are treated as prices.
                const auto dollarNumbers = extractDollarPrefixedNumbers(line);
                for (const long long value : dollarNumbers)
                {
                    if (value > maxBuy)
                    {
                        continue;
                    }

                    if (!bestCurrencyTaggedMatch || value > *bestCurrencyTaggedMatch)
                    {
                        bestCurrencyTaggedMatch = value;
                    }

                    if (!bestMatch || value > *bestMatch)
                    {
                        bestMatch = value;
                    }
                }

                // Fall back to comma-formatted numbers (e.g. "600,000") if no '$'-prefixed
                // price was found. This excludes bare numbers like a Pokemon level ("45").
                if (dollarNumbers.empty())
                {
                    for (const long long value : extractCommaFormattedNumbers(line))
                    {
                        if (value > maxBuy)
                        {
                            continue;
                        }

                        if (!bestMatch || value > *bestMatch)
                        {
                            bestMatch = value;
                        }
                    }
                }
            }

            if (bestCurrencyTaggedMatch)
            {
                return bestCurrencyTaggedMatch;
            }

            return bestMatch;
        };

        std::vector<std::string> preferredLines;
        preferredLines.reserve(detectedTexts.size());
        if (!preferredContextText.empty())
        {
            for (const auto &line : detectedTexts)
            {
                if (containsTextIgnoreCaseInLine(line, preferredContextText))
                {
                    preferredLines.push_back(line);
                }
            }
        }

        if (!preferredLines.empty())
        {
            const auto preferredMatch = selectBestPriceFromLines(preferredLines);
            if (preferredMatch)
            {
                return preferredMatch;
            }
        }

        return selectBestPriceFromLines(detectedTexts);
    }

    bool rowMatchesDetectedText(const csv::CsvMatchRule &rule, const std::vector<std::string> &detectedTexts)
    {
        const bool requiredTextMatched = std::all_of(rule.requiredFields.begin(),
                                                     rule.requiredFields.end(),
                                                     [&detectedTexts](const std::string &required)
                                                     { return containsTextIgnoreCase(detectedTexts, required); });
        if (!requiredTextMatched)
        {
            return false;
        }

        return findDetectedPriceAtOrBelowMaxBuy(detectedTexts, rule.maxBuy, rule.primaryMatchText).has_value();
    }
}
