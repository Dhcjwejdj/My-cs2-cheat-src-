#pragma once
#include "color.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

struct Config {

    bool dark_mode = true;

    // ─── AIMBOT ───────────────────────────────────────────────────────────────
    struct Aimbot {
        bool       enabled = true;
        int        key = VK_XBUTTON1;
        int        fov = 5;
        int        smoothing = 10;
        bool       rcs = true;
        int        rcs_strength = 100;
        bool       visible_only = true;
        bool       teammates = false;
        int        hitbox = 2;
        bool       draw_fov = true;
        bool       auto_fire = false;
        float      auto_fire_fov = 1.5f;
        float      prediction = 1.0f;   // velocity prediction in 128Hz ticks
        bool       player_lock = false;  // lock onto first acquired target
        bool       aim_at_shoot = false;  // only aim when mouse button is held
        bool       only_scoped = false;  // only aim when local player is scoped
        int        key2 = 0;       // secondary activation key (0 = disabled)
        int        target_switch_delay = 200;  // ms before switching targets
        float      h_speed = 10.f;   // horizontal smoothing speed
        float      v_speed = 10.f;   // vertical smoothing speed
        float      hitscan_coef = 1.0f;   // hitscan coefficient multiplier
        draw::rgba fov_color{ 200, 80, 100, 180 };
    } aimbot;

    // ─── TRIGGERBOT ───────────────────────────────────────────────────────────
    struct Triggerbot {
        bool       enabled = true;
        int        key = VK_XBUTTON2;
        int        delay = 50;
        bool       visible_only = true;
        bool       teammates = false;
        int        hitbox = 2;
        int        shots_delay = 50;   // delay between shots
        bool       only_scoped = false;
    } triggerbot;

    // ─── ESP ──────────────────────────────────────────────────────────────────
    struct ESP {
        bool       enabled = true;

        // Box
        bool       box = true;
        bool       box_fill = false;
        bool       corner_box = false;
        bool       box_outline = true;    // dark halo outline around box
        bool       box_3d = false;   // 3D wireframe box

        // Bars
        bool       health = true;
        bool       health_text = false;
        bool       armor_bar = true;

        // Skeleton / bones
        bool       skeleton = false;
        bool       joints = false;   // filled circles at key joints
        bool       wireframe_esp = false;   // thick wireframe (separate from skeleton)

        // Chams (2D overlay projection — fills a convex hull of bone screen points)
        bool       chams = false;
        bool       chams_occluded = true;    // show chams through walls too

        // Labels
        bool       name = true;
        bool       distance = true;
        bool       weapon_esp = false;
        bool       ammo_esp = false;
        bool       ping_esp = false;

        // Overlays
        bool       head_dot = false;
        bool       tracers = false;
        bool       snap_lines = false;

        // Alerts
        bool       low_hp_alert = true;
        int        low_hp_threshold = 25;
        bool       flash_bar = false;
        bool       flags_esp = false;

        // Reload tracker — shows when an enemy is reloading
        bool       reload_tracker = false;

        // Hit chance — novel feature: shows estimated hit probability
        // based on enemy distance + lateral velocity
        bool       hit_chance = false;

        // Health gradient: box outline shifts green→red with enemy HP
        bool       health_gradient_box = false;

        // Misc
        bool       bomb_timer = true;
        bool       visible_only = false;
        bool       teammates = false;
        bool       use_max_dist = false;
        float      max_distance = 3000.f;

        // Colors
        draw::rgba box_color{ 200,  80, 100, 255 };
        draw::rgba occluded_color{ 90,  30,  45, 255 };
        draw::rgba low_hp_color{ 255,  60,  60, 255 };
        draw::rgba health_color{ 80, 210, 120, 255 };
        draw::rgba armor_color{ 80, 160, 230, 255 };
        draw::rgba skeleton_color{ 255, 255, 255, 180 };
        draw::rgba wireframe_color{ 255, 220,  60, 200 };
        draw::rgba chams_visible_color{ 230,  60,  60,  90 };
        draw::rgba chams_occluded_color{ 60,  60, 220,  55 };
        draw::rgba tracer_color{ 200,  80, 100, 140 };
        draw::rgba snap_color{ 200, 200,  80, 120 };
        draw::rgba head_dot_color{ 255, 255, 255, 200 };
    } esp;

    // ─── BHOP ─────────────────────────────────────────────────────────────────
    struct Bhop {
        bool enabled = false;
        int  key = VK_SPACE;
    } bhop;

    // ─── MISC ─────────────────────────────────────────────────────────────────
    struct Misc {
        bool  radar = false;
        float radar_size = 110.f;
        float radar_range = 1500.f;
        bool  radar_rotate = true;
        bool  spectator_list = true;
        bool  crosshair = false;
        int   crosshair_size = 6;
        int   crosshair_gap = 3;
        int   crosshair_thick = 1;
        bool  crosshair_dot = true;
        draw::rgba crosshair_color{ 255, 255, 255, 220 };
        bool  show_clock = false;
        bool  local_info = false;
        bool  no_flash = false;
        bool  no_spread = false;  // visual spread indicator
        bool  auto_pistol = false;  // rapid-fire semi-autos
        bool  velocity_info = false;
        bool  grenade_warning = false;

        // Punch dot — shows where bullets actually go after aim punch
        // Reads m_aimPunchAngle * 2.5 and projects to screen using view matrix FOV
        bool  punch_dot = false;
        draw::rgba punch_dot_color{ 255, 200, 50, 220 };

        // Round timer - overlay showing remaining round time
        bool  round_timer = false;

        // Footstep ESP - circles at moving enemies' feet
        bool  footstep_esp = false;
        float footstep_threshold = 80.f;  // min speed (u/s) to show circle
        draw::rgba footstep_color{ 255, 220, 80, 160 };

        // Auto-crouch while firing
        bool  auto_crouch = false;
        int   auto_crouch_key = VK_XBUTTON1; // triggers with this key held
    } misc;

    bool menu_open = false;

    // ─── Persistence ──────────────────────────────────────────────────────────
private:
    static constexpr uint32_t k_magic = 0x574B4346u;
    static constexpr uint32_t k_version = 12u; // bump: chams, wireframe, reload, hit_chance, punch_dot

#pragma pack(push, 1)
    struct FileHeader {
        uint32_t magic, version, payload_size;
        char     slot_name[32];
    };
#pragma pack(pop)

    static std::string SlotPath(int slot) {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        char* s = strrchr(exe, '\\');
        if (s) *(s + 1) = '\0';
        return std::string(exe) + "wicked_cfg_" + std::to_string(slot) + ".bin";
    }

public:
    bool Save(int slot = 0, const char* name = "Default") const {
        FILE* f = nullptr;
        if (fopen_s(&f, SlotPath(slot).c_str(), "wb") || !f) return false;
        Config p = *this; p.menu_open = false;
        FileHeader h{ k_magic, k_version, sizeof(Config), {} };
        strncpy_s(h.slot_name, name, _TRUNCATE);
        fwrite(&h, sizeof(h), 1, f);
        fwrite(&p, sizeof(p), 1, f);
        fclose(f); return true;
    }

    bool Load(int slot = 0) {
        FILE* f = nullptr;
        if (fopen_s(&f, SlotPath(slot).c_str(), "rb") || !f) return false;
        FileHeader h{};
        if (fread(&h, sizeof(h), 1, f) != 1 ||
            h.magic != k_magic || h.version != k_version ||
            h.payload_size != sizeof(Config)) {
            fclose(f); return false;
        }
        Config ld{};
        if (fread(&ld, sizeof(ld), 1, f) != 1) { fclose(f); return false; }
        fclose(f);
        bool wo = menu_open; *this = ld; menu_open = wo;
        return true;
    }
};

extern Config g_Config;