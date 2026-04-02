// Duskull
// duskull_util.h
// Created by Duskull Project on 4/2/26 at 12:51 AM.
// Copyright © 2026 Duskull Project. All rights reserved.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

namespace duskull::util
{
    inline std::string trim(std::string_view text)
    {
        const auto isSpace = [](unsigned char ch)
        {
            return std::isspace(ch) != 0;
        };

        std::size_t start = 0;
        while (start < text.size() && isSpace(static_cast<unsigned char>(text[start])))
        {
            ++start;
        }

        std::size_t end = text.size();
        while (end > start && isSpace(static_cast<unsigned char>(text[end - 1])))
        {
            --end;
        }

        return std::string(text.substr(start, end - start));
    }

    inline std::string sanitizeForTerminal(std::string_view text)
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

    inline std::string toLowerAscii(std::string_view text)
    {
        std::string lowered;
        lowered.reserve(text.size());
        for (const char ch : text)
        {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return lowered;
    }

    inline bool equalsIgnoreCaseAscii(std::string_view left, std::string_view right)
    {
        if (left.size() != right.size())
        {
            return false;
        }

        return std::equal(left.begin(), left.end(), right.begin(),
                          [](unsigned char lhs, unsigned char rhs)
                          {
                              return std::tolower(lhs) == std::tolower(rhs);
                          });
    }

    inline std::optional<long long> parseWholeNumber(std::string_view text)
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

    inline std::optional<long long> parseSignedInteger(std::string_view text)
    {
        const std::string cleaned = trim(text);
        if (cleaned.empty())
        {
            return std::nullopt;
        }

        try
        {
            std::size_t consumed = 0;
            const long long value = std::stoll(cleaned, &consumed, 10);
            if (consumed != cleaned.size())
            {
                return std::nullopt;
            }

            return value;
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

    inline bool isTruthyEnvValue(const char *raw)
    {
        if (raw == nullptr)
        {
            return false;
        }

        const std::string value = trim(raw);
        return equalsIgnoreCaseAscii(value, "1") ||
               equalsIgnoreCaseAscii(value, "true") ||
               equalsIgnoreCaseAscii(value, "yes") ||
               equalsIgnoreCaseAscii(value, "on");
    }

    inline std::string readTrimmedEnv(const char *name)
    {
        const char *raw = std::getenv(name);
        return raw == nullptr ? std::string() : trim(raw);
    }

    inline bool existingPathContainsSymlink(const std::filesystem::path &path,
                                            std::filesystem::path &symlinkPath)
    {
        if (path.empty())
        {
            return false;
        }

        const auto normalized = path.lexically_normal();
        std::filesystem::path current = normalized.root_path();

        for (const auto &part : normalized.relative_path())
        {
            current /= part;

            std::error_code statusError;
            const auto status = std::filesystem::symlink_status(current, statusError);
            if (statusError)
            {
                continue;
            }

            if (!std::filesystem::exists(status))
            {
                break;
            }

            if (std::filesystem::is_symlink(status))
            {
                symlinkPath = current;
                return true;
            }
        }

        return false;
    }

    inline bool setOwnerOnlyPermissions(const std::filesystem::path &targetPath,
                                        std::string &error)
    {
        std::error_code permissionsError;
        std::filesystem::permissions(targetPath,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     permissionsError);
        if (permissionsError)
        {
            error = "Failed to set secure file permissions on '" + targetPath.string() + "': " +
                    permissionsError.message();
            return false;
        }

        return true;
    }

    inline bool writeTextFileAtomically(const std::filesystem::path &targetPath,
                                        std::string_view content,
                                        std::string &error)
    {
        const std::filesystem::path parent = targetPath.parent_path();
        if (!parent.empty())
        {
            std::filesystem::path symlinkPath;
            if (existingPathContainsSymlink(parent, symlinkPath))
            {
                error = "Refusing to write through symlinked directory component: '" + symlinkPath.string() + "'.";
                return false;
            }

            std::error_code dirError;
            std::filesystem::create_directories(parent, dirError);
            if (dirError)
            {
                error = "Failed to create directory: " + dirError.message();
                return false;
            }

            if (existingPathContainsSymlink(parent, symlinkPath))
            {
                error = "Refusing to write through symlinked directory component: '" + symlinkPath.string() + "'.";
                return false;
            }
        }

        std::error_code statusError;
        const auto targetStatus = std::filesystem::symlink_status(targetPath, statusError);
        if (!statusError && std::filesystem::exists(targetStatus) && std::filesystem::is_symlink(targetStatus))
        {
            error = "Refusing to write through symlinked target: '" + targetPath.string() + "'.";
            return false;
        }

        std::random_device rd;
        std::stringstream suffix;
        suffix << ".tmp." << std::hex << rd();
        const std::filesystem::path tempPath = targetPath.string() + suffix.str();

        {
            std::ofstream tempFile(tempPath, std::ios::out | std::ios::trunc);
            if (!tempFile)
            {
                error = "Failed to open temp file for writing: '" + tempPath.string() + "'.";
                return false;
            }

            tempFile << content;
            if (!tempFile.good())
            {
                std::filesystem::remove(tempPath);
                error = "Failed while writing temp file: '" + tempPath.string() + "'.";
                return false;
            }
        }

        std::error_code renameError;
        std::filesystem::rename(tempPath, targetPath, renameError);
        if (!renameError)
        {
            return setOwnerOnlyPermissions(targetPath, error);
        }

        std::error_code secondStatusError;
        const auto secondStatus = std::filesystem::symlink_status(targetPath, secondStatusError);
        if (!secondStatusError && std::filesystem::exists(secondStatus) && std::filesystem::is_symlink(secondStatus))
        {
            std::filesystem::remove(tempPath);
            error = "Refusing to replace symlinked target after rename retry: '" + targetPath.string() + "'.";
            return false;
        }

        std::error_code removeError;
        std::filesystem::remove(targetPath, removeError);
        std::error_code secondRenameError;
        std::filesystem::rename(tempPath, targetPath, secondRenameError);
        if (!secondRenameError)
        {
            return setOwnerOnlyPermissions(targetPath, error);
        }

        std::filesystem::remove(tempPath);
        error = "Failed to atomically replace file '" + targetPath.string() + "': " + secondRenameError.message();
        return false;
    }
}