#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>

// Function pointer signature for the DLL entry point
typedef BOOL(WINAPI* DLL_MAIN)(HMODULE, DWORD, LPVOID);

// Reflective mapping function
bool ManuallyMapAndExecuteDLL(const std::wstring& dllPath) {
    std::wcout << L"\n[Reflective Loader] Loading raw bytes of: " << dllPath << std::endl;

    // 1. Read DLL file into a local buffer
    std::ifstream file(dllPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::wcerr << L"[!] Failed to open DLL file for manual mapping." << std::endl;
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> fileBuffer(static_cast<size_t>(fileSize));
    if (!file.read(fileBuffer.data(), fileSize)) {
        std::wcerr << L"[!] Failed to read DLL contents into buffer." << std::endl;
        return false;
    }

    // 2. Parse PE Headers from buffer
    PIMAGE_DOS_HEADER pDosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(fileBuffer.data());
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        std::wcerr << L"[!] Invalid DOS signature in DLL buffer." << std::endl;
        return false;
    }

    PIMAGE_NT_HEADERS64 pNtHeaders = reinterpret_cast<PIMAGE_NT_HEADERS64>(fileBuffer.data() + pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
        std::wcerr << L"[!] Invalid NT signature in DLL buffer." << std::endl;
        return false;
    }

    // 3. Allocate virtual memory for the PE image (Prefer original ImageBase, fallback to system allocation)
    SIZE_T imageSize = pNtHeaders->OptionalHeader.SizeOfImage;
    LPVOID pAllocatedMemory = VirtualAlloc(
        reinterpret_cast<LPVOID>(pNtHeaders->OptionalHeader.ImageBase),
        imageSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE
    );

    if (!pAllocatedMemory) {
        // Fallback: Let Windows choose base address if preferred base is occupied
        pAllocatedMemory = VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!pAllocatedMemory) {
            std::wcerr << L"[!] VirtualAlloc failed to allocate memory for PE image: " << GetLastError() << std::endl;
            return false;
        }
    }

    std::wcout << L"[+] Allocated PE image memory at: " << pAllocatedMemory << std::endl;

    // 4. Copy PE Header and Sections into allocated memory
    memcpy(pAllocatedMemory, fileBuffer.data(), pNtHeaders->OptionalHeader.SizeOfHeaders);

    PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
    for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i) {
        LPVOID pSectionDestination = reinterpret_cast<LPBYTE>(pAllocatedMemory) + pSectionHeader[i].VirtualAddress;
        LPVOID pSectionSource = fileBuffer.data() + pSectionHeader[i].PointerToRawData;
        
        if (pSectionHeader[i].SizeOfRawData > 0) {
            memcpy(pSectionDestination, pSectionSource, pSectionHeader[i].SizeOfRawData);
        }
    }

    // 5. Perform Base Relocations if allocated address differs from preferred ImageBase
    DWORD_PTR deltaRelocation = reinterpret_cast<DWORD_PTR>(pAllocatedMemory) - pNtHeaders->OptionalHeader.ImageBase;
    if (deltaRelocation != 0) {
        IMAGE_DATA_DIRECTORY relocDir = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (relocDir.Size > 0) {
            PIMAGE_BASE_RELOCATION pBaseReloc = reinterpret_cast<PIMAGE_BASE_RELOCATION>(
                reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + relocDir.VirtualAddress
            );

            while (pBaseReloc->VirtualAddress != 0) {
                DWORD numberOfEntries = (pBaseReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                PWORD pRelocData = reinterpret_cast<PWORD>(pBaseReloc + 1);

                for (DWORD i = 0; i < numberOfEntries; ++i) {
                    WORD type = pRelocData[i] >> 12;
                    WORD offset = pRelocData[i] & 0x0FFF;

                    if (type == IMAGE_REL_BASED_DIR64) {
                        PDWORD_PTR pPatchAddress = reinterpret_cast<PDWORD_PTR>(
                            reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + pBaseReloc->VirtualAddress + offset
                        );
                        *pPatchAddress += deltaRelocation;
                    }
                }
                pBaseReloc = reinterpret_cast<PIMAGE_BASE_RELOCATION>(
                    reinterpret_cast<BYTE*>(pBaseReloc) + pBaseReloc->SizeOfBlock
                );
            }
            std::wcout << L"[+] Base relocations applied successfully." << std::endl;
        }
    }

    // 6. Resolve Import Address Table (IAT)
    IMAGE_DATA_DIRECTORY importDir = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.Size > 0) {
        PIMAGE_IMPORT_DESCRIPTOR pImportDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
            reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + importDir.VirtualAddress
        );

        while (pImportDesc->Name != 0) {
            LPCSTR moduleName = reinterpret_cast<LPCSTR>(reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + pImportDesc->Name);
            HMODULE hDll = LoadLibraryA(moduleName); // Load required dependent system DLLs

            if (hDll) {
                PIMAGE_THUNK_DATA pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(
                    reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + pImportDesc->FirstThunk
                );
                PIMAGE_THUNK_DATA pOriginalThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(
                    reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + (pImportDesc->OriginalFirstThunk ? pImportDesc->OriginalFirstThunk : pImportDesc->FirstThunk)
                );

                for (; pOriginalThunk->u1.AddressOfData != 0; ++pThunk, ++pOriginalThunk) {
                    if (IMAGE_SNAP_BY_ORDINAL(pOriginalThunk->u1.Ordinal)) {
                        pThunk->u1.Function = reinterpret_cast<DWORD_PTR>(GetProcAddress(hDll, reinterpret_cast<LPCSTR>(IMAGE_ORDINAL(pOriginalThunk->u1.Ordinal))));
                    } else {
                        PIMAGE_IMPORT_BY_NAME pIBN = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                            reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + pOriginalThunk->u1.AddressOfData
                        );
                        pThunk->u1.Function = reinterpret_cast<DWORD_PTR>(GetProcAddress(hDll, pIBN->Name));
                    }
                }
            }
            pImportDesc++;
        }
        std::wcout << L"[+] Import Address Table (IAT) resolved successfully." << std::endl;
    }

    // 7. Manually Invoke DllMain (DLL_PROCESS_ATTACH) without OS loader registration
    DWORD_PTR entryPointRVA = pNtHeaders->OptionalHeader.AddressOfEntryPoint;
    if (entryPointRVA != 0) {
        DLL_MAIN pDllMain = reinterpret_cast<DLL_MAIN>(reinterpret_cast<DWORD_PTR>(pAllocatedMemory) + entryPointRVA);
        std::wcout << L"[+] Invoking manual DllMain at: " << (LPVOID)pDllMain << std::endl;
        
        BOOL success = pDllMain(reinterpret_cast<HMODULE>(pAllocatedMemory), DLL_PROCESS_ATTACH, nullptr);
        if (success) {
            std::wcout << L"[SUCCESS] Reflective in-memory mapping and execution completed!" << std::endl;
            return true;
        }
    }

    return false;
}

int wmain(int argc, wchar_t* argv[]) {
    std::wcout << L"=== Project ACEx — Reflective Loader Research Harness ===" << std::endl;
    std::wstring targetDll = (argc > 1) ? argv[1] : L"Dll1.dll";
    
    ManuallyMapAndExecuteDLL(targetDll);
    return 0;
}