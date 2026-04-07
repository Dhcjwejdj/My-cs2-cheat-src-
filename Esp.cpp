#define NOMINMAX
#include "esp.h"
#include "memory.h"
#include "offsets.h"
#include "structs.h"
#include "utils.h"
#include "imgui_layer.h"
#include "config.h"
#include <string>
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <windows.h>

namespace {

    // ── Weapon name ───────────────────────────────────────────────────────────────
    static const char* WeaponName(uint16_t idx) {
        switch (idx) {
        case 1:return"DEAGLE";   case 2:return"ELITES";  case 3:return"FIVESEVEN";
        case 4:return"GLOCK";    case 7:return"AK-47";   case 8:return"AUG";
        case 9:return"AWP";      case 10:return"FAMAS";  case 11:return"G3SG1";
        case 13:return"GALIL";   case 14:return"M249";   case 16:return"M4A4";
        case 17:return"MAC-10";  case 19:return"P90";    case 23:return"MP5-SD";
        case 24:return"UMP-45";  case 25:return"XM1014"; case 26:return"BIZON";
        case 27:return"MAG-7";   case 28:return"NEGEV";  case 29:return"SAWEDOFF";
        case 30:return"TEC-9";   case 31:return"ZEUS";   case 32:return"P2000";
        case 33:return"MP7";     case 34:return"MP9";    case 35:return"NOVA";
        case 36:return"P250";    case 38:return"SCAR-20"; case 39:return"SG553";
        case 40:return"SSG08";   case 60:return"M4A1-S"; case 61:return"USP-S";
        case 63:return"CZ75";    case 64:return"R8";     default:return nullptr;
        }
    }

    // Confirmed CS2 skeleton bone pairs
    static constexpr int SKELETON[][2] = {
        {6,5},{5,4},{4,3},{3,0},
        {4,7},{7,8},{8,9},{9,10},
        {4,11},{11,12},{12,13},{13,14},
        {0,22},{22,23},{23,24},
        {0,25},{25,26},{26,27},
    };

    // ── Reload tracker state ──────────────────────────────────────────────────────
    struct ReloadState {
        int  last_clip = -1;
        bool is_reloading = false;
        ULONGLONG reload_start = 0;
    };
    static std::unordered_map<uintptr_t, ReloadState> s_reload_map;

    // ── Player data ───────────────────────────────────────────────────────────────
    struct Player {
        uintptr_t   pawn, controller;
        Vector3     origin, head;
        Vector3     velocity;        // for hit chance
        int         health, armor, team, ping;
        bool        visible, has_helmet, has_defuser, is_scoped;
        float       distance, flash_alpha;
        std::string name, weapon;
        int         clip, reserve;
        bool        is_reloading;    // filled by reload tracker
    };

    // ── Entity list helpers ───────────────────────────────────────────────────────
    struct EntityLayout { std::ptrdiff_t entry_off, stride; };

    static uintptr_t GetEntityByIndex(Memory& mem, uintptr_t el,
        std::ptrdiff_t eoff, std::ptrdiff_t stride, int idx)
    {
        if (idx <= 0) return 0;
        const uintptr_t chunk = mem.Read<uintptr_t>(el + eoff + 8 * (idx >> 9));
        if (!chunk) return 0;
        return mem.Read<uintptr_t>(chunk + stride * (idx & 0x1FF));
    }

    static EntityLayout SelectBestLayout(Memory& mem, uintptr_t el, uintptr_t lpawn)
    {
        const std::ptrdiff_t ke[] = { offsets::entity_list_entry,0x10,0x8 };
        const std::ptrdiff_t ks[] = { offsets::entity_list_controller_stride,0x78,0x70 };
        EntityLayout best{ offsets::entity_list_entry, offsets::entity_list_controller_stride };
        int best_score = -1;
        for (auto eoff : ke) for (auto stride : ks) {
            int score = 0;
            for (int i = 1; i <= 128; ++i) {
                const uintptr_t ctrl = GetEntityByIndex(mem, el, eoff, stride, i);
                if (!ctrl) continue;
                for (std::ptrdiff_t poff : {
                    static_cast<std::ptrdiff_t>(schemas::m_hPlayerPawn),
                        static_cast<std::ptrdiff_t>(0x7E4),
                        static_cast<std::ptrdiff_t>(0x90C)})
                {
                    const uint32_t h = mem.Read<uint32_t>(ctrl + poff);
                    if (!h || h == 0xFFFFFFFF) continue;
                    const uintptr_t pawn = GetEntityByIndex(mem, el, eoff, stride, (int)(h & 0x7FFF));
                    if (!pawn || pawn == lpawn) continue;
                    if (mem.Read<int>(pawn + schemas::m_iHealth) > 0) { ++score; break; }
                }
            }
            if (score > best_score) { best_score = score; best = { eoff,stride }; }
        }
        return best;
    }

    static std::ptrdiff_t SelectBestPawnOffset(Memory& mem, uintptr_t el,
        const EntityLayout& lay, uintptr_t lpawn)
    {
        for (std::ptrdiff_t po : {
            static_cast<std::ptrdiff_t>(schemas::m_hPlayerPawn),
                static_cast<std::ptrdiff_t>(0x7E4),
                static_cast<std::ptrdiff_t>(0x90C)})
        {
            for (int i = 1; i <= 128; ++i) {
                const uintptr_t ctrl = GetEntityByIndex(mem, el, lay.entry_off, lay.stride, i);
                if (!ctrl) continue;
                const uint32_t h = mem.Read<uint32_t>(ctrl + po);
                if (!h || h == 0xFFFFFFFF) continue;
                const uintptr_t pawn = GetEntityByIndex(mem, el, lay.entry_off, lay.stride, (int)(h & 0x7FFF));
                if (!pawn || pawn == lpawn) continue;
                if (mem.Read<int>(pawn + schemas::m_iHealth) > 0) return po;
            }
        }
        return schemas::m_hPlayerPawn;
    }

    static bool IsValidPlayer(Memory& mem, uintptr_t pawn, int local_team, bool& out_vis)
    {
        const auto hp1 = mem.ReadChecked<int>(pawn + schemas::m_iHealth);
        if (!hp1 || *hp1 <= 0 || *hp1 > 100) return false;
        const int hp2 = mem.Read<int>(pawn + schemas::m_iHealth);
        if (hp2 <= 0 || hp2 > 100 || std::abs(*hp1 - hp2) > 30) return false;
        const int team = mem.Read<int>(pawn + schemas::m_iTeamNum);
        if (!g_Config.esp.teammates && team == local_team) return false;
        const uintptr_t obs = mem.Read<uintptr_t>(pawn + schemas::m_pObserverServices);
        if (obs && mem.Read<int>(obs + schemas::m_iObserverMode) != 0) return false;
        out_vis = mem.Read<bool>(pawn + schemas::m_entitySpottedState + schemas::m_bSpotted);
        return true;
    }

    static std::string GetPlayerName(Memory& mem, uintptr_t ctrl)
    {
        auto raw = mem.Read<std::array<char, 128>>(ctrl + schemas::m_iszPlayerName);
        for (char& c : raw) { const auto u = (unsigned char)c; if (u && (u < 0x20 || u>0x7E))c = '?'; }
        std::string n(raw.data(), strnlen_s(raw.data(), raw.size()));
        return n.empty() ? "enemy" : n;
    }

    static std::string GetWeapon(Memory& mem, uintptr_t pawn)
    {
        const uintptr_t wep = mem.Read<uintptr_t>(pawn + schemas::m_pClippingWeapon);
        if (!wep) return {};
        const uint16_t def = mem.Read<uint16_t>(
            wep + schemas::m_AttributeManager + schemas::m_Item + schemas::m_iItemDefinitionIndex);
        const char* n = WeaponName(def); return n ? n : "";
    }

    static ImU32 ToImU32(const draw::rgba& c) { return IM_COL32(c.r, c.g, c.b, c.a); }

    // ── Draw helpers ──────────────────────────────────────────────────────────────

    static void DrawBox(float x, float y, float w, float h, const draw::rgba& col, bool fill)
    {
        if (fill)
            draw::rect_filled(x + 1.f, y + 1.f, w - 2.f, h - 2.f, draw::rgba(col.r, col.g, col.b, 35));
        draw::rect(x, y, w, h, col, 1.f);
    }

    static void DrawCornerBox(ImDrawList* dl, float x, float y, float w, float h,
        const draw::rgba& col)
    {
        const ImU32 c = ToImU32(col), sh = IM_COL32(0, 0, 0, 100);
        const float lw = std::min(w, h) * 0.22f, th = 2.f, r = x + w, b = y + h;
        auto L = [&](float x1, float y1, float x2, float y2) {
            dl->AddLine(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 + 1, y2 + 1), sh, th);
            dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), c, th);
            };
        L(x, y, x + lw, y); L(x, y, x, y + lw);
        L(r, y, r - lw, y); L(r, y, r, y + lw);
        L(x, b, x + lw, b); L(x, b, x, b - lw);
        L(r, b, r - lw, b); L(r, b, r, b - lw);
    }

    static void Draw3DBox(ImDrawList* dl, const Player& p,
        const view_matrix_t& vm, int sw, int sh, const draw::rgba& col)
    {
        const float R = 16.f, H = 72.f;
        const Vector3& o = p.origin;
        const Vector3 corners[8] = {
            {o.x - R,o.y - R,o.z},{o.x + R,o.y - R,o.z},
            {o.x + R,o.y + R,o.z},{o.x - R,o.y + R,o.z},
            {o.x - R,o.y - R,o.z + H},{o.x + R,o.y - R,o.z + H},
            {o.x + R,o.y + R,o.z + H},{o.x - R,o.y + R,o.z + H},
        };
        Vector2 sc[8];
        for (int i = 0; i < 8; ++i)
            if (!WorldToScreen(corners[i], sc[i], vm, sw, sh)) return;
        const ImU32 c = IM_COL32(col.r, col.g, col.b, (int)(col.a * 0.65f));
        auto L = [&](int a, int b) {dl->AddLine(ImVec2(sc[a].x, sc[a].y), ImVec2(sc[b].x, sc[b].y), c, 1.f); };
        L(0, 1); L(1, 2); L(2, 3); L(3, 0);
        L(4, 5); L(5, 6); L(6, 7); L(7, 4);
        L(0, 4); L(1, 5); L(2, 6); L(3, 7);
    }

    static void DrawGradientFill(ImDrawList* dl, float x, float y, float w, float h,
        const draw::rgba& col)
    {
        dl->AddRectFilledMultiColor(
            ImVec2(x + 1, y + 1), ImVec2(x + w - 1, y + h - 1),
            IM_COL32(col.r, col.g, col.b, 50), IM_COL32(col.r, col.g, col.b, 50),
            IM_COL32(col.r, col.g, col.b, 5), IM_COL32(col.r, col.g, col.b, 5));
    }

    static void DrawBar(float x, float y, float h, int val, int max,
        const draw::rgba& color, bool right_side)
    {
        const float pct = std::clamp(val / (float)max, 0.f, 1.f);
        const float bh = h * pct, bx = right_side ? x + 4.f : x - 7.f;
        draw::rect(bx, y, 5.f, h, draw::rgba(0, 0, 0, 160), 1.f);
        draw::rect_filled(bx + 1.f, y + h - bh, 3.f, bh, color);
    }

    static void DrawLabel(float cx, float y, const std::string& text, const draw::rgba& col)
    {
        auto [tw, th] = draw::measure_text(text);
        draw::text<draw::tstyles::outlined>(cx - tw * 0.5f, y, text.c_str(), col);
    }

    // ── 2D Overlay Chams ──────────────────────────────────────────────────────────
    // External chams aren't possible via DLL injection, so this is the overlay
    // equivalent: project all skeleton bones to screen, sort the 2D points by
    // angle around their centroid (giving a convex order), then fill the polygon.
    // The result is a coloured silhouette that reads visually like real chams.
    // Visible enemies get a saturated tint; occluded get a dim through-wall tint.
    static void DrawChams(ImDrawList* dl, Memory& mem, const Player& p,
        const view_matrix_t& vm, int sw, int sh)
    {
        if (!g_Config.esp.chams) return;
        if (!p.visible && !g_Config.esp.chams_occluded) return;

        // Collect all bone screen projections (use all 28 known bones)
        static constexpr int k_chams_bones[] = {
            0,3,4,5,6,        // spine + head
            7,8,9,10,         // left arm
            11,12,13,14,      // right arm
            22,23,24,         // left leg
            25,26,27          // right leg
        };
        std::vector<ImVec2> pts;
        pts.reserve(18);
        for (const int b : k_chams_bones) {
            Vector3 bw{};
            if (!ReadBone(mem, p.pawn, b, bw)) continue;
            Vector2 bs{};
            if (!WorldToScreen(bw, bs, vm, sw, sh)) continue;
            pts.push_back(ImVec2(bs.x, bs.y));
        }
        if (pts.size() < 4) return;

        // Compute centroid
        float sum_x = 0.f, sum_y = 0.f;
        for (const auto& pt : pts) { sum_x += pt.x; sum_y += pt.y; }
        const float ccx = sum_x / (float)pts.size();
        const float ccy = sum_y / (float)pts.size();

        // Sort by angle around centroid — gives convex winding order for
        // a roughly player-shaped cloud of points
        std::sort(pts.begin(), pts.end(), [&](const ImVec2& a, const ImVec2& b) {
            return atan2f(a.y - ccy, a.x - ccx) < atan2f(b.y - ccy, b.x - ccx);
            });

        const draw::rgba& chams_col = p.visible
            ? g_Config.esp.chams_visible_color
            : g_Config.esp.chams_occluded_color;

        // Filled silhouette
        dl->AddConvexPolyFilled(pts.data(), (int)pts.size(),
            IM_COL32(chams_col.r, chams_col.g, chams_col.b, chams_col.a));

        // Bright outline to simulate the "glow" look of chams
        const int outline_alpha = std::min(255, (int)chams_col.a * 3);
        dl->AddPolyline(pts.data(), (int)pts.size(),
            IM_COL32(chams_col.r, chams_col.g, chams_col.b, outline_alpha),
            ImDrawFlags_Closed, 2.0f);
    }

    // ── Wireframe skeleton (thick + joints) ───────────────────────────────────────
    static void DrawWireframe(ImDrawList* dl, Memory& mem, const Player& p,
        const view_matrix_t& vm, int sw, int sh)
    {
        if (!g_Config.esp.wireframe_esp) return;
        const ImU32 wc = ToImU32(g_Config.esp.wireframe_color);
        const ImU32 jc = IM_COL32(g_Config.esp.wireframe_color.r,
            g_Config.esp.wireframe_color.g,
            g_Config.esp.wireframe_color.b, 255);
        const ImU32 sh_col = IM_COL32(0, 0, 0, 120);

        for (const auto& pair : SKELETON) {
            Vector3 b1, b2;
            if (!ReadBone(mem, p.pawn, pair[0], b1)) continue;
            if (!ReadBone(mem, p.pawn, pair[1], b2)) continue;
            const float sl = b1.Distance(b2);
            if (!std::isfinite(sl) || sl < 0.5f || sl > 70.f) continue;
            Vector2 s1, s2;
            if (!WorldToScreen(b1, s1, vm, sw, sh)) continue;
            if (!WorldToScreen(b2, s2, vm, sw, sh)) continue;
            // Shadow pass
            dl->AddLine(ImVec2(s1.x + 1, s1.y + 1), ImVec2(s2.x + 1, s2.y + 1), sh_col, 3.5f);
            // Main line
            dl->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), wc, 2.5f);
        }

        // Large joint spheres at key nodes
        static constexpr int k_wf_joints[] = { 0,3,4,5,6,9,13,22,23,25,26 };
        for (const int b : k_wf_joints) {
            Vector3 bw{};
            if (!ReadBone(mem, p.pawn, b, bw)) continue;
            Vector2 bs{};
            if (!WorldToScreen(bw, bs, vm, sw, sh)) continue;
            dl->AddCircleFilled(ImVec2(bs.x, bs.y), 4.5f, sh_col, 10);
            dl->AddCircleFilled(ImVec2(bs.x, bs.y), 3.5f, jc, 10);
        }
    }

    // ── Hit Chance display ────────────────────────────────────────────────────────
    // Novel feature: estimates the probability of landing a shot on this enemy.
    // Formula uses two inputs we already have per-frame:
    //   1. Distance penalty: linear from 0% at 5000u → 100% at 0u
    //   2. Lateral velocity: a fast-strafing enemy is harder to hit
    //      (penalises based on XY speed vs. max strafe speed ~260 u/s)
    // Result displayed as a colored percentage: green ≥70%, yellow ≥40%, red <40%.
    // This gives an at-a-glance "is it worth peeking this guy right now" signal.
    static float CalcHitChance(const Player& p)
    {
        const float dist_factor = std::clamp(1.f - p.distance / 5000.f, 0.f, 1.f);
        const float spd = std::sqrt(p.velocity.x * p.velocity.x +
            p.velocity.y * p.velocity.y);
        const float vel_factor = std::clamp(1.f - spd / 260.f, 0.f, 1.f);
        // Weighted: distance matters more (60%), velocity secondary (40%)
        return std::clamp((dist_factor * 0.60f + vel_factor * 0.40f) * 100.f, 2.f, 99.f);
    }

} // anonymous namespace

// ============================================================================
//  ESP::Render
// ============================================================================
void ESP::Render(Memory& mem, uintptr_t client, int screen_w, int screen_h)
{
    if (!g_Config.esp.enabled) return;

    const uintptr_t local_pawn = mem.Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
    if (!local_pawn) return;
    const int         local_team = mem.Read<int>(local_pawn + schemas::m_iTeamNum);
    const Vector3     local_origin = mem.Read<Vector3>(local_pawn + schemas::m_vOldOrigin);
    const view_matrix_t vm = mem.Read<view_matrix_t>(client + offsets::dwViewMatrix);
    const uintptr_t   entity_list = mem.Read<uintptr_t>(client + offsets::dwEntityList);
    if (!entity_list) return;

    static EntityLayout   s_layout{ offsets::entity_list_entry,
                                     offsets::entity_list_controller_stride };
    static std::ptrdiff_t s_pawn_off{ schemas::m_hPlayerPawn };
    static DWORD          s_pid{ 0 };
    static uintptr_t      s_elist{ 0 }, s_lpawn{ 0 };
    static ULONGLONG      s_tick{ 0 };
    {
        const ULONGLONG now = GetTickCount64();
        if (mem.process_id != s_pid || entity_list != s_elist
            || local_pawn != s_lpawn || (now - s_tick) > 1000ULL)
        {
            s_layout = SelectBestLayout(mem, entity_list, local_pawn);
            s_pawn_off = SelectBestPawnOffset(mem, entity_list, s_layout, local_pawn);
            s_pid = mem.process_id; s_elist = entity_list;
            s_lpawn = local_pawn;  s_tick = now;
        }
    }

    std::vector<Player> players;
    players.reserve(128);

    for (int i = 1; i <= 128; ++i) {
        const uintptr_t ctrl = GetEntityByIndex(mem, entity_list,
            s_layout.entry_off, s_layout.stride, i);
        if (!ctrl) continue;
        const uint32_t pawn_handle = mem.Read<uint32_t>(ctrl + s_pawn_off);
        if (!pawn_handle || pawn_handle == 0xFFFFFFFF) continue;
        const uintptr_t pawn = GetEntityByIndex(mem, entity_list,
            s_layout.entry_off, s_layout.stride, (int)(pawn_handle & 0x7FFF));
        if (!pawn || pawn == local_pawn) continue;

        bool visible = false;
        if (!IsValidPlayer(mem, pawn, local_team, visible)) continue;
        if (g_Config.esp.visible_only && !visible) continue;

        Player p;
        p.pawn = pawn;
        p.controller = ctrl;
        p.origin = mem.Read<Vector3>(pawn + schemas::m_vOldOrigin);
        p.health = mem.Read<int>(pawn + schemas::m_iHealth);
        p.armor = mem.Read<int>(pawn + schemas::m_ArmorValue);
        p.team = mem.Read<int>(pawn + schemas::m_iTeamNum);
        p.visible = visible;
        p.name = GetPlayerName(mem, ctrl);
        p.distance = p.origin.Distance(local_origin);
        p.flash_alpha = mem.Read<float>(pawn + schemas::m_flFlashOverlayAlpha);
        p.is_scoped = mem.Read<bool>(pawn + schemas::m_bIsScoped);
        p.has_helmet = mem.Read<bool>(ctrl + schemas::m_bPawnHasHelmet);
        p.has_defuser = mem.Read<bool>(ctrl + schemas::m_bPawnHasDefuser);
        p.ping = mem.Read<int>(ctrl + schemas::m_iPing);
        p.velocity = mem.Read<Vector3>(pawn + schemas::m_vecVelocity);

        if (g_Config.esp.use_max_dist && p.distance > g_Config.esp.max_distance) continue;

        if (g_Config.esp.weapon_esp || g_Config.esp.ammo_esp || g_Config.esp.reload_tracker) {
            const uintptr_t wep = mem.Read<uintptr_t>(pawn + schemas::m_pClippingWeapon);
            if (wep) {
                if (g_Config.esp.weapon_esp) p.weapon = GetWeapon(mem, pawn);
                p.clip = mem.Read<int>(wep + schemas::m_iClip1);
                p.reserve = mem.Read<int>(wep + schemas::m_pReserveAmmo);
            }
        }

        // ── Reload tracker logic ──────────────────────────────────────────────
        // Detects the moment clip drops to 0 (or low) and tracks until it
        // fills back up (reload complete). No guessing reload durations —
        // we just watch the clip value flip back to non-zero.
        if (g_Config.esp.reload_tracker) {
            auto& rs = s_reload_map[pawn];
            if (rs.last_clip > 0 && p.clip == 0) {
                // Edge: clip just hit zero → reload started
                rs.is_reloading = true;
                rs.reload_start = GetTickCount64();
            }
            else if (rs.is_reloading && p.clip > 0 && p.clip > rs.last_clip) {
                // Clip refilled → reload complete
                rs.is_reloading = false;
            }
            // Safety: cap reload display at 8 seconds (prevents stuck indicator)
            if (rs.is_reloading && (GetTickCount64() - rs.reload_start) > 8000ULL)
                rs.is_reloading = false;
            rs.last_clip = p.clip;
            p.is_reloading = rs.is_reloading;
        }

        if (!ReadBone(mem, pawn, 6, p.head)) {
            const Vector3 vo = mem.Read<Vector3>(pawn + schemas::m_vecViewOffset);
            p.head = { p.origin.x + vo.x, p.origin.y + vo.y, p.origin.z + vo.z + 8.f };
        }
        players.push_back(std::move(p));
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float screen_cx = screen_w * 0.5f;
    const float screen_cy = (float)screen_h;

    // ── PASS 1: Chams (drawn first so everything sits on top) ─────────────────
    for (const auto& p : players)
        DrawChams(dl, mem, p, vm, screen_w, screen_h);

    // ── PASS 2: Wireframe (before box so box outlines are on top) ─────────────
    for (const auto& p : players)
        DrawWireframe(dl, mem, p, vm, screen_w, screen_h);

    // ── PASS 3: All other ESP per player ──────────────────────────────────────
    for (const auto& p : players) {
        Vector2 feet_s, head_s;
        if (!WorldToScreen(p.origin, feet_s, vm, screen_w, screen_h)) continue;
        if (!WorldToScreen(p.head, head_s, vm, screen_w, screen_h)) continue;

        const float box_h = feet_s.y - head_s.y;
        if (box_h < 8.f) continue;
        if (p.origin.x == 0.f && p.origin.y == 0.f && p.origin.z == 0.f) continue;

        // Bone AABB for box sizing
        static constexpr int k_box_bones[] = { 0,1,2,3,4,5,6,8,13,22,23,24,25,26,27 };
        float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
        int bone_hits = 0;
        for (const int b : k_box_bones) {
            Vector3 w{};
            if (!ReadBone(mem, p.pawn, b, w)) continue;
            Vector2 ss{};
            if (!WorldToScreen(w, ss, vm, screen_w, screen_h)) continue;
            min_x = std::min(min_x, ss.x); max_x = std::max(max_x, ss.x);
            min_y = std::min(min_y, ss.y); max_y = std::max(max_y, ss.y);
            ++bone_hits;
        }

        float box_x, box_y, box_w, draw_h;
        if (bone_hits >= 6 && max_x > min_x && max_y > min_y) {
            const float rh = max_y - min_y;
            min_x -= std::clamp(rh * 0.045f, 1.5f, 12.f);
            max_x += std::clamp(rh * 0.045f, 1.5f, 12.f);
            min_y -= std::clamp(rh * 0.020f, 0.5f, 6.f);
            max_y += std::clamp(rh * 0.040f, 1.0f, 10.f);
            const float bh = max_y - min_y;
            const float cw = std::clamp(max_x - min_x, bh * 0.26f, bh * 0.56f);
            const float ccx = (min_x + max_x) * 0.5f;
            min_x = ccx - cw * 0.5f; max_x = ccx + cw * 0.5f;
            box_x = min_x; box_y = min_y;
            box_w = max_x - min_x; draw_h = max_y - min_y;
        }
        else {
            const float ccx = (head_s.x + feet_s.x) * 0.5f;
            const float fw = std::clamp(box_h * 0.40f, 20.f, 120.f);
            const float pad = box_h * 0.05f;
            box_x = ccx - fw * 0.5f; box_y = head_s.y - pad;
            box_w = fw; draw_h = box_h + pad;
        }

        if (!std::isfinite(box_w) || !std::isfinite(draw_h) ||
            box_w < 4.f || draw_h < 8.f) continue;

        const float cx = box_x + box_w * 0.5f;

        const bool is_low_hp = g_Config.esp.low_hp_alert
            && (p.health <= g_Config.esp.low_hp_threshold);
        const draw::rgba& base_col = p.visible
            ? g_Config.esp.box_color : g_Config.esp.occluded_color;

        // Health gradient: interpolate box color green→red as HP drops.
        // Computed inline; the lambda just avoids a helper function.
        draw::rgba gradient_col = base_col;
        if (g_Config.esp.health_gradient_box && !is_low_hp) {
            const float t = 1.f - std::clamp(p.health / 100.f, 0.f, 1.f);
            // green  (80,210,120) → yellow (255,200,60) → red (220,70,70)
            if (t < 0.5f) {
                const float u = t * 2.f;
                gradient_col = {
                    uint8_t(80 + u * (255 - 80)),
                    uint8_t(210 + u * (200 - 210)),
                    uint8_t(120 + u * (60 - 120)),
                    base_col.a };
            }
            else {
                const float u = (t - 0.5f) * 2.f;
                gradient_col = {
                    uint8_t(255 + u * (220 - 255)),
                    uint8_t(200 + u * (70 - 200)),
                    uint8_t(60 + u * (70 - 60)),
                    base_col.a };
            }
        }
        const draw::rgba& col = is_low_hp ? g_Config.esp.low_hp_color
            : (g_Config.esp.health_gradient_box ? gradient_col : base_col);

        // Snap lines
        if (g_Config.esp.snap_lines)
            dl->AddLine(ImVec2(screen_cx, screen_cy), ImVec2(cx, box_y + draw_h),
                ToImU32(g_Config.esp.snap_color), 1.f);
        // Tracers
        if (g_Config.esp.tracers)
            dl->AddLine(ImVec2(screen_cx, 0.f), ImVec2(cx, box_y),
                ToImU32(g_Config.esp.tracer_color), 1.f);

        // 3D box
        if (g_Config.esp.box_3d)
            Draw3DBox(dl, p, vm, screen_w, screen_h, col);

        // Box outline (dark halo improves readability over complex backgrounds)
        if (g_Config.esp.box_outline && g_Config.esp.box) {
            const draw::rgba out{ 0,0,0,145 };
            draw::rect(box_x - 1.f, box_y - 1.f, box_w + 2.f, draw_h + 2.f, out, 2.f);
            draw::rect(box_x + 1.f, box_y + 1.f, box_w - 2.f, draw_h - 2.f, out, 1.f);
        }

        // 2D box
        if (g_Config.esp.box) {
            if (g_Config.esp.box_fill)
                DrawGradientFill(dl, box_x, box_y, box_w, draw_h, col);
            if (g_Config.esp.corner_box)
                DrawCornerBox(dl, box_x, box_y, box_w, draw_h, col);
            else
                DrawBox(box_x, box_y, box_w, draw_h, col, false);
        }

        // Head dot
        if (g_Config.esp.head_dot) {
            Vector2 hs;
            if (WorldToScreen(p.head, hs, vm, screen_w, screen_h))
                dl->AddCircleFilled(ImVec2(hs.x, hs.y), 3.f,
                    ToImU32(g_Config.esp.head_dot_color), 8);
        }

        // Health bar (left)
        if (g_Config.esp.health) {
            DrawBar(box_x, box_y, draw_h, p.health, 100, g_Config.esp.health_color, false);
            if (g_Config.esp.health_text) {
                char hbuf[8]; snprintf(hbuf, sizeof(hbuf), "%d", p.health);
                auto [tw, th] = draw::measure_text(hbuf);
                draw::text<draw::tstyles::outlined>(
                    box_x - 7.f - tw, box_y + draw_h * 0.5f - th * 0.5f,
                    hbuf, g_Config.esp.health_color);
            }
        }

        // Armor bar (right)
        if (g_Config.esp.armor_bar && p.armor > 0)
            DrawBar(box_x + box_w, box_y, draw_h, p.armor, 100, g_Config.esp.armor_color, true);

        // Flash bar
        if (g_Config.esp.flash_bar && p.flash_alpha > 10.f) {
            const float frac = std::clamp(p.flash_alpha / 255.f, 0.f, 1.f);
            dl->AddRectFilled(ImVec2(box_x, box_y - 4.f),
                ImVec2(box_x + box_w * frac, box_y - 2.f), IM_COL32(255, 240, 180, 230));
        }

        // Regular skeleton
        if (g_Config.esp.skeleton) {
            const ImU32 sc = ToImU32(g_Config.esp.skeleton_color);
            for (const auto& pair : SKELETON) {
                Vector3 b1, b2;
                if (!ReadBone(mem, p.pawn, pair[0], b1)) continue;
                if (!ReadBone(mem, p.pawn, pair[1], b2)) continue;
                const float sl = b1.Distance(b2);
                if (!std::isfinite(sl) || sl < 0.5f || sl>70.f) continue;
                Vector2 s1, s2;
                if (!WorldToScreen(b1, s1, vm, screen_w, screen_h)) continue;
                if (!WorldToScreen(b2, s2, vm, screen_w, screen_h)) continue;
                dl->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), sc, 1.f);
            }
            if (g_Config.esp.joints) {
                static constexpr int k_joints[] = { 0,4,5,6,9,13,22,25 };
                const ImU32 jc = IM_COL32(
                    g_Config.esp.skeleton_color.r,
                    g_Config.esp.skeleton_color.g,
                    g_Config.esp.skeleton_color.b, 220);
                for (const int b : k_joints) {
                    Vector3 bw{};
                    if (!ReadBone(mem, p.pawn, b, bw)) continue;
                    Vector2 bs{};
                    if (!WorldToScreen(bw, bs, vm, screen_w, screen_h)) continue;
                    dl->AddCircleFilled(ImVec2(bs.x, bs.y), 2.5f, jc, 8);
                }
            }
        }

        // Name
        float label_y = box_y - 14.f;
        if (g_Config.esp.name) { DrawLabel(cx, label_y, p.name, col); label_y -= 13.f; }

        // ── Reload tracker indicator ──────────────────────────────────────────
        // Rendered just below the name so it's always visible.
        if (g_Config.esp.reload_tracker && p.is_reloading) {
            // Animated pulse using sin so the text fades in/out
            const float t = (float)(GetTickCount64() % 800) / 800.f;
            const int   alpha = (int)(180.f + 75.f * sinf(t * 6.28318f));
            DrawLabel(cx, label_y, "\xe2\x86\xbb RELOADING",
                draw::rgba{ 255, 200, 60, (uint8_t)alpha });
            label_y -= 13.f;

            // Small progress bar showing elapsed reload time (capped at 5s)
            const auto it = s_reload_map.find(p.pawn);
            if (it != s_reload_map.end()) {
                const float elapsed = std::min(5000.f,
                    (float)(GetTickCount64() - it->second.reload_start));
                const float frac = elapsed / 5000.f;
                const float bar_w2 = box_w * 0.7f;
                const float bx2 = cx - bar_w2 * 0.5f;
                dl->AddRectFilled(ImVec2(bx2, label_y + 2.f),
                    ImVec2(bx2 + bar_w2, label_y + 5.f),
                    IM_COL32(40, 40, 50, 200), 2.f);
                dl->AddRectFilled(ImVec2(bx2, label_y + 2.f),
                    ImVec2(bx2 + bar_w2 * frac, label_y + 5.f),
                    IM_COL32(255, 200, 60, 220), 2.f);
                label_y -= 8.f;
            }
        }

        // Labels below box
        float foot_y = box_y + draw_h + 2.f;
        if (g_Config.esp.distance) {
            char buf[16]; sprintf_s(buf, "%.0fm", p.distance / 100.f);
            auto [dw, dh] = draw::measure_text(buf);
            draw::text<draw::tstyles::outlined>(cx - dw * 0.5f, foot_y, buf,
                draw::rgba(185, 190, 200, 220));
            foot_y += 13.f;
        }
        if (g_Config.esp.weapon_esp && !p.weapon.empty()) {
            DrawLabel(cx, foot_y, p.weapon, draw::rgba(220, 200, 140, 220)); foot_y += 13.f;
        }
        if (g_Config.esp.ammo_esp && (p.clip > 0 || p.reserve > 0)) {
            char abuf[16]; snprintf(abuf, sizeof(abuf), "%d / %d", p.clip, p.reserve);
            DrawLabel(cx, foot_y, abuf, draw::rgba(200, 200, 220, 200)); foot_y += 13.f;
        }
        if (g_Config.esp.ping_esp && p.ping > 0) {
            char pbuf[16]; snprintf(pbuf, sizeof(pbuf), "%dms", p.ping);
            const draw::rgba pc = p.ping < 60 ? draw::rgba{ 80,210,120,200 }
            : p.ping < 120 ? draw::rgba{ 255,200,60,200 } : draw::rgba{ 255,80,80,200 };
            DrawLabel(cx, foot_y, pbuf, pc); foot_y += 13.f;
        }
        if (g_Config.esp.flags_esp) {
            std::string flags;
            if (p.has_defuser) flags += "[D] ";
            if (p.has_helmet)  flags += "[H] ";
            if (p.is_scoped)   flags += "[S]";
            if (!flags.empty()) {
                while (!flags.empty() && flags.back() == ' ') flags.pop_back();
                DrawLabel(cx, foot_y, flags, draw::rgba(200, 220, 255, 200));
                foot_y += 13.f;
            }
        }

        // Hit chance — novel feature
        if (g_Config.esp.hit_chance) {
            const float chance = CalcHitChance(p);
            char cbuf[12]; snprintf(cbuf, sizeof(cbuf), "%.0f%%", chance);
            const draw::rgba cc = chance > 70.f
                ? draw::rgba{ 80,210,120,210 }
                : chance > 40.f ? draw::rgba{ 220,190,60,210 }
            : draw::rgba{ 220,70,70,210 };
            DrawLabel(cx, foot_y, cbuf, cc);
        }
    }

    // ── Bomb timer ────────────────────────────────────────────────────────────
    if (g_Config.esp.bomb_timer) {
        const uintptr_t c4l = mem.Read<uintptr_t>(client + offsets::dwPlantedC4);
        if (c4l) {
            const uintptr_t c4 = mem.Read<uintptr_t>(c4l);
            if (c4) {
                const bool  tick = mem.Read<bool>(c4 + schemas::m_bBombTicking);
                const bool  exp = mem.Read<bool>(c4 + schemas::m_bHasExploded);
                const bool  def = mem.Read<bool>(c4 + schemas::m_bBombDefused);
                const bool  defg = mem.Read<bool>(c4 + schemas::m_bBeingDefused);
                const int   site = mem.Read<int>(c4 + schemas::m_nBombSite);
                const float blow = mem.Read<float>(c4 + schemas::m_flC4Blow);
                const float deft = mem.Read<float>(c4 + schemas::m_flDefuseCountDown);
                const float curt = mem.Read<float>(client + offsets::dwGlobalVars + 0x34);
                if (tick && !exp && !def && blow > 0.f && curt > 0.f) {
                    const float rem = std::max(0.f, blow - curt);
                    const ImU32 tc = rem < 5.f ? IM_COL32(255, 80, 80, 255)
                        : rem < 15.f ? IM_COL32(255, 220, 60, 255) : IM_COL32(220, 220, 220, 255);
                    const float px = screen_w * 0.5f - 80.f, py = (float)screen_h - 108.f;
                    dl->AddRectFilled(ImVec2(px - 6, py - 6), ImVec2(px + 166, py + 52),
                        IM_COL32(8, 10, 16, 220), 5.f);
                    dl->AddRect(ImVec2(px - 6, py - 6), ImVec2(px + 166, py + 52),
                        IM_COL32(0, 180, 140, 200), 5.f, 0, 1.5f);
                    const char* ss = (site == 0) ? "BOMB  [A]" : "BOMB  [B]";
                    dl->AddText(ImVec2(px, py), IM_COL32(200, 210, 225, 255), ss);
                    char buf[32]; snprintf(buf, sizeof(buf), "%.1f s", rem);
                    dl->AddText(ImGui::GetFont(), 24.f, ImVec2(px, py + 20.f), tc, buf);
                    if (defg && deft > curt) {
                        const float df = std::clamp((deft - curt) / 10.f, 0.f, 1.f);
                        dl->AddRectFilled(ImVec2(px, py + 46.f),
                            ImVec2(px + 160.f * (1.f - df), py + 50.f),
                            IM_COL32(0, 200, 140, 220));
                        dl->AddText(ImVec2(px, py + 50.f),
                            IM_COL32(0, 200, 140, 255), "DEFUSING");
                    }
                }
            }
        }
    }
}