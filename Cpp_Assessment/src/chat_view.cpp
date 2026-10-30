#include "chat_view.h"
#include <iostream>
#include <WebView2.h>

// WebView2 loader runtime dependency header
#include <WebView2EnvironmentOptions.h>

ChatView::ChatView()
    : m_hWndParent(nullptr),
      m_isInitialized(false) {}

ChatView::~ChatView() {
    Shutdown();
}

bool ChatView::Initialize(HWND hWndParent, const std::wstring& initialHtmlContent) {
    m_hWndParent = hWndParent;

    // Create WebView2 environment and controller
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, initialHtmlContent](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    std::wcerr << L"[ChatView] Failed to create WebView2 environment: " << result << std::endl;
                    return result;
                }

                env->CreateCoreWebView2Controller(
                    m_hWndParent,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, initialHtmlContent](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(res) || !controller) {
                                std::wcerr << L"[ChatView] Failed to create WebView2 controller: " << res << std::endl;
                                return res;
                            }

                            m_controller = controller;
                            m_controller->get_CoreWebView2(&m_webView);

                            // Configure WebView2 settings
                            wil::com_ptr<ICoreWebView2Settings> settings;
                            m_webView->get_Settings(&settings);
                            settings->put_IsScriptEnabled(TRUE);
                            settings->put_AreDevToolsEnabled(TRUE);
                            settings->put_AreDefaultContextMenusEnabled(TRUE);

                            // Resize to parent window bounds
                            RECT bounds;
                            GetClientRect(m_hWndParent, &bounds);
                            m_controller->put_Bounds(bounds);

                            // Load initial markdown-styled chat shell or content
                            std::wstring defaultHtml = initialHtmlContent.empty() ? 
                                L"<html><body style='background:#1e1e1e;color:#d4d4d4;font-family:Segoe UI,sans-serif;padding:16px;'>"
                                L"<h2>Project ACEx — Chat Interface</h2>"
                                L"<div style='border-left:3px solid #007acc;padding-left:10px;margin-top:10px;'>"
                                L"<p><b>System:</b> WebView2 initialized successfully. Ready for AI communication.</p>"
                                L"</div></body></html>" : initialHtmlContent;

                            m_webView->NavigateToString(defaultHtml.c_str());
                            m_isInitialized = true;
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        std::wcerr << L"[ChatView] CreateCoreWebView2EnvironmentWithOptions failed: " << hr << std::endl;
        return false;
    }

    return true;
}

void ChatView::Resize(const RECT& bounds) {
    if (m_controller) {
        m_controller->put_Bounds(bounds);
    }
}

void ChatView::NavigateToString(const std::wstring& htmlContent) {
    if (m_webView) {
        m_webView->NavigateToString(htmlContent.c_str());
    }
}

void ChatView::SetVisible(bool visible) {
    if (m_controller) {
        m_controller->put_IsVisible(visible ? TRUE : FALSE);
    }
}

void ChatView::Shutdown() {
    if (m_controller) {
        m_controller->Close();
        m_controller = nullptr;
        m_webView = nullptr;
    }
    m_isInitialized = false;
}