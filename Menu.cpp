// ============================================================================
//  Wicked.Services — Menu.cpp  [Phantom UI v2]
//  Violet theme. Custom-drawn sliders (zero color bleed). Toggle switches.
//  All text rendered via explicit AddText/PushStyleColor — never ambient.
// ============================================================================
#include "menu.h"
#include <windows.h>
#include <string>
#include <cmath>
#include <algorithm>

// ── Palette ───────────────────────────────────────────────────────────────────
namespace P {
    static const ImVec4 Bg0{ 0.028f,0.028f,0.042f,1.f }; // #070711 deepest
    static const ImVec4 Bg1{ 0.048f,0.048f,0.072f,1.f }; // #0C0C12 window
    static const ImVec4 Bg2{ 0.068f,0.068f,0.102f,1.f }; // #11111A panel
    static const ImVec4 Bg3{ 0.095f,0.095f,0.138f,1.f }; // #181823 hover
    static const ImVec4 Bg4{ 0.122f,0.122f,0.175f,1.f }; // #1F1F2C active
    static const ImVec4 Ac{ 0.478f,0.400f,0.920f,1.f }; // #7A66EB violet
    static const ImVec4 AcHov{ 0.578f,0.500f,0.960f,1.f }; // #9480F5 hover
    static const ImVec4 AcDim{ 0.220f,0.185f,0.425f,1.f }; // #382F6C dim
    static const ImVec4 Txt{ 0.875f,0.875f,0.925f,1.f }; // #DFDFE8 primary
    static const ImVec4 TxtMd{ 0.550f,0.550f,0.640f,1.f }; // #8C8CA3 mid
    static const ImVec4 TxtDm{ 0.280f,0.280f,0.360f,1.f }; // #47475C dim
    static const ImVec4 Bdr{ 0.098f,0.098f,0.150f,1.f }; // #191926
    static const ImVec4 Ok{ 0.275f,0.820f,0.490f,1.f }; // green
    static const ImVec4 Err{ 0.870f,0.270f,0.310f,1.f }; // red

    static ImU32 U(const ImVec4& v) {
        return IM_COL32(int(v.x * 255), int(v.y * 255), int(v.z * 255), int(v.w * 255));
    }
    static ImU32 UA(const ImVec4& v, float a) {
        return IM_COL32(int(v.x * 255), int(v.y * 255), int(v.z * 255), int(a * 255));
    }
    // Lerp two colours for gradient effects
    static ImU32 Lerp(const ImVec4& a, const ImVec4& b, float t) {
        return IM_COL32(
            int((a.x + (b.x - a.x) * t) * 255),
            int((a.y + (b.y - a.y) * t) * 255),
            int((a.z + (b.z - a.z) * t) * 255),
            int((a.w + (b.w - a.w) * t) * 255));
    }
}

// ── Theme ─────────────────────────────────────────────────────────────────────
static constexpr int kCPush = 25, kVPush = 4;
static void PushTheme() {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, P::Bg1);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, P::Bg1); // match WindowBg — no bleed rectangles
    ImGui::PushStyleColor(ImGuiCol_PopupBg, P::Bg0);
    ImGui::PushStyleColor(ImGuiCol_Border, P::Bdr);
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, P::Bg2);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, P::Bg3);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, P::Bg4);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, P::Bg0);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, P::Bg0);
    ImGui::PushStyleColor(ImGuiCol_Text, P::Txt);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, P::TxtDm);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, P::Ac);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, P::AcHov);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, P::Ac);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(P::Ac.x, P::Ac.y, P::Ac.z, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, P::AcDim);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, P::Ac);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, P::Bg1);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, P::AcDim);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, P::Ac);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, P::AcHov);
    // Globally suppress default ImGui blue button bleed — all explicit
    // call-sites push their own Button colors on top of these safe defaults.
    ImGui::PushStyleColor(ImGuiCol_Button, P::Bg3);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, P::Bg4);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, P::AcDim);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 6.f);
}
static void PopTheme() {
    ImGui::PopStyleColor(kCPush);
    ImGui::PopStyleVar(kVPush);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Custom widgets — all text via explicit AddText/PushStyleColor. Zero bleed.
// ─────────────────────────────────────────────────────────────────────────────

// Toggle switch
static bool CCheck(const char* label, bool& v) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float tw = 30.f, th = 16.f, r = th * 0.5f;
    const float fs = ImGui::GetFontSize();
    const float rh = th > fs ? th : fs;
    const float fullW = tw + 8.f + ImGui::CalcTextSize(label).x;
    ImGui::InvisibleButton(label, ImVec2(fullW, rh + 3.f));
    const bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) v = !v;
    const float cy = p.y + (rh - th) * 0.5f + 1.f;
    const ImU32 trkCol = v ? (hov ? P::U(P::AcHov) : P::U(P::Ac)) : (hov ? P::U(P::Bg4) : P::U(P::Bg3));
    dl->AddRectFilled(ImVec2(p.x, cy), ImVec2(p.x + tw, cy + th), trkCol, r);
    dl->AddRect(ImVec2(p.x, cy), ImVec2(p.x + tw, cy + th),
        v ? P::UA(P::Ac, 0.55f) : P::U(P::Bdr), r, 0, 1.f);
    const float tx = v ? (p.x + tw - r + 1.f) : (p.x + r - 1.f);
    dl->AddCircleFilled(ImVec2(tx, cy + r), r - 2.5f,
        v ? IM_COL32(255, 255, 255, 242) : P::U(P::TxtMd), 14);
    dl->AddText(ImVec2(p.x + tw + 8.f, p.y + (rh - fs) * 0.5f + 1.f),
        v ? P::U(P::Txt) : P::U(P::TxtMd), label);
    return v;
}

// ── Custom slider — fully drawn, zero ImGui color inheritance ─────────────────
// Design: thin rounded track (4px), gradient fill left→grab, circle grab with
// white center pip, value text centred on track.  Grows slightly on hover/drag.
// ─────────────────────────────────────────────────────────────────────────────
static void DrawSliderTrack(const char* label, float frac, bool act, bool hov,
    const char* val_text, bool modified)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const float bar_h = 26.f, track_h = 4.f, grab_r = 7.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();

    // Label (dim, above track)
    dl->AddText(ImVec2(p.x, p.y), P::U(P::TxtDm), label);
    const float label_h = ImGui::GetFontSize() + 3.f;
    const ImVec2 tp = ImVec2(p.x, p.y + label_h); // track origin

    // Track groove
    const float ty = tp.y + (bar_h - track_h) * 0.5f;
    const float track_w = w - grab_r * 2.f;
    const float grab_x = tp.x + grab_r + frac * track_w;

    // Groove background
    dl->AddRectFilled(ImVec2(tp.x + grab_r, ty), ImVec2(tp.x + w - grab_r, ty + track_h),
        P::U(P::Bg4), track_h * 0.5f);

    // Gradient fill: violet left → slightly brighter at grab
    if (frac > 0.001f) {
        const ImU32 c0 = P::UA(P::Ac, 0.55f);
        const ImU32 c1 = P::U(P::Ac);
        dl->AddRectFilledMultiColor(
            ImVec2(tp.x + grab_r, ty), ImVec2(grab_x, ty + track_h),
            c0, c1, c1, c0);
    }

    // Grab circle
    const float gr = act ? grab_r : (hov ? grab_r - 0.5f : grab_r - 1.5f);
    const float gy = tp.y + bar_h * 0.5f;
    // Shadow
    dl->AddCircleFilled(ImVec2(grab_x, gy + 1.f), gr + 0.5f, IM_COL32(0, 0, 0, 80), 14);
    // Fill
    dl->AddCircleFilled(ImVec2(grab_x, gy), gr,
        act ? P::U(P::AcHov) : P::U(P::Ac), 14);
    // Inner ring (slightly darker rim)
    dl->AddCircle(ImVec2(grab_x, gy), gr, P::UA(P::Bg0, 0.35f), 14, 1.f);
    // White pip
    dl->AddCircleFilled(ImVec2(grab_x, gy), gr * 0.32f, IM_COL32(255, 255, 255, 220), 8);

    // Value text — centred on the track area
    if (val_text && val_text[0]) {
        const ImVec2 ts = ImGui::CalcTextSize(val_text);
        // Show in full Txt colour when changed recently, else TxtMd
        dl->AddText(ImVec2(tp.x + w * 0.5f - ts.x * 0.5f, ty + (track_h - ts.y) * 0.5f - 1.f),
            modified ? P::U(P::Txt) : P::U(P::TxtMd), val_text);
    }

    // Advance cursor past label + bar
    ImGui::Dummy(ImVec2(w, label_h + bar_h + 2.f));
}

static bool CSliderI(const char* label, int& v, int lo, int hi, const char* fmt = "%d") {
    ImGui::PushID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float bar_h = 26.f, label_h = ImGui::GetFontSize() + 3.f;
    const float track_w = w - 14.f; // grab_r*2 = 14
    const float grab_r = 7.f;

    // Interaction region covers the full bar area
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + label_h);
    ImGui::InvisibleButton("##s", ImVec2(w, bar_h));
    bool changed = false;
    const bool act = ImGui::IsItemActive();
    const bool hov = ImGui::IsItemHovered();
    if (act) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float frac = std::clamp((mx - p.x - grab_r) / track_w, 0.f, 1.f);
        const int nv = lo + (int)(frac * (float)(hi - lo) + 0.5f);
        if (nv != v) { v = nv; changed = true; }
    }
    ImGui::SetCursorScreenPos(p); // rewind to draw over the invisible button

    const float frac = std::clamp((float)(v - lo) / (float)(hi - lo), 0.f, 1.f);
    char val[32]; snprintf(val, sizeof(val), fmt, v);
    DrawSliderTrack(label, frac, act, hov, val, changed);

    ImGui::PopID();
    return changed;
}

static bool CSliderF(const char* label, float& v, float lo, float hi, const char* fmt = "%.2f") {
    ImGui::PushID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float bar_h = 26.f, label_h = ImGui::GetFontSize() + 3.f;
    const float track_w = w - 14.f;
    const float grab_r = 7.f;

    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + label_h);
    ImGui::InvisibleButton("##s", ImVec2(w, bar_h));
    bool changed = false;
    const bool act = ImGui::IsItemActive();
    const bool hov = ImGui::IsItemHovered();
    if (act) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float frac = std::clamp((mx - p.x - grab_r) / track_w, 0.f, 1.f);
        const float nv = lo + frac * (hi - lo);
        if (nv != v) { v = nv; changed = true; }
    }
    ImGui::SetCursorScreenPos(p);

    const float frac = std::clamp((v - lo) / (hi - lo), 0.f, 1.f);
    char val[32]; snprintf(val, sizeof(val), fmt, v);
    DrawSliderTrack(label, frac, act, hov, val, changed);

    ImGui::PopID();
    return changed;
}

// Combo (uses ImGui internally but pushes all colors explicitly)
static bool CCombo(const char* label, int& v, const char* const* items, int n) {
    ImGui::PushID(label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fs = ImGui::GetFontSize();
    dl->AddText(p, P::U(P::TxtDm), label);
    ImGui::Dummy(ImVec2(0.f, fs + 3.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, P::Bg3);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, P::Bg4);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, P::Bg0);
    ImGui::PushStyleColor(ImGuiCol_Text, P::Txt);
    ImGui::PushStyleColor(ImGuiCol_Header, P::AcDim);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, P::Ac);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool r = ImGui::Combo("##v", &v, items, n);
    ImGui::PopStyleColor(6);
    ImGui::Dummy(ImVec2(0.f, 3.f));
    ImGui::PopID();
    return r;
}

// Color picker — swatch + explicit label text
static bool CColor(const char* label, draw::rgba& col) {
    ImGui::PushID(label);
    ImVec4 c(col.r / 255.f, col.g / 255.f, col.b / 255.f, col.a / 255.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
    bool r = ImGui::ColorEdit4("##c", &c.x,
        ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_AlphaPreviewHalf |
        ImGuiColorEditFlags_AlphaBar);
    ImGui::PopStyleVar();
    ImGui::SameLine(0.f, 8.f);
    ImGui::PushStyleColor(ImGuiCol_Text, P::TxtMd);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (r) {
        col.r = uint8_t(c.x * 255); col.g = uint8_t(c.y * 255);
        col.b = uint8_t(c.z * 255); col.a = uint8_t(c.w * 255);
    }
    ImGui::PopID();
    return r;
}

// Keybind
static bool CKeybind(const char* label, int& v) {
    ImGui::PushID(label);
    static int* s_cap = nullptr;
    const bool cap = (s_cap == &v);
    char buf[48];
    if (cap) {
        snprintf(buf, sizeof(buf), " press key... ");
        for (int k = 1; k < 256; ++k) {
            if (k == VK_LBUTTON || k == VK_RBUTTON) continue;
            if (GetAsyncKeyState(k) & 0x8001) {
                v = (k == VK_ESCAPE) ? 0 : k;
                s_cap = nullptr; break;
            }
        }
    }
    else if (v == 0) {
        snprintf(buf, sizeof(buf), " None ");
    }
    else {
        char nm[32]{};
        if (!GetKeyNameTextA((MapVirtualKeyA(v, MAPVK_VK_TO_VSC) << 16), nm, 32))
            snprintf(nm, 32, "0x%02X", v);
        snprintf(buf, sizeof(buf), " %s ", nm);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fs = ImGui::GetFontSize();
    dl->AddText(p, P::U(P::TxtDm), label);
    ImGui::Dummy(ImVec2(0.f, fs + 3.f));
    ImGui::PushStyleColor(ImGuiCol_Button, cap ? P::AcDim : P::Bg3);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cap ? P::Ac : P::Bg4);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, P::Ac);
    ImGui::PushStyleColor(ImGuiCol_Text, cap ? P::Txt : P::TxtMd);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
    if (ImGui::Button(buf, ImVec2(ImGui::GetContentRegionAvail().x, 24.f)))
        s_cap = cap ? nullptr : &v;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::PopID();
    return false;
}

// Section header: 2px accent bar + dim label + hairline rule
static void Hdr(const char* t) {
    ImGui::Dummy(ImVec2(0.f, 4.f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  fs = ImGui::GetFontSize();
    dl->AddRectFilled(ImVec2(p.x, p.y + 2.f), ImVec2(p.x + 2.f, p.y + fs - 1.f), P::U(P::Ac));
    dl->AddText(ImVec2(p.x + 8.f, p.y), P::U(P::TxtMd), t);
    ImGui::Dummy(ImVec2(0.f, fs + 1.f));
    const ImVec2 lp = ImGui::GetCursorScreenPos();
    dl->AddLine(lp, ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y), P::U(P::Bdr), 1.f);
    ImGui::Dummy(ImVec2(0.f, 6.f));
}

static void Note(const char* t) {
    ImGui::PushStyleColor(ImGuiCol_Text, P::TxtDm);
    ImGui::TextWrapped("  %s", t);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 2.f));
}

// ── Tabs ──────────────────────────────────────────────────────────────────────
static int         s_tab = 0;
static const char* kTabLabels[] = { "Aimbot","Visuals","Trigger","Misc","Config" };
static constexpr int kTabCount = 5;

static int         s_weapon_tab = 0;
static const char* kWeapTabs[] = { "Global","Rifles","Pistols","SMGs","Shotguns","Snipers" };
static constexpr int kWepCount = 6;

static void DrawWeaponTabs() {
    const float pw = ImGui::GetContentRegionAvail().x;
    const float tw = (pw - (kWepCount - 1) * 4.f) / (float)kWepCount;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
    for (int i = 0; i < kWepCount; ++i) {
        const bool act = (s_weapon_tab == i);
        ImGui::PushStyleColor(ImGuiCol_Button, act ? P::AcDim : P::Bg2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, act ? ImVec4(P::Ac.x, P::Ac.y, P::Ac.z, 0.30f) : P::Bg3);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, P::AcDim);
        ImGui::PushStyleColor(ImGuiCol_Text, act ? P::Ac : P::TxtDm);
        ImGui::PushStyleColor(ImGuiCol_Border, act ? P::Ac : P::Bdr);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, act ? 1.f : 0.f);
        if (ImGui::Button(kWeapTabs[i], ImVec2(tw, 24.f))) s_weapon_tab = i;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);
        if (i < kWepCount - 1) ImGui::SameLine(0.f, 4.f);
    }
    ImGui::PopStyleVar(2);
    ImGui::Dummy(ImVec2(0.f, 8.f));
}

// ── Status ────────────────────────────────────────────────────────────────────
static const char* s_status = nullptr;
static bool s_st_ok = false;
static ULONGLONG s_st_t = 0;
static void SetStatus(const char* m, bool ok) { s_status = m; s_st_ok = ok; s_st_t = GetTickCount64(); }

// ── Two-column layout ─────────────────────────────────────────────────────────
// s_col_h is captured BEFORE opening the left child so ColRight() uses the
// same full-panel height, not the near-zero remaining height inside ##cl.
static float s_col_w = 0.f;
static float s_col_h = 0.f;
static void ColBegin() {
    const float aw = ImGui::GetContentRegionAvail().x;
    s_col_h = ImGui::GetContentRegionAvail().y;
    s_col_w = (aw - 8.f) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
    ImGui::BeginChild("##cl", ImVec2(s_col_w, s_col_h), false);
    ImGui::PopStyleVar();
}
static void ColRight() {
    ImGui::EndChild();
    ImGui::SameLine(0.f, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
    ImGui::BeginChild("##cr", ImVec2(s_col_w, s_col_h), false);
    ImGui::PopStyleVar();
}
static void ColEnd() { ImGui::EndChild(); }

// ============================================================================
//  Menu::Draw
// ============================================================================
void Menu::Draw()
{
    if (!g_Config.menu_open) return;
    PushTheme();

    ImGui::SetNextWindowSize(ImVec2(720.f, 510.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(210.f, 110.f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::Begin("##W", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    const float ww = ImGui::GetWindowWidth();

    // Outer glow border
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const float  wh = ImGui::GetWindowHeight();
        dl->AddRect(wp, ImVec2(wp.x + ww, wp.y + wh), P::UA(P::Ac, 0.28f), 10.f, 0, 1.f);
    }

    // ── Header bar ────────────────────────────────────────────────────────────
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, P::Bg0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::BeginChild("##hdr", ImVec2(0.f, 38.f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 hp = ImGui::GetWindowPos();
        dl->AddRectFilled(ImVec2(hp.x, hp.y + 36.f), ImVec2(hp.x + ww, hp.y + 38.f), P::UA(P::Ac, 0.45f));
        // Diamond logo
        const float dmx = hp.x + 18.f, dmy = hp.y + 19.f, dms = 5.f;
        dl->AddQuadFilled(ImVec2(dmx, dmy - dms), ImVec2(dmx + dms, dmy),
            ImVec2(dmx, dmy + dms), ImVec2(dmx - dms, dmy), P::U(P::Ac));
        dl->AddQuadFilled(ImVec2(dmx, dmy - dms + 2.5f), ImVec2(dmx + dms - 2.5f, dmy),
            ImVec2(dmx, dmy + dms - 2.5f), ImVec2(dmx - dms + 2.5f, dmy), P::U(P::Bg0));
        ImGui::SetCursorPos(ImVec2(30.f, 11.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.f));
        ImGui::Text("Wicked");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosY(11.f);
        ImGui::PushStyleColor(ImGuiCol_Text, P::Ac);
        ImGui::Text(".Services");
        ImGui::PopStyleColor();
        ImGui::SetCursorPos(ImVec2(ww - 148.f, 12.f));
        ImGui::PushStyleColor(ImGuiCol_Text, P::TxtDm);
        ImGui::Text("INSERT  |  END exit");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Top tab bar ───────────────────────────────────────────────────────────
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, P::Bg0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 5.f));
        ImGui::BeginChild("##tabbar", ImVec2(0.f, 34.f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 tp = ImGui::GetWindowPos();
        const float  tbh = ImGui::GetWindowHeight();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.f, 4.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
        for (int i = 0; i < kTabCount; ++i) {
            const bool act = (s_tab == i);
            ImGui::PushStyleColor(ImGuiCol_Button, act ? P::AcDim : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, act ? P::AcDim : P::Bg3);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, P::AcDim);
            ImGui::PushStyleColor(ImGuiCol_Text, act ? P::Ac : P::TxtDm);
            ImGui::PushStyleColor(ImGuiCol_Border, act ? P::Ac : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, act ? 1.f : 0.f);
            if (ImGui::Button(kTabLabels[i])) s_tab = i;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(5);
            if (i < kTabCount - 1) ImGui::SameLine(0.f, 2.f);
        }
        ImGui::PopStyleVar(3);
        dl->AddLine(ImVec2(tp.x, tp.y + tbh - 1.f), ImVec2(tp.x + ww, tp.y + tbh - 1.f), P::U(P::Bdr), 1.f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ── Content ───────────────────────────────────────────────────────────────
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
    ImGui::BeginChild("##cp", ImVec2(0.f, 0.f), false);
    ImGui::PopStyleVar();

    // ================================================================  AIMBOT
    if (s_tab == 0) {
        DrawWeaponTabs();
        if (s_weapon_tab > 0) { Note("Settings from Global apply. Per-weapon overrides coming soon."); }
        else {
            ColBegin();
            Hdr("GENERAL");
            CCheck("Enable Aimbot", g_Config.aimbot.enabled);
            { bool eo = !g_Config.aimbot.teammates; if (CCheck("Enemy Only", eo)) g_Config.aimbot.teammates = !eo; }
            CCheck("Visible Only", g_Config.aimbot.visible_only);
            CCheck("Player Lock", g_Config.aimbot.player_lock);
            CCheck("Aim at Shoot", g_Config.aimbot.aim_at_shoot);
            CCheck("Only Scoped", g_Config.aimbot.only_scoped);
            ImGui::Dummy(ImVec2(0.f, 4.f));
            Hdr("HITBOX");
            {
                const char* b[] = { "Head","Neck","Chest","Pelvis","Legs" };
                CCombo("Target Bone", g_Config.aimbot.hitbox, b, 5);
            }
            CSliderI("Target Switch Delay", g_Config.aimbot.target_switch_delay, 0, 500, "%d ms");
            CSliderF("Prediction Ticks", g_Config.aimbot.prediction, 0.f, 3.f, "%.1f");
            ImGui::Dummy(ImVec2(0.f, 4.f));
            Hdr("KEYS");
            CKeybind("Primary Aim Key", g_Config.aimbot.key);
            CKeybind("Second Aim Key", g_Config.aimbot.key2);

            ColRight();
            Hdr("SPEED");
            CSliderF("Horizontal Speed", g_Config.aimbot.h_speed, 1.f, 100.f, "%.1f");
            CSliderF("Vertical Speed", g_Config.aimbot.v_speed, 1.f, 100.f, "%.1f");
            CSliderF("Hitscan Coeff", g_Config.aimbot.hitscan_coef, 0.1f, 3.f, "%.2f");
            ImGui::Dummy(ImVec2(0.f, 4.f));
            Hdr("RECOIL CONTROL");
            CCheck("Enable RCS", g_Config.aimbot.rcs);
            if (g_Config.aimbot.rcs)
                CSliderI("RCS Strength", g_Config.aimbot.rcs_strength, 0, 200);
            ImGui::Dummy(ImVec2(0.f, 4.f));
            Hdr("AUTO-FIRE");
            CCheck("Auto Fire", g_Config.aimbot.auto_fire);
            if (g_Config.aimbot.auto_fire)
                CSliderF("FOV Threshold", g_Config.aimbot.auto_fire_fov, 0.1f, 5.f, "%.1f deg");
            ImGui::Dummy(ImVec2(0.f, 4.f));
            Hdr("VISUAL");
            CCheck("Draw FOV Circle", g_Config.aimbot.draw_fov);
            if (g_Config.aimbot.draw_fov) {
                CSliderI("FOV Radius", g_Config.aimbot.fov, 1, 30);
                CColor("FOV Color", g_Config.aimbot.fov_color);
            }
            ColEnd();
        }
    }

    // ================================================================  VISUALS
    if (s_tab == 1) {
        ColBegin();
        Hdr("GENERAL");
        CCheck("Enable ESP", g_Config.esp.enabled);
        { bool eo = !g_Config.esp.teammates; if (CCheck("Enemy Only", eo)) g_Config.esp.teammates = !eo; }
        CCheck("Visible Only", g_Config.esp.visible_only);
        CCheck("Max Distance", g_Config.esp.use_max_dist);
        if (g_Config.esp.use_max_dist)
            CSliderF("Max Dist", g_Config.esp.max_distance, 200.f, 6000.f, "%.0f u");
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("BOX");
        CCheck("Bounding Box", g_Config.esp.box);
        if (g_Config.esp.box) {
            CCheck("  Corner Box", g_Config.esp.corner_box);
            CCheck("  3D Wireframe Box", g_Config.esp.box_3d);
            CCheck("  Box Outline", g_Config.esp.box_outline);
            CCheck("  Box Fill", g_Config.esp.box_fill);
            CCheck("  HP Gradient Color", g_Config.esp.health_gradient_box);
            CColor("  Box Color", g_Config.esp.box_color);
            CColor("  Occluded Color", g_Config.esp.occluded_color);
        }
        CCheck("Head Dot", g_Config.esp.head_dot);
        if (g_Config.esp.head_dot)
            CColor("  Head Color", g_Config.esp.head_dot_color);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("BARS");
        CCheck("Health Bar", g_Config.esp.health);
        if (g_Config.esp.health) {
            CCheck("  HP Number", g_Config.esp.health_text);
            CColor("  Health Color", g_Config.esp.health_color);
        }
        CCheck("Armor Bar", g_Config.esp.armor_bar);
        if (g_Config.esp.armor_bar)
            CColor("  Armor Color", g_Config.esp.armor_color);
        CCheck("Flash Indicator", g_Config.esp.flash_bar);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("ALERTS");
        CCheck("Low HP Alert", g_Config.esp.low_hp_alert);
        if (g_Config.esp.low_hp_alert) {
            CSliderI("  HP Threshold", g_Config.esp.low_hp_threshold, 1, 99);
            CColor("  Alert Color", g_Config.esp.low_hp_color);
        }

        ColRight();
        Hdr("SKELETON");
        CCheck("Skeleton", g_Config.esp.skeleton);
        if (g_Config.esp.skeleton) {
            CCheck("  Joints", g_Config.esp.joints);
            CColor("  Color", g_Config.esp.skeleton_color);
        }
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("LABELS");
        CCheck("Player Name", g_Config.esp.name);
        CCheck("Distance", g_Config.esp.distance);
        CCheck("Weapon Name", g_Config.esp.weapon_esp);
        CCheck("Ammo Counter", g_Config.esp.ammo_esp);
        CCheck("Ping", g_Config.esp.ping_esp);
        CCheck("Flags [D][H][S]", g_Config.esp.flags_esp);
        CCheck("Reload Tracker", g_Config.esp.reload_tracker);
        CCheck("Hit Chance %", g_Config.esp.hit_chance);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("SILHOUETTE");
        CCheck("Chams", g_Config.esp.chams);
        if (g_Config.esp.chams) {
            CCheck("  Through Walls", g_Config.esp.chams_occluded);
            CColor("  Visible Color", g_Config.esp.chams_visible_color);
            CColor("  Occluded Color", g_Config.esp.chams_occluded_color);
        }
        CCheck("Wireframe", g_Config.esp.wireframe_esp);
        if (g_Config.esp.wireframe_esp)
            CColor("  Wireframe Color", g_Config.esp.wireframe_color);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("LINES");
        CCheck("Tracers (top)", g_Config.esp.tracers);
        if (g_Config.esp.tracers)
            CColor("  Tracer Color", g_Config.esp.tracer_color);
        CCheck("Snap Lines (bottom)", g_Config.esp.snap_lines);
        if (g_Config.esp.snap_lines)
            CColor("  Snap Color", g_Config.esp.snap_color);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("MISC");
        CCheck("Bomb Timer", g_Config.esp.bomb_timer);
        ColEnd();
    }

    // ================================================================  TRIGGER
    if (s_tab == 2) {
        ColBegin();
        Hdr("TRIGGERBOT");
        CCheck("Enable", g_Config.triggerbot.enabled);
        CKeybind("Activation Key", g_Config.triggerbot.key);
        CSliderI("First Shot Delay", g_Config.triggerbot.delay, 0, 500, "%d ms");
        CSliderI("Shots Delay", g_Config.triggerbot.shots_delay, 10, 500, "%d ms");
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("FILTERS");
        CCheck("Visible Only", g_Config.triggerbot.visible_only);
        { bool eo = !g_Config.triggerbot.teammates; if (CCheck("Enemy Only", eo)) g_Config.triggerbot.teammates = !eo; }
        CCheck("Only Scoped (Sniper)", g_Config.triggerbot.only_scoped);

        ColRight();
        Hdr("HITBOX");
        {
            const char* b[] = { "Head","Neck","Chest","Pelvis","Legs" };
            CCombo("Target Bone", g_Config.triggerbot.hitbox, b, 5);
        }
        Note("Fires when crosshair overlaps an enemy hitbox.");
        Note("Use with a delay for legit play.");
        ColEnd();
    }

    // ================================================================  MISC
    if (s_tab == 3) {
        ColBegin();
        Hdr("MOVEMENT");
        CCheck("Bunny Hop", g_Config.bhop.enabled);
        CKeybind("  Jump Key", g_Config.bhop.key);
        CCheck("Auto Crouch (while firing)", g_Config.misc.auto_crouch);
        if (g_Config.misc.auto_crouch) {
            CKeybind("  Trigger Key", g_Config.misc.auto_crouch_key);
            Note("Crouches while the trigger key is held.");
        }
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("WEAPON");
        CCheck("No Recoil (Visual)", g_Config.misc.no_spread);
        CCheck("Auto Pistol", g_Config.misc.auto_pistol);
        Note("Auto Pistol: rapid-fires semi-autos.");
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("VISION");
        CCheck("Anti-Flash", g_Config.misc.no_flash);
        CCheck("Spectator List", g_Config.misc.spectator_list);
        CCheck("Grenade Timers", g_Config.misc.grenade_warning);
        CCheck("Footstep ESP", g_Config.misc.footstep_esp);
        if (g_Config.misc.footstep_esp) {
            CSliderF("  Min Speed", g_Config.misc.footstep_threshold, 20.f, 300.f, "%.0f u/s");
            CColor("  Footstep Color", g_Config.misc.footstep_color);
        }

        ColRight();
        Hdr("DISPLAY");
        CCheck("Custom Crosshair", g_Config.misc.crosshair);
        if (g_Config.misc.crosshair) {
            CSliderI("  Size", g_Config.misc.crosshair_size, 1, 20);
            CSliderI("  Gap", g_Config.misc.crosshair_gap, 0, 10);
            CSliderI("  Thickness", g_Config.misc.crosshair_thick, 1, 5);
            CCheck("  Center Dot", g_Config.misc.crosshair_dot);
            CColor("  Color", g_Config.misc.crosshair_color);
        }
        CCheck("Velocity Indicator", g_Config.misc.velocity_info);
        CCheck("Punch Dot", g_Config.misc.punch_dot);
        if (g_Config.misc.punch_dot)
            CColor("  Dot Color", g_Config.misc.punch_dot_color);
        CCheck("Round Timer", g_Config.misc.round_timer);
        CCheck("Clock", g_Config.misc.show_clock);
        CCheck("Local Player Info", g_Config.misc.local_info);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        Hdr("RADAR");
        CCheck("Radar", g_Config.misc.radar);
        if (g_Config.misc.radar) {
            CSliderF("  Size", g_Config.misc.radar_size, 60.f, 220.f, "%.0f px");
            CSliderF("  Range", g_Config.misc.radar_range, 500.f, 5000.f, "%.0f u");
            CCheck("  Rotate View", g_Config.misc.radar_rotate);
        }
        ColEnd();
    }

    // ================================================================  CONFIG
    if (s_tab == 4) {
        ColBegin();
        Hdr("SAVE / LOAD");
        static const char* kLabels[] = {
            "Slot 1 — Default","Slot 2 — Rage","Slot 3 — Legit",
            "Slot 4 — Custom A","Slot 5 — Custom B" };
        static const char* kNames[] = { "Default","Rage","Legit","CustomA","CustomB" };
        static int s_slot = 0;
        CCombo("Slot", s_slot, kLabels, 5);
        ImGui::Dummy(ImVec2(0.f, 8.f));
        ImGui::PushStyleColor(ImGuiCol_Button, P::AcDim);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, P::Ac);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, P::AcHov);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
        if (ImGui::Button("  Save Config  ", ImVec2(-1.f, 30.f)))
            SetStatus(g_Config.Save(s_slot, kNames[s_slot]) ? "Saved." : "Save failed.", true);
        ImGui::PopStyleVar(); ImGui::PopStyleColor(4);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        ImGui::PushStyleColor(ImGuiCol_Button, P::Bg3);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, P::Bg4);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, P::AcDim);
        ImGui::PushStyleColor(ImGuiCol_Text, P::Txt);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
        if (ImGui::Button("  Load Config  ", ImVec2(-1.f, 30.f))) {
            const bool ok = g_Config.Load(s_slot);
            SetStatus(ok ? "Loaded." : "No file or version mismatch.", ok);
        }
        ImGui::PopStyleVar(); ImGui::PopStyleColor(4);
        if (s_status && (GetTickCount64() - s_st_t) < 2500ULL) {
            ImGui::Dummy(ImVec2(0.f, 6.f));
            ImGui::PushStyleColor(ImGuiCol_Text, s_st_ok ? P::Ok : P::Err);
            ImGui::Text("  %s", s_status);
            ImGui::PopStyleColor();
        }
        else { s_status = nullptr; }

        ColRight();
        Hdr("DANGER ZONE");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.34f, 0.07f, 0.09f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.10f, 0.12f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.13f, 0.15f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.80f, 0.80f, 1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
        if (ImGui::Button("  Reset to Defaults  ", ImVec2(-1.f, 30.f))) {
            const bool mo = g_Config.menu_open;
            g_Config = Config{}; g_Config.menu_open = mo;
            SetStatus("Reset to defaults.", true);
        }
        ImGui::PopStyleVar(); ImGui::PopStyleColor(4);
        ImGui::Dummy(ImVec2(0.f, 12.f));
        Note("k_version = 12. Bump in config.h after");
        Note("any struct layout change.");
        Note("Old save files auto-reject cleanly.");
        ColEnd();
    }

    ImGui::EndChild(); // ##cp
    ImGui::End();
    PopTheme();
}