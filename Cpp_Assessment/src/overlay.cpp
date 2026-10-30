#include "overlay.h"
#include "chat_window.h"
#include <dwmapi.h>
#include <shellapi.h>

#include <stdexcept>
#include <iostream>
#include <cstdio>

#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

// Suppress console window popup automatically for Windows subsystem
#pragma comment(linker, "/SUBSYSTEM:windows")
#pragma comment(linker, "/ENTRY:mainCRTStartup")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <windows.h>

// Windows 10 version 2004+ supports excluding a window from supported capture APIs.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#include <fstream>
#include <vector>
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include <winhttp.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam
);

namespace {

struct ChatMessage {
    std::string role;
    std::string content;
    std::string base64Image;
};

struct ChatState {
    std::mutex mutex;
    std::vector<ChatMessage> messages{
        {"assistant", "Groq AI Companion initialized. Add your API key in Customization & Settings, then send a message or press Ctrl + Alt + S to capture and send a screenshot."}
    };
    std::string status = "Ready";
    bool busy = false;
};

std::string Base64Encode(const BYTE* data, size_t length) {
    static const char encodeLookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((length + 2) / 3) * 4);
    size_t i = 0;
    while (i < length) {
        uint32_t octet_a = i < length ? data[i++] : 0;
        uint32_t octet_b = i < length ? data[i++] : 0;
        uint32_t octet_c = i < length ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        encoded.push_back(encodeLookup[(triple >> 18) & 0x3F]);
        encoded.push_back(encodeLookup[(triple >> 12) & 0x3F]);
        encoded.push_back(i > length + 1 ? '=' : encodeLookup[(triple >> 6) & 0x3F]);
        encoded.push_back(i > length ? '=' : encodeLookup[triple & 0x3F]);
    }
    return encoded;
}

std::string CallGroqChatCompletion(
    const std::string& apiKey,
    const std::vector<ChatMessage>& conversation
) {
    using nlohmann::json;

    bool hasVisionContent = false;
    for (const auto& msg : conversation) {
        if (!msg.base64Image.empty()) {
            hasVisionContent = true;
            break;
        }
    }

    json payload;
    payload["model"] = hasVisionContent ? "llama-3.2-11b-vision-preview" : "openai/gpt-oss-20b";
    payload["messages"] = json::array();
    payload["temperature"] = 0.7;
    payload["max_completion_tokens"] = 1024;

    payload["messages"].push_back({
        {"role", "system"},
        {"content", "You are a helpful AI assistant embedded in a desktop application. Give clear, useful answers and analyze screenshots when provided."}
    });

    for (const auto& message : conversation) {
        if (message.role == "user" || message.role == "assistant" || message.role == "system") {
            if (!message.base64Image.empty()) {
                json contentArray = json::array();
                contentArray.push_back({
                    {"type", "text"},
                    {"text", message.content.empty() ? "Analyze this stealth screenshot:" : message.content}
                });
                contentArray.push_back({
                    {"type", "image_url"},
                    {"image_url", {
                        {"url", "data:image/jpeg;base64," + message.base64Image}
                    }}
                });
                payload["messages"].push_back({
                    {"role", message.role},
                    {"content", contentArray}
                });
            } else {
                payload["messages"].push_back({
                    {"role", message.role},
                    {"content", message.content}
                });
            }
        }
    }

    const std::string requestBody = payload.dump();
    HINTERNET session = WinHttpOpen(
        L"ProjectACEx/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!session) return "Network error: could not initialize WinHTTP.";

    WinHttpSetTimeouts(session, 5000, 5000, 15000, 30000);
    HINTERNET connection = WinHttpConnect(session, L"api.groq.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return "Network error: could not connect to api.groq.com.";
    }

    HINTERNET request = WinHttpOpenRequest(
        connection, L"POST", L"/openai/v1/chat/completions",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
    );
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return "Network error: could not create the HTTPS request.";
    }

    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer ";
    headers.append(apiKey.begin(), apiKey.end());
    headers += L"\r\n";

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    std::string responseBody;
    BOOL sent = WinHttpSendRequest(
        request, headers.c_str(), static_cast<DWORD>(headers.size()),
        const_cast<char*>(requestBody.data()), static_cast<DWORD>(requestBody.size()),
        static_cast<DWORD>(requestBody.size()), 0
    );
    BOOL received = sent ? WinHttpReceiveResponse(request, nullptr) : FALSE;

    if (received) {
        WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX
        );

        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> buffer(available);
            DWORD bytesRead = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &bytesRead) || bytesRead == 0) break;
            responseBody.append(buffer.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!sent || !received) return "Network error: the Groq request failed.";

    json response = json::parse(responseBody, nullptr, false);
    if (response.is_discarded()) return "Groq returned an unreadable response.";

    if (statusCode < 200 || statusCode >= 300) {
        std::string message = "Request failed with HTTP " + std::to_string(statusCode) + ".";
        if (response.contains("error")) {
            const auto& error = response["error"];
            if (error.is_object() && error.contains("message") && error["message"].is_string()) {
                message = error["message"].get<std::string>();
            }
        }
        return "Groq API error: " + message;
    }

    try {
        const auto& content = response.at("choices").at(0).at("message").at("content");
        if (content.is_string()) return content.get<std::string>();
    } catch (const json::exception&) {}
    return "Groq returned a response, but no assistant message was found.";
}

void SendGroqMessage(
    const std::shared_ptr<ChatState>& state,
    const std::string& apiKey,
    const std::string& userText,
    const std::string& base64Image = ""
) {
    std::vector<ChatMessage> conversation;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->busy) {
            state->status = "Please wait for the current response.";
            return;
        }
        state->messages.push_back({"user", userText, base64Image});
        state->busy = true;
        state->status = "Contacting Groq Vision API...";
        conversation = state->messages;
    }

    std::thread([state, apiKey, conversation = std::move(conversation)]() mutable {
        std::string answer = CallGroqChatCompletion(apiKey, conversation);
        std::lock_guard<std::mutex> lock(state->mutex);
        state->messages.push_back({"assistant", std::move(answer), ""});
        state->busy = false;
        state->status = "Ready";
    }).detach();
}

} // namespace

OverlayWindow::OverlayWindow()
    : m_hWnd(nullptr),
      m_width(1280),
      m_height(720),
      m_pd3dDevice(nullptr),
      m_pd3dDeviceContext(nullptr),
      m_pSwapChain(nullptr),
      m_mainRenderTargetView(nullptr),
      m_isInitialized(false),
      m_showDemoWindow(false) {
}

OverlayWindow::~OverlayWindow() {
    Shutdown();
}

bool OverlayWindow::Initialize(HINSTANCE hInstance, int width, int height) {
    m_width = GetSystemMetrics(SM_CXSCREEN);
    m_height = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = nullptr;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = L"ProjectACExOverlayClass";
    wc.hIconSm = nullptr;

    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    m_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Project ACEx HUD Overlay",
        WS_POPUP,
        0, 0,
        m_width, m_height,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hWnd) return false;

    // Privacy: ask Windows to exclude this overlay from supported screen-capture APIs.
    // This is not a guarantee against every capture method or external camera.
    if (!SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE)) {
        // Keep the app usable if the OS/window configuration does not support this setting.
        // GetLastError() can be inspected here while debugging.
    }

    SetLayeredWindowAttributes(m_hWnd, 0, 255, LWA_ALPHA);

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hWnd, &margins);

    if (!CreateDeviceD3D()) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
        return false;
    }

    CreateRenderTarget();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.WindowPadding = ImVec2(12.0f, 12.0f);

    if (!ImGui_ImplWin32_Init(m_hWnd) || !ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
        return false;
    }

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);

    m_isInitialized = true;
    return true;
}

bool OverlayWindow::CreateDeviceD3D() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = m_width;
    sd.BufferDesc.Height = m_height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext
    );

    return SUCCEEDED(hr);
}

void OverlayWindow::CreateRenderTarget() {
    if (!m_pSwapChain || !m_pd3dDevice) return;
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) && pBackBuffer) {
        m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void OverlayWindow::CleanupRenderTarget() {
    if (m_mainRenderTargetView) {
        m_mainRenderTargetView->Release();
        m_mainRenderTargetView = nullptr;
    }
}

void OverlayWindow::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
    if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
    if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() != nullptr) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) return true;
    }

    OverlayWindow* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<OverlayWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis) return pThis->HandleMessage(hWnd, uMsg, wParam, lParam);
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE:
        if (m_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED && m_pSwapChain != nullptr) {
            CleanupRenderTarget();
            if (SUCCEEDED(m_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0))) {
                CreateRenderTarget();
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    std::vector<BYTE> codecInfo(size);
    Gdiplus::ImageCodecInfo* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(codecInfo.data());
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return j;
        }
    }
    return -1;
}

bool TakeScreenshotAsJPEG(const std::wstring& filePath) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreenDC = GetDC(nullptr);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);

    bool success = false;
    {
        Gdiplus::Bitmap bitmap(hBitmap, nullptr);
        CLSID jpegClsid;
        if (GetEncoderClsid(L"image/jpeg", &jpegClsid) != -1) {
            Gdiplus::EncoderParameters encoderParams;
            encoderParams.Count = 1;
            encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
            encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
            ULONG quality = 80;
            encoderParams.Parameter[0].NumberOfValues = 1;
            encoderParams.Parameter[0].Value = &quality;

            success = (bitmap.Save(filePath.c_str(), &jpegClsid, &encoderParams) == Gdiplus::Ok);
        }
    }

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(nullptr, hScreenDC);

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return success;
}

void OverlayWindow::Run() {
    if (!m_isInitialized) return;

    MSG msg = {};
    bool exitRequested = false;
    static bool s_isOverlayVisible = true;
    static bool s_hotkeyHeld = false;
    static bool s_screenshotKeyHeld = false;

    static auto s_chatState = std::make_shared<ChatState>();
    static char s_statusMessage[128] = "Status: Ready";
    static char apiKeyBuffer[256] = "";
    static float windowOpacity = 1.0f;
    static bool showExitModal = false;

    static int s_lastRegionX = -1;
    static int s_lastRegionY = -1;
    static int s_lastRegionWidth = -1;
    static int s_lastRegionHeight = -1;

    while (msg.message != WM_QUIT) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) break;
        }

        if (msg.message == WM_QUIT) break;

        // Hotkey detection
        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool backslashPressed = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;
        bool insertPressed = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        bool f1Pressed = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;

        bool triggerToggle = insertPressed || f1Pressed || (ctrlPressed && backslashPressed);

        if (triggerToggle && !s_hotkeyHeld) {
            s_isOverlayVisible = !s_isOverlayVisible;
            if (s_isOverlayVisible) {
                // Remove the previous region before showing; it will be recalculated
                // after ImGui lays out the sidebar in this frame.
                SetWindowRgn(m_hWnd, nullptr, TRUE);
                s_lastRegionX = -1;
                s_lastRegionY = -1;
                s_lastRegionWidth = -1;
                s_lastRegionHeight = -1;

                ShowWindow(m_hWnd, SW_SHOW);
                SetWindowPos(
                    m_hWnd,
                    HWND_TOPMOST,
                    0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
                );
            } else {
                ShowWindow(m_hWnd, SW_HIDE);
                SetWindowRgn(m_hWnd, nullptr, TRUE);

                s_lastRegionX = -1;
                s_lastRegionY = -1;
                s_lastRegionWidth = -1;
                s_lastRegionHeight = -1;
            }
        }
        s_hotkeyHeld = triggerToggle;
        
        // Ctrl + Alt + S shortcut
        bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool sPressed = (GetAsyncKeyState('S') & 0x8000) != 0;
        bool screenshotShortcut = ctrlPressed && altPressed && sPressed;

        if (screenshotShortcut && !s_screenshotKeyHeld) {
            const std::wstring screenshotPath = L"ProjectACEx_screenshot_" + std::to_wstring(GetTickCount64()) + L".jpg";

            if (TakeScreenshotAsJPEG(screenshotPath)) {
                std::snprintf(s_statusMessage, sizeof(s_statusMessage), "Status: Screenshot saved & sent to AI Vision");
                
                std::ifstream imgFile(screenshotPath, std::ios::binary | std::ios::ate);
                if (imgFile.is_open()) {
                    std::streamsize size = imgFile.tellg();
                    imgFile.seekg(0, std::ios::beg);
                    std::vector<BYTE> buffer((size_t)size);
                    if (imgFile.read(reinterpret_cast<char*>(buffer.data()), size)) {
                        std::string base64Data = Base64Encode(buffer.data(), buffer.size());
                        
                        std::string pathStr;
                        pathStr.reserve(screenshotPath.length());
                        for (wchar_t wc : screenshotPath) {
                            pathStr.push_back(static_cast<char>(wc));
                        }

                        std::string apiKey(apiKeyBuffer);
                        if (apiKey.empty()) {
                            std::lock_guard<std::mutex> lock(s_chatState->mutex);
                            s_chatState->messages.push_back({
                                "system",
                                "[Stealth Screenshot Saved to " + pathStr + "]: Please add your Groq API key in Settings to analyze screenshots automatically."
                            });
                        } else {
                            SendGroqMessage(s_chatState, apiKey, "Analyze this stealth screenshot.", base64Data);
                        }
                    }
                    imgFile.close();
                }
            } else {
                std::snprintf(s_statusMessage, sizeof(s_statusMessage), "Status: Screenshot capture failed");
            }
        }
        s_screenshotKeyHeld = screenshotShortcut;

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Sidebar UI
        if (s_isOverlayVisible) {
            ImGui::SetNextWindowSize(ImVec2(400.0f, 600.0f), ImGuiCond_FirstUseEver);

            ImGui::Begin("Project ACEx - AI Control Panel", &s_isOverlayVisible, ImGuiWindowFlags_NoCollapse);

            ImGui::TextColored(ImVec4(0.25f, 0.75f, 1.0f, 1.0f), "PROJECT ACEx");
            ImGui::SameLine();
            ImGui::TextDisabled("| FPS %.0f", ImGui::GetIO().Framerate);
            ImGui::Separator();

            if (ImGui::Button("Open AI Chat Window", ImVec2(-1.0f, 30.0f))) {
                OpenAIChatWindow(GetModuleHandleW(nullptr));
            }
            if (ImGui::Button("Hide Sidebar", ImVec2(-1.0f, 28.0f))) {
                s_isOverlayVisible = false;
                ShowWindow(m_hWnd, SW_HIDE);
                SetWindowRgn(m_hWnd, nullptr, TRUE);

                s_lastRegionX = -1;
                s_lastRegionY = -1;
                s_lastRegionWidth = -1;
                s_lastRegionHeight = -1;
            }
            if (ImGui::Button("Exit Application", ImVec2(-1.0f, 28.0f))) {
                showExitModal = true;
            }

            // Exit Confirmation Modal Popup
            if (showExitModal) {
                ImGui::OpenPopup("Exit Confirmation");
            }

            if (ImGui::BeginPopupModal("Exit Confirmation", &showExitModal, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Are you sure you want to exit Project ACEx?\n\n");
                ImGui::Separator();
                
                if (ImGui::Button("Exit For Sure", ImVec2(120, 0))) {
                    // Hide immediately so the overlay cannot leave a blank/white layer.
                    exitRequested = true;
                    if (m_hWnd && IsWindow(m_hWnd)) {
                        ShowWindow(m_hWnd, SW_HIDE);
                    }
                    PostQuitMessage(0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Restart", ImVec2(120, 0))) {
                    char path[MAX_PATH];
                    GetModuleFileNameA(NULL, path, MAX_PATH);
                    ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOW);
                    // Hide the current instance before leaving the render loop.
                    exitRequested = true;
                    if (m_hWnd && IsWindow(m_hWnd)) {
                        ShowWindow(m_hWnd, SW_HIDE);
                    }
                    PostQuitMessage(0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    showExitModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            static char userMessageInput[1024] = "";
            static int selectedTheme = 0;

            if (ImGui::BeginTabBar("SidebarTabs")) {
                if (ImGui::BeginTabItem("AI Chat")) {
                    std::vector<ChatMessage> visibleMessages;
                    std::string chatStatus;
                    bool requestBusy = false;
                    {
                        std::lock_guard<std::mutex> lock(s_chatState->mutex);
                        visibleMessages = s_chatState->messages;
                        chatStatus = s_chatState->status;
                        requestBusy = s_chatState->busy;
                    }

                    ImGui::TextDisabled("Groq API | Vision & Chat Enabled");
                    ImGui::Text("Status: %s", chatStatus.c_str());
                    ImGui::Separator();

                    ImGui::BeginChild(
                        "ChatScrollRegion",
                        ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f),
                        true,
                        ImGuiWindowFlags_HorizontalScrollbar
                    );
                    for (const auto& message : visibleMessages) {
                        if (message.role == "user") {
                            ImGui::TextColored(ImVec4(0.35f, 0.80f, 1.0f, 1.0f), "You");
                        } else if (message.role == "system") {
                            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "System Notification");
                        } else {
                            ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.60f, 1.0f), "Groq AI");
                        }
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextUnformatted(message.content.c_str());
                        if (!message.base64Image.empty()) {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[Image Attachment Included]");
                        }
                        ImGui::PopTextWrapPos();
                        ImGui::Spacing();
                        ImGui::Separator();
                    }
                    if (requestBusy) {
                        ImGui::TextDisabled("Generating response...");
                    }
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f) {
                        ImGui::SetScrollHereY(1.0f);
                    }
                    ImGui::EndChild();

                    ImGui::SetNextItemWidth(-72.0f);
                    const bool enterPressed = ImGui::InputText(
                        "##UserInput",
                        userMessageInput,
                        IM_ARRAYSIZE(userMessageInput),
                        ImGuiInputTextFlags_EnterReturnsTrue
                    );
                    ImGui::SameLine();
                    const bool sendClicked = ImGui::Button("Send", ImVec2(64.0f, 0.0f));

                    if ((enterPressed || sendClicked) && !requestBusy) {
                        const std::string userText(userMessageInput);
                        const std::string apiKey(apiKeyBuffer);
                        if (!userText.empty()) {
                            if (apiKey.empty()) {
                                std::lock_guard<std::mutex> lock(s_chatState->mutex);
                                s_chatState->status = "Add your Groq API key in Customization & Settings first.";
                            } else {
                                SendGroqMessage(s_chatState, apiKey, userText, "");
                                userMessageInput[0] = '\0';
                            }
                        }
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Customization & Settings")) {
                    ImGui::Text("UI Theme Customization");
                    if (ImGui::Button("Dark", ImVec2(80.0f, 0.0f))) {
                        ImGui::StyleColorsDark();
                        selectedTheme = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Light", ImVec2(80.0f, 0.0f))) {
                        ImGui::StyleColorsLight();
                        selectedTheme = 1;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Classic", ImVec2(80.0f, 0.0f))) {
                        ImGui::StyleColorsClassic();
                        selectedTheme = 2;
                    }

                    ImGui::Separator();
                    ImGui::Text("Overlay Transparency / Opacity");
                    if (ImGui::SliderFloat("Opacity", &windowOpacity, 0.2f, 1.0f, "%.2f")) {
                        BYTE alpha = static_cast<BYTE>(windowOpacity * 255);
                        SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);
                    }

                    ImGui::Separator();
                    ImGui::Text("Groq API Settings");
                    ImGui::InputText(
                        "Groq API Key",
                        apiKeyBuffer,
                        IM_ARRAYSIZE(apiKeyBuffer),
                        ImGuiInputTextFlags_Password
                    );
                    if (ImGui::Button("Clear API Key", ImVec2(-1.0f, 0.0f))) {
                        SecureZeroMemory(apiKeyBuffer, sizeof(apiKeyBuffer));
                    }

                    ImGui::Separator();
                    ImGui::Text("Keyboard Shortcuts");
                    ImGui::BulletText("Insert / F1 / Ctrl+\\: Toggle sidebar");
                    ImGui::BulletText("Ctrl + Alt + S: Stealth screenshot & AI vision upload");
                    ImGui::TextWrapped("%s", s_statusMessage);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            // Capture sidebar bounding box for mouse click-through clipping
            const ImVec2 sidebarPos = ImGui::GetWindowPos();
            const ImVec2 sidebarSize = ImGui::GetWindowSize();
            ImGui::End();

            // The ImGui title-bar X sets s_isOverlayVisible to false. Synchronize
            // that state with the native HWND as well, otherwise the full-screen
            // overlay window can remain visible and keep intercepting input.
            if (!s_isOverlayVisible) {
                ShowWindow(m_hWnd, SW_HIDE);
                SetWindowRgn(m_hWnd, nullptr, TRUE);
                s_lastRegionX = -1;
                s_lastRegionY = -1;
                s_lastRegionWidth = -1;
                s_lastRegionHeight = -1;
            } else {
                // SetWindowRgn coordinates are relative to the top-left of the
                // top-level window. This overlay is a borderless WS_POPUP window;
                // convert ImGui's client coordinates in case the client origin differs.
                POINT clientOrigin = { 0, 0 };
                if (ClientToScreen(m_hWnd, &clientOrigin)) {
                RECT windowRect = {};
                if (GetWindowRect(m_hWnd, &windowRect)) {
                    const int originOffsetX = clientOrigin.x - windowRect.left;
                    const int originOffsetY = clientOrigin.y - windowRect.top;

                    const int windowWidth = windowRect.right - windowRect.left;
                    const int windowHeight = windowRect.bottom - windowRect.top;

                    int left = static_cast<int>(sidebarPos.x) + originOffsetX;
                    int top = static_cast<int>(sidebarPos.y) + originOffsetY;
                    int right = left + static_cast<int>(sidebarSize.x);
                    int bottom = top + static_cast<int>(sidebarSize.y);

                    // Clip to the top-level window bounds.
                    left = (std::max)(0, (std::min)(left, windowWidth));
                    top = (std::max)(0, (std::min)(top, windowHeight));
                    right = (std::max)(left, (std::min)(right, windowWidth));
                    bottom = (std::max)(top, (std::min)(bottom, windowHeight));

                    const int regionWidth = right - left;
                    const int regionHeight = bottom - top;

                    if (regionWidth > 0 && regionHeight > 0 &&
                        (left != s_lastRegionX ||
                         top != s_lastRegionY ||
                         regionWidth != s_lastRegionWidth ||
                         regionHeight != s_lastRegionHeight)) {

                        HRGN sidebarRegion = CreateRectRgn(left, top, right, bottom);
                        if (sidebarRegion != nullptr) {
                            if (SetWindowRgn(m_hWnd, sidebarRegion, TRUE) != 0) {
                                // Windows owns the region after success.
                                s_lastRegionX = left;
                                s_lastRegionY = top;
                                s_lastRegionWidth = regionWidth;
                                s_lastRegionHeight = regionHeight;
                            } else {
                                DeleteObject(sidebarRegion);
                            }
                        }
                    }
                }
                }
            }
        }

        // Stop before rendering/presenting another frame after confirmed exit.
        if (exitRequested) {
            break;
        }

        // Render
        ImGui::Render();
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        if (m_pd3dDeviceContext && m_mainRenderTargetView) {
            m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
            m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clearColor);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        if (m_pSwapChain) {
            m_pSwapChain->Present(1, 0);
        }
    }

    // Release ImGui, Direct3D, and the native window when Run exits.
    // Shutdown() is safe to call again from the destructor.
    Shutdown();
}

void OverlayWindow::Shutdown() {
    if (m_isInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    CleanupDeviceD3D();

    if (m_hWnd) {
        // Restore default capture behavior before destroying the native window.
        SetWindowDisplayAffinity(m_hWnd, WDA_NONE);
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }

    UnregisterClassW(L"ProjectACExOverlayClass", GetModuleHandle(nullptr));
    m_isInitialized = false;
}