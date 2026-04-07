#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "imgui.h"
#include "color.h" 

namespace draw {
    using font = ImFont;
    enum class draw_layer : int {
        foreground = 0,
        background
    };

    namespace tstyles {
        struct outlined {};
    }

    bool initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void shutdown();
    void begin_frame();
    void end_frame();
    void set_draw_layer(draw_layer layer) noexcept;
    draw_layer get_draw_layer() noexcept;

    void set_display_size(int width, int height) noexcept;
    std::pair<int, int> get_display_size() noexcept;
    float get_delta_time() noexcept;

    font* add_font_from_file(std::string_view filepath, float size_pixels);
    void push_font(font* fnt);
    void pop_font();
    std::pair<float, float> measure_text(std::string_view text, const font* fnt = nullptr);

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> load_texture_from_memory(std::span<const std::byte> data, int* out_width, int* out_height);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> load_texture_from_rgba(std::span<const std::uint8_t> rgba, int width, int height);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> load_texture_from_file(std::string_view filepath, int* out_width, int* out_height);

    void line(float x0, float y0, float x1, float y1, rgba color, float thickness = 1.0f);
    void rect(float x, float y, float w, float h, rgba color, float thickness = 1.0f);
    void rect_cornered(float x, float y, float w, float h, rgba color, float corner_length, float thickness = 1.0f);
    void rect_filled(float x, float y, float w, float h, rgba color);
    void rect_filled_multi_color(float x, float y, float w, float h, rgba color_tl, rgba color_tr, rgba color_br, rgba color_bl);
    void rect_textured(float x, float y, float w, float h, ID3D11ShaderResourceView* tex, float u0, float v0, float u1, float v1, rgba color);
    void convex_poly_filled(std::span<const float> points, rgba color);
    void polyline(std::span<const float> points, rgba color, bool closed, float thickness = 1.0f);
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, rgba color, float thickness = 1.0f);
    void triangle_filled(float x0, float y0, float x1, float y1, float x2, float y2, rgba color);
    void circle(float x, float y, float radius, rgba color, int segments = 0, float thickness = 1.0f);
    void circle_filled(float x, float y, float radius, rgba color, int segments = 0);
    void arc(float x, float y, float radius, float start_angle, float end_angle, rgba color, int segments = 0, float thickness = 1.0f);
    void arc_filled(float x, float y, float radius, float start_angle, float end_angle, rgba color, int segments = 0);

    template <typename Style = void>
    void text(float x, float y, std::string_view value, rgba color, const font* fnt = nullptr);
}

namespace ui {
    struct rect {
        float x{ 0.0f };
        float y{ 0.0f };
        float w{ 0.0f };
        float h{ 0.0f };
    };

    struct style {
        draw::rgba accent{ 165, 52, 52, 255 };
        draw::rgba window_bg{ 14, 14, 14, 255 };
        draw::rgba window_border{ 34, 34, 34, 255 };
        draw::rgba nested_bg{ 12, 12, 12, 255 };
        draw::rgba nested_border{ 28, 28, 28, 255 };
        draw::rgba group_box_bg{ 16, 16, 16, 255 };
        draw::rgba group_box_border{ 30, 30, 30, 255 };
        draw::rgba button_bg{ 22, 22, 22, 255 };
        draw::rgba button_hovered{ 28, 28, 28, 255 };
        draw::rgba button_active{ 18, 18, 18, 255 };
        draw::rgba text{ 220, 220, 224, 255 };
        float window_padding_x{ 10.0f };
        float window_padding_y{ 10.0f };
        float item_spacing_y{ 6.0f };
    };

    enum class style_color {
        button_bg,
        button_hovered,
        button_active,
        text
    };

    enum class style_var {
        window_padding_x,
        window_padding_y
    };

    namespace detail {
        struct window_state {
            rect bounds{};
            rect last_item{};
            float cursor_y{ 0.0f };
            float line_height{ 0.0f };
        };

        window_state* get_current_window();
        bool mouse_hovered(const rect& r);
        bool mouse_clicked();
        float mouse_x();
        float mouse_y();
        bool overlay_blocking_input();
    }

    bool initialize(HWND hwnd);
    void shutdown();
    void begin();
    void end();
    bool process_wndproc_message(UINT msg, WPARAM w_param, LPARAM l_param);
    bool hit_test_active_ui(float x, float y);

    style& get_style();
    draw::rgba get_accent_color();
    draw::rgba lighten(const draw::rgba& color, float factor);
    draw::rgba lerp(const draw::rgba& a, const draw::rgba& b, float t);

    bool begin_window(const char* title, float& x, float& y, float& w, float& h, bool movable, float min_w, float min_h, bool resizable = false);
    void end_window();
    bool begin_nested_window(const char* id, float width, float height);
    void end_nested_window();
    bool begin_group_box(const char* title, float width);
    void end_group_box();

    std::pair<float, float> get_content_region_avail();
    void set_cursor_pos(float x, float y);
    void same_line();

    void text(const char* value);
    bool button(const char* label, float width = 0.0f, float height = 0.0f);
    bool checkbox(const char* label, bool& value);
    bool slider_int(const char* label, int& value, int min_value, int max_value);
    bool slider_float(const char* label, float& value, float min_value, float max_value, const char* format = "%.3f");
    bool combo(const char* label, int& value, const char* const* items, int count);
    bool multicombo(const char* label, bool* values, const char* const* items, int count);
    bool color_picker(const char* label, draw::rgba& color, float width = 0.0f, bool show_alpha = true);
    bool text_input(const char* label, std::string& value, std::size_t max_len, const char* hint = nullptr);
    bool text_input(const char* label, char* value, std::size_t max_len, const char* hint = nullptr);
    bool keybind(const char* label, int& value);

    void push_style_color(style_color idx, const draw::rgba& color);
    void pop_style_color(int count = 1);
    void push_style_var(style_var idx, float value);
    void pop_style_var(int count = 1);
}

template <typename Style>
void draw::text(float x, float y, std::string_view value, rgba color, const font* fnt) {
    if constexpr (std::is_same_v<Style, tstyles::outlined>) {
        const rgba shadow{ 16, 16, 16, color.a }; // NOT pure black: (0,0,0) is the LWA_COLORKEY transparent color — 16 gives better safety margin than 8
        draw::text(x - 1.0f, y, value, shadow, fnt);
        draw::text(x + 1.0f, y, value, shadow, fnt);
        draw::text(x, y - 1.0f, value, shadow, fnt);
        draw::text(x, y + 1.0f, value, shadow, fnt);
    }

    ImDrawList* draw_list = (draw::get_draw_layer() == draw::draw_layer::background)
        ? ImGui::GetBackgroundDrawList()
        : ImGui::GetForegroundDrawList();
    if (!draw_list || value.empty()) {
        return;
    }
    draw_list->AddText(
        const_cast<ImFont*>(fnt ? fnt : ImGui::GetFont()),
        fnt ? fnt->LegacySize : ImGui::GetFontSize(),
        ImVec2(x, y),
        IM_COL32(color.r, color.g, color.b, color.a),
        value.data(),
        value.data() + value.size());
}
