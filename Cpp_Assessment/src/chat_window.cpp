#include "chat_window.h"

#include <wrl.h>
#include <WebView2.h>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace {
    HWND g_chatWindow = nullptr;

    ComPtr<ICoreWebView2Controller> g_controller;
    ComPtr<ICoreWebView2> g_webView;

    const wchar_t* WINDOW_CLASS = L"ACExChatClass";

    LRESULT CALLBACK ChatWindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        switch (message) {
        case WM_SIZE:
            if (g_controller) {
                RECT bounds{};
                GetClientRect(hwnd, &bounds);
                g_controller->put_Bounds(bounds);
            }
            return 0;

        case WM_DESTROY:
            if (hwnd == g_chatWindow) {
                g_chatWindow = nullptr;
                g_webView.Reset();
                g_controller.Reset();
            }
            return 0;
        }

        return DefWindowProcW(
            hwnd, message, wParam, lParam);
    }
}

void OpenAIChatWindow(HINSTANCE hInstance)
{
    // If the window already exists, show it again.
    if (g_chatWindow && IsWindow(g_chatWindow)) {
        ShowWindow(g_chatWindow, SW_SHOW);
        SetForegroundWindow(g_chatWindow);
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChatWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(
            nullptr,
            L"Could not register the chat window class.",
            L"Project ACEx",
            MB_OK | MB_ICONERROR);
        return;
    }

    g_chatWindow = CreateWindowExW(
        0,
        WINDOW_CLASS,
        L"Project ACEx - AI Chat Companion",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        650,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!g_chatWindow) {
        MessageBoxW(
            nullptr,
            L"Could not create the chat window.",
            L"Project ACEx",
            MB_OK | MB_ICONERROR);
        return;
    }

    ShowWindow(g_chatWindow, SW_SHOW);
    UpdateWindow(g_chatWindow);

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        nullptr,
        nullptr,
        Microsoft::WRL::Callback<
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result,
               ICoreWebView2Environment* environment) -> HRESULT {

                if (FAILED(result) || !environment) {
                    MessageBoxW(
                        nullptr,
                        L"Could not initialize WebView2. "
                        L"Check the WebView2 Runtime installation.",
                        L"Project ACEx",
                        MB_OK | MB_ICONERROR);
                    return result;
                }

                if (!g_chatWindow ||
                    !IsWindow(g_chatWindow)) {
                    return S_OK;
                }

                return environment->CreateCoreWebView2Controller(
                    g_chatWindow,
                    Microsoft::WRL::Callback<
                    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT controllerResult,
                           ICoreWebView2Controller* controller) -> HRESULT {

                            if (FAILED(controllerResult) ||
                                !controller) {
                                return controllerResult;
                            }

                            if (!g_chatWindow ||
                                !IsWindow(g_chatWindow)) {
                                controller->Close();
                                return S_OK;
                            }

                            g_controller = controller;

                            HRESULT result =
                                g_controller->get_CoreWebView2(
                                    &g_webView);

                            if (FAILED(result) || !g_webView) {
                                return result;
                            }

                            RECT bounds{};
                            GetClientRect(g_chatWindow, &bounds);
                            g_controller->put_Bounds(bounds);

                            return g_webView->Navigate(
                                L"https://chatgpt.com");
                        }).Get());
            }).Get());

    if (FAILED(hr)) {
        std::cerr
            << "[ChatWindow] WebView2 initialization failed."
            << std::endl;
    }
}
