// Duskull
// windows_platform.cpp
// Created by spiderguts on 3/31/26 at 9:06 AM.
// Copyright © 2026 spiderguts. All rights reserved.
//
// Windows implementation of the macos_native namespace.
// Compiled only on Windows (MSVC cl.exe with Windows 10+ SDK / C++/WinRT).
//
// Implements: display metrics (GDI), screen capture (GDI BitBlt), OCR
// (Windows.Media.Ocr via C++/WinRT), mouse/keyboard input (SendInput),
// cursor position (GetCursorPos), Escape abort listener (WH_KEYBOARD_LL),
// and frontmost-app query (GetForegroundWindow + QueryFullProcessImageName).
//
// Build requirements:
//   cl.exe /std:c++17 /EHsc /permissive- ... windows_platform.cpp
//   /link user32.lib gdi32.lib psapi.lib windowsapp.lib

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // Windows 10+
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "windowsapp.lib")

// C++/WinRT (Windows 10 SDK)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

// IMemoryBufferByteAccess — COM interface for writing raw pixels into SoftwareBitmap
#include <robuffer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "macos_native.h"

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    // ── RNG ──────────────────────────────────────────────────────────────────

    std::mt19937_64 &rng()
    {
        static thread_local std::mt19937_64 gen([]() -> std::uint64_t
                                                {
            std::random_device rd;
            const auto now = static_cast<std::uint64_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            return static_cast<std::uint64_t>(rd()) ^
                   (static_cast<std::uint64_t>(rd()) << 1ULL) ^
                   (now << 7ULL); }());
        return gen;
    }

    int randomInt(int lo, int hi)
    {
        if (hi < lo)
            std::swap(lo, hi);
        return std::uniform_int_distribution<int>(lo, hi)(rng());
    }

    double randomDouble(double lo, double hi)
    {
        if (hi < lo)
            std::swap(lo, hi);
        return std::uniform_real_distribution<double>(lo, hi)(rng());
    }

    void sleepMs(int lo, int hi)
    {
        const int ms = randomInt(std::max(0, lo), std::max(lo, hi));
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    // ── HumanInputProfile ────────────────────────────────────────────────────

    const macos_native::HumanInputProfile &defaultProfile()
    {
        static const macos_native::HumanInputProfile p = []()
        {
            macos_native::HumanInputProfile q;
            const char *raw = std::getenv("DUSKULL_INPUT_SPEED");
            const std::string speed = raw ? raw : "normal";
            if (speed == "fast")
            {
                q.moveStepDelayMinMs = 3;
                q.moveStepDelayMaxMs = 10;
                q.clickDwellMinMs    = 40;
                q.clickDwellMaxMs    = 120;
                q.clickHoldMinMs     = 20;
                q.clickHoldMaxMs     = 60;
                q.keyInterDelayMinMs = 30;
                q.keyInterDelayMaxMs = 100;
                q.keyHoldMinMs       = 15;
                q.keyHoldMaxMs       = 50;
                q.pathJitterPx       = 0.4;
            }
            else if (speed == "cautious")
            {
                q.moveStepDelayMinMs = 12;
                q.moveStepDelayMaxMs = 30;
                q.clickDwellMinMs    = 150;
                q.clickDwellMaxMs    = 500;
                q.clickHoldMinMs     = 70;
                q.clickHoldMaxMs     = 200;
                q.keyInterDelayMinMs = 100;
                q.keyInterDelayMaxMs = 400;
                q.keyHoldMinMs       = 50;
                q.keyHoldMaxMs       = 160;
                q.pathJitterPx       = 1.4;
            }
            return q;
        }();
        return p;
    }

    // ── 2D Bezier mouse path (mirrors macos_native.mm logic) ─────────────────

    struct Pt
    {
        double x, y;
    };

    Pt cubicBezier(Pt p0, Pt p1, Pt p2, Pt p3, double t)
    {
        const double u  = 1.0 - t;
        const double u2 = u * u, u3 = u2 * u;
        const double t2 = t * t, t3 = t2 * t;
        return {
            u3 * p0.x + 3.0 * u2 * t * p1.x + 3.0 * u * t2 * p2.x + t3 * p3.x,
            u3 * p0.y + 3.0 * u2 * t * p1.y + 3.0 * u * t2 * p2.y + t3 * p3.y
        };
    }

    std::vector<Pt> buildBezierPath(Pt start, Pt end, std::size_t n, double jitter)
    {
        std::vector<Pt> pts;
        pts.reserve(n + 1);

        const double dx   = end.x - start.x;
        const double dy   = end.y - start.y;
        const double dist = std::hypot(dx, dy);
        if (dist < 1.0)
        {
            pts.push_back(end);
            return pts;
        }

        const Pt tan  = {dx / dist, dy / dist};
        const Pt nor  = {-tan.y, tan.x};
        const double sA   = dist * randomDouble(0.18, 0.36);
        const double sB   = dist * randomDouble(0.58, 0.82);
        const double bend = std::max(3.0, dist * randomDouble(0.03, 0.10));

        const Pt c1 = {start.x + tan.x * sA + nor.x * randomDouble(-bend, bend),
                       start.y + tan.y * sA + nor.y * randomDouble(-bend, bend)};
        const Pt c2 = {start.x + tan.x * sB + nor.x * randomDouble(-bend, bend),
                       start.y + tan.y * sB + nor.y * randomDouble(-bend, bend)};

        for (std::size_t i = 1; i <= n; ++i)
        {
            double t = static_cast<double>(i) / static_cast<double>(n);
            t = t * t * (3.0 - 2.0 * t); // smoothstep ease-in/out
            Pt p = cubicBezier(start, c1, c2, end, t);
            if (i < n)
            {
                p.x += randomDouble(-jitter, jitter);
                p.y += randomDouble(-jitter, jitter);
            }
            pts.push_back(p);
        }
        return pts;
    }

    // ── SendInput helpers ─────────────────────────────────────────────────────
    // SendInput MOUSEEVENTF_ABSOLUTE uses a [0, 65535] coordinate space.

    int screenW() { static const int w = std::max(1, GetSystemMetrics(SM_CXSCREEN)); return w; }
    int screenH() { static const int h = std::max(1, GetSystemMetrics(SM_CYSCREEN)); return h; }

    void postMouseMove(double x, double y)
    {
        INPUT inp{};
        inp.type    = INPUT_MOUSE;
        inp.mi.dx   = static_cast<LONG>(std::round(x * 65535.0 / screenW()));
        inp.mi.dy   = static_cast<LONG>(std::round(y * 65535.0 / screenH()));
        inp.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
        SendInput(1, &inp, sizeof(INPUT));
    }

    void postMouseButton(double x, double y, bool down)
    {
        INPUT inp{};
        inp.type    = INPUT_MOUSE;
        inp.mi.dx   = static_cast<LONG>(std::round(x * 65535.0 / screenW()));
        inp.mi.dy   = static_cast<LONG>(std::round(y * 65535.0 / screenH()));
        inp.mi.dwFlags = MOUSEEVENTF_ABSOLUTE |
                         (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
        SendInput(1, &inp, sizeof(INPUT));
    }

    // ── WinRT COM apartment (once per thread) ─────────────────────────────────

    void ensureWinRt()
    {
        thread_local bool done = false;
        if (!done)
        {
            done = true;
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
            }
            catch (const winrt::hresult_error &e)
            {
                // RPC_E_CHANGED_MODE (0x80010106): apartment already initialized
                // with a different type — safe to ignore.
                constexpr int32_t kRpcEChangedMode = static_cast<int32_t>(0x80010106u);
                if (static_cast<int32_t>(e.code()) != kRpcEChangedMode)
                    throw;
            }
        }
    }

    // ── UTF-16 → UTF-8 conversion ─────────────────────────────────────────────

    std::string wideToUtf8(const wchar_t *w, int len = -1)
    {
        if (!w || (len != -1 && len == 0))
            return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
        if (n <= 0)
            return {};
        std::string s(static_cast<std::size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
        // When len == -1, WideCharToMultiByte includes the null terminator in n.
        if (len == -1 && !s.empty() && s.back() == '\0')
            s.pop_back();
        return s;
    }

    // ── Emergency abort: low-level keyboard hook ──────────────────────────────

    std::atomic<bool>  gAbortRequested{false};
    std::atomic<bool>  gListenerRunning{false};
    std::mutex         gListenerMutex;
    std::atomic<DWORD> gListenerThreadId{0};
    std::thread        gListenerThread;

    LRESULT CALLBACK lowLevelKeyProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode == HC_ACTION && wParam == WM_KEYDOWN)
        {
            const KBDLLHOOKSTRUCT *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
            if (kb && kb->vkCode == VK_ESCAPE)
                gAbortRequested.store(true, std::memory_order_relaxed);
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// macos_native namespace — Windows implementations
// ─────────────────────────────────────────────────────────────────────────────
namespace macos_native
{
    bool startEmergencyAbortListener(std::string &error)
    {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (gListenerRunning.load())
            return true;

        gAbortRequested.store(false);
        gListenerRunning.store(true);

        gListenerThread = std::thread([]()
                                      {
            // WH_KEYBOARD_LL requires a message loop in the installing thread.
            HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyProc, nullptr, 0);
            if (!hook)
            {
                gListenerRunning.store(false);
                return;
            }

            gListenerThreadId.store(GetCurrentThreadId());

            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            UnhookWindowsHookEx(hook);
            gListenerThreadId.store(0); });

        return true;
    }

    void stopEmergencyAbortListener()
    {
        {
            std::lock_guard<std::mutex> lock(gListenerMutex);
            if (!gListenerRunning.load())
                return;
            gListenerRunning.store(false);
        }

        const DWORD tid = gListenerThreadId.load();
        if (tid != 0)
            PostThreadMessageW(tid, WM_QUIT, 0, 0);

        if (gListenerThread.joinable())
            gListenerThread.join();
    }

    bool isEmergencyAbortRequested()
    {
        return gAbortRequested.load(std::memory_order_relaxed);
    }

    void clearEmergencyAbortRequested()
    {
        gAbortRequested.store(false, std::memory_order_relaxed);
    }

    bool getFrontmostApplicationBundleId(std::string &bundleId, std::string &error)
    {
        const HWND fg = GetForegroundWindow();
        if (!fg)
        {
            error = "GetForegroundWindow returned null.";
            return false;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid == 0)
        {
            error = "Could not determine process ID for foreground window.";
            return false;
        }

        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!proc)
        {
            error = "OpenProcess failed for foreground window process (error " +
                    std::to_string(GetLastError()) + ").";
            return false;
        }

        wchar_t path[MAX_PATH] = {};
        DWORD   pathLen = MAX_PATH;
        const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &pathLen);
        CloseHandle(proc);

        if (!ok)
        {
            error = "QueryFullProcessImageNameW failed (error " +
                    std::to_string(GetLastError()) + ").";
            return false;
        }

        // Return just the exe filename (e.g. "Temtem.exe") as the Windows
        // analog to a macOS bundle ID.  Users set DUSKULL_EXPECTED_BUNDLE_ID
        // to this filename to enable the frontmost-app guard.
        const std::wstring fullPath(path, pathLen);
        const std::size_t sep = fullPath.rfind(L'\\');
        const std::wstring exeName = (sep != std::wstring::npos)
                                         ? fullPath.substr(sep + 1)
                                         : fullPath;
        bundleId = wideToUtf8(exeName.c_str());
        return true;
    }

    bool getPrimaryDisplayMetrics(DisplayMetrics &metrics, std::string &error)
    {
        const int logW = GetSystemMetrics(SM_CXSCREEN);
        const int logH = GetSystemMetrics(SM_CYSCREEN);
        if (logW <= 0 || logH <= 0)
        {
            error = "GetSystemMetrics returned invalid screen dimensions.";
            return false;
        }

        // On Windows (non-DPI-aware process), GDI logical pixels are the same
        // coordinate space used by SendInput and BitBlt, so scaleFactor = 1.0.
        // The calibration retina-correction path will not activate (it requires
        // scaleFactor > 1.1), which is correct for this configuration.
        metrics.widthPoints  = static_cast<long long>(logW);
        metrics.heightPoints = static_cast<long long>(logH);
        metrics.widthPixels  = metrics.widthPoints;
        metrics.heightPixels = metrics.heightPoints;
        metrics.scaleFactor  = 1.0;
        return true;
    }

    std::vector<std::string> recognizeTextInRegion(long long x, long long y,
                                                    long long width, long long height,
                                                    std::string &error)
    {
        std::vector<std::string> result;
        if (width <= 0 || height <= 0)
        {
            error = "Capture width/height must be positive.";
            return result;
        }

        const int sx = static_cast<int>(x);
        const int sy = static_cast<int>(y);
        const int sw = static_cast<int>(width);
        const int sh = static_cast<int>(height);

        // ── 1. GDI screen capture ─────────────────────────────────────────────
        HDC screenDC = GetDC(nullptr);
        if (!screenDC)
        {
            error = "GetDC failed.";
            return result;
        }

        HDC memDC = CreateCompatibleDC(screenDC);
        if (!memDC)
        {
            ReleaseDC(nullptr, screenDC);
            error = "CreateCompatibleDC failed.";
            return result;
        }

        HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, sw, sh);
        if (!hBitmap)
        {
            DeleteDC(memDC);
            ReleaseDC(nullptr, screenDC);
            error = "CreateCompatibleBitmap failed.";
            return result;
        }

        HGDIOBJ old     = SelectObject(memDC, hBitmap);
        const BOOL blitOk = BitBlt(memDC, 0, 0, sw, sh, screenDC, sx, sy, SRCCOPY);
        SelectObject(memDC, old);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);

        if (!blitOk)
        {
            DeleteObject(hBitmap);
            error = "BitBlt screen capture failed.";
            return result;
        }

        // ── 2. Extract pixel data (BGRX → BGRA) ──────────────────────────────
        BITMAPINFOHEADER bih{};
        bih.biSize        = sizeof(BITMAPINFOHEADER);
        bih.biWidth       = sw;
        bih.biHeight      = -sh; // negative = top-down scanlines
        bih.biPlanes      = 1;
        bih.biBitCount    = 32;
        bih.biCompression = BI_RGB;

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(sw) * sh * 4);
        HDC dcDib  = GetDC(nullptr);
        const int lines = GetDIBits(dcDib, hBitmap, 0, static_cast<UINT>(sh),
                                     pixels.data(),
                                     reinterpret_cast<BITMAPINFO *>(&bih),
                                     DIB_RGB_COLORS);
        ReleaseDC(nullptr, dcDib);
        DeleteObject(hBitmap);

        if (lines == 0)
        {
            error = "GetDIBits failed to read pixel data.";
            return result;
        }

        // GDI BI_RGB 32bpp stores pixels as BGRX (alpha byte is 0).
        // WinRT BitmapPixelFormat::Bgra8 expects fully-opaque BGRA.
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;

        // ── 3. WinRT OCR ──────────────────────────────────────────────────────
        ensureWinRt();
        try
        {
            using namespace winrt::Windows::Graphics::Imaging;
            using namespace winrt::Windows::Media::Ocr;

            SoftwareBitmap bmp(BitmapPixelFormat::Bgra8, sw, sh,
                               BitmapAlphaMode::Premultiplied);
            {
                auto buf       = bmp.LockBuffer(BitmapBufferAccessMode::Write);
                auto ref       = buf.CreateReference();
                auto byteAccess = ref.as<::IMemoryBufferByteAccess>();

                BYTE  *pData = nullptr;
                UINT32 cap   = 0;
                if (FAILED(byteAccess->GetBuffer(&pData, &cap)) || !pData)
                {
                    error = "Failed to lock SoftwareBitmap buffer for writing.";
                    return result;
                }
                const UINT32 copyBytes =
                    std::min(cap, static_cast<UINT32>(pixels.size()));
                std::memcpy(pData, pixels.data(), copyBytes);
            }

            OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine)
            {
                error = "Could not create Windows OCR engine. "
                        "Ensure an OCR language pack is installed "
                        "(Settings \u2192 Time & Language \u2192 Language & Region).";
                return result;
            }

            const OcrResult ocrResult = engine.RecognizeAsync(bmp).get();

            // OcrResult lines are in document reading order (top-to-bottom).
            // Words within each line are left-to-right.
            for (const auto &line : ocrResult.Lines())
            {
                for (const auto &word : line.Words())
                {
                    const std::wstring wText{word.Text().c_str()};
                    if (!wText.empty())
                    {
                        std::string utf8 = wideToUtf8(wText.c_str(),
                                                       static_cast<int>(wText.size()));
                        if (!utf8.empty())
                            result.push_back(std::move(utf8));
                    }
                }
            }
        }
        catch (const winrt::hresult_error &ex)
        {
            char hexBuf[16];
            std::snprintf(hexBuf, sizeof(hexBuf), "%08lX",
                          static_cast<unsigned long>(
                              static_cast<std::uint32_t>(
                                  static_cast<int32_t>(ex.code()))));
            error = "WinRT OCR error (0x" + std::string(hexBuf) + "): " +
                    wideToUtf8(ex.message().c_str());
        }

        return result;
    }

    bool getCursorPosition(long long &x, long long &y, std::string &error)
    {
        POINT pt{};
        if (!GetCursorPos(&pt))
        {
            error = "GetCursorPos failed (error " +
                    std::to_string(GetLastError()) + ").";
            return false;
        }
        x = static_cast<long long>(pt.x);
        y = static_cast<long long>(pt.y);
        return true;
    }

    bool moveCursorHumanized(long long x, long long y, std::string &error)
    {
        long long curX = 0, curY = 0;
        if (!getCursorPosition(curX, curY, error))
            return false;

        const Pt start{static_cast<double>(curX), static_cast<double>(curY)};
        const Pt tgt  {static_cast<double>(x),    static_cast<double>(y)   };
        const double dist = std::hypot(tgt.x - start.x, tgt.y - start.y);

        if (dist < 0.5)
        {
            postMouseMove(tgt.x, tgt.y);
            return true;
        }

        const auto  &prof    = defaultProfile();
        const double baseDur = 85.0 + 20.0 * std::pow(dist, 0.58);
        const double dur     = std::clamp(baseDur * randomDouble(0.90, 1.18), 120.0, 950.0);
        const double avgStep = (static_cast<double>(prof.moveStepDelayMinMs) +
                                static_cast<double>(prof.moveStepDelayMaxMs)) * 0.5;
        const std::size_t n  = static_cast<std::size_t>(
            std::clamp(dur / std::max(1.0, avgStep), 10.0, 120.0));

        auto path = buildBezierPath(start, tgt, n, prof.pathJitterPx);
        if (path.empty())
            path.push_back(tgt);

        for (const Pt &p : path)
        {
            if (isEmergencyAbortRequested())
            {
                error = "Input automation canceled by emergency abort.";
                return false;
            }
            postMouseMove(p.x, p.y);
            sleepMs(prof.moveStepDelayMinMs, prof.moveStepDelayMaxMs);
        }

        postMouseMove(tgt.x, tgt.y);
        return true;
    }

    bool clickAt(long long x, long long y, std::string &error)
    {
        if (!moveCursorHumanized(x, y, error))
            return false;

        const auto  &prof = defaultProfile();
        const double dx   = static_cast<double>(x);
        const double dy   = static_cast<double>(y);

        sleepMs(prof.clickDwellMinMs, prof.clickDwellMaxMs);
        postMouseButton(dx, dy, true);
        sleepMs(prof.clickHoldMinMs, prof.clickHoldMaxMs);
        postMouseButton(dx, dy, false);
        sleepMs(20, 80);
        return true;
    }

    bool pressKeyHumanized(int keyCode, std::string &error)
    {
        if (keyCode <= 0 || keyCode > 0xFE)
        {
            error = "Invalid Windows virtual key code for pressKeyHumanized.";
            return false;
        }
        if (isEmergencyAbortRequested())
        {
            error = "Input automation canceled by emergency abort.";
            return false;
        }

        const auto &prof = defaultProfile();
        const WORD  vk   = static_cast<WORD>(keyCode);

        sleepMs(prof.keyInterDelayMinMs, prof.keyInterDelayMaxMs);

        INPUT down{};
        down.type   = INPUT_KEYBOARD;
        down.ki.wVk = vk;
        SendInput(1, &down, sizeof(INPUT));

        sleepMs(prof.keyHoldMinMs, prof.keyHoldMaxMs);

        INPUT up      = down;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &up, sizeof(INPUT));
        return true;
    }

    bool typeTextHumanized(const std::string &text, std::string &error)
    {
        if (text.empty())
            return true;

        // Convert UTF-8 → UTF-16
        const int wLen = MultiByteToWideChar(CP_UTF8, 0,
                                              text.c_str(), -1,
                                              nullptr, 0);
        if (wLen <= 0)
        {
            error = "Invalid UTF-8 text passed to typeTextHumanized.";
            return false;
        }

        std::vector<wchar_t> wText(static_cast<std::size_t>(wLen));
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wText.data(), wLen);

        const auto &prof = defaultProfile();

        // wLen includes the null terminator; iterate up to wLen - 1.
        for (int i = 0; i < wLen - 1; ++i)
        {
            if (isEmergencyAbortRequested())
            {
                error = "Input automation canceled by emergency abort.";
                return false;
            }

            const wchar_t wc = wText[static_cast<std::size_t>(i)];
            const bool isSpaceChar = (wc == L' ' || wc == L'\t' ||
                                      wc == L'\r' || wc == L'\n');
            const int extraMin = isSpaceChar ? 40  : 0;
            const int extraMax = isSpaceChar ? 140 : 0;

            sleepMs(prof.keyInterDelayMinMs + extraMin,
                    prof.keyInterDelayMaxMs + extraMax);

            INPUT down{};
            down.type       = INPUT_KEYBOARD;
            down.ki.wScan   = wc;
            down.ki.dwFlags = KEYEVENTF_UNICODE;
            SendInput(1, &down, sizeof(INPUT));

            sleepMs(prof.keyHoldMinMs, prof.keyHoldMaxMs);

            INPUT up      = down;
            up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(1, &up, sizeof(INPUT));
        }

        return true;
    }

} // namespace macos_native
