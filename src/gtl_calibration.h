// Duskull
// gtl_calibration.h
// Created by Duskull Project on 3/29/26 at 11:58 PM.
// Copyright © 2026 Duskull Project. All rights reserved.

#pragma once

#include <filesystem>
#include <string>

namespace gtl::calibration
{
    struct CoordinateSet
    {
        long long refreshX = 0;
        long long refreshY = 0;
        long long buyX = 0;
        long long buyY = 0;
        long long cropX = 0;
        long long cropY = 0;
        long long cropWidth = 0;
        long long cropHeight = 0;
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
