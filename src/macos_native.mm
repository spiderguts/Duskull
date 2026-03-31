// Duskull
// macos_native.mm
// Created by spiderguts on 3/29/26 at 11:20 PM.
// Copyright © 2026 spiderguts. All rights reserved.

#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "macos_native.h"

namespace
{
    constexpr CGKeyCode kEscapeKeyCode = 53;

    std::atomic<bool> gEmergencyAbortRequested{false};
    std::atomic<bool> gEmergencyAbortListenerRunning{false};
    std::mutex gEmergencyAbortListenerMutex;
    CFMachPortRef gEmergencyAbortEventTap = nullptr;
    CFRunLoopSourceRef gEmergencyAbortSource = nullptr;
    CFRunLoopRef gEmergencyAbortRunLoop = nullptr;
    std::thread gEmergencyAbortThread;

    CGEventRef emergencyAbortCallback(CGEventTapProxy,
                                      CGEventType type,
                                      CGEventRef event,
                                      void *)
    {
        if (type != kCGEventKeyDown)
        {
            return event;
        }

        const CGKeyCode keyCode = static_cast<CGKeyCode>(CGEventGetIntegerValueField(event,
                                                                                       kCGKeyboardEventKeycode));
        if (keyCode == kEscapeKeyCode)
        {
            gEmergencyAbortRequested.store(true);
        }

        return event;
    }

    const macos_native::HumanInputProfile &defaultHumanInputProfile()
    {
        static const macos_native::HumanInputProfile profile = []
        {
            macos_native::HumanInputProfile p;

            const char *raw = std::getenv("DUSKULL_INPUT_SPEED");
            const std::string speed = (raw != nullptr) ? raw : "normal";

            if (speed == "fast")
            {
                p.moveStepDelayMinMs  = 3;
                p.moveStepDelayMaxMs  = 10;
                p.clickDwellMinMs     = 40;
                p.clickDwellMaxMs     = 120;
                p.clickHoldMinMs      = 20;
                p.clickHoldMaxMs      = 60;
                p.keyInterDelayMinMs  = 30;
                p.keyInterDelayMaxMs  = 100;
                p.keyHoldMinMs        = 15;
                p.keyHoldMaxMs        = 50;
                p.pathJitterPx        = 0.4;
            }
            else if (speed == "cautious")
            {
                p.moveStepDelayMinMs  = 12;
                p.moveStepDelayMaxMs  = 30;
                p.clickDwellMinMs     = 150;
                p.clickDwellMaxMs     = 500;
                p.clickHoldMinMs      = 70;
                p.clickHoldMaxMs      = 200;
                p.keyInterDelayMinMs  = 100;
                p.keyInterDelayMaxMs  = 400;
                p.keyHoldMinMs        = 50;
                p.keyHoldMaxMs        = 160;
                p.pathJitterPx        = 1.4;
            }
            // else: "normal" defaults are already set by the struct initializers

            return p;
        }();

        return profile;
    }

    std::mt19937_64 &rng()
    {
        static thread_local std::mt19937_64 generator([]
                                                       {
                                                           std::random_device device;
                                                           const auto now = static_cast<unsigned long long>(
                                                               std::chrono::high_resolution_clock::now().time_since_epoch().count());
                                                           const auto seedA = static_cast<unsigned long long>(device());
                                                           const auto seedB = static_cast<unsigned long long>(device());
                                                           return seedA ^ (seedB << 1U) ^ (now << 7U);
                                                       }());
        return generator;
    }

    int randomInt(int minValue, int maxValue)
    {
        if (maxValue < minValue)
        {
            std::swap(minValue, maxValue);
        }

        std::uniform_int_distribution<int> distribution(minValue, maxValue);
        return distribution(rng());
    }

    double randomDouble(double minValue, double maxValue)
    {
        if (maxValue < minValue)
        {
            std::swap(minValue, maxValue);
        }

        std::uniform_real_distribution<double> distribution(minValue, maxValue);
        return distribution(rng());
    }

    void sleepForJitteredMs(int minMs, int maxMs)
    {
        const int clampedMin = std::max(0, minMs);
        const int clampedMax = std::max(clampedMin, maxMs);
        const int delayMs = randomInt(clampedMin, clampedMax);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    bool postMouseEvent(CGEventType type,
                        CGPoint point,
                        CGMouseButton button,
                        std::string &error)
    {
        CGEventRef event = CGEventCreateMouseEvent(nullptr, type, point, button);
        if (event == nullptr)
        {
            error = "Failed to create mouse event.";
            return false;
        }

        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        return true;
    }

    bool postKeyboardEvent(CGKeyCode keyCode, bool isDown, std::string &error)
    {
        CGEventRef event = CGEventCreateKeyboardEvent(nullptr, keyCode, isDown);
        if (event == nullptr)
        {
            error = "Failed to create keyboard event.";
            return false;
        }

        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        return true;
    }

    CGPoint cubicBezier(CGPoint p0, CGPoint p1, CGPoint p2, CGPoint p3, double t)
    {
        const double oneMinusT = 1.0 - t;
        const double oneMinusT2 = oneMinusT * oneMinusT;
        const double oneMinusT3 = oneMinusT2 * oneMinusT;
        const double t2 = t * t;
        const double t3 = t2 * t;

        const double x = (oneMinusT3 * p0.x) + (3.0 * oneMinusT2 * t * p1.x) + (3.0 * oneMinusT * t2 * p2.x) + (t3 * p3.x);
        const double y = (oneMinusT3 * p0.y) + (3.0 * oneMinusT2 * t * p1.y) + (3.0 * oneMinusT * t2 * p2.y) + (t3 * p3.y);
        return CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y));
    }

    std::vector<CGPoint> buildHumanizedCurvePath(CGPoint start,
                                                 CGPoint target,
                                                 std::size_t sampleCount,
                                                 double jitterPx)
    {
        std::vector<CGPoint> points;
        points.reserve(sampleCount + 1);

        const double dx = target.x - start.x;
        const double dy = target.y - start.y;
        const double distance = std::hypot(dx, dy);

        if (distance < 1.0)
        {
            points.push_back(target);
            return points;
        }

        const double invDistance = 1.0 / distance;
        const CGPoint tangent = CGPointMake(static_cast<CGFloat>(dx * invDistance),
                                            static_cast<CGFloat>(dy * invDistance));
        const CGPoint normal = CGPointMake(static_cast<CGFloat>(-tangent.y), static_cast<CGFloat>(tangent.x));

        const double controlSpreadA = distance * randomDouble(0.18, 0.36);
        const double controlSpreadB = distance * randomDouble(0.58, 0.82);
        const double bendScale = std::max(3.0, distance * randomDouble(0.03, 0.10));

        const CGPoint control1 = CGPointMake(static_cast<CGFloat>(start.x + tangent.x * controlSpreadA + normal.x * randomDouble(-bendScale, bendScale)),
                                             static_cast<CGFloat>(start.y + tangent.y * controlSpreadA + normal.y * randomDouble(-bendScale, bendScale)));
        const CGPoint control2 = CGPointMake(static_cast<CGFloat>(start.x + tangent.x * controlSpreadB + normal.x * randomDouble(-bendScale, bendScale)),
                                             static_cast<CGFloat>(start.y + tangent.y * controlSpreadB + normal.y * randomDouble(-bendScale, bendScale)));

        for (std::size_t i = 1; i <= sampleCount; ++i)
        {
            double t = static_cast<double>(i) / static_cast<double>(sampleCount);
            // Ease-in/ease-out so acceleration and deceleration feel less robotic.
            t = t * t * (3.0 - 2.0 * t);

            CGPoint point = cubicBezier(start, control1, control2, target, t);
            if (i < sampleCount)
            {
                point.x += static_cast<CGFloat>(randomDouble(-jitterPx, jitterPx));
                point.y += static_cast<CGFloat>(randomDouble(-jitterPx, jitterPx));
            }

            points.push_back(point);
        }

        return points;
    }

    bool postUnicodeCharacter(unichar character, std::string &error)
    {
        CGEventRef keyDown = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(0), true);
        CGEventRef keyUp = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(0), false);
        if (keyDown == nullptr || keyUp == nullptr)
        {
            if (keyDown != nullptr)
            {
                CFRelease(keyDown);
            }
            if (keyUp != nullptr)
            {
                CFRelease(keyUp);
            }

            error = "Failed to create unicode keyboard events.";
            return false;
        }

        CGEventKeyboardSetUnicodeString(keyDown, 1, &character);
        CGEventKeyboardSetUnicodeString(keyUp, 1, &character);
        CGEventPost(kCGHIDEventTap, keyDown);
        CGEventPost(kCGHIDEventTap, keyUp);

        CFRelease(keyDown);
        CFRelease(keyUp);
        return true;
    }

    bool getPrimaryDisplay(SCDisplay **display, std::string &error)
    {
        __block SCShareableContent *content = nil;
        __block NSError *contentError = nil;
        dispatch_semaphore_t contentSemaphore = dispatch_semaphore_create(0);

        [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                   onScreenWindowsOnly:NO
                                                      completionHandler:^(SCShareableContent *shareableContent,
                                                                          NSError *shareableContentError)
         {
            content = shareableContent;
            contentError = shareableContentError;
            dispatch_semaphore_signal(contentSemaphore);
         }];

        dispatch_semaphore_wait(contentSemaphore, DISPATCH_TIME_FOREVER);

        if (contentError != nil)
        {
            error = "ScreenCaptureKit content query failed: " +
                    std::string([[contentError localizedDescription] UTF8String]);
            return false;
        }

        if (content == nil || [content.displays count] == 0)
        {
            error = "No capturable display found.";
            return false;
        }

        if (display == nullptr)
        {
            error = "Display output pointer is null.";
            return false;
        }

        *display = content.displays[0];
        return true;
    }

    bool captureRectWithScreenCaptureKit(CGRect rect, CGImageRef &capturedImage, std::string &error)
    {
        SCDisplay *display = nil;
        if (!getPrimaryDisplay(&display, error))
        {
            return false;
        }

        SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
        SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
        configuration.sourceRect = rect;
        configuration.width = static_cast<size_t>(CGRectGetWidth(rect));
        configuration.height = static_cast<size_t>(CGRectGetHeight(rect));

        __block CGImageRef image = nullptr;
        __block NSError *captureError = nil;
        dispatch_semaphore_t captureSemaphore = dispatch_semaphore_create(0);

        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:configuration
                                  completionHandler:^(CGImageRef captured,
                                                      NSError *screenshotError)
         {
            if (captured != nullptr)
            {
                image = CGImageRetain(captured);
            }
            captureError = screenshotError;
            dispatch_semaphore_signal(captureSemaphore);
         }];

        dispatch_semaphore_wait(captureSemaphore, DISPATCH_TIME_FOREVER);

        if (captureError != nil)
        {
            error = "ScreenCaptureKit screenshot failed: " +
                    std::string([[captureError localizedDescription] UTF8String]);
            return false;
        }

        if (image == nullptr)
        {
            error = "ScreenCaptureKit returned no image.";
            return false;
        }

        capturedImage = image;
        return true;
    }

    std::vector<std::string> recognizeTextFromCgImage(CGImageRef image, std::string &error)
    {
        std::vector<std::string> lines;

        struct OcrToken
        {
            std::string text;
            double minX = 0.0;
            double minY = 0.0;
        };
        std::vector<OcrToken> tokens;

        @autoreleasepool
        {
            if (image == nullptr)
            {
                error = "Invalid image for OCR.";
                return lines;
            }

            NSError *ocrError = nil;
            VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
            request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
            request.usesLanguageCorrection = NO;

            VNImageRequestHandler *handler =
                [[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
            if (![handler performRequests:@[request] error:&ocrError])
            {
                error = "Vision OCR failed: " + std::string([[ocrError localizedDescription] UTF8String]);
                return lines;
            }

            NSArray<VNRecognizedTextObservation *> *results = request.results;
            for (VNRecognizedTextObservation *observation in results)
            {
                NSArray<VNRecognizedText *> *candidates = [observation topCandidates:1];
                if ([candidates count] == 0)
                {
                    continue;
                }

                VNRecognizedText *best = candidates[0];
                if (best.string != nil && [best.string length] > 0)
                {
                    OcrToken token;
                    token.text = [best.string UTF8String];
                    token.minX = observation.boundingBox.origin.x;
                    token.minY = observation.boundingBox.origin.y;
                    tokens.push_back(std::move(token));
                }
            }

            // Vision observation order is not guaranteed. Sort into reading order so
            // downstream parsing (name/nature/IV columns/price) is stable.
            constexpr double kSameRowTolerance = 0.03;
            std::sort(tokens.begin(), tokens.end(), [](const OcrToken &left, const OcrToken &right)
                      {
                          if (std::fabs(left.minY - right.minY) > kSameRowTolerance)
                          {
                              return left.minY > right.minY; // top-to-bottom
                          }

                          return left.minX < right.minX; // left-to-right
                      });

            lines.reserve(tokens.size());
            for (const auto &token : tokens)
            {
                lines.push_back(token.text);
            }
        }

        return lines;
    }
}

namespace macos_native
{
    bool startEmergencyAbortListener(std::string &error)
    {
        std::lock_guard<std::mutex> lock(gEmergencyAbortListenerMutex);

        if (gEmergencyAbortListenerRunning.load())
        {
            return true;
        }

        gEmergencyAbortRequested.store(false);
        gEmergencyAbortEventTap = CGEventTapCreate(kCGSessionEventTap,
                                                   kCGHeadInsertEventTap,
                                                   kCGEventTapOptionListenOnly,
                                                   CGEventMaskBit(kCGEventKeyDown),
                                                   emergencyAbortCallback,
                                                   nullptr);
        if (gEmergencyAbortEventTap == nullptr)
        {
            error = "Failed to create Escape key listener. Grant Input Monitoring and Accessibility permissions in macOS Settings.";
            return false;
        }

        gEmergencyAbortSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,
                                                               gEmergencyAbortEventTap,
                                                               0);
        if (gEmergencyAbortSource == nullptr)
        {
            error = "Failed to create Escape listener run loop source.";
            CFRelease(gEmergencyAbortEventTap);
            gEmergencyAbortEventTap = nullptr;
            return false;
        }

        gEmergencyAbortListenerRunning.store(true);
        gEmergencyAbortThread = std::thread([]
                                            {
            CFRunLoopRef runLoop = CFRunLoopGetCurrent();
            {
                std::lock_guard<std::mutex> lock(gEmergencyAbortListenerMutex);
                gEmergencyAbortRunLoop = runLoop;
                if (gEmergencyAbortSource != nullptr)
                {
                    CFRunLoopAddSource(runLoop, gEmergencyAbortSource, kCFRunLoopCommonModes);
                }
                if (gEmergencyAbortEventTap != nullptr)
                {
                    CGEventTapEnable(gEmergencyAbortEventTap, true);
                }
            }

            CFRunLoopRun();

            std::lock_guard<std::mutex> lock(gEmergencyAbortListenerMutex);
            if (gEmergencyAbortSource != nullptr)
            {
                CFRunLoopRemoveSource(runLoop, gEmergencyAbortSource, kCFRunLoopCommonModes);
            }
            gEmergencyAbortRunLoop = nullptr; });

        return true;
    }

    void stopEmergencyAbortListener()
    {
        {
            std::lock_guard<std::mutex> lock(gEmergencyAbortListenerMutex);
            if (!gEmergencyAbortListenerRunning.load())
            {
                return;
            }

            if (gEmergencyAbortRunLoop != nullptr)
            {
                CFRunLoopStop(gEmergencyAbortRunLoop);
            }
        }

        if (gEmergencyAbortThread.joinable())
        {
            gEmergencyAbortThread.join();
        }

        std::lock_guard<std::mutex> lock(gEmergencyAbortListenerMutex);
        if (gEmergencyAbortSource != nullptr)
        {
            CFRelease(gEmergencyAbortSource);
            gEmergencyAbortSource = nullptr;
        }
        if (gEmergencyAbortEventTap != nullptr)
        {
            CFRelease(gEmergencyAbortEventTap);
            gEmergencyAbortEventTap = nullptr;
        }

        gEmergencyAbortListenerRunning.store(false);
    }

    bool isEmergencyAbortRequested()
    {
        return gEmergencyAbortRequested.load();
    }

    void clearEmergencyAbortRequested()
    {
        gEmergencyAbortRequested.store(false);
    }

    bool getFrontmostApplicationBundleId(std::string &bundleId, std::string &error)
    {
        CFArrayRef windowList = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly |
                                                               kCGWindowListExcludeDesktopElements,
                                                           kCGNullWindowID);
        if (windowList == nullptr)
        {
            error = "Could not read visible window list for frontmost app check.";
            return false;
        }

        const CFIndex count = CFArrayGetCount(windowList);
        for (CFIndex i = 0; i < count; ++i)
        {
            CFDictionaryRef windowInfo = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windowList, i));
            if (windowInfo == nullptr)
            {
                continue;
            }

            CFNumberRef layerNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(windowInfo, kCGWindowLayer));
            int layer = -1;
            if (layerNumber == nullptr || !CFNumberGetValue(layerNumber, kCFNumberIntType, &layer) || layer != 0)
            {
                continue;
            }

            CFStringRef ownerName = static_cast<CFStringRef>(CFDictionaryGetValue(windowInfo, kCGWindowOwnerName));
            if (ownerName == nullptr)
            {
                continue;
            }

            char utf8[512] = {0};
            if (!CFStringGetCString(ownerName, utf8, sizeof(utf8), kCFStringEncodingUTF8))
            {
                continue;
            }

            bundleId = utf8;
            CFRelease(windowList);
            return true;
        }

        CFRelease(windowList);
        error = "Could not identify frontmost application owner.";
        return false;
    }

    bool getPrimaryDisplayMetrics(DisplayMetrics &metrics, std::string &error)
    {
        SCDisplay *display = nil;
        if (!getPrimaryDisplay(&display, error))
        {
            return false;
        }

        const CGRect frame = display.frame;
        metrics.widthPoints = static_cast<long long>(CGRectGetWidth(frame));
        metrics.heightPoints = static_cast<long long>(CGRectGetHeight(frame));
        metrics.widthPixels = static_cast<long long>(display.width);
        metrics.heightPixels = static_cast<long long>(display.height);

        if (metrics.widthPoints <= 0 || metrics.heightPoints <= 0)
        {
            error = "Primary display size is invalid.";
            return false;
        }

        metrics.scaleFactor = static_cast<double>(metrics.widthPixels) /
                              static_cast<double>(metrics.widthPoints);
        if (metrics.scaleFactor <= 0.0)
        {
            metrics.scaleFactor = 1.0;
        }

        return true;
    }

    std::vector<std::string> recognizeTextInRegion(long long x, long long y, long long width, long long height,
                                                   std::string &error)
    {
        std::vector<std::string> lines;
        if (width <= 0 || height <= 0)
        {
            error = "Capture width/height must be positive.";
            return lines;
        }

        CGRect rect = CGRectMake(static_cast<CGFloat>(x),
                                 static_cast<CGFloat>(y),
                                 static_cast<CGFloat>(width),
                                 static_cast<CGFloat>(height));
        CGImageRef image = nullptr;
        if (!captureRectWithScreenCaptureKit(rect, image, error))
        {
            return lines;
        }

        lines = recognizeTextFromCgImage(image, error);
        CGImageRelease(image);
        return lines;
    }

    bool getCursorPosition(long long &x, long long &y, std::string &error)
    {
        CGEventRef event = CGEventCreate(nullptr);
        if (event == nullptr)
        {
            error = "Failed to read cursor position.";
            return false;
        }

        const CGPoint point = CGEventGetLocation(event);
        CFRelease(event);

        x = static_cast<long long>(point.x);
        y = static_cast<long long>(point.y);
        return true;
    }

    bool moveCursorHumanized(long long x, long long y, std::string &error)
    {
        long long currentX = 0;
        long long currentY = 0;
        if (!getCursorPosition(currentX, currentY, error))
        {
            return false;
        }

        const CGPoint start = CGPointMake(static_cast<CGFloat>(currentX), static_cast<CGFloat>(currentY));
        const CGPoint target = CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y));
        const double distance = std::hypot(target.x - start.x, target.y - start.y);

        if (distance < 0.5)
        {
            return postMouseEvent(kCGEventMouseMoved, target, kCGMouseButtonLeft, error);
        }

        const auto &profile = defaultHumanInputProfile();
        const double baseDurationMs = 85.0 + 20.0 * std::pow(distance, 0.58);
        const double durationMs = std::clamp(baseDurationMs * randomDouble(0.90, 1.18), 120.0, 950.0);
        const double avgStepDelayMs = (static_cast<double>(profile.moveStepDelayMinMs) +
                                       static_cast<double>(profile.moveStepDelayMaxMs)) *
                                      0.5;
        const std::size_t sampleCount = static_cast<std::size_t>(std::clamp(durationMs / std::max(1.0, avgStepDelayMs),
                                                                             10.0,
                                                                             120.0));

        std::vector<CGPoint> path = buildHumanizedCurvePath(start,
                                                            target,
                                                            sampleCount,
                                                            profile.pathJitterPx);
        if (path.empty())
        {
            path.push_back(target);
        }

        for (const CGPoint point : path)
        {
            if (isEmergencyAbortRequested())
            {
                error = "Input automation canceled by emergency abort.";
                return false;
            }

            if (!postMouseEvent(kCGEventMouseMoved, point, kCGMouseButtonLeft, error))
            {
                return false;
            }

            sleepForJitteredMs(profile.moveStepDelayMinMs, profile.moveStepDelayMaxMs);
        }

        return postMouseEvent(kCGEventMouseMoved, target, kCGMouseButtonLeft, error);
    }

    bool clickAt(long long x, long long y, std::string &error)
    {
        if (!moveCursorHumanized(x, y, error))
        {
            return false;
        }

        const auto &profile = defaultHumanInputProfile();
        const CGPoint point = CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y));

        sleepForJitteredMs(profile.clickDwellMinMs, profile.clickDwellMaxMs);
        if (!postMouseEvent(kCGEventLeftMouseDown, point, kCGMouseButtonLeft, error))
        {
            return false;
        }

        sleepForJitteredMs(profile.clickHoldMinMs, profile.clickHoldMaxMs);
        if (!postMouseEvent(kCGEventLeftMouseUp, point, kCGMouseButtonLeft, error))
        {
            return false;
        }

        sleepForJitteredMs(20, 80);
        return true;
    }

    bool pressKeyHumanized(int keyCode, std::string &error)
    {
        if (keyCode < 0 || keyCode > static_cast<int>(std::numeric_limits<std::uint16_t>::max()))
        {
            error = "Invalid macOS key code for pressKeyHumanized.";
            return false;
        }

        if (isEmergencyAbortRequested())
        {
            error = "Input automation canceled by emergency abort.";
            return false;
        }

        const auto &profile = defaultHumanInputProfile();
        const CGKeyCode key = static_cast<CGKeyCode>(keyCode);

        sleepForJitteredMs(profile.keyInterDelayMinMs, profile.keyInterDelayMaxMs);
        if (!postKeyboardEvent(key, true, error))
        {
            return false;
        }

        sleepForJitteredMs(profile.keyHoldMinMs, profile.keyHoldMaxMs);
        return postKeyboardEvent(key, false, error);
    }

    bool typeTextHumanized(const std::string &text, std::string &error)
    {
        if (text.empty())
        {
            return true;
        }

        NSString *nsText = [NSString stringWithUTF8String:text.c_str()];
        if (nsText == nil)
        {
            error = "Invalid UTF-8 text passed to typeTextHumanized.";
            return false;
        }

        const auto &profile = defaultHumanInputProfile();
        const NSUInteger length = [nsText length];
        for (NSUInteger index = 0; index < length; ++index)
        {
            if (isEmergencyAbortRequested())
            {
                error = "Input automation canceled by emergency abort.";
                return false;
            }

            const unichar character = [nsText characterAtIndex:index];
            int minDelay = profile.keyInterDelayMinMs;
            int maxDelay = profile.keyInterDelayMaxMs;
            if ([[NSCharacterSet whitespaceAndNewlineCharacterSet] characterIsMember:character])
            {
                minDelay += 40;
                maxDelay += 140;
            }

            sleepForJitteredMs(minDelay, maxDelay);
            if (!postUnicodeCharacter(character, error))
            {
                return false;
            }

            sleepForJitteredMs(profile.keyHoldMinMs, profile.keyHoldMaxMs);
        }

        return true;
    }

}
