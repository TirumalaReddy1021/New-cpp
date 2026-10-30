#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <d3d11.h>
#include <string>

class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    bool Initialize(HINSTANCE hInstance, int width, int height);
    void Run();
    void Shutdown();

    HWND GetHWND() const { return m_hWnd; }

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    HWND m_hWnd;
    int m_width;
    int m_height;

    // DirectX 11 variables
    ID3D11Device* m_pd3dDevice;
    ID3D11DeviceContext* m_pd3dDeviceContext;
    IDXGISwapChain* m_pSwapChain;
    ID3D11RenderTargetView* m_mainRenderTargetView;

    bool m_isInitialized;
    bool m_showDemoWindow;
};