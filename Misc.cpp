#define NOMINMAX
#include "misc.h"
#include "memory.h"
#include "offsets.h"
#include "structs.h"
#include "utils.h"
#include "imgui_layer.h"
#include "config.h"
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <windows.h>

namespace {

    static uintptr_t EntByIdx(Memory& mem, uintptr_t elist, int idx)
    {
        if (idx <= 0) return 0;
        const uintptr_t chunk = mem.Read<uintptr_t>(
            elist + offsets::entity_list_entry + 8 * (idx >> 9));
        if (!chunk) return 0;
        return mem.Read<uintptr_t>(
            chunk + offsets::entity_list_controller_stride * (idx & 0x1FF));
    }

    // ── No-Flash ──────────────────────────────────────────────────────────────────
    static void RunNoFlash(Memory& mem, uintptr_t client)
    {
        if (!g_Config.misc.no_flash) return;
        const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
        if (!lpawn) return;
        const float zero = 0.f;
        mem.Write<float>(lpawn + schemas::m_flFlashOverlayAlpha, zero);
        mem.Write<float>(lpawn + schemas::m_flFlashMaxAlpha, zero);
    }

    // ── Velocity Indicator ────────────────────────────────────────────────────────
    static void DrawVelocity(Memory& mem, uintptr_t client, int sw, int sh)
    {
        if (!g_Config.misc.velocity_info) return;
        const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
        if (!lpawn) return;
        const Vector3 vel = mem.Read<Vector3>(lpawn + schemas::m_vecVelocity);
        const float   spd = std::sqrt(vel.x * vel.x + vel.y * vel.y);

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float bw = 140.f, bh = 5.f;
        const float px = sw * 0.5f - bw * 0.5f;
        const float py = sh * 0.5f + 40.f;
        const float frac = std::clamp(spd / 320.f, 0.f, 1.f);

        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + bw, py + bh), IM_COL32(10, 12, 20, 180), 2.f);
        const ImU32 bar_col = spd < 200.f ? IM_COL32(70, 210, 110, 220)
            : spd < 280.f ? IM_COL32(220, 190, 50, 220)
            : IM_COL32(220, 70, 70, 220);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + bw * frac, py + bh), bar_col, 2.f);

        char buf[16]; snprintf(buf, sizeof(buf), "%.0f", spd);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(sw * 0.5f - ts.x * 0.5f, py + bh + 3.f), IM_COL32(200, 205, 215, 220), buf);
    }

    // ── Punch Dot ─────────────────────────────────────────────────────────────────
    // Novel feature: shows where your bullets actually go after aim punch.
    // We read m_aimPunchAngle, apply the 2.5x CS2 multiplier, then convert from
    // angular offset (degrees) to screen pixels using the view matrix FOV scale.
    //
    //   vm.matrix[0][0] = horizontal FOV scale factor (same as WorldToScreen uses)
    //   vm.matrix[1][1] = vertical   FOV scale factor
    //
    // For small angles:
    //   screen_dx = -tan(punch_yaw_rad * 2.5) * vm[0][0] * (sw/2)
    //   screen_dy = +tan(punch_pitch_rad * 2.5) * vm[1][1] * (sh/2)
    //
    // The minus on dx: CS2 yaw rotates the view left with positive values,
    // which moves the screen crosshair LEFT, hence the bullet impact moves RIGHT
    // (negative screen x offset from centre).
    static void DrawPunchDot(Memory& mem, uintptr_t client,
        const view_matrix_t& vm, int sw, int sh)
    {
        if (!g_Config.misc.punch_dot) return;
        const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
        if (!lpawn) return;
        const int shots = mem.Read<int>(lpawn + schemas::m_iShotsFired);
        if (shots == 0) return; // Only show while actively spraying

        const Vector3 punch = mem.Read<Vector3>(lpawn + schemas::m_aimPunchAngle);
        // CS2 applies aim_punch * 2.5 to the actual view
        const float yaw_deg = punch.y * 2.5f;
        const float pitch_deg = punch.x * 2.5f;
        const float yaw_rad = yaw_deg * (3.14159265f / 180.f);
        const float pitch_rad = pitch_deg * (3.14159265f / 180.f);

        // Project to screen using the view matrix FOV scale factors
        const float fov_x = vm.matrix[0][0];
        const float fov_y = vm.matrix[1][1];
        const float dot_x = sw * 0.5f - tanf(yaw_rad) * fov_x * (sw * 0.5f);
        const float dot_y = sh * 0.5f + tanf(pitch_rad) * fov_y * (sh * 0.5f);

        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        // Faint line from crosshair to punch dot (shows magnitude + direction)
        dl->AddLine(ImVec2(sw * 0.5f, sh * 0.5f), ImVec2(dot_x, dot_y),
            IM_COL32(g_Config.misc.punch_dot_color.r,
                g_Config.misc.punch_dot_color.g,
                g_Config.misc.punch_dot_color.b, 90), 1.f);

        // Black shadow ring
        dl->AddCircleFilled(ImVec2(dot_x, dot_y), 5.5f, IM_COL32(8, 8, 8, 200), 10);
        // Coloured dot
        dl->AddCircleFilled(ImVec2(dot_x, dot_y), 4.f,
            IM_COL32(g_Config.misc.punch_dot_color.r,
                g_Config.misc.punch_dot_color.g,
                g_Config.misc.punch_dot_color.b,
                g_Config.misc.punch_dot_color.a), 10);
        // White center pip so the dot reads against any background
        dl->AddCircleFilled(ImVec2(dot_x, dot_y), 1.5f, IM_COL32(255, 255, 255, 220), 6);
    }

    // ── Grenade Timers ────────────────────────────────────────────────────────────
    static void DrawGrenadeWarnings(Memory& mem, uintptr_t client, int sw, int sh,
        const view_matrix_t& vm)
    {
        if (!g_Config.misc.grenade_warning) return;
        const float curtime = mem.Read<float>(client + offsets::dwGlobalVars + 0x34);
        if (curtime <= 0.f) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const uintptr_t elist = mem.Read<uintptr_t>(client + offsets::dwEntityList);
        if (!elist) return;
        for (int i = 1; i <= 512; ++i) {
            const uintptr_t ent = EntByIdx(mem, elist, i);
            if (!ent) continue;
            const bool did_smoke = mem.Read<bool>(ent + schemas::m_bDidSmokeEffect);
            const float spawn = mem.Read<float>(ent + schemas::m_flSpawnTime);
            if (spawn <= 0.f) continue;
            float duration = 0.f; ImU32 color = 0; const char* lbl = nullptr;
            if (did_smoke) { duration = 18.f; color = IM_COL32(120, 200, 255, 230); lbl = "SMOKE"; }
            else {
                const bool is_mol = mem.Read<bool>(ent + schemas::m_bIsIncGrenade);
                if (is_mol) { duration = 7.f; color = IM_COL32(255, 140, 50, 230); lbl = "MOLOTOV"; }
            }
            if (!lbl || duration <= 0.f) continue;
            const float remaining = duration - (curtime - spawn);
            if (remaining <= 0.f || remaining > duration) continue;
            const Vector3 pos = mem.Read<Vector3>(ent + schemas::m_vOldOrigin);
            Vector2 screen{};
            if (!WorldToScreen(pos, screen, vm, sw, sh)) continue;
            char buf[32]; snprintf(buf, sizeof(buf), "%s  %.1fs", lbl, remaining);
            const ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddRectFilled(
                ImVec2(screen.x - ts.x * 0.5f - 4.f, screen.y - ts.y * 0.5f - 2.f),
                ImVec2(screen.x + ts.x * 0.5f + 4.f, screen.y + ts.y * 0.5f + 2.f),
                IM_COL32(10, 12, 20, 180), 3.f);
            dl->AddText(ImVec2(screen.x - ts.x * 0.5f, screen.y - ts.y * 0.5f), color, buf);
        }
    }

    // ── Spectator List ────────────────────────────────────────────────────────────
    static void DrawSpectatorList(Memory& mem, uintptr_t client, int, int)
    {
        if (!g_Config.misc.spectator_list) return;
        const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
        if (!lpawn) return;
        const uintptr_t elist = mem.Read<uintptr_t>(client + offsets::dwEntityList);
        if (!elist) return;
        std::vector<std::string> specs;
        for (int i = 1; i <= 128; ++i) {
            const uintptr_t ctrl = EntByIdx(mem, elist, i); if (!ctrl) continue;
            const uint32_t ph = mem.Read<uint32_t>(ctrl + schemas::m_hPlayerPawn);
            if (!ph || ph == 0xFFFFFFFF) continue;
            const uintptr_t pawn = EntByIdx(mem, elist, (int)(ph & 0x7FFF));
            if (!pawn || pawn == lpawn) continue;
            const uintptr_t obs = mem.Read<uintptr_t>(pawn + schemas::m_pObserverServices);
            if (!obs || mem.Read<int>(obs + schemas::m_iObserverMode) == 0) continue;
            const uint32_t th = mem.Read<uint32_t>(obs + schemas::m_hObserverTarget);
            if (!th || th == 0xFFFFFFFF) continue;
            if (EntByIdx(mem, elist, (int)(th & 0x7FFF)) != lpawn) continue;
            auto raw = mem.Read<std::array<char, 128>>(ctrl + schemas::m_iszPlayerName);
            for (char& c : raw) { const auto u = (unsigned char)c; if (u && (u < 0x20 || u>0x7E))c = '?'; }
            std::string n(raw.data(), strnlen_s(raw.data(), raw.size()));
            if (!n.empty()) specs.push_back(std::move(n));
        }
        if (specs.empty()) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float lh = 16.f, pw = 165.f, phh = 20.f + lh * (float)specs.size();
        const float px = 14.f, py = 60.f;
        dl->AddRectFilled(ImVec2(px - 4, py - 4), ImVec2(px + pw, py + phh), IM_COL32(10, 12, 20, 200), 4.f);
        dl->AddRect(ImVec2(px - 4, py - 4), ImVec2(px + pw, py + phh), IM_COL32(180, 50, 80, 180), 4.f, 0, 1.f);
        dl->AddText(ImVec2(px, py), IM_COL32(200, 80, 110, 255), "SPECTATORS");
        float yy = py + 16.f;
        for (const auto& n : specs) {
            dl->AddText(ImVec2(px + 6.f, yy), IM_COL32(210, 215, 225, 215), n.c_str()); yy += lh;
        }
    }

    // ── Custom Crosshair ──────────────────────────────────────────────────────────
    static void DrawCrosshair(int sw, int sh)
    {
        if (!g_Config.misc.crosshair) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float cx = sw * 0.5f, cy = sh * 0.5f;
        const float sz = (float)g_Config.misc.crosshair_size;
        const float gp = (float)g_Config.misc.crosshair_gap;
        const float th = (float)std::max(1, g_Config.misc.crosshair_thick);
        const ImU32 col = IM_COL32(g_Config.misc.crosshair_color.r, g_Config.misc.crosshair_color.g,
            g_Config.misc.crosshair_color.b, g_Config.misc.crosshair_color.a);
        const ImU32 out = IM_COL32(8, 8, 8, 160);
        auto line = [&](float x1, float y1, float x2, float y2) {
            dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), out, th + 2.f);
            dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), col, th);
            };
        line(cx - sz - gp, cy, cx - gp, cy); line(cx + gp, cy, cx + sz + gp, cy);
        line(cx, cy - sz - gp, cx, cy - gp); line(cx, cy + gp, cx, cy + sz + gp);
        if (g_Config.misc.crosshair_dot) {
            dl->AddCircleFilled(ImVec2(cx, cy), th * 0.55f + 1.f, out, 6);
            dl->AddCircleFilled(ImVec2(cx, cy), th * 0.55f, col, 6);
        }
    }

    // ── Clock ─────────────────────────────────────────────────────────────────────
    static void DrawClock(int sw, int)
    {
        if (!g_Config.misc.show_clock) return;
        SYSTEMTIME st; GetLocalTime(&st);
        char buf[16]; snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const float px = sw - ts.x - 16.f, py = 10.f;
        dl->AddRectFilled(ImVec2(px - 5, py - 3), ImVec2(px + ts.x + 5, py + ts.y + 3), IM_COL32(10, 12, 20, 185), 3.f);
        dl->AddText(ImVec2(px, py), IM_COL32(200, 210, 225, 235), buf);
    }

    // ── Local Player Info ─────────────────────────────────────────────────────────
    static void DrawLocalInfo(Memory& mem, uintptr_t client, int sw, int sh)
    {
        if (!g_Config.misc.local_info) return;
        const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
        if (!lpawn) return;
        const int hp = mem.Read<int>(lpawn + schemas::m_iHealth);
        const int armor = mem.Read<int>(lpawn + schemas::m_ArmorValue);
        if (hp <= 0) return;
        int clip = 0, reserve = 0;
        const uintptr_t wep = mem.Read<uintptr_t>(lpawn + schemas::m_pClippingWeapon);
        if (wep) { clip = mem.Read<int>(wep + schemas::m_iClip1); reserve = mem.Read<int>(wep + schemas::m_pReserveAmmo); }
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float pw = 200.f, phh = 52.f;
        const float px = sw * 0.5f - pw * 0.5f, py = (float)sh - 145.f;
        dl->AddRectFilled(ImVec2(px - 4, py - 4), ImVec2(px + pw, py + phh), IM_COL32(10, 12, 20, 195), 5.f);
        dl->AddRect(ImVec2(px - 4, py - 4), ImVec2(px + pw, py + phh), IM_COL32(160, 50, 75, 155), 5.f, 0, 1.f);
        const float hp_frac = std::clamp(hp / 100.f, 0.f, 1.f);
        const ImU32 hp_col = hp > 50 ? IM_COL32(70, 210, 110, 255) : hp > 25 ? IM_COL32(230, 190, 50, 255) : IM_COL32(230, 65, 65, 255);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + 188.f * hp_frac, py + 7.f), hp_col, 2.f);
        dl->AddRect(ImVec2(px, py), ImVec2(px + 188.f, py + 7.f), IM_COL32(55, 60, 75, 200), 2.f, 0, 1.f);
        if (armor > 0) {
            const float ar_frac = std::clamp(armor / 100.f, 0.f, 1.f);
            dl->AddRectFilled(ImVec2(px, py + 10.f), ImVec2(px + 188.f * ar_frac, py + 17.f), IM_COL32(70, 155, 230, 255), 2.f);
            dl->AddRect(ImVec2(px, py + 10.f), ImVec2(px + 188.f, py + 17.f), IM_COL32(55, 60, 75, 200), 2.f, 0, 1.f);
        }
        char buf[64]; snprintf(buf, sizeof(buf), "HP %d   ARM %d   %d / %d", hp, armor, clip, reserve);
        dl->AddText(ImVec2(px, py + 22.f), IM_COL32(210, 215, 225, 235), buf);
    }

    // ── Radar ─────────────────────────────────────────────────────────────────────
    static void DrawRadar(Memory& mem, uintptr_t client, int sw, int)
    {
        if (!g_Config.misc.radar) return;
        const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
        if (!lpawn) return;
        const Vector3 lo = mem.Read<Vector3>(lpawn + schemas::m_vOldOrigin);
        const Vector3 va = mem.Read<Vector3>(client + offsets::dwViewAngles);
        const uintptr_t elist = mem.Read<uintptr_t>(client + offsets::dwEntityList);
        if (!elist) return;
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float R = g_Config.misc.radar_size, cx = sw - R - 18.f, cy = R + 18.f, rng = g_Config.misc.radar_range;
        dl->AddCircleFilled(ImVec2(cx, cy), R, IM_COL32(10, 12, 20, 205), 64);
        dl->AddCircle(ImVec2(cx, cy), R, IM_COL32(180, 55, 80, 200), 64, 1.5f);
        dl->AddCircle(ImVec2(cx, cy), R * 0.5f, IM_COL32(255, 255, 255, 18), 48, 1.f);
        dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy - 11.f), IM_COL32(80, 210, 120, 180), 1.5f);
        dl->AddCircleFilled(ImVec2(cx, cy), 3.5f, IM_COL32(80, 210, 120, 255), 10);
        const float yr = g_Config.misc.radar_rotate ? -va.y * (3.14159265f / 180.f) : 0.f;
        const float cy_ = cosf(yr), sy_ = sinf(yr);
        const int lteam = mem.Read<int>(lpawn + schemas::m_iTeamNum);
        for (int i = 1; i <= 128; ++i) {
            const uintptr_t ctrl = EntByIdx(mem, elist, i); if (!ctrl) continue;
            const uint32_t ph = mem.Read<uint32_t>(ctrl + schemas::m_hPlayerPawn);
            if (!ph || ph == 0xFFFFFFFF) continue;
            const uintptr_t pawn = EntByIdx(mem, elist, (int)(ph & 0x7FFF));
            if (!pawn || pawn == lpawn) continue;
            const int hp = mem.Read<int>(pawn + schemas::m_iHealth);
            if (hp <= 0 || hp > 100) continue;
            const int team = mem.Read<int>(pawn + schemas::m_iTeamNum);
            const Vector3 po = mem.Read<Vector3>(pawn + schemas::m_vOldOrigin);
            float dx = po.x - lo.x, dy = po.y - lo.y;
            float rx = dx * cy_ - dy * sy_, ry = dx * sy_ + dy * cy_;
            const float d2 = sqrtf(rx * rx + ry * ry);
            const bool at_edge = (d2 >= rng);
            if (at_edge) { rx *= rng / d2; ry *= rng / d2; }
            const float scale = R / rng;
            const float dot_x = cx + rx * scale, dot_y = cy - ry * scale;
            const ImU32 dot_col = (team != lteam) ? IM_COL32(220, 65, 88, 240) : IM_COL32(75, 200, 115, 240);
            dl->AddCircleFilled(ImVec2(dot_x, dot_y), at_edge ? 2.5f : 3.f, dot_col, 8);
            if (at_edge) dl->AddCircle(ImVec2(dot_x, dot_y), 4.5f, dot_col, 8, 1.f);
        }
        dl->AddText(ImVec2(cx - 17.f, cy + R + 4.f), IM_COL32(140, 145, 155, 170), "RADAR");
    }


    // ── Auto Crouch ───────────────────────────────────────────────────────────────
    static void RunAutoCrouch()
    {
        if (!g_Config.misc.auto_crouch) return;
        const bool key = (GetAsyncKeyState(g_Config.misc.auto_crouch_key) & 0x8000) != 0;
        static bool s_crouching = false;
        if (key && !s_crouching) {
            INPUT inp = {};
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = VK_LCONTROL;
            inp.ki.dwFlags = 0;
            SendInput(1, &inp, sizeof(INPUT));
            s_crouching = true;
        }
        else if (!key && s_crouching) {
            INPUT inp = {};
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = VK_LCONTROL;
            inp.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &inp, sizeof(INPUT));
            s_crouching = false;
        }
    }

    // ── Round Timer ───────────────────────────────────────────────────────────────
    // Reads curtime from GlobalVars and compares to a cached round-start time.
    // Round-start is detected when curtime resets to near 0 or drops significantly.
    static void DrawRoundTimer(Memory& mem, uintptr_t client, int sw, int sh)
    {
        if (!g_Config.misc.round_timer) return;
        const float curtime = mem.Read<float>(client + offsets::dwGlobalVars + 0x34);
        if (curtime <= 0.f) return;

        static float s_round_start = 0.f;
        static float s_last_curtime = 0.f;
        // Detect new round: curtime went backwards or reset
        if (curtime < s_last_curtime - 5.f || (s_last_curtime < 2.f && curtime < 2.f))
            s_round_start = curtime;
        if (s_round_start == 0.f) s_round_start = curtime;
        s_last_curtime = curtime;

        // CS2 competitive round length ≈ 115 seconds (buy time + play time)
        const float elapsed = curtime - s_round_start;
        const float remaining = std::max(0.f, 115.f - elapsed);

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float px = sw * 0.5f - 38.f;
        const float py = 56.f; // just below the scoreboard timer

        // Background pill
        dl->AddRectFilled(ImVec2(px - 6, py - 4), ImVec2(px + 82, py + 20),
            IM_COL32(7, 7, 11, 210), 6.f);
        dl->AddRect(ImVec2(px - 6, py - 4), ImVec2(px + 82, py + 20),
            remaining < 20.f ? IM_COL32(220, 70, 70, 180) : IM_COL32(122, 102, 235, 120),
            6.f, 0, 1.f);

        // Time text
        const int mins = (int)remaining / 60;
        const int secs = (int)remaining % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const ImU32 tcol = remaining < 20.f ? IM_COL32(255, 100, 100, 255)
            : remaining < 40.f ? IM_COL32(255, 220, 80, 255)
            : IM_COL32(200, 200, 220, 255);
        dl->AddText(ImVec2(px + 38.f - ts.x * 0.5f, py), tcol, buf);
    }

    // ── Footstep ESP ──────────────────────────────────────────────────────────────
    // Draws a fading circle at the projected feet of moving enemies.
    // Size scales with speed so faster movement = bigger ring.
    static void DrawFootstepESP(Memory& mem, uintptr_t client,
        const view_matrix_t& vm, int sw, int sh)
    {
        if (!g_Config.misc.footstep_esp) return;
        const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
        if (!lpawn) return;
        const int      lteam = mem.Read<int>(lpawn + schemas::m_iTeamNum);
        const uintptr_t elist = mem.Read<uintptr_t>(client + offsets::dwEntityList);
        if (!elist) return;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const draw::rgba& fc = g_Config.misc.footstep_color;

        for (int i = 1; i <= 128; ++i) {
            const uintptr_t ctrl = EntByIdx(mem, elist, i);
            if (!ctrl) continue;
            const uint32_t ph = mem.Read<uint32_t>(ctrl + schemas::m_hPlayerPawn);
            if (!ph || ph == 0xFFFFFFFF) continue;
            const uintptr_t pawn = EntByIdx(mem, elist, (int)(ph & 0x7FFF));
            if (!pawn || pawn == lpawn) continue;
            const int hp = mem.Read<int>(pawn + schemas::m_iHealth);
            if (hp <= 0 || hp > 100) continue;
            const int team = mem.Read<int>(pawn + schemas::m_iTeamNum);
            if (team == lteam) continue; // teammates only if esp.teammates active — skip for now

            const Vector3 vel = mem.Read<Vector3>(pawn + schemas::m_vecVelocity);
            const float   spd = std::sqrt(vel.x * vel.x + vel.y * vel.y);
            if (spd < g_Config.misc.footstep_threshold) continue;

            const Vector3 origin = mem.Read<Vector3>(pawn + schemas::m_vOldOrigin);
            Vector2 ss{};
            if (!WorldToScreen(origin, ss, vm, sw, sh)) continue;

            // Radius scales 8..24px with speed 80..400
            const float r = std::clamp(8.f + (spd - g_Config.misc.footstep_threshold)
                / (400.f - g_Config.misc.footstep_threshold) * 16.f, 8.f, 24.f);

            // Outer glow ring
            dl->AddCircle(ImVec2(ss.x, ss.y), r + 3.f,
                IM_COL32(fc.r, fc.g, fc.b, fc.a / 4), 24, 1.f);
            // Main ring
            dl->AddCircle(ImVec2(ss.x, ss.y), r,
                IM_COL32(fc.r, fc.g, fc.b, fc.a), 24, 1.5f);
        }
    }

} // anonymous namespace

void Misc::Render(Memory& mem, uintptr_t client, int screen_w, int screen_h)
{
    RunNoFlash(mem, client);
    const view_matrix_t vm = mem.Read<view_matrix_t>(client + offsets::dwViewMatrix);
    DrawVelocity(mem, client, screen_w, screen_h);
    DrawPunchDot(mem, client, vm, screen_w, screen_h);
    DrawGrenadeWarnings(mem, client, screen_w, screen_h, vm);
    DrawSpectatorList(mem, client, screen_w, screen_h);
    DrawCrosshair(screen_w, screen_h);
    DrawClock(screen_w, screen_h);
    DrawLocalInfo(mem, client, screen_w, screen_h);
    DrawRadar(mem, client, screen_w, screen_h);
    RunAutoCrouch();
    DrawRoundTimer(mem, client, screen_w, screen_h);
    DrawFootstepESP(mem, client, vm, screen_w, screen_h);
}
