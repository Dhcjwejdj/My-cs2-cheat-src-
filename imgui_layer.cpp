#define IMGUI_DEFINE_MATH_OPERATORS
#define _CRT_SECURE_NO_WARNINGS
#include "imgui_layer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <objidl.h>
#include <wincodec.h>

#include "../imgui-master/backends/imgui_impl_dx11.h"
#include "../imgui-master/backends/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#include "../imgui-master/imgui_internal.h"

namespace {
    struct DrawContext {
        HWND hwnd{ nullptr };
        ID3D11Device* device{ nullptr };
        ID3D11DeviceContext* device_context{ nullptr };
        int display_width{ 0 };
        int display_height{ 0 };
    };

    struct GroupBoxState {
        ImDrawList* draw_list{ nullptr };
        ImVec2 start_screen{};
        float width{ 0.0f };
        bool split_active{ false };
    };

    struct WindowFrameState {
        ui::detail::window_state state{};
    };

    DrawContext g_draw_ctx{};
    ui::style g_style{};
    draw::draw_layer g_draw_layer{ draw::draw_layer::foreground };
    std::vector<WindowFrameState> g_window_stack{};
    std::vector<GroupBoxState> g_group_boxes{};
    RECT g_last_menu_bounds{ 0, 0, 0, 0 };
    bool g_menu_bounds_valid{ false };
    ImGuiID g_active_keybind_id{ 0 };
    ImGuiID g_keybind_wait_release_id{ 0 };
    bool g_initialized{ false };

    bool SameColor(const draw::rgba& a, const draw::rgba& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    bool SameUiStyle(const ui::style& a, const ui::style& b) {
        return SameColor(a.accent, b.accent)
            && SameColor(a.window_bg, b.window_bg)
            && SameColor(a.window_border, b.window_border)
            && SameColor(a.nested_bg, b.nested_bg)
            && SameColor(a.nested_border, b.nested_border)
            && SameColor(a.group_box_bg, b.group_box_bg)
            && SameColor(a.group_box_border, b.group_box_border)
            && SameColor(a.button_bg, b.button_bg)
            && SameColor(a.button_hovered, b.button_hovered)
            && SameColor(a.button_active, b.button_active)
            && SameColor(a.text, b.text)
            && a.window_padding_x == b.window_padding_x
            && a.window_padding_y == b.window_padding_y
            && a.item_spacing_y == b.item_spacing_y;
    }

    ImU32 ToImU32(const draw::rgba& color) {
        return IM_COL32(color.r, color.g, color.b, color.a);
    }

    ImVec4 ToImVec4(const draw::rgba& color) {
        return ImVec4(
            static_cast<float>(color.r) / 255.0f,
            static_cast<float>(color.g) / 255.0f,
            static_cast<float>(color.b) / 255.0f,
            static_cast<float>(color.a) / 255.0f);
    }

    draw::rgba FromImVec4(const ImVec4& color) {
        return draw::rgba(
            static_cast<std::uint8_t>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(std::clamp(color.w, 0.0f, 1.0f) * 255.0f));
    }

    ImDrawList* ActiveDrawList() {
        return (g_draw_layer == draw::draw_layer::background)
            ? ImGui::GetBackgroundDrawList()
            : ImGui::GetForegroundDrawList();
    }

    void UpdateCurrentWindowState() {
        if (g_window_stack.empty()) {
            return;
        }

        WindowFrameState& frame = g_window_stack.back();
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        frame.state.bounds = ui::rect{ pos.x, pos.y, size.x, size.y };
        frame.state.cursor_y = ImGui::GetCursorPosY();
    }

    void SyncCursorFromState() {
        if (g_window_stack.empty()) {
            return;
        }

        WindowFrameState& frame = g_window_stack.back();
        const float current = ImGui::GetCursorPosY();
        if (std::fabs(current - frame.state.cursor_y) > 0.01f) {
            ImGui::SetCursorPosY(frame.state.cursor_y);
        }
    }

    void UpdateLastItem() {
        if (g_window_stack.empty() || !ImGui::GetCurrentWindowRead()) {
            return;
        }

        WindowFrameState& frame = g_window_stack.back();
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        const ImVec2 window_pos = ImGui::GetWindowPos();
        frame.state.last_item = ui::rect{
            item_min.x - window_pos.x,
            item_min.y - window_pos.y,
            item_max.x - item_min.x,
            item_max.y - item_min.y
        };
        frame.state.cursor_y = ImGui::GetCursorPosY();
    }

    std::string VisibleLabel(const char* label) {
        if (!label) {
            return {};
        }
        const std::string_view view(label);
        const std::size_t pos = view.find("##");
        return std::string(view.substr(0, pos));
    }

    int PollPressedVirtualKey(bool include_mouse_buttons) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            return 0;
        }

        for (int vk = 1; vk < 256; ++vk) {
            if (!include_mouse_buttons && (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2)) {
                continue;
            }
            if ((GetAsyncKeyState(vk) & 0x1) != 0) {
                return vk;
            }
        }
        return -1;
    }

    std::string KeyName(int vk) {
        if (vk <= 0) {
            return "none";
        }

        switch (vk) {
        case VK_LBUTTON: return "mouse1";
        case VK_RBUTTON: return "mouse2";
        case VK_MBUTTON: return "mouse3";
        case VK_XBUTTON1: return "mouse4";
        case VK_XBUTTON2: return "mouse5";
        }

        UINT scan = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
        char buffer[64]{};
        const LONG lparam = static_cast<LONG>(scan << 16);
        if (GetKeyNameTextA(lparam, buffer, static_cast<int>(std::size(buffer))) > 0) {
            return buffer;
        }
        return "vk " + std::to_string(vk);
    }

    std::optional<std::vector<std::byte>> LoadFileBytes(std::string_view filepath) {
        std::error_code ec{};
        const std::filesystem::path path(filepath);
        if (!std::filesystem::exists(path, ec) || ec) {
            return std::nullopt;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return std::nullopt;
        }

        std::vector<char> chars((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<std::byte> bytes(chars.size());
        if (!chars.empty()) {
            std::memcpy(bytes.data(), chars.data(), chars.size());
        }
        return bytes;
    }

    bool DecodeImageRgba(std::span<const std::byte> data, std::vector<std::uint8_t>& pixels, int& out_width, int& out_height) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory{};
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICStream> stream{};
        if (FAILED(factory->CreateStream(&stream))) {
            return false;
        }

        std::vector<BYTE> buffer(data.size());
        if (!data.empty()) {
            std::memcpy(buffer.data(), data.data(), data.size());
        }

        if (FAILED(stream->InitializeFromMemory(buffer.data(), static_cast<DWORD>(buffer.size())))) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder{};
        if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame{};
        if (FAILED(decoder->GetFrame(0, &frame))) {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter{};
        if (FAILED(factory->CreateFormatConverter(&converter))) {
            return false;
        }

        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            return false;
        }

        UINT width = 0;
        UINT height = 0;
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) {
            return false;
        }

        const std::size_t stride = static_cast<std::size_t>(width) * 4u;
        pixels.resize(stride * static_cast<std::size_t>(height));
        if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(pixels.size()), pixels.data()))) {
            pixels.clear();
            return false;
        }

        out_width = static_cast<int>(width);
        out_height = static_cast<int>(height);
        return true;
    }
}

bool draw::initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!hwnd || !device || !context) {
        return false;
    }

    if (!g_initialized) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (!ImGui_ImplWin32_Init(hwnd)) {
            return false;
        }
        if (!ImGui_ImplDX11_Init(device, context)) {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return false;
        }
        g_initialized = true;
    }

    g_draw_ctx.hwnd = hwnd;
    g_draw_ctx.device = device;
    g_draw_ctx.device_context = context;
    return true;
}

void draw::shutdown() {
    if (!g_initialized) {
        return;
    }

    ui::shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
}

void draw::begin_frame() {
    if (!g_initialized) {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    if (g_draw_ctx.display_width > 0 && g_draw_ctx.display_height > 0) {
        io.DisplaySize = ImVec2(static_cast<float>(g_draw_ctx.display_width), static_cast<float>(g_draw_ctx.display_height));
    }
    ImGui::NewFrame();
    g_draw_layer = draw::draw_layer::foreground;
    g_window_stack.clear();
    g_group_boxes.clear();
}

void draw::end_frame() {
    if (!g_initialized) {
        return;
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void draw::set_display_size(int width, int height) noexcept {
    g_draw_ctx.display_width = width;
    g_draw_ctx.display_height = height;
}

std::pair<int, int> draw::get_display_size() noexcept {
    return { g_draw_ctx.display_width, g_draw_ctx.display_height };
}

float draw::get_delta_time() noexcept {
    return g_initialized ? ImGui::GetIO().DeltaTime : 0.0f;
}

void draw::set_draw_layer(draw_layer layer) noexcept {
    g_draw_layer = layer;
}

draw::draw_layer draw::get_draw_layer() noexcept {
    return g_draw_layer;
}

draw::font* draw::add_font_from_file(std::string_view filepath, float size_pixels) {
    if (!g_initialized || filepath.empty()) {
        return nullptr;
    }
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(filepath.data(), size_pixels);
}

void draw::push_font(draw::font* fnt) {
    if (fnt) {
        ImGui::PushFont(fnt);
    }
}

void draw::pop_font() {
    ImGui::PopFont();
}

std::pair<float, float> draw::measure_text(std::string_view text, const draw::font* fnt) {
    if (!g_initialized) {
        return { 0.0f, 0.0f };
    }
    if (fnt) {
        const ImVec2 size = const_cast<draw::font*>(fnt)->CalcTextSizeA(fnt->LegacySize, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
        return { size.x, size.y };
    }
    const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    return { size.x, size.y };
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> draw::load_texture_from_memory(std::span<const std::byte> data, int* out_width, int* out_height) {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
    if (!g_draw_ctx.device || data.empty()) {
        return srv;
    }

    std::vector<std::uint8_t> pixels{};
    int width = 0;
    int height = 0;
    if (!DecodeImageRgba(data, pixels, width, height)) {
        return srv;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data{};
    init_data.pSysMem = pixels.data();
    init_data.SysMemPitch = static_cast<UINT>(width * 4);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
    if (FAILED(g_draw_ctx.device->CreateTexture2D(&desc, &init_data, &texture))) {
        return srv;
    }
    if (FAILED(g_draw_ctx.device->CreateShaderResourceView(texture.Get(), nullptr, &srv))) {
        srv.Reset();
        return srv;
    }

    if (out_width) {
        *out_width = width;
    }
    if (out_height) {
        *out_height = height;
    }
    return srv;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> draw::load_texture_from_rgba(std::span<const std::uint8_t> rgba, int width, int height) {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv{};
    if (!g_draw_ctx.device || width <= 0 || height <= 0) {
        return srv;
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (rgba.size() < expected) {
        return srv;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data{};
    init_data.pSysMem = rgba.data();
    init_data.SysMemPitch = static_cast<UINT>(width * 4);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
    if (FAILED(g_draw_ctx.device->CreateTexture2D(&desc, &init_data, &texture))) {
        return srv;
    }
    if (FAILED(g_draw_ctx.device->CreateShaderResourceView(texture.Get(), nullptr, &srv))) {
        srv.Reset();
        return srv;
    }
    return srv;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> draw::load_texture_from_file(std::string_view filepath, int* out_width, int* out_height) {
    const auto bytes = LoadFileBytes(filepath);
    if (!bytes.has_value()) {
        return {};
    }
    return load_texture_from_memory(*bytes, out_width, out_height);
}

void draw::line(float x0, float y0, float x1, float y1, rgba color, float thickness) {
    ActiveDrawList()->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), ToImU32(color), thickness);
}

void draw::rect(float x, float y, float w, float h, rgba color, float thickness) {
    ActiveDrawList()->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), ToImU32(color), 0.0f, 0, thickness);
}

void draw::rect_cornered(float x, float y, float w, float h, rgba color, float corner_length, float thickness) {
    const float cl = (std::min)({ corner_length, w * 0.5f, h * 0.5f });
    line(x, y, x + cl, y, color, thickness);
    line(x, y, x, y + cl, color, thickness);
    line(x + w - cl, y, x + w, y, color, thickness);
    line(x + w, y, x + w, y + cl, color, thickness);
    line(x, y + h - cl, x, y + h, color, thickness);
    line(x, y + h, x + cl, y + h, color, thickness);
    line(x + w - cl, y + h, x + w, y + h, color, thickness);
    line(x + w, y + h - cl, x + w, y + h, color, thickness);
}

void draw::rect_filled(float x, float y, float w, float h, rgba color) {
    ActiveDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ToImU32(color));
}

void draw::rect_filled_multi_color(float x, float y, float w, float h, rgba color_tl, rgba color_tr, rgba color_br, rgba color_bl) {
    ActiveDrawList()->AddRectFilledMultiColor(
        ImVec2(x, y),
        ImVec2(x + w, y + h),
        ToImU32(color_tl),
        ToImU32(color_tr),
        ToImU32(color_br),
        ToImU32(color_bl));
}

void draw::rect_textured(float x, float y, float w, float h, ID3D11ShaderResourceView* tex, float u0, float v0, float u1, float v1, rgba color) {
    if (!tex) {
        return;
    }
    ActiveDrawList()->AddImage(
        tex,
        ImVec2(x, y),
        ImVec2(x + w, y + h),
        ImVec2(u0, v0),
        ImVec2(u1, v1),
        ToImU32(color));
}

void draw::convex_poly_filled(std::span<const float> points, rgba color) {
    if (points.size() < 6 || (points.size() % 2) != 0) {
        return;
    }
    static thread_local std::vector<ImVec2> converted;
    converted.resize(points.size() / 2);
    for (std::size_t i = 0; i < converted.size(); ++i) {
        converted[i] = ImVec2(points[i * 2], points[i * 2 + 1]);
    }
    ActiveDrawList()->AddConvexPolyFilled(converted.data(), static_cast<int>(converted.size()), ToImU32(color));
}

void draw::polyline(std::span<const float> points, rgba color, bool closed, float thickness) {
    if (points.size() < 4 || (points.size() % 2) != 0) {
        return;
    }
    static thread_local std::vector<ImVec2> converted;
    converted.resize(points.size() / 2);
    for (std::size_t i = 0; i < converted.size(); ++i) {
        converted[i] = ImVec2(points[i * 2], points[i * 2 + 1]);
    }
    ActiveDrawList()->AddPolyline(converted.data(), static_cast<int>(converted.size()), ToImU32(color), closed ? ImDrawFlags_Closed : 0, thickness);
}

void draw::triangle(float x0, float y0, float x1, float y1, float x2, float y2, rgba color, float thickness) {
    ActiveDrawList()->AddTriangle(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x2, y2), ToImU32(color), thickness);
}

void draw::triangle_filled(float x0, float y0, float x1, float y1, float x2, float y2, rgba color) {
    ActiveDrawList()->AddTriangleFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x2, y2), ToImU32(color));
}

void draw::circle(float x, float y, float radius, rgba color, int segments, float thickness) {
    ActiveDrawList()->AddCircle(ImVec2(x, y), radius, ToImU32(color), segments, thickness);
}

void draw::circle_filled(float x, float y, float radius, rgba color, int segments) {
    ActiveDrawList()->AddCircleFilled(ImVec2(x, y), radius, ToImU32(color), segments);
}

void draw::arc(float x, float y, float radius, float start_angle, float end_angle, rgba color, int segments, float thickness) {
    ImDrawList* draw_list = ActiveDrawList();
    draw_list->PathArcTo(ImVec2(x, y), radius, start_angle, end_angle, segments);
    draw_list->PathStroke(ToImU32(color), false, thickness);
}

void draw::arc_filled(float x, float y, float radius, float start_angle, float end_angle, rgba color, int segments) {
    ImDrawList* draw_list = ActiveDrawList();
    draw_list->PathArcTo(ImVec2(x, y), radius, start_angle, end_angle, segments);
    draw_list->PathFillConvex(ToImU32(color));
}

bool ui::initialize(HWND hwnd) {
    return hwnd != nullptr;
}

void ui::shutdown() {
    g_window_stack.clear();
    g_group_boxes.clear();
    g_active_keybind_id = 0;
    g_keybind_wait_release_id = 0;
    g_menu_bounds_valid = false;
}

void ui::begin() {
    static bool style_initialized = false;
    static ui::style last_applied_style{};
    if (style_initialized && SameUiStyle(last_applied_style, g_style)) {
        return;
    }

    ImGuiStyle& style_ref = ImGui::GetStyle();
    style_ref.WindowRounding = 0.0f;
    style_ref.ChildRounding = 0.0f;
    style_ref.FrameRounding = 0.0f;
    style_ref.PopupRounding = 0.0f;
    style_ref.GrabRounding = 0.0f;
    style_ref.ScrollbarRounding = 0.0f;
    style_ref.FrameBorderSize = 1.0f;
    style_ref.WindowBorderSize = 1.0f;
    style_ref.GrabMinSize = 16.0f;
    style_ref.ScrollbarSize = 12.0f;
    style_ref.FramePadding = ImVec2(8.0f, 5.0f);
    style_ref.ItemSpacing = ImVec2(10.0f, g_style.item_spacing_y + 1.0f);
    style_ref.ItemInnerSpacing = ImVec2(8.0f, 5.0f);
    style_ref.WindowPadding = ImVec2(g_style.window_padding_x, g_style.window_padding_y);
    style_ref.Colors[ImGuiCol_WindowBg] = ToImVec4(g_style.window_bg);
    style_ref.Colors[ImGuiCol_Border] = ToImVec4(g_style.window_border);
    style_ref.Colors[ImGuiCol_ChildBg] = ToImVec4(g_style.nested_bg);
    style_ref.Colors[ImGuiCol_Text] = ToImVec4(g_style.text);
    style_ref.Colors[ImGuiCol_TextDisabled] = ToImVec4(draw::rgba{ 148, 148, 154, 255 });
    style_ref.Colors[ImGuiCol_Button] = ToImVec4(g_style.button_bg);
    style_ref.Colors[ImGuiCol_ButtonHovered] = ToImVec4(g_style.button_hovered);
    style_ref.Colors[ImGuiCol_ButtonActive] = ToImVec4(g_style.button_active);
    style_ref.Colors[ImGuiCol_FrameBg] = ToImVec4(g_style.button_bg);
    style_ref.Colors[ImGuiCol_FrameBgHovered] = ToImVec4(g_style.button_hovered);
    style_ref.Colors[ImGuiCol_FrameBgActive] = ToImVec4(g_style.button_active);
    style_ref.Colors[ImGuiCol_CheckMark] = ToImVec4(g_style.accent);
    style_ref.Colors[ImGuiCol_SliderGrab] = ToImVec4(g_style.accent);
    style_ref.Colors[ImGuiCol_SliderGrabActive] = ToImVec4(lighten(g_style.accent, 1.1f));
    style_ref.Colors[ImGuiCol_Header] = ToImVec4(g_style.button_bg);
    style_ref.Colors[ImGuiCol_HeaderHovered] = ToImVec4(g_style.button_hovered);
    style_ref.Colors[ImGuiCol_HeaderActive] = ToImVec4(g_style.button_active);
    style_ref.Colors[ImGuiCol_Separator] = ToImVec4(draw::rgba{ 58, 58, 62, 170 });
    style_ref.Colors[ImGuiCol_SeparatorHovered] = ToImVec4(draw::rgba{ 90, 90, 96, 220 });
    style_ref.Colors[ImGuiCol_SeparatorActive] = ToImVec4(draw::rgba{ 110, 110, 116, 235 });
    style_ref.Colors[ImGuiCol_TableBorderLight] = ToImVec4(draw::rgba{ 48, 48, 54, 170 });
    style_ref.Colors[ImGuiCol_TableBorderStrong] = ToImVec4(draw::rgba{ 72, 72, 78, 220 });
    style_ref.Colors[ImGuiCol_NavHighlight] = ToImVec4(draw::rgba{ g_style.accent.r, g_style.accent.g, g_style.accent.b, 255 });
    last_applied_style = g_style;
    style_initialized = true;
}

void ui::end() {
}

bool ui::process_wndproc_message(UINT msg, WPARAM w_param, LPARAM l_param) {
    return ImGui_ImplWin32_WndProcHandler(g_draw_ctx.hwnd, msg, w_param, l_param) != 0;
}

bool ui::hit_test_active_ui(float x, float y) {
    if (g_menu_bounds_valid) {
        if (x >= static_cast<float>(g_last_menu_bounds.left)
            && x <= static_cast<float>(g_last_menu_bounds.right)
            && y >= static_cast<float>(g_last_menu_bounds.top)
            && y <= static_cast<float>(g_last_menu_bounds.bottom)) {
            return true;
        }
    }

    return ImGui::GetIO().WantCaptureMouse || g_active_keybind_id != 0;
}

ui::style& ui::get_style() {
    return g_style;
}

draw::rgba ui::get_accent_color() {
    return g_style.accent;
}

draw::rgba ui::lighten(const draw::rgba& color, float factor) {
    return draw::rgba(
        static_cast<std::uint8_t>(std::clamp(color.r * factor, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(color.g * factor, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(color.b * factor, 0.0f, 255.0f)),
        color.a);
}

draw::rgba ui::lerp(const draw::rgba& a, const draw::rgba& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto blend = [t](std::uint8_t lhs, std::uint8_t rhs) -> std::uint8_t {
        return static_cast<std::uint8_t>(lhs + (rhs - lhs) * t);
        };
    return draw::rgba(blend(a.r, b.r), blend(a.g, b.g), blend(a.b, b.b), blend(a.a, b.a));
}

bool ui::begin_window(const char* title, float& x, float& y, float& w, float& h, bool movable, float min_w, float min_h, bool resizable) {
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w, h), resizable ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
    if (resizable) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(min_w, min_h), ImVec2(FLT_MAX, FLT_MAX));
    }
    else {
        const float lock_w = (std::max)(min_w, w);
        const float lock_h = (std::max)(min_h, h);
        ImGui::SetNextWindowSizeConstraints(ImVec2(lock_w, lock_h), ImVec2(lock_w, lock_h));
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!movable) {
        flags |= ImGuiWindowFlags_NoMove;
    }
    if (!resizable) {
        flags |= ImGuiWindowFlags_NoResize;
    }

    const bool open = ImGui::Begin(title, nullptr, flags);
    g_window_stack.push_back(WindowFrameState{});
    UpdateCurrentWindowState();
    x = ImGui::GetWindowPos().x;
    y = ImGui::GetWindowPos().y;
    w = ImGui::GetWindowSize().x;
    h = ImGui::GetWindowSize().y;
    g_last_menu_bounds = RECT{
        static_cast<LONG>(x),
        static_cast<LONG>(y),
        static_cast<LONG>(x + w),
        static_cast<LONG>(y + h)
    };
    g_menu_bounds_valid = true;
    return open;
}

void ui::end_window() {
    if (!g_window_stack.empty()) {
        g_window_stack.pop_back();
    }
    ImGui::End();
}

bool ui::begin_nested_window(const char* id, float width, float height) {
    SyncCursorFromState();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(g_style.nested_bg));
    ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(g_style.nested_border));
    const bool open = ImGui::BeginChild(id, ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    g_window_stack.push_back(WindowFrameState{});
    UpdateCurrentWindowState();
    return open;
}

void ui::end_nested_window() {
    if (!g_window_stack.empty()) {
        g_window_stack.pop_back();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    UpdateLastItem();
}

bool ui::begin_group_box(const char* title, float width) {
    SyncCursorFromState();
    GroupBoxState group{};
    group.draw_list = ImGui::GetWindowDrawList();
    group.start_screen = ImGui::GetCursorScreenPos();
    group.width = width;
    if (group.draw_list) {
        group.draw_list->ChannelsSplit(2);
        group.draw_list->ChannelsSetCurrent(1);
        group.split_active = true;
    }

    g_group_boxes.push_back(group);
    ImGui::PushID(title);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(width, 0.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - width + 10.0f);
    ImGui::TextUnformatted(title);
    ImGui::Spacing();
    ImGui::PushItemWidth((width - 20.0f) * 0.55f);
    return true;
}

void ui::end_group_box() {
    if (g_group_boxes.empty()) {
        return;
    }

    ImGui::PopItemWidth();
    ImGui::EndGroup();
    const ImVec2 end = ImGui::GetItemRectMax();
    GroupBoxState group = g_group_boxes.back();
    g_group_boxes.pop_back();
    ImGui::PopID();

    if (group.split_active && group.draw_list) {
        const ImVec2 min = group.start_screen;
        const ImVec2 max_pos = ImVec2(min.x + group.width, end.y + 6.0f);
        group.draw_list->ChannelsSetCurrent(0);
        group.draw_list->AddRectFilled(min, max_pos, ToImU32(g_style.group_box_bg));
        group.draw_list->AddRect(min, max_pos, ToImU32(g_style.group_box_border));
        group.draw_list->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max_pos.x - 1.0f, min.y + 2.0f), ToImU32(draw::rgba(g_style.accent.r, g_style.accent.g, g_style.accent.b, 180)));
        group.draw_list->ChannelsMerge();
    }

    UpdateLastItem();
}

std::pair<float, float> ui::get_content_region_avail() {
    SyncCursorFromState();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    return { avail.x, avail.y };
}

void ui::set_cursor_pos(float x, float y) {
    ImGui::SetCursorPos(ImVec2(x, y));
    if (!g_window_stack.empty()) {
        g_window_stack.back().state.cursor_y = y;
    }
}

void ui::same_line() {
    ImGui::SameLine();
}

void ui::text(const char* value) {
    SyncCursorFromState();
    ImGui::TextUnformatted(value ? value : "");
    UpdateLastItem();
}

bool ui::button(const char* label, float width, float height) {
    SyncCursorFromState();
    const bool changed = ImGui::Button(label, ImVec2(width, height));
    UpdateLastItem();
    return changed;
}

bool ui::checkbox(const char* label, bool& value) {
    SyncCursorFromState();
    const bool changed = ImGui::Checkbox(label, &value);
    UpdateLastItem();
    return changed;
}

bool ui::slider_int(const char* label, int& value, int min_value, int max_value) {
    SyncCursorFromState();
    const bool changed = ImGui::SliderInt(label, &value, min_value, max_value);
    UpdateLastItem();
    return changed;
}

bool ui::slider_float(const char* label, float& value, float min_value, float max_value, const char* format) {
    SyncCursorFromState();
    const bool changed = ImGui::SliderFloat(label, &value, min_value, max_value, format);
    UpdateLastItem();
    return changed;
}

bool ui::combo(const char* label, int& value, const char* const* items, int count) {
    SyncCursorFromState();
    const bool changed = ImGui::Combo(label, &value, items, count);
    UpdateLastItem();
    return changed;
}

bool ui::multicombo(const char* label, bool* values, const char* const* items, int count) {
    SyncCursorFromState();
    std::string preview{};
    for (int i = 0; i < count; ++i) {
        if (!values[i]) {
            continue;
        }
        if (!preview.empty()) {
            preview += ", ";
        }
        preview += items[i];
    }
    if (preview.empty()) {
        preview = "none";
    }

    bool changed = false;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (int i = 0; i < count; ++i) {
            bool selected = values[i];
            if (ImGui::Selectable(items[i], &selected, ImGuiSelectableFlags_DontClosePopups)) {
                values[i] = selected;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    UpdateLastItem();
    return changed;
}

bool ui::color_picker(const char* label, draw::rgba& color, float width, bool show_alpha) {
    SyncCursorFromState();
    if (width > 0.0f) {
        ImGui::SetNextItemWidth(width);
    }
    ImVec4 value = ToImVec4(color);
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoOptions;
    if (show_alpha) {
        flags |= ImGuiColorEditFlags_AlphaBar;
    }
    else {
        flags |= ImGuiColorEditFlags_NoAlpha;
    }
    const bool changed = ImGui::ColorEdit4(label, &value.x, flags);
    if (changed) {
        color = FromImVec4(value);
    }
    UpdateLastItem();
    return changed;
}

bool ui::text_input(const char* label, std::string& value, std::size_t max_len, const char* hint) {
    SyncCursorFromState();
    std::vector<char> buffer(max_len + 1, '\0');
    std::strncpy(buffer.data(), value.c_str(), max_len);
    const bool changed = hint && hint[0]
        ? ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size())
        : ImGui::InputText(label, buffer.data(), buffer.size());
    if (changed) {
        value = buffer.data();
    }
    UpdateLastItem();
    return changed;
}

bool ui::text_input(const char* label, char* value, std::size_t max_len, const char* hint) {
    SyncCursorFromState();
    const bool changed = hint && hint[0]
        ? ImGui::InputTextWithHint(label, hint, value, max_len + 1)
        : ImGui::InputText(label, value, max_len + 1);
    UpdateLastItem();
    return changed;
}

bool ui::keybind(const char* label, int& value) {
    SyncCursorFromState();
    const std::string visible = VisibleLabel(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(visible.c_str());
    ImGui::SameLine();

    ImGui::PushID(label);
    const ImGuiID id = ImGui::GetID("##keybind_button");
    const std::string button_text = (g_active_keybind_id == id) ? "press key..." : KeyName(value);
    bool changed = false;
    if (ImGui::Button(button_text.c_str(), ImVec2(ImGui::CalcItemWidth(), 0.0f))) {
        if (g_active_keybind_id == id) {
            g_active_keybind_id = 0;
            g_keybind_wait_release_id = 0;
        }
        else {
            g_active_keybind_id = id;
            // Avoid consuming the same LMB click that activated this keybind button.
            g_keybind_wait_release_id = id;
        }
    }

    if (g_active_keybind_id == id) {
        if (g_keybind_wait_release_id == id) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                g_keybind_wait_release_id = 0;
            }
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            value = VK_LBUTTON;
            g_active_keybind_id = 0;
            changed = true;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            value = VK_RBUTTON;
            g_active_keybind_id = 0;
            changed = true;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            value = VK_MBUTTON;
            g_active_keybind_id = 0;
            changed = true;
        }
        else {
            const int pressed = PollPressedVirtualKey(false);
            if (pressed >= 0) {
                value = pressed;
                g_active_keybind_id = 0;
                changed = true;
            }
        }
    }

    ImGui::PopID();
    UpdateLastItem();
    return changed;
}

void ui::push_style_color(style_color idx, const draw::rgba& color) {
    ImGuiCol target = ImGuiCol_Button;
    switch (idx) {
    case style_color::button_bg: target = ImGuiCol_Button; break;
    case style_color::button_hovered: target = ImGuiCol_ButtonHovered; break;
    case style_color::button_active: target = ImGuiCol_ButtonActive; break;
    case style_color::text: target = ImGuiCol_Text; break;
    }
    ImGui::PushStyleColor(target, ToImVec4(color));
}

void ui::pop_style_color(int count) {
    ImGui::PopStyleColor(count);
}

void ui::push_style_var(style_var idx, float value) {
    switch (idx) {
    case style_var::window_padding_x:
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(value, ImGui::GetStyle().WindowPadding.y));
        break;
    case style_var::window_padding_y:
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, value));
        break;
    }
}

void ui::pop_style_var(int count) {
    ImGui::PopStyleVar(count);
}

ui::detail::window_state* ui::detail::get_current_window() {
    if (g_window_stack.empty()) {
        return nullptr;
    }
    UpdateCurrentWindowState();
    return &g_window_stack.back().state;
}

bool ui::detail::mouse_hovered(const rect& r) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return mouse.x >= r.x && mouse.x <= (r.x + r.w) && mouse.y >= r.y && mouse.y <= (r.y + r.h);
}

bool ui::detail::mouse_clicked() {
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

float ui::detail::mouse_x() {
    return ImGui::GetIO().MousePos.x;
}

float ui::detail::mouse_y() {
    return ImGui::GetIO().MousePos.y;
}

bool ui::detail::overlay_blocking_input() {
    return g_active_keybind_id != 0 || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
}





