#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

struct TypingConfig {
    DWORD nominalDelayMs = 85;     // Nominal timing parameter: 85 ms
    double fatigueFactor = 0.10;   // Fatigue factor: 10%
    double slipRate = 0.02;        // Typographical slip-rate parameter: 2%
};

class ScreenEngine {
public:
    ScreenEngine();
    ~ScreenEngine();

    // Screen Capture (GDI)
    bool CaptureScreenRegion(int x, int y, int width, int height, std::wstring& outFilePath);
    
    // Input Automation (SendInput with stochastic timing & Unicode)
    bool SendStochasticUnicodeText(const std::wstring& text, const TypingConfig& config, std::atomic<bool>& cancelToken);

private:
    HBITMAP CopyScreenToBitmap(int x, int y, int width, int height, HDC& hScreenDC);
    bool SaveHBitmapToBMP(HBITMAP hBitmap, const std::wstring& filePath);
};