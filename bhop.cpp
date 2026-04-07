#define NOMINMAX
#include "bhop.h"
#include "offsets.h"
#include "structs.h"
#include "config.h"
#include <windows.h>

static constexpr uint32_t FL_ONGROUND = 1u;

// ── Why the previous two-frame machine stopped working ───────────────────────
//
// The old approach:
//   Landing frame → send synthetic VK keydown → wait 8 ms → send synthetic keyup
//
// Problem: after the synthetic keyup, the PHYSICAL key (still held) fires a
// keyboard-repeat event at ~30 Hz (~every 33 ms) which immediately re-asserts
// VK_SPACE = DOWN in the OS async key-state table.  The very next landing frame
// GetAsyncKeyState returns DOWN because the physical hold re-set it, so CS2
// has never seen UP since our last synthetic keydown — no UP→DOWN edge, no jump.
// After 1-2 hops the timing diverges and every subsequent landing is missed.
//
// Fix — continuous airborne keyup:
//   While in the air, send synthetic keyup EVERY render frame.
//   Our render loop runs at ~60-165 fps  (~6-16 ms interval).
//   Physical key-repeat fires at ~30 Hz  (~33 ms interval).
//   We're 2-5× faster, so we consistently win the race and keep
//   GetAsyncKeyState(VK_SPACE) returning 0 (UP) between landings.
//   On landing detection, send keydown — CS2 sees a clean 0→1 edge every time.
// ─────────────────────────────────────────────────────────────────────────────

static void KeyDown(WORD vk) {
    INPUT i = {};
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = vk;
    i.ki.dwFlags = 0;                // pure virtual-key down, no scan-code
    SendInput(1, &i, sizeof(INPUT));
}

static void KeyUp(WORD vk) {
    INPUT i = {};
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = vk;
    i.ki.dwFlags = KEYEVENTF_KEYUP;  // pure virtual-key up, no scan-code
    SendInput(1, &i, sizeof(INPUT));
}

void Bhop::Run(Memory& mem, uintptr_t client)
{
    if (!g_Config.bhop.enabled) return;

    const WORD   vk = static_cast<WORD>(g_Config.bhop.key);
    const bool   key_held = (GetAsyncKeyState(vk) & 0x8000) != 0;

    static bool s_was_on_ground = false;

    const uintptr_t lpawn =
        mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
    if (!lpawn) {
        s_was_on_ground = false;
        return;
    }

    const uint32_t flags = mem.Read<uint32_t>(lpawn + schemas::m_fFlags);
    const bool     on_ground = (flags & FL_ONGROUND) != 0;
    const bool     landed = on_ground && !s_was_on_ground;
    s_was_on_ground = on_ground;

    if (!key_held) return; // don't interfere when player isn't bhopping

    if (!on_ground) {
        // ── AIRBORNE: continuously suppress the physical hold ─────────────────
        // Sending keyup every frame keeps GetAsyncKeyState = 0 (UP) so CS2
        // sees a clean rising edge the moment we send keydown on landing.
        KeyUp(vk);
    }
    else if (landed) {
        // ── JUST LANDED: fire the jump ────────────────────────────────────────
        // Key state is guaranteed UP from the last airborne frame.
        // This keydown is a clean 0→1 transition — CS2 registers the jump.
        KeyDown(vk);
    }
    // While on-ground-and-not-just-landed: do nothing.
    // Let the game's normal jump handle the stationary press.
}