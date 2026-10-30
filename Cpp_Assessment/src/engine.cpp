#include "engine.h"
#include <random>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>

ScreenEngine::ScreenEngine() {}

ScreenEngine::~ScreenEngine() {}

HBITMAP ScreenEngine::CopyScreenToBitmap(int x, int y, int width, int height, HDC& hScreenDC) {
    hScreenDC = GetDC(nullptr);
    if (!hScreenDC) return nullptr;

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        ReleaseDC(nullptr, hScreenDC);
        return nullptr;
    }

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!hBitmap) {
        DeleteDC(hMemoryDC);
        ReleaseDC(nullptr, hScreenDC);
        return nullptr;
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, x, y, SRCCOPY | CAPTUREBLT);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteDC(hMemoryDC);
    return hBitmap;
}

bool ScreenEngine::SaveHBitmapToBMP(HBITMAP hBitmap, const std::wstring& filePath) {
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);

    BITMAPINFOHEADER bi;
    ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = bmp.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    DWORD dwBmpSize = ((bmp.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmp.bmHeight;
    HANDLE hDIB = GlobalAlloc(GHND, dwBmpSize);
    char* lpbitmap = (char*)GlobalLock(hDIB);

    HDC hDC = GetDC(nullptr);
    GetDIBits(hDC, hBitmap, 0, (UINT)bmp.bmHeight, lpbitmap, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hDC);

    // Create BMP file structure
    BITMAPFILEHEADER bmfHeader;
    ZeroMemory(&bmfHeader, sizeof(BITMAPFILEHEADER));
    bmfHeader.bfType = 0x4D42; // "BM"
    bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = bmfHeader.bfOffBits + dwBmpSize;

    std::ofstream file(filePath, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        GlobalUnlock(hDIB);
        GlobalFree(hDIB);
        return false;
    }

    file.write((const char*)&bmfHeader, sizeof(BITMAPFILEHEADER));
    file.write((const char*)&bi, sizeof(BITMAPINFOHEADER));
    file.write(lpbitmap, dwBmpSize);
    file.close();

    GlobalUnlock(hDIB);
    GlobalFree(hDIB);
    return true;
}

bool ScreenEngine::CaptureScreenRegion(int x, int y, int width, int height, std::wstring& outFilePath) {
    HDC hScreenDC = nullptr;
    HBITMAP hBitmap = CopyScreenToBitmap(x, y, width, height, hScreenDC);
    if (!hBitmap) return false;

    outFilePath = L"acex_capture.bmp";
    bool success = SaveHBitmapToBMP(hBitmap, outFilePath);

    DeleteObject(hBitmap);
    if (hScreenDC) ReleaseDC(nullptr, hScreenDC);

    return success;
}

bool ScreenEngine::SendStochasticUnicodeText(const std::wstring& text, const TypingConfig& config, std::atomic<bool>& cancelToken) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> slipDist(0.0, 1.0);
    
    // Normal distribution for stochastic timing variation & fatigue
    size_t totalChars = text.length();

    for (size_t i = 0; i < totalChars; ++i) {
        if (cancelToken) {
            std::wcout << L"[Engine] Input automation cancelled by user." << std::endl;
            return false;
        }

        wchar_t ch = text[i];

        // Simulate typographical slip (2% rate)
        if (slipDist(gen) < config.slipRate && totalChars > 1) {
            // Send an adjacent dummy keystroke or backspace simulation
            INPUT slipInput[2] = {};
            slipInput[0].type = INPUT_KEYBOARD;
            slipInput[0].ki.wVk = VK_SPACE;
            slipInput[1].type = INPUT_KEYBOARD;
            slipInput[1].ki.wVk = VK_SPACE;
            slipInput[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, slipInput, sizeof(INPUT));
        }

        // Send actual Unicode character via KEYEVENTF_UNICODE
        INPUT inputs[2] = {};
        
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = ch;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        UINT sent = SendInput(2, inputs, sizeof(INPUT));
        if (sent != 2) {
            std::wcerr << L"[Engine] SendInput failed or partially blocked. Error: " << GetLastError() << std::endl;
        }

        // Calculate stochastic delay with fatigue factor progression
        double fatigueMultiplier = 1.0 + (static_cast<double>(i) / totalChars) * config.fatigueFactor;
        std::normal_distribution<double> delayDist(config.nominalDelayMs * fatigueMultiplier, config.nominalDelayMs * 0.2);
        
        long long calculatedDelay = static_cast<long long>(delayDist(gen));
        if (calculatedDelay < 10) calculatedDelay = 10;

        std::this_thread::sleep_for(std::chrono::milliseconds(calculatedDelay));
    }

    return true;
}