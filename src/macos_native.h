// Duskull
// macos_native.h
// Created by Duskull Project on 3/29/26 at 11:20 PM.
// Copyright © 2026 Duskull Project. All rights reserved.

#pragma once

#include <string>
#include <vector>

namespace macos_native
{
    struct HumanInputProfile
    {
        int moveStepDelayMinMs = 3;
        int moveStepDelayMaxMs = 11;
        int clickDwellMinMs = 120;
        int clickDwellMaxMs = 420;
        int clickHoldMinMs = 55;
        int clickHoldMaxMs = 180;
        int keyInterDelayMinMs = 24;
        int keyInterDelayMaxMs = 90;
        int keyHoldMinMs = 14;
        int keyHoldMaxMs = 45;
        double pathJitterPx = 0.65;
    };

    struct DisplayMetrics
    {
        long long widthPoints = 0;
        long long heightPoints = 0;
        long long widthPixels = 0;
        long long heightPixels = 0;
        double scaleFactor = 1.0;
    };

    bool getPrimaryDisplayMetrics(DisplayMetrics &metrics, std::string &error);
    std::vector<std::string> recognizeTextInRegion(long long x, long long y, long long width, long long height,
                                                   std::string &error);
    bool getCursorPosition(long long &x, long long &y, std::string &error);
    bool startEmergencyAbortListener(std::string &error);
    void stopEmergencyAbortListener();
    bool isEmergencyAbortRequested();
    void clearEmergencyAbortRequested();
    bool getFrontmostApplicationBundleId(std::string &bundleId, std::string &error);
    bool moveCursorHumanized(long long x, long long y, std::string &error);
    bool clickAt(long long x, long long y, std::string &error);
    bool pressKeyHumanized(int keyCode, std::string &error);
    bool typeTextHumanized(const std::string &text, std::string &error);
}
