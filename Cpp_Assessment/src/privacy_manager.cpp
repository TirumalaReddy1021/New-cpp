#include "privacy_manager.h"
#include <iostream>

// Ensure WDA_EXCLUDEFROMCAPTURE is defined if older SDK headers are used
#ifndef WDA_NONE
#define WDA_NONE 0x00
#endif

#ifndef WDA_MONITOR
#define WDA_MONITOR 0x01
#endif

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x11
#endif

PrivacyManager::PrivacyManager(HWND hWnd)
    : m_hWnd(hWnd),
      m_isExcluded(false) {}

PrivacyManager::~PrivacyManager() {
    if (m_isExcluded) {
        EnableDisplayExclusion(false);
    }
}

bool PrivacyManager::EnableDisplayExclusion(bool enable) {
    if (!m_hWnd) {
        std::wcerr << L"[Privacy] Invalid HWND provided to PrivacyManager." << std::endl;
        return false;
    }

    DWORD affinity = enable ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    
    // SetWindowDisplayAffinity controls capture protection
    BOOL success = SetWindowDisplayAffinity(m_hWnd, affinity);
    if (!success) {
        DWORD err = GetLastError();
        std::wcerr << L"[Privacy] SetWindowDisplayAffinity failed with error: " << err << std::endl;
        // Document version limitation: WDA_EXCLUDEFROMCAPTURE requires Windows 10 version 2004 or later.
        return false;
    }

    m_isExcluded = enable;
    std::wcout << L"[Privacy] Window display capture exclusion " << (enable ? L"ENABLED" : L"DISABLED") << std::endl;
    return true;
}

void PrivacyManager::DocumentPlatformLimitations() {
    std::wcout << L"=== Windows Display Affinity & Privacy Limitations ===" << std::endl;
    std::wcout << L"1. Requires Windows 10 (Build 19041 / Version 2004) or Windows 11." << std::endl;
    std::wcout << L"2. WDA_EXCLUDEFROMCAPTURE hides the window from BitBlt, PrintWindow, and desktop capture APIs." << std::endl;
    std::wcout << L"3. Note: Display affinity is a privacy control mechanism, not a universal security guarantee " << std::endl;
    std::wcout << L"   against kernel-level debuggers, specialized screen scrapers, or physical recording devices." << std::endl;
}