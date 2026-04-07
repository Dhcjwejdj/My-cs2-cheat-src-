#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>

LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class Overlay {
public:
    static IDXGISwapChain* GetSwapChain() { return m_pSwapChain; }
    static HWND                 GetHWND() { return m_hwnd; }
    static ID3D11Device* GetDevice() { return m_pd3dDevice; }
    static ID3D11DeviceContext* GetContext() { return m_pd3dDeviceContext; }

    static bool Initialize();
    static void Shutdown();

    static void BeginFrame();
    static void EndFrame();

    static void SetMenuOpen(bool open);
    static void GetDisplaySize(int& width, int& height);

    static bool CreateRenderTarget();
    static void CleanupRenderTarget();

    static int m_screenWidth;
    static int m_screenHeight;

private:
    static bool CreateWindowClass();
    static bool CreateOverlayWindow();
    static bool CreateD3D11Device();
    static bool CreateBlendState();

    static HWND                     m_hwnd;
    static ID3D11Device* m_pd3dDevice;
    static ID3D11DeviceContext* m_pd3dDeviceContext;
    static IDXGISwapChain* m_pSwapChain;
    static ID3D11RenderTargetView* m_mainRenderTargetView;
    static ID3D11BlendState* m_pBlendState;
    static bool                     m_menuOpen;
    static std::atomic<bool>        m_initialized;
};