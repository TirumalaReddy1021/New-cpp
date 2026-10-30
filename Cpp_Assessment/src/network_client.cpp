#include "network_client.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <iostream>

NetworkClient::NetworkClient(const std::wstring& userAgent)
    : m_userAgent(userAgent) {}

NetworkClient::~NetworkClient() {}

bool NetworkClient::SendHttpsPost(const std::wstring& serverHost, const std::wstring& endpoint, 
                                  const std::wstring& apiKey, const std::string& jsonPayload, 
                                  std::string& outResponse) {
    HINTERNET hSession = WinHttpOpen(
        m_userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession) {
        std::wcerr << L"[Network] WinHttpOpen failed with error: " << GetLastError() << std::endl;
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, serverHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        std::wcerr << L"[Network] WinHttpConnect failed with error: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
        endpoint.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE // Force HTTPS
    );

    if (!hRequest) {
        std::wcerr << L"[Network] WinHttpOpenRequest failed with error: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Prepare headers including Authorization API key and Content-Type
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + apiKey + L"\r\n";

    BOOL sent = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        (LPVOID)jsonPayload.data(),
        (DWORD)jsonPayload.length(),
        (DWORD)jsonPayload.length(),
        0
    );

    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        std::wcerr << L"[Network] Request transmission or response failed. Error: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Read response data
    DWORD bytesAvailable = 0;
    std::vector<char> responseBuffer;

    do {
        DWORD bytesRead = 0;
        WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
        if (bytesAvailable == 0) break;

        std::vector<char> tempBuffer(bytesAvailable);
        if (WinHttpReadData(hRequest, tempBuffer.data(), bytesAvailable, &bytesRead)) {
            responseBuffer.insert(responseBuffer.end(), tempBuffer.begin(), tempBuffer.begin() + bytesRead);
        }
    } while (bytesAvailable > 0);

    outResponse.assign(responseBuffer.begin(), responseBuffer.end());

    // Clean up handles
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return true;
}

bool NetworkClient::EncryptData(const std::vector<BYTE>& plainText, std::vector<BYTE>& outCipherText) {
    DATA_BLOB dataIn;
    DATA_BLOB dataOut;

    dataIn.pbData = const_cast<BYTE*>(plainText.data());
    dataIn.cbData = (DWORD)plainText.size();

    if (!CryptProtectData(&dataIn, L"ACEx Secure Credential", nullptr, nullptr, nullptr, 0, &dataOut)) {
        std::wcerr << L"[Crypto] CryptProtectData failed with error: " << GetLastError() << std::endl;
        return false;
    }

    outCipherText.assign(dataOut.pbData, dataOut.pbData + dataOut.cbData);
    LocalFree(dataOut.pbData);
    return true;
}

bool NetworkClient::DecryptData(const std::vector<BYTE>& cipherText, std::vector<BYTE>& outPlainText) {
    DATA_BLOB dataIn;
    DATA_BLOB dataOut;

    dataIn.pbData = const_cast<BYTE*>(cipherText.data());
    dataIn.cbData = (DWORD)cipherText.size();

    if (!CryptUnprotectData(&dataIn, nullptr, nullptr, nullptr, nullptr, 0, &dataOut)) {
        std::wcerr << L"[Crypto] CryptUnprotectData failed with error: " << GetLastError() << std::endl;
        return false;
    }

    outPlainText.assign(dataOut.pbData, dataOut.pbData + dataOut.cbData);
    LocalFree(dataOut.pbData);
    return true;
}