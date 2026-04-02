// Duskull
// gtl_calibration.h
// Created by Duskull Project on 3/29/26 at 11:58 PM.
// Copyright © 2026 Duskull Project. All rights reserved.

#pragma once

#include <filesystem>
#include <string>

namespace gtl::calibration
{
    struct ClickBounds
    {
        long long minX = 0;
        long long maxX = 0;
        long long minY = 0;
        long long maxY = 0;
    };

    struct CoordinateSet
    {
        long long refreshMinX = 0;
        long long refreshMaxX = 0;
        long long refreshMinY = 0;
        long long refreshMaxY = 0;
        long long buyMinX = 0;
        long long buyMaxX = 0;
        long long buyMinY = 0;
        long long buyMaxY = 0;
        long long cropX = 0;
        long long cropY = 0;
        long long cropWidth = 0;
        long long cropHeight = 0;

        [[nodiscard]] ClickBounds refreshBounds() const
        {
            return ClickBounds{refreshMinX, refreshMaxX, refreshMinY, refreshMaxY};
        }

        [[nodiscard]] ClickBounds buyBounds() const
        {
            return ClickBounds{buyMinX, buyMaxX, buyMinY, buyMaxY};
        }

        [[nodiscard]] bool isComplete() const
        {
            return cropWidth > 0 && cropHeight > 0 &&
                   refreshMinX <= refreshMaxX && refreshMinY <= refreshMaxY &&
                   buyMinX <= buyMaxX && buyMinY <= buyMaxY;
        }
    };

    [[nodiscard]] std::filesystem::path defaultCoordinateConfigPath();

    [[nodiscard]] bool loadCoordinatesFromEnv(CoordinateSet &coords, std::string &diagnostic);
    [[nodiscard]] bool loadCoordinatesFromConfig(const std::filesystem::path &configPath,
                                                 CoordinateSet &coords,
                                                 std::string &diagnostic);
    [[nodiscard]] bool saveCoordinatesToConfig(const std::filesystem::path &configPath,
                                               const CoordinateSet &coords,
                                               std::string &error);

    void promptCaptureCoordinates(CoordinateSet &coords);

    [[nodiscard]] bool prepareCoordinatesForCurrentDisplay(CoordinateSet &coords,
                                                           bool saveAdjustedCoordinates,
                                                           const std::filesystem::path &configPath);
}
