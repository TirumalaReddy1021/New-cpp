#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>

class Watchdog {
public:
    Watchdog(std::wstring targetPath, DWORD maxRetries = 2, DWORD restartDelayMs = 2000);
    ~Watchdog();

    bool StartMonitoring();
    void StopMonitoring();
    bool InspectInputDesktop() const;
    bool CheckAndMigrateDesktop(); // Newly added method

    DWORD GetLastExitCode() const { return m_lastExitCode; }
    uint32_t GetRestartCount() const { return m_restartCount; }

private:
    void MonitorLoop();

    std::wstring m_targetPath;
    DWORD m_maxRetries;
    DWORD m_restartDelayMs;
    
    HANDLE m_hProcess;
    HANDLE m_hThread;
    std::atomic<bool> m_isRunning;
    std::thread m_monitorThread;
    
    DWORD m_lastExitCode;
    std::atomic<uint32_t> m_restartCount;
};