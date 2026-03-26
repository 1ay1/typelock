#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <numbers>

namespace typelock {

// ============================================================================
//  Strong<T, Tag> — phantom-typed wrapper preventing accidental type confusion
//
//  Strong<int, struct PixelTag> and Strong<int, struct SecondTag> are
//  distinct types at the type level but identical at the value level.
//  You cannot add pixels to seconds, assign one to the other, or pass
//  one where the other is expected. The Tag is never instantiated — it
//  exists only to distinguish types (hence "phantom").
//
//  This is the C++ encoding of a newtype in Haskell:
//      newtype Pixels = Pixels Int
//      newtype Seconds = Seconds Double
// ============================================================================

template <typename T, typename Tag>
struct Strong {
    T value{};

    constexpr Strong() = default;
    constexpr explicit Strong(T v) : value(v) {}

    constexpr auto operator<=>(const Strong&) const = default;

    constexpr auto operator+(Strong o) const -> Strong { return Strong{value + o.value}; }
    constexpr auto operator-(Strong o) const -> Strong { return Strong{value - o.value}; }
    constexpr auto operator*(T s) const -> Strong { return Strong{value * s}; }
    constexpr auto operator/(T s) const -> Strong { return Strong{value / s}; }

    constexpr auto operator+=(Strong o) -> Strong& { value += o.value; return *this; }
    constexpr auto operator-=(Strong o) -> Strong& { value -= o.value; return *this; }
};

using Px      = Strong<float, struct PxTag>;
using Seconds = Strong<double, struct SecondsTag>;

// ============================================================================
//  Color — constexpr RGBA with compile-time hex parsing
//
//  Colors are constructed at compile time via Color::hex() or Color::rgba().
//  The compiler verifies that all colors are valid before the program runs.
// ============================================================================

struct Color {
    uint8_t r{}, g{}, b{}, a{255};

    static constexpr auto hex(uint32_t v) -> Color {
        return {
            static_cast<uint8_t>((v >> 16) & 0xFF),
            static_cast<uint8_t>((v >> 8) & 0xFF),
            static_cast<uint8_t>(v & 0xFF),
            255
        };
    }

    static constexpr auto rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) -> Color {
        return {r, g, b, a};
    }

    constexpr auto with_alpha(uint8_t new_a) const -> Color {
        return {r, g, b, new_a};
    }

    constexpr auto rf() const -> double { return r / 255.0; }
    constexpr auto gf() const -> double { return g / 255.0; }
    constexpr auto bf() const -> double { return b / 255.0; }
    constexpr auto af() const -> double { return a / 255.0; }

    constexpr bool operator==(const Color&) const = default;
};

// Compile-time color palette — proved valid at compile time
namespace palette {
    inline constexpr Color bg_dark     = Color::hex(0x141420);
    inline constexpr Color text_white  = Color::hex(0xDDDDE0);
    inline constexpr Color text_dim    = Color::hex(0xAAAAAA);
    inline constexpr Color dot_white   = Color::hex(0xF0F0F5);
    inline constexpr Color error_red   = Color::hex(0xE04040);
    inline constexpr Color field_bg    = Color::rgba(255, 255, 255, 20);
    inline constexpr Color clock_white = Color::hex(0xFFFFFF);
}

static_assert(palette::bg_dark.r == 0x14);
static_assert(palette::error_red.r == 0xE0);
static_assert(palette::field_bg.a == 20);

// ============================================================================
//  Easing — compile-time easing functions as types
//
//  Each easing function is a type with a static constexpr `ease(float) -> float`.
//  This allows easing to be a template parameter — the compiler can inline
//  the easing function entirely, and you can constrain animations to specific
//  easing families at the type level.
//
//  These are morphisms in the unit interval [0,1] → [0,1].
// ============================================================================

template <typename E>
concept Easing = requires(float t) {
    { E::ease(t) } -> std::convertible_to<float>;
};

struct Linear {
    static constexpr float ease(float t) { return t; }
};

struct EaseOut {
    static constexpr float ease(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
};

struct EaseIn {
    static constexpr float ease(float t) { return t * t; }
};

struct EaseInOut {
    static constexpr float ease(float t) {
        return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
    }
};

struct EaseOutBack {
    static constexpr float ease(float t) {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return 1.0f + c3 * (t - 1.0f) * (t - 1.0f) * (t - 1.0f) + c1 * (t - 1.0f) * (t - 1.0f);
    }
};

static_assert(Linear::ease(0.0f) == 0.0f);
static_assert(Linear::ease(1.0f) == 1.0f);
static_assert(EaseOut::ease(0.0f) == 0.0f);
static_assert(EaseOut::ease(1.0f) == 1.0f);

// ============================================================================
//  Animation<Easing> — time-parametric interpolation
//
//  An animation maps elapsed time to a value in [0,1] using an easing
//  function. The easing is a template parameter — a type-level choice
//  that the compiler can fully inline.
// ============================================================================

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration  = Clock::duration;

template <Easing E = EaseOut>
struct Animation {
    TimePoint start{};
    Duration  duration{};
    bool      active = false;

    void begin(Duration dur) {
        start    = Clock::now();
        duration = dur;
        active   = true;
    }

    auto progress() const -> float {
        if (!active) return 1.0f;
        auto elapsed = Clock::now() - start;
        float t = std::clamp(
            static_cast<float>(elapsed.count()) / static_cast<float>(duration.count()),
            0.0f, 1.0f);
        return E::ease(t);
    }

    bool done() const {
        if (!active) return true;
        return (Clock::now() - start) >= duration;
    }
};

// Typed animation presets — the easing is part of the type
using FadeIn    = Animation<EaseOut>;
using ShakeAnim = Animation<EaseOut>;
using PulseAnim = Animation<EaseInOut>;

// ============================================================================
//  Shake — damped sinusoidal oscillation for error feedback
// ============================================================================

inline float shake_offset(float progress, float amplitude = 10.0f) {
    float decay = 1.0f - progress;
    return amplitude * decay * std::sin(progress * 3.0f * std::numbers::pi_v<float>);
}

}  // namespace typelock
