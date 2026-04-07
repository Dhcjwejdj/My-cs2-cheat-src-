#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <cstdio>
#include <thread>
#include <chrono>
#include <atomic>
#include <shared_mutex>
#include <shellapi.h>
#include "overlay.h"
#include "imgui_layer.h"
#include "menu.h"
#include "aimbot.h"
#include "triggerbot.h"
#include "bhop.h"
#include "esp.h"
#include "misc.h"          // ← NEW: must be included for Misc::Render
#include "memory.h"
#include "offsets.h"
#include "structs.h"
#include "config.h"

// ── Global config ─────────────────────────────────────────────────────────────
// Defined here — every other .cpp only declares it extern via config.h.
// CRITICAL: If you change config.h's struct layout (add/remove/reorder fields),
// you MUST do Build → Rebuild Solution so this object file is recompiled too.
// Stale object files cause all features to read wrong offsets from g_Config.
Config g_Config;

Memory* g_mem = nullptr;

std::atomic<bool> g_gameReady{ false };
std::atomic<bool> g_cheatLoaded{ false };
std::atomic<bool> g_shouldExit{ false };

// Shared mutex: scanner holds exclusive lock only while calling UpdateProcess.
// Render thread acquires shared lock — multiple readers run concurrently.
std::shared_mutex g_memMutex;

// Cached client.dll base — updated by scanner, read by render (no lock needed,
// atomic load/store is safe for a single pointer-sized value).
std::atomic<uintptr_t> g_clientBase{ 0 };

// ── FPS counter ───────────────────────────────────────────────────────────────
static double     g_fps = 0.0;
static int        g_frameCount = 0;
static auto       g_lastFpsTime = std::chrono::steady_clock::now();

static void UpdateFPS() {
    ++g_frameCount;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - g_lastFpsTime).count();
    if (elapsed >= 1.0) {
        g_fps = g_frameCount / elapsed;
        g_frameCount = 0;
        g_lastFpsTime = now;
    }
}

// ── Elevation check ───────────────────────────────────────────────────────────
static bool IsElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD sz = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &sz);
    CloseHandle(tok);
    return ok && el.TokenIsElevated != 0;
}

// ── ProcessScannerThread ──────────────────────────────────────────────────────
// Split into two lock phases so the expensive TH32CS_SNAPMODULE snapshot
// (GetModuleAddress) doesn't block the render thread for 15-30 ms:
//
//   Phase 1 – exclusive lock: UpdateProcess (opens/closes the handle).
//   Phase 2 – shared lock:   GetModuleAddress (read-only on process_id).
//
// Interval bumped to 3 s — CS2 never restarts in under 3 seconds in practice.
void ProcessScannerThread() {
    while (!g_shouldExit) {

        // Phase 1 — exclusive (handle mutation)
        {
            std::unique_lock<std::shared_mutex> wl(g_memMutex);
            if (g_mem) g_mem->UpdateProcess("cs2.exe");
        }

        // Phase 2 — shared (read-only)
        {
            std::shared_lock<std::shared_mutex> rl(g_memMutex);
            if (g_mem && g_mem->IsValid()) {
                const uintptr_t client = g_mem->GetModuleAddress("client.dll");
                if (client) {
                    if (!g_gameReady.exchange(true))
                        std::cout << "[+] CS2 PID " << g_mem->process_id
                        << "  client.dll 0x" << std::hex << client
                        << std::dec << "\n";
                    g_clientBase.store(client);
                }
                else {
                    g_gameReady.store(false);
                    g_clientBase.store(0);
                }
            }
            else {
                g_gameReady.store(false);
                g_clientBase.store(0);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

// ============================================================================
//  main
// ============================================================================
int main() {
    // Re-launch elevated if needed
    if (!IsElevated()) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = exe;
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) return 0;
        MessageBoxW(nullptr, L"Administrator privileges required.",
            L"Wicked.Services", MB_ICONERROR);
        return 1;
    }

    // Console (closed after F2 loads the cheat)
    AllocConsole();
    {
        HWND hc = GetConsoleWindow();
        if (hc) {
            SetWindowPos(hc, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            ShowWindow(hc, SW_SHOW);
            SetForegroundWindow(hc);
        }
    }
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    std::cout << "Wicked.Services - Starting...\n";

    // Memory is constructed AFTER elevation check so OpenProcess is authorised
    Memory mem("cs2.exe");
    g_mem = &mem;

    if (!Overlay::Initialize()) return 1;
    if (!draw::initialize(Overlay::GetHWND(),
        Overlay::GetDevice(),
        Overlay::GetContext())) return 1;

    // Auto-load the default config slot on startup (silently ignores missing files)
    g_Config.Load(0);

    Menu menu;

    std::thread scanner(ProcessScannerThread);
    scanner.detach();

    std::cout << "Waiting for CS2...  (F2 = load cheat)\n";
    Overlay::SetMenuOpen(false);

    MSG  msg{};
    bool running = true;
    bool consoleClosed = false;

    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) running = false;
        }

        if (GetAsyncKeyState(VK_END) & 1) { running = false; break; }

        // F2 — load cheat (requires CS2 to be found first)
        if ((GetAsyncKeyState(VK_F2) & 1) && g_gameReady && !g_cheatLoaded) {
            g_cheatLoaded = true;
            Overlay::SetMenuOpen(true);
            g_Config.menu_open = true;
            std::cout << "[+] Cheat loaded.\n";
            if (!consoleClosed) { FreeConsole(); consoleClosed = true; }
        }

        // INSERT — toggle menu
        if (g_cheatLoaded && (GetAsyncKeyState(VK_INSERT) & 1)) {
            g_Config.menu_open = !g_Config.menu_open;
            Overlay::SetMenuOpen(g_Config.menu_open);
        }

        Overlay::BeginFrame();
        draw::begin_frame();
        ui::begin();

        int sw, sh;
        Overlay::GetDisplaySize(sw, sh);
        draw::set_display_size(sw, sh);

        // ── Info panel (top-left) ──────────────────────────────────────────────
        {
            UpdateFPS();
            char fps_buf[32];
            snprintf(fps_buf, sizeof(fps_buf), "%.1f fps", g_fps);

            ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.82f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.10f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.10f, 0.18f, 0.90f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
            if (ImGui::Begin("##info", nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoFocusOnAppearing))
            {
                ImGui::TextColored(ImVec4(0.88f, 0.25f, 0.38f, 1.f), "Wicked.Services");
                ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.84f, 1.f), "%s", fps_buf);
                if (g_cheatLoaded) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.45f, 1.f), "| LOADED");
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }

        // ── Feature rendering ──────────────────────────────────────────────────
        // shared_lock: concurrent with other readers, exclusive with scanner's
        // UpdateProcess phase.  try_to_lock skips one frame if scanner is
        // mid-update rather than stalling the render thread.
        if (g_cheatLoaded && g_gameReady) {
            const uintptr_t client = g_clientBase.load();
            if (client) {
                std::shared_lock<std::shared_mutex> lock(g_memMutex, std::try_to_lock);
                if (lock.owns_lock() && g_mem && g_mem->IsValid()) {
                    Aimbot::Run(*g_mem, client);
                    Triggerbot::Run(*g_mem, client);
                    Bhop::Run(*g_mem, client);
                    ESP::Render(*g_mem, client, sw, sh);
                    Misc::Render(*g_mem, client, sw, sh);  // ← NEW
                    Aimbot::Render();
                }
            }
        }

        // ── Menu / hint ────────────────────────────────────────────────────────
        if (g_cheatLoaded && g_Config.menu_open) {
            menu.Draw();
        }
        else if (!g_cheatLoaded && g_gameReady) {
            ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.5f),
                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.72f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.90f));
            if (ImGui::Begin("##hint", nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoFocusOnAppearing))
            {
                ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.96f, 1.f),
                    "Press F2 to load cheat");
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }

        ui::end();
        draw::end_frame();
        Overlay::EndFrame();
    }

    g_shouldExit = true;
    g_mem = nullptr;
    draw::shutdown();
    Overlay::Shutdown();
    if (!consoleClosed) FreeConsole();
    return 0;
}