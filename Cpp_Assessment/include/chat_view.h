#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <string>
#include <wrl.h>
#include <wil/com.h>

// Forward declarations for WebView2 COM interfaces
struct ICoreWebView2Controller;
struct ICoreWebView2;

class ChatView {
public:
    ChatView();
    ~ChatView();

    bool Initialize(HWND hWndParent, const std::wstring& initialHtmlContent = L"");
    void Resize(const RECT& bounds);
    void NavigateToString(const std::wstring& htmlContent);
    void SetVisible(bool visible);
    void Shutdown();

    HWND GetParentWindow() const { return m_hWndParent; }

private:
    HWND m_hWndParent;
    wil::com_ptr<ICoreWebView2Controller> m_controller;
    wil::com_ptr<ICoreWebView2> m_webView;
    bool m_isInitialized;
};