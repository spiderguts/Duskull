// Duskull
// gtl_csv.cpp
// Created by Duskull Project on 3/29/26 at 11:58 PM.
// Copyright © 2026 Duskull Project. All rights reserved.

#include "gtl_csv.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "console.h"

namespace gtl::csv
{
    namespace
    {
        constexpr std::size_t kExpectedCsvColumnCount = 11;
        constexpr std::uintmax_t kMaxCsvFileBytes = 1024 * 1024;
        constexpr std::size_t kMaxCsvLineLength = 4096;
        constexpr std::size_t kMaxCsvRules = 5000;
        constexpr std::size_t kMaxCsvMemoryBytes = 10 * 1024 * 1024;

        constexpr std::size_t kPokemonColumnIndex = 0;
        constexpr std::size_t kNatureColumnIndex = 1;
        constexpr std::size_t kHpColumnIndex = 2;
        constexpr std::size_t kAtkColumnIndex = 3;
        constexpr std::size_t kDefColumnIndex = 4;
        constexpr std::size_t kSpAtkColumnIndex = 5;
        constexpr std::size_t kSpDefColumnIndex = 6;
        constexpr std::size_t kSpdColumnIndex = 7;
        constexpr std::size_t kItemColumnIndex = 8;
        constexpr std::size_t kMaxBuyColumnIndex = 9;
        constexpr std::size_t kMinIvColumnIndex = 10;

        struct CsvRecord
        {
            std::string rawLine;
            std::vector<std::string> fields;
        };

        std::string trim(const std::string &in)
        {
            const auto start = std::find_if_not(in.begin(), in.end(), [](unsigned char ch)
                                                { return std::isspace(ch); });
            const auto end = std::find_if_not(in.rbegin(), in.rend(), [](unsigned char ch)
                                              { return std::isspace(ch); })
                                 .base();
            return (start < end) ? std::string(start, end) : std::string();
        }

        std::string sanitizeForTerminal(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const unsigned char ch : text)
            {
                const bool keep = ch == '\t' || ch == '\n' || (ch >= 32 && ch != 127);
                out.push_back(keep ? static_cast<char>(ch) : '?');
            }
            return out;
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
            catch (const std::invalid_argument &)
            {
                return std::nullopt;
            }
            catch (const std::out_of_range &)
            {
                return std::nullopt;
            }
        }

        std::vector<std::string> parseCsvLine(const std::string &line)
        {
            std::vector<std::string> fields;
            std::string currentField;
            bool inQuotes = false;

            for (std::size_t i = 0; i < line.size(); ++i)
            {
                const char ch = line[i];

                if (ch == '"')
                {
                    if (inQuotes && (i + 1) < line.size() && line[i + 1] == '"')
                    {
                        currentField.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = !inQuotes;
                    }
                }
                else if (ch == ',' && !inQuotes)
                {
                    fields.push_back(trim(currentField));
                    currentField.clear();
                }
                else
                {
                    currentField.push_back(ch);
                }
            }

            fields.push_back(trim(currentField));
            return fields;
        }

        bool isMeaningfulCsvRow(const std::vector<std::string> &fields)
        {
            return std::any_of(fields.begin(), fields.end(), [](const std::string &field)
                               { return !trim(field).empty(); });
        }

        std::vector<CsvRecord> loadCsvRows(const std::filesystem::path &csvPath, std::string &fatalError)
        {
            std::error_code sizeError;
            const auto csvFileSize = std::filesystem::file_size(csvPath, sizeError);
            if (!sizeError && csvFileSize > kMaxCsvFileBytes)
            {
                fatalError = "CSV file exceeds 1MB safety limit.";
                return {};
            }

            std::ifstream csvFile(csvPath);
            std::vector<CsvRecord> rows;

            if (!csvFile)
            {
                fatalError = "Error opening file '" + csvPath.string() + "'.";
                return rows;
            }

            std::string line;
            bool skippedHeader = false;
            std::size_t totalRowBytes = 0;

            while (std::getline(csvFile, line))
            {
                if (!skippedHeader)
                {
                    skippedHeader = true;
                    continue;
                }

                if (trim(line).empty())
                {
                    continue;
                }

                if (line.size() > kMaxCsvLineLength)
                {
                    console::printError("Skipping oversized CSV row (line too long).");
                    continue;
                }

                const auto fields = parseCsvLine(line);
                if (fields.size() != kExpectedCsvColumnCount)
                {
                    console::printError("Skipping malformed CSV row: " + sanitizeForTerminal(line));
                    continue;
                }

                if (isMeaningfulCsvRow(fields))
                {
                    totalRowBytes += line.size();
                    for (const auto &field : fields)
                    {
                        totalRowBytes += field.size();
                    }

                    if (totalRowBytes > kMaxCsvMemoryBytes)
                    {
                        fatalError = "CSV content exceeds in-memory safety limit (10MB).";
                        return {};
                    }

                    rows.push_back(CsvRecord{line, fields});
                    if (rows.size() >= kMaxCsvRules)
                    {
                        console::printError("CSV row limit reached (5000). Extra rows will be ignored.");
                        break;
                    }
                }
            }

            return rows;
        }

        std::string normalizedField(const CsvRecord &record, std::size_t index)
        {
            if (record.fields.size() <= index)
            {
                return std::string();
            }

            return trim(record.fields[index]);
        }

        bool buildCsvMatchRule(const CsvRecord &record, CsvMatchRule &rule, std::string &error)
        {
            const std::string pokemon = normalizedField(record, kPokemonColumnIndex);
            const std::string nature = normalizedField(record, kNatureColumnIndex);
            const std::string hp = normalizedField(record, kHpColumnIndex);
            const std::string atk = normalizedField(record, kAtkColumnIndex);
            const std::string def = normalizedField(record, kDefColumnIndex);
            const std::string spAtk = normalizedField(record, kSpAtkColumnIndex);
            const std::string spDef = normalizedField(record, kSpDefColumnIndex);
            const std::string spd = normalizedField(record, kSpdColumnIndex);
            const std::string item = normalizedField(record, kItemColumnIndex);
            const std::string maxBuy = normalizedField(record, kMaxBuyColumnIndex);
            const std::string minIv = normalizedField(record, kMinIvColumnIndex);


            if (maxBuy.empty())
            {
                error = "'MaxBuy' must not be blank";
                return false;
            }

            const auto maxBuyValue = parseWholeNumber(maxBuy);
            if (!maxBuyValue)
            {
                error = "'MaxBuy' must be a whole number (digits with optional commas)";
                return false;
            }

            const bool hasPokemon = !pokemon.empty();
            const bool hasItem = !item.empty();
            if (hasPokemon == hasItem)
            {
                error = "row must include either 'Pokemon' or 'Item', but not both";
                return false;
            }

            rule.requiredFields.clear();
            rule.maxBuy = *maxBuyValue;
            if (hasItem)
            {
                rule.mode = "item";
                rule.requiredFields.push_back(item);
                rule.primaryMatchText = item;
                return true;
            }

            rule.mode = "pokemon";
            rule.requiredFields.push_back(pokemon);
            rule.primaryMatchText = pokemon;
            rule.nature = nature;

            const std::array<std::string, 7> optionalPokemonFields{nature, hp, atk, def, spAtk, spDef, spd};
            for (const auto &field : optionalPokemonFields)
            {
                if (!field.empty())
                {
                    rule.requiredFields.push_back(field);
                }
            }

            if (!minIv.empty())
            {
                const auto minIvValue = parseWholeNumber(minIv);
                if (!minIvValue || *minIvValue > 186)
                {
                    error = "'MinIV' must be a whole number between 0 and 186";
                    return false;
                }
                rule.minIV = static_cast<int>(*minIvValue);
            }

            return true;
        }
    }

    std::vector<PreparedCsvRule> loadPreparedRules(const std::filesystem::path &csvPath, std::string &fatalError)
    {
        const auto rows = loadCsvRows(csvPath, fatalError);
        if (!fatalError.empty())
        {
            return {};
        }

        std::vector<PreparedCsvRule> preparedRules;
        preparedRules.reserve(rows.size());

        for (const auto &row : rows)
        {
            CsvMatchRule rule;
            std::string validationError;
            if (!buildCsvMatchRule(row, rule, validationError))
            {
                console::printError("Skipping invalid CSV row: '" + sanitizeForTerminal(row.rawLine) + "' (" + validationError + ").");
                continue;
            }

            preparedRules.push_back(PreparedCsvRule{rule, row.rawLine});
        }

        return preparedRules;
    }
}

