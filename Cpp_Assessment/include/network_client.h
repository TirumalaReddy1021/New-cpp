#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <string>
#include <vector>

class NetworkClient {
public:
    NetworkClient(const std::wstring& userAgent = L"ProjectACEx/1.0");
    ~NetworkClient();

    bool SendHttpsPost(const std::wstring& serverHost, const std::wstring& endpoint, 
                       const std::wstring& apiKey, const std::string& jsonPayload, 
                       std::string& outResponse);

    // CryptProtectData / CryptUnprotectData wrappers for secure local storage
    static bool EncryptData(const std::vector<BYTE>& plainText, std::vector<BYTE>& outCipherText);
    static bool DecryptData(const std::vector<BYTE>& cipherText, std::vector<BYTE>& outPlainText);

private:
    std::wstring m_userAgent;
};