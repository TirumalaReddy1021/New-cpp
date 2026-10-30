#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <iostream>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        std::wcout << L"[Dll1] DLL_PROCESS_ATTACH received. Module handle: " << hModule << std::endl;
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        std::wcout << L"[Dll1] DLL_PROCESS_DETACH received." << std::endl;
        break;
    }
    return TRUE;
}

// Exported test function
extern "C" __declspec(dllexport) void ShowTestMessage() {
    MessageBoxW(nullptr, L"Hello from ACEx Dll1 Exported Function!", L"ACEx PE Module Test", MB_OK | MB_ICONINFORMATION);
}