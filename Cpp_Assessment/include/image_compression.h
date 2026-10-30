#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

using namespace Gdiplus;

enum class ImageFormat {
    PNG,
    JPEG
};

class ImageCompressor {
public:
    ImageCompressor();
    ~ImageCompressor();

    bool CompressBitmap(HBITMAP hBitmap, ImageFormat format, int quality, std::vector<BYTE>& outBuffer);
    bool SaveCompressedToFile(HBITMAP hBitmap, ImageFormat format, int quality, const std::wstring& filePath);

private:
    bool InitializeGDIPlus();
    void ShutdownGDIPlus();
    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

    ULONG_PTR m_gdiplusToken;
    bool m_isInitialized;
};