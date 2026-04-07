#define NOMINMAX
#include "aimbot.h"
#include "memory.h"
#include "offsets.h"
#include "structs.h"
#include "utils.h"
#include "config.h"
#include "imgui_layer.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <windows.h>

static constexpr float CS2_SENSITIVITY = 1.0f;
static constexpr float DPC = CS2_SENSITIVITY * 0.022f;

namespace {
    struct EntityLayout { std::ptrdiff_t entry_off, stride; };

    static uintptr_t EntByIdx(Memory& mem, uintptr_t elist,
        std::ptrdiff_t eoff, std::ptrdiff_t stride, int idx)
    {
        if (idx <= 0) return 0;
        const uintptr_t chunk = mem.Read<uintptr_t>(elist + eoff + 8 * (idx >> 9));
        if (!chunk) return 0;
        return mem.Read<uintptr_t>(chunk + stride * (idx & 0x1FF));
    }

    static EntityLayout ProbeLayout(Memory& mem, uintptr_t elist, uintptr_t lpawn)
    {
        const std::ptrdiff_t ke[] = { offsets::entity_list_entry,0x10,0x8 };
        const std::ptrdiff_t ks[] = { offsets::entity_list_controller_stride,0x78,0x70 };
        EntityLayout best{ offsets::entity_list_entry,offsets::entity_list_controller_stride };
        int best_score = -1;
        for (auto e : ke) for (auto s : ks) {
            int score = 0;
            for (int i = 1; i <= 128; ++i) {
                const uintptr_t ctrl = EntByIdx(mem, elist, e, s, i); if (!ctrl) continue;
                for (std::ptrdiff_t po : {static_cast<std::ptrdiff_t>(schemas::m_hPlayerPawn),
                    static_cast<std::ptrdiff_t>(0x7E4), static_cast<std::ptrdiff_t>(0x90C)})
                {
                    const uint32_t h = mem.Read<uint32_t>(ctrl + po);
                    if (!h || h == 0xFFFFFFFF) continue;
                    const uintptr_t pawn = EntByIdx(mem, elist, e, s, (int)(h & 0x7FFF));
                    if (!pawn || pawn == lpawn) continue;
                    if (mem.Read<int>(pawn + schemas::m_iHealth) > 0) { ++score; break; }
                }
            }
            if (score > best_score) { best_score = score; best = { e,s }; }
        }
        return best;
    }

    static std::ptrdiff_t ProbePawnOff(Memory& mem, uintptr_t elist,
        const EntityLayout& lay, uintptr_t lpawn)
    {
        for (std::ptrdiff_t po : {static_cast<std::ptrdiff_t>(schemas::m_hPlayerPawn),
            static_cast<std::ptrdiff_t>(0x7E4), static_cast<std::ptrdiff_t>(0x90C)})
        {
            for (int i = 1; i <= 128; ++i) {
                const uintptr_t ctrl = EntByIdx(mem, elist, lay.entry_off, lay.stride, i);
                if (!ctrl) continue;
                const uint32_t h = mem.Read<uint32_t>(ctrl + po);
                if (!h || h == 0xFFFFFFFF) continue;
                const uintptr_t pawn = EntByIdx(mem, elist, lay.entry_off, lay.stride, (int)(h & 0x7FFF));
                if (!pawn || pawn == lpawn) continue;
                if (mem.Read<int>(pawn + schemas::m_iHealth) > 0) return po;
            }
        }
        return schemas::m_hPlayerPawn;
    }

    static std::mt19937& Rng() { static std::mt19937 r{ std::random_device{}() }; return r; }

    static Vector3 ReadEye(Memory& mem, uintptr_t pawn) {
        const Vector3 o = mem.Read<Vector3>(pawn + schemas::m_vOldOrigin);
        const Vector3 v = mem.Read<Vector3>(pawn + schemas::m_vecViewOffset);
        return { o.x + v.x,o.y + v.y,o.z + v.z };
    }
    static bool GetHitpoint(Memory& mem, uintptr_t pawn, int hitbox, Vector3& out) {
        const int bones[] = { 6,5,4,0,23 };
        return ReadBone(mem, pawn, bones[std::clamp(hitbox, 0, 4)], out);
    }
    static bool IsVisible(Memory& mem, uintptr_t pawn) {
        const uintptr_t st = pawn + schemas::m_entitySpottedState;
        if (mem.Read<bool>(st + schemas::m_bSpotted)) return true;
        return (mem.Read<uint32_t>(st + schemas::m_bSpottedByMask) |
            mem.Read<uint32_t>(st + schemas::m_bSpottedByMask + 4)) != 0;
    }
    static Vector3 CalcAngles(const Vector3& src, const Vector3& dst) {
        const Vector3 d = dst - src;
        const float hyp = std::sqrt(d.x * d.x + d.y * d.y);
        Vector3 a;
        a.x = ClampPitch(-std::atan2(d.z, hyp) * (180.f / 3.14159265f));
        a.y = NormalizeYaw(std::atan2(d.y, d.x) * (180.f / 3.14159265f));
        a.z = 0.f;
        return a;
    }
    static float GetFov(const Vector3& view, const Vector3& target) {
        return std::sqrt(std::pow(target.x - view.x, 2.f) +
            std::pow(NormalizeYaw(target.y - view.y), 2.f));
    }
    static void MoveMouse(long dx, long dy) {
        if (!dx && !dy) return;
        INPUT inp = {};
        inp.type = INPUT_MOUSE; inp.mi.dx = dx; inp.mi.dy = dy;
        inp.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &inp, sizeof(INPUT));
    }
    static void SendClick() {
        INPUT inp[2] = {};
        inp[0].type = INPUT_MOUSE; inp[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inp[1].type = INPUT_MOUSE; inp[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, inp, sizeof(INPUT));
    }

    // ── Aimbot persistent state ──────────────────────────────────────────────
    struct AimbotState {
        float residualX = 0.f, residualY = 0.f;
        EntityLayout lay{ offsets::entity_list_entry, offsets::entity_list_controller_stride };
        std::ptrdiff_t pawnOff = schemas::m_hPlayerPawn;
        uintptr_t lastElist = 0, lastLpawn = 0;
        ULONGLONG lastProbeTick = 0;

        uintptr_t lockedPawn = 0;          // player lock target
        uintptr_t lastTarget = 0;           // for switch delay
        ULONGLONG switchStartTime = 0;
    };
    static AimbotState s_state;

    static void UpdateLayoutProbe(Memory& mem, uintptr_t elist, uintptr_t lpawn) {
        const ULONGLONG now = GetTickCount64();
        if (elist != s_state.lastElist || lpawn != s_state.lastLpawn ||
            (now - s_state.lastProbeTick) > 1000ULL) {
            s_state.lay = ProbeLayout(mem, elist, lpawn);
            s_state.pawnOff = ProbePawnOff(mem, elist, s_state.lay, lpawn);
            s_state.lastElist = elist;
            s_state.lastLpawn = lpawn;
            s_state.lastProbeTick = now;
        }
    }

    static bool IsTargetValid(Memory& mem, uintptr_t pawn, uintptr_t localPawn, int localTeam) {
        auto hpOpt = mem.ReadChecked<int>(pawn + schemas::m_iHealth);
        if (!hpOpt || *hpOpt <= 0 || *hpOpt > 100) return false;
        const int team = mem.Read<int>(pawn + schemas::m_iTeamNum);
        if (!g_Config.aimbot.teammates && team == localTeam) return false;
        const uintptr_t obs = mem.Read<uintptr_t>(pawn + schemas::m_pObserverServices);
        if (obs && mem.Read<int>(obs + schemas::m_iObserverMode) != 0) return false;
        if (g_Config.aimbot.visible_only && !IsVisible(mem, pawn)) return false;
        return true;
    }

} // namespace

void Aimbot::Run(Memory& mem, uintptr_t client)
{
    if (!g_Config.aimbot.enabled) return;

    const bool key1 = (GetAsyncKeyState(g_Config.aimbot.key) & 0x8000) != 0;
    const bool key2 = g_Config.aimbot.key2 && (GetAsyncKeyState(g_Config.aimbot.key2) & 0x8000) != 0;
    if (!key1 && !key2) {
        s_state.residualX = s_state.residualY = 0.f;
        return;
    }

    if (g_Config.aimbot.aim_at_shoot && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        s_state.residualX = s_state.residualY = 0.f;
        return;
    }

    const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
    if (!lpawn) return;

    if (g_Config.aimbot.only_scoped && !mem.Read<bool>(lpawn + schemas::m_bIsScoped)) {
        s_state.residualX = s_state.residualY = 0.f;
        return;
    }

    const Vector3 eye = ReadEye(mem, lpawn);
    const int local_team = mem.Read<int>(lpawn + schemas::m_iTeamNum);
    const Vector3 view_ang = mem.Read<Vector3>(client + offsets::dwViewAngles);
    const int shots = mem.Read<int>(lpawn + schemas::m_iShotsFired);
    const Vector3 punch = mem.Read<Vector3>(lpawn + schemas::m_aimPunchAngle);
    const uintptr_t elist = mem.Read<uintptr_t>(client + offsets::dwEntityList);
    if (!elist) return;

    UpdateLayoutProbe(mem, elist, lpawn);

    // ── Validate locked target ────────────────────────────────────────────────
    if (g_Config.aimbot.player_lock && s_state.lockedPawn) {
        if (!IsTargetValid(mem, s_state.lockedPawn, lpawn, local_team))
            s_state.lockedPawn = 0;
    }

    // ── Target scan ───────────────────────────────────────────────────────────
    uintptr_t best_pawn = 0;
    Vector3   best_hit{};
    float     best_fov = FLT_MAX;

    // If we have a locked target, try it first
    if (g_Config.aimbot.player_lock && s_state.lockedPawn) {
        Vector3 hit;
        if (GetHitpoint(mem, s_state.lockedPawn, g_Config.aimbot.hitbox, hit)) {
            if (g_Config.aimbot.prediction > 0.f) {
                const Vector3 vel = mem.Read<Vector3>(s_state.lockedPawn + schemas::m_vecVelocity);
                if (vel.Length() < 400.f) {
                    const float dt = g_Config.aimbot.prediction / 128.f;
                    hit.x += vel.x * dt; hit.y += vel.y * dt; hit.z += vel.z * dt;
                }
            }
            best_pawn = s_state.lockedPawn;
            best_hit = hit;
            best_fov = GetFov(view_ang, CalcAngles(eye, hit));
        }
    }

    // Normal scan (skip if we already have a valid locked target)
    if (!best_pawn || !g_Config.aimbot.player_lock) {
        for (int i = 1; i <= 128; ++i) {
            const uintptr_t ctrl = EntByIdx(mem, elist, s_state.lay.entry_off, s_state.lay.stride, i);
            if (!ctrl) continue;
            const uint32_t hnd = mem.Read<uint32_t>(ctrl + s_state.pawnOff);
            if (!hnd || hnd == 0xFFFFFFFF) continue;
            const uintptr_t pawn = EntByIdx(mem, elist, s_state.lay.entry_off, s_state.lay.stride, (int)(hnd & 0x7FFF));
            if (!pawn || pawn == lpawn) continue;
            if (!IsTargetValid(mem, pawn, lpawn, local_team)) continue;

            Vector3 hit;
            if (!GetHitpoint(mem, pawn, g_Config.aimbot.hitbox, hit)) continue;

            if (g_Config.aimbot.prediction > 0.f) {
                const Vector3 vel = mem.Read<Vector3>(pawn + schemas::m_vecVelocity);
                if (vel.Length() < 400.f) {
                    const float dt = g_Config.aimbot.prediction / 128.f;
                    hit.x += vel.x * dt; hit.y += vel.y * dt; hit.z += vel.z * dt;
                }
            }

            const float fov = GetFov(view_ang, CalcAngles(eye, hit));
            if (fov > (float)g_Config.aimbot.fov) continue;
            if (fov < best_fov) { best_fov = fov; best_pawn = pawn; best_hit = hit; }
        }
    }

    if (!best_pawn) { s_state.lockedPawn = 0; return; }

    // ── Target switch delay (unified with player lock) ────────────────────────
    if (g_Config.aimbot.player_lock) {
        // If we have a locked target and it's different from the new best,
        // only switch after delay.
        if (s_state.lockedPawn && best_pawn != s_state.lockedPawn) {
            const ULONGLONG now = GetTickCount64();
            if (s_state.lastTarget != best_pawn) {
                s_state.lastTarget = best_pawn;
                s_state.switchStartTime = now;
            }
            if (now - s_state.switchStartTime < (ULONGLONG)g_Config.aimbot.target_switch_delay) {
                best_pawn = s_state.lockedPawn; // keep old
                // recompute best_hit for locked pawn
                GetHitpoint(mem, s_state.lockedPawn, g_Config.aimbot.hitbox, best_hit);
            }
            else {
                s_state.lockedPawn = best_pawn; // switch
                s_state.lastTarget = best_pawn;
                s_state.switchStartTime = now;
            }
        }
        else if (!s_state.lockedPawn) {
            s_state.lockedPawn = best_pawn;
            s_state.lastTarget = best_pawn;
            s_state.switchStartTime = GetTickCount64();
        }
        else {
            // locked target == best_pawn, update lastTarget timestamp
            s_state.lastTarget = best_pawn;
            s_state.switchStartTime = GetTickCount64();
        }
    }
    else {
        // No player lock, use simple delay without persistent lock
        static uintptr_t s_prev_target = 0;
        static ULONGLONG s_prev_time = 0;
        if (best_pawn != s_prev_target && s_prev_target != 0 &&
            g_Config.aimbot.target_switch_delay > 0) {
            if (GetTickCount64() - s_prev_time < (ULONGLONG)g_Config.aimbot.target_switch_delay)
                best_pawn = s_prev_target;
            else {
                s_prev_target = best_pawn;
                s_prev_time = GetTickCount64();
            }
        }
        else if (best_pawn != s_prev_target) {
            s_prev_target = best_pawn;
            s_prev_time = GetTickCount64();
        }
    }

    // ── Auto-fire ─────────────────────────────────────────────────────────────
    if (g_Config.aimbot.auto_fire && best_fov <= g_Config.aimbot.auto_fire_fov) {
        static ULONGLONG s_last_fire = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_last_fire >= 80ULL) { SendClick(); s_last_fire = now; }
    }

    // ── Angle delta ───────────────────────────────────────────────────────────
    const Vector3 target_ang = CalcAngles(eye, best_hit);
    Vector3 delta = target_ang - view_ang;
    delta.y = NormalizeYaw(delta.y);

    if (g_Config.aimbot.rcs && shots > 0) {
        const float str = g_Config.aimbot.rcs_strength / 100.f;
        delta.x -= punch.x * 2.5f * str;
        delta.y -= punch.y * 2.5f * str;
    }

    const float sh = std::max(1.f, g_Config.aimbot.h_speed);
    const float sv = std::max(1.f, g_Config.aimbot.v_speed);
    const float hc = std::clamp(g_Config.aimbot.hitscan_coef, 0.1f, 3.f);

    float sx = std::clamp(delta.x / sv, -10.f, 10.f) * hc;
    float sy = std::clamp(delta.y / sh, -10.f, 10.f) * hc;

    if (std::abs(delta.x) < 0.3f && std::abs(delta.y) < 0.3f) { sx = delta.x; sy = delta.y; }

    std::uniform_real_distribution<float> j(-0.12f, 0.12f);
    sx += j(Rng()); sy += j(Rng());

    s_state.residualX -= sy;
    s_state.residualY += sx;

    const long mx = static_cast<long>(s_state.residualX / DPC);
    const long my = static_cast<long>(s_state.residualY / DPC);
    s_state.residualX -= (float)mx * DPC;
    s_state.residualY -= (float)my * DPC;
    if (mx || my) MoveMouse(mx, my);
}

void Aimbot::Render()
{
    if (!g_Config.aimbot.draw_fov) return;
    auto [w, h] = draw::get_display_size();
    float radius = std::tan(g_Config.aimbot.fov * 0.0174533f) * std::min(w, h) * 0.5f;
    radius = std::clamp(radius, 4.f, std::min(w, h) * 0.49f);
    draw::circle(w * 0.5f, h * 0.5f, radius, g_Config.aimbot.fov_color, 84, 1.5f);
}