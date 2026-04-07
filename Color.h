#pragma once
#include <cstdint>

namespace draw {
    struct rgba {
        std::uint8_t r{ 0 };
        std::uint8_t g{ 0 };
        std::uint8_t b{ 0 };
        std::uint8_t a{ 255 };

        constexpr rgba() noexcept = default;
        constexpr rgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255) noexcept
            : r(red), g(green), b(blue), a(alpha) {
        }
    };
}