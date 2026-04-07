#define NOMINMAX
#include "triggerbot.h"
#include "memory.h"
#include "offsets.h"
#include "structs.h"
#include "config.h"
#include <windows.h>

namespace {
    struct EntityLayout { std::ptrdiff_t entry_off, stride; };

    static uintptr_t EntByIdx(
        Memory& mem, uintptr_t elist,
        std::ptrdiff_t eoff, std::ptrdiff_t stride, int idx)
    {
        if (idx <= 0) return 0;
        const uintptr_t chunk = mem.Read<uintptr_t>(elist + eoff + 8 * (idx >> 9));
        if (!chunk) return 0;
        return mem.Read<uintptr_t>(chunk + stride * (idx & 0x1FF));
    }

    static EntityLayout ProbeLayout(Memory& mem, uintptr_t elist, uintptr_t lpawn)
    {
        const std::ptrdiff_t k_entry[] = { offsets::entity_list_entry, 0x10, 0x8 };
        const std::ptrdiff_t k_stride[] = { offsets::entity_list_controller_stride, 0x78, 0x70 };
        EntityLayout best{ offsets::entity_list_entry, offsets::entity_list_controller_stride };
        int best_score = -1;
        for (auto e : k_entry) {
            for (auto s : k_stride) {
                int score = 0;
                for (int i = 1; i <= 128; ++i) {
                    const uintptr_t ctrl = EntByIdx(mem, elist, e, s, i);
                    if (!ctrl) continue;
                    for (std::ptrdiff_t po : {
                        static_cast<std::ptrdiff_t>(schemas::m_hPlayerPawn),
                            static_cast<std::ptrdiff_t>(0x7E4),
                            static_cast<std::ptrdiff_t>(0x90C) })
                    {
                        const uint32_t h = mem.Read<uint32_t>(ctrl + po);
                        if (!h || h == 0xFFFFFFFF) continue;
                        const uintptr_t pawn = EntByIdx(mem, elist, e, s, (int)(h & 0x7FFF));
                        if (!pawn || pawn == lpawn) continue;
                        if (mem.Read<int>(pawn + schemas::m_iHealth) > 0) { ++score; break; }
                    }
                }
                if (score > best_score) { best_score = score; best = { e, s }; }
            }
        }
        return best;
    }

    static void SendClick() {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, inputs, sizeof(INPUT));
    }
}

void Triggerbot::Run(Memory& mem, uintptr_t client)
{
    if (!g_Config.triggerbot.enabled) return;
    if (!(GetAsyncKeyState(g_Config.triggerbot.key) & 0x8000)) return;

    const uintptr_t lpawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
    if (!lpawn) return;

    // ── Only scoped check (FIX) ──────────────────────────────────────────────
    if (g_Config.triggerbot.only_scoped && !mem.Read<bool>(lpawn + schemas::m_bIsScoped))
        return;

    const uintptr_t elist = mem.Read<uintptr_t>(client + offsets::dwEntityList);
    if (!elist) return;

    static EntityLayout s_lay{ offsets::entity_list_entry, offsets::entity_list_controller_stride };
    static uintptr_t    s_elist{ 0 };
    static uintptr_t    s_lpawn{ 0 };
    static ULONGLONG    s_tick{ 0 };
    {
        const ULONGLONG now = GetTickCount64();
        if (elist != s_elist || lpawn != s_lpawn || (now - s_tick) > 1000ULL) {
            s_lay = ProbeLayout(mem, elist, lpawn);
            s_elist = elist;
            s_lpawn = lpawn;
            s_tick = now;
        }
    }

    const int xhair_idx = mem.Read<int>(lpawn + schemas::m_iIDEntIndex);
    if (xhair_idx <= 0) return;

    const uintptr_t target_pawn = EntByIdx(
        mem, elist, s_lay.entry_off, s_lay.stride, xhair_idx);
    if (!target_pawn || target_pawn == lpawn) return;

    const int hp = mem.Read<int>(target_pawn + schemas::m_iHealth);
    if (hp <= 0 || hp > 100) return;

    const int local_team = mem.Read<int>(lpawn + schemas::m_iTeamNum);
    const int target_team = mem.Read<int>(target_pawn + schemas::m_iTeamNum);
    if (!g_Config.triggerbot.teammates && target_team == local_team) return;

    const uintptr_t obs = mem.Read<uintptr_t>(target_pawn + schemas::m_pObserverServices);
    if (obs && mem.Read<int>(obs + schemas::m_iObserverMode) != 0) return;

    if (g_Config.triggerbot.visible_only) {
        const bool spotted = mem.Read<bool>(
            target_pawn + schemas::m_entitySpottedState + schemas::m_bSpotted);
        if (!spotted) return;
    }

    static uintptr_t s_last_target = 0;
    static ULONGLONG s_aim_start = 0;
    static bool      s_fired = false;

    if (target_pawn != s_last_target) {
        s_last_target = target_pawn;
        s_aim_start = GetTickCount64();
        s_fired = false;
    }

    if (s_fired) return;

    const ULONGLONG dwell = GetTickCount64() - s_aim_start;
    if (dwell < static_cast<ULONGLONG>(g_Config.triggerbot.delay)) return;

    SendClick();
    s_fired = true;
}