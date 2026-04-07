#define NOMINMAX
#include "overlay.h"
#include <imgui.h>
#include <iostream>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

HWND                    Overlay::m_hwnd = nullptr;
ID3D11Device* Overlay::m_pd3dDevice = nullptr;
ID3D11DeviceContext* Overlay::m_pd3dDeviceContext = nullptr;
IDXGISwapChain* Overlay::m_pSwapChain = nullptr;
ID3D11RenderTargetView* Overlay::m_mainRenderTargetView = nullptr;
ID3D11BlendState* Overlay::m_pBlendState = nullptr;
bool                    Overlay::m_menuOpen = false;
int                     Overlay::m_screenWidth = 0;
int                     Overlay::m_screenHeight = 0;
std::atomic<bool>       Overlay::m_initialized{ false };

LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return TRUE;

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && Overlay::GetSwapChain()) {
            Overlay::CleanupRenderTarget();
            Overlay::GetSwapChain()->ResizeBuffers(
                0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            Overlay::CreateRenderTarget();
            Overlay::m_screenWidth = (int)LOWORD(lParam);
            Overlay::m_screenHeight = (int)HIWORD(lParam);
        }
        return 0;
    case WM_DPICHANGED:
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

bool Overlay::CreateWindowClass() {
    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, OverlayWndProc, 0, 0,
        GetModuleHandle(nullptr), nullptr,
        LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr,
        L"WickedOverlay", nullptr
    };
    return RegisterClassEx(&wc) != 0;
}

bool Overlay::CreateOverlayWindow() {
    m_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_screenHeight = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"WickedOverlay", L"Wicked.Services",
        WS_POPUP,
        0, 0, m_screenWidth, m_screenHeight,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!m_hwnd) {
        std::cout << "[ERROR] CreateWindowEx failed\n";
        return false;
    }

    // COLOR KEY TRANSPARENCY: LWA_COLORKEY makes every pure-black pixel (0,0,0)
    // invisible at the DWM compositor level. We clear the render target to
    // opaque black each frame, so anything not drawn by ImGui or draw:: disappears.
    // This is the reliable approach -- no DXGI_ALPHA_MODE needed.
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    std::cout << "[+] Overlay window created: "
        << m_screenWidth << "x" << m_screenHeight << "\n";
    return true;
}

bool Overlay::CreateD3D11Device() {
    // DXGI_SWAP_EFFECT_DISCARD (not FLIP_DISCARD).
    // FLIP_DISCARD on a WS_POPUP layered window bypasses DWM composition,
    // which causes the solid black overlay you saw and breaks vsync (hence 3020fps).
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = (UINT)m_screenWidth;
    sd.BufferDesc.Height = (UINT)m_screenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
        &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);

    if (FAILED(hr)) {
        std::cout << "[ERROR] D3D11CreateDeviceAndSwapChain failed: 0x"
            << std::hex << hr << std::dec << "\n";
        return false;
    }

    std::cout << "[+] D3D11 device created (feature level 0x"
        << std::hex << featureLevel << std::dec << ")\n";

    if (!CreateRenderTarget()) return false;
    if (!CreateBlendState())   return false;
    return true;
}

bool Overlay::CreateBlendState() {
    // Without a blend state, ImGui alpha (semi-transparent fills, text AA)
    // renders as fully opaque. This makes boxes look blocky and names look wrong.
    D3D11_BLEND_DESC bd = {};
    bd.AlphaToCoverageEnable = FALSE;
    bd.IndependentBlendEnable = FALSE;
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = m_pd3dDevice->CreateBlendState(&bd, &m_pBlendState);
    if (FAILED(hr)) {
        std::cout << "[ERROR] CreateBlendState failed: 0x"
            << std::hex << hr << std::dec << "\n";
        return false;
    }
    return true;
}

bool Overlay::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (FAILED(m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) return false;
    HRESULT hr = m_pd3dDevice->CreateRenderTargetView(
        pBackBuffer, nullptr, &m_mainRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        std::cout << "[ERROR] CreateRenderTargetView failed: 0x"
            << std::hex << hr << std::dec << "\n";
        return false;
    }
    return true;
}

void Overlay::CleanupRenderTarget() {
    if (m_mainRenderTargetView) {
        m_mainRenderTargetView->Release();
        m_mainRenderTargetView = nullptr;
    }
}

bool Overlay::Initialize() {
    if (m_initialized) return true;
    if (!CreateWindowClass()) {
        std::cout << "[ERROR] Failed to register window class\n";
        return false;
    }
    if (!CreateOverlayWindow()) return false;
    if (!CreateD3D11Device())   return false;
    m_initialized = true;
    std::cout << "[+] Overlay initialized successfully\n";
    return true;
}

void Overlay::Shutdown() {
    if (!m_initialized) return;
    CleanupRenderTarget();
    if (m_pBlendState) { m_pBlendState->Release();       m_pBlendState = nullptr; }
    if (m_pSwapChain) { m_pSwapChain->Release();        m_pSwapChain = nullptr; }
    if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
    if (m_pd3dDevice) { m_pd3dDevice->Release();        m_pd3dDevice = nullptr; }
    if (m_hwnd) { DestroyWindow(m_hwnd);          m_hwnd = nullptr; }
    UnregisterClass(L"WickedOverlay", GetModuleHandle(nullptr));
    m_initialized = false;
    std::cout << "[+] Overlay shutdown\n";
}

void Overlay::BeginFrame() {
    m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);

    // Clear to OPAQUE BLACK (alpha = 1.0).
    // LWA_COLORKEY makes every (0,0,0) pixel transparent at the compositor.
    // Do NOT use (0,0,0,0) here -- that is the DWM alpha-composition path
    // which requires DXGI_ALPHA_MODE_PREMULTIPLIED in the swap chain.
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clearColor);

    float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_pd3dDeviceContext->OMSetBlendState(m_pBlendState, blendFactor, 0xFFFFFFFF);
}

void Overlay::EndFrame() {
    // Present(1, 0) = vsync. Now correct because DXGI_SWAP_EFFECT_DISCARD
    // hooks into DWM's vsync signal properly on layered windows.
    m_pSwapChain->Present(1, 0);
}

void Overlay::SetMenuOpen(bool open) {
    m_menuOpen = open;
    LONG_PTR exStyle = GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);

    if (open) {
        exStyle &= ~WS_EX_TRANSPARENT;
        exStyle &= ~WS_EX_NOACTIVATE;
        SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }
    else {
        exStyle |= WS_EX_TRANSPARENT;
        exStyle |= WS_EX_NOACTIVATE;
        SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // Re-assert color key after every style change -- SetWindowLongPtr can
    // silently reset layered attributes on some Windows builds.
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
}

void Overlay::GetDisplaySize(int& width, int& height) {
    width = m_screenWidth;
    height = m_screenHeight;
}