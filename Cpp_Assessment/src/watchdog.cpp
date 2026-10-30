#include "watchdog.h"
#include <iostream>
#include <vector>

Watchdog::Watchdog(std::wstring targetPath, DWORD maxRetries, DWORD restartDelayMs)
    : m_targetPath(std::move(targetPath)),
      m_maxRetries(maxRetries),
      m_restartDelayMs(restartDelayMs),
      m_hProcess(nullptr),
      m_hThread(nullptr),
      m_isRunning(false),
      m_lastExitCode(0),
      m_restartCount(0) {}

Watchdog::~Watchdog() {
    StopMonitoring();
}

bool Watchdog::InspectInputDesktop() const {
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS);
    if (!hDesk) {
        std::wcerr << L"[Watchdog] OpenInputDesktop failed with error: " << GetLastError() << std::endl;
        return false;
    }
    
    CloseDesktop(hDesk);
    return true;
}

bool Watchdog::CheckAndMigrateDesktop() {
    HDESK hNewDesktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (!hNewDesktop) {
        DWORD err = GetLastError();
        std::wcerr << L"[Watchdog] OpenInputDesktop failed. Access to active desktop restricted or changed. Error: " << err << std::endl;
        return false;
    }

    wchar_t desktopName[256];
    DWORD needed = 0;
    if (GetUserObjectInformationW(hNewDesktop, UOI_NAME, desktopName, sizeof(desktopName), &needed)) {
        std::wcout << L"[Watchdog] Active Input Desktop detected: " << desktopName << std::endl;
        
        if (wcscmp(desktopName, L"Default") != 0) {
            std::wcout << L"[Watchdog] NOTICE: Secure desktop environment active. Adjusting thread desktop." << std::endl;
            if (!SetThreadDesktop(hNewDesktop)) {
                std::wcerr << L"[Watchdog] Failed to assign thread to secure desktop. Error: " << GetLastError() << std::endl;
            }
        }
    }

    CloseDesktop(hNewDesktop);
    return true;
}

bool Watchdog::StartMonitoring() {
    if (m_isRunning) return false;
    m_isRunning = true;
    
    m_monitorThread = std::thread(&Watchdog::MonitorLoop, this);
    return true;
}

void Watchdog::StopMonitoring() {
    if (!m_isRunning) return;
    m_isRunning = false;

    if (m_hProcess) {
        TerminateProcess(m_hProcess, 0);
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
    }

    if (m_hThread) {
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }

    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
}

void Watchdog::MonitorLoop() {
    while (m_isRunning) {
        InspectInputDesktop();
        CheckAndMigrateDesktop();

        // Query the name of the currently active input desktop so we can pass it to STARTUPINFO
        HDESK hCurrDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        std::vector<wchar_t> desktopNameBuf(256, 0);
        DWORD needed = 0;
        if (hCurrDesk) {
            GetUserObjectInformationW(hCurrDesk, UOI_NAME, desktopNameBuf.data(), (DWORD)(desktopNameBuf.size() * sizeof(wchar_t)), &needed);
            CloseDesktop(hCurrDesk);
        }

        STARTUPINFOW si = { sizeof(STARTUPINFOW) };
        // Explicitly set the desktop assignment for the spawned process to match active input station
        si.lpDesktop = (desktopNameBuf[0] != L'\0') ? desktopNameBuf.data() : const_cast<LPWSTR>(L"Default");

        PROCESS_INFORMATION pi = { 0 };

        // Resolve absolute path fallback if generic executable name like "notepad.exe" is given
        std::wstring executablePath = m_targetPath;
        if (executablePath == L"notepad.exe" || executablePath == L"notepad") {
            executablePath = L"C:\\Windows\\System32\\notepad.exe";
        }

        BOOL success = CreateProcessW(
            executablePath.c_str(),
            nullptr,
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        if (!success) {
            std::wcerr << L"[Watchdog] CreateProcessW failed for: " << executablePath << L" Error: " << GetLastError() << std::endl;
            break;
        }

        m_hProcess = pi.hProcess;
        m_hThread = pi.hThread;

        WaitForSingleObject(m_hProcess, INFINITE);

        GetExitCodeProcess(m_hProcess, &m_lastExitCode);
        CloseHandle(m_hProcess);
        CloseHandle(m_hThread);
        m_hProcess = nullptr;
        m_hThread = nullptr;

        std::wcout << L"[Watchdog] Process terminated with exit code: " << m_lastExitCode << std::endl;

        if (!m_isRunning) break;

        if (m_restartCount >= m_maxRetries) {
            std::wcerr << L"[Watchdog] Max retry limit (" << m_maxRetries << L") reached. Halting supervision." << std::endl;
            break;
        }

        m_restartCount++;
        Sleep(m_restartDelayMs);
    }

    m_isRunning = false;
}