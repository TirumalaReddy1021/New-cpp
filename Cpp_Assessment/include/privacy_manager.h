#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <string>

class PrivacyManager {
public:
    PrivacyManager(HWND hWnd);
    ~PrivacyManager();

    bool EnableDisplayExclusion(bool enable = true);
    bool IsExclusionActive() const { return m_isExcluded; }

    static void DocumentPlatformLimitations();

private:
    HWND m_hWnd;
    bool m_isExcluded;
};