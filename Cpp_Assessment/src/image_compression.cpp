#include "image_compression.h"
#include <iostream>

// Link GDI+ library
#pragma comment(lib, "gdiplus.lib")

ImageCompressor::ImageCompressor()
    : m_gdiplusToken(0),
      m_isInitialized(false) {
    m_isInitialized = InitializeGDIPlus();
}

ImageCompressor::~ImageCompressor() {
    ShutdownGDIPlus();
}

bool ImageCompressor::InitializeGDIPlus() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::Status status = Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);
    return (status == Gdiplus::Ok);
}

void ImageCompressor::ShutdownGDIPlus() {
    if (m_isInitialized) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_isInitialized = false;
    }
}

int ImageCompressor::GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    std::vector<BYTE> buffer(size);
    Gdiplus::ImageCodecInfo* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());

    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return j;
        }
    }
    return -1;
}

bool ImageCompressor::CompressBitmap(HBITMAP hBitmap, ImageFormat format, int quality, std::vector<BYTE>& outBuffer) {
    if (!m_isInitialized || !hBitmap) return false;

    // Create GDI+ Bitmap from HBITMAP
    Gdiplus::Bitmap bitmap(hBitmap, nullptr);
    if (bitmap.GetLastStatus() != Gdiplus::Ok) {
        std::wcerr << L"[ImageCompressor] Failed to create GDI+ bitmap from HBITMAP." << std::endl;
        return false;
    }

    // Determine MIME type and encoder CLSID
    const WCHAR* mimeType = (format == ImageFormat::PNG) ? L"image/png" : L"image/jpeg";
    CLSID encoderClsid;
    if (GetEncoderClsid(mimeType, &encoderClsid) == -1) {
        std::wcerr << L"[ImageCompressor] Failed to find image encoder for MIME type." << std::endl;
        return false;
    }

    // Setup encoder parameters for quality (if JPEG)
    EncoderParameters encoderParams;
    EncoderParameters* pEncoderParams = nullptr;

    if (format == ImageFormat::JPEG) {
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = EncoderQuality;
        encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        
        // Clamp quality between 0 and 100
        ULONG qual = static_cast<ULONG>(quality < 0 ? 0 : (quality > 100 ? 100 : quality));
        encoderParams.Parameter[0].Value = &qual;
        pEncoderParams = &encoderParams;
    }

    // Save to IStream (memory stream)
    IStream* pStream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &pStream);
    if (FAILED(hr)) return false;

    Gdiplus::Status status = bitmap.Save(pStream, &encoderClsid, pEncoderParams);
    if (status != Gdiplus::Ok) {
        pStream->Release();
        std::wcerr << L"[ImageCompressor] GDI+ image save to stream failed." << std::endl;
        return false;
    }

    // Extract buffer data from IStream
    STATSTG statstg;
    pStream->Stat(&statstg, STATFLAG_NONAME);
    ULONG dataSize = static_cast<ULONG>(statstg.cbSize.QuadPart);

    outBuffer.resize(dataSize);
    LARGE_INTEGER liZero = { 0 };
    pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);

    ULONG bytesRead = 0;
    pStream->Read(outBuffer.data(), dataSize, &bytesRead);
    pStream->Release();

    return (bytesRead == dataSize);
}

bool ImageCompressor::SaveCompressedToFile(HBITMAP hBitmap, ImageFormat format, int quality, const std::wstring& filePath) {
    std::vector<BYTE> buffer;
    if (!CompressBitmap(hBitmap, format, quality, buffer)) {
        return false;
    }

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    BOOL writeSuccess = WriteFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesWritten, nullptr);
    CloseHandle(hFile);

    return writeSuccess && (bytesWritten == buffer.size());
}