#include "harness.hpp"
#include "core/types.hpp"
#include "core/config.hpp"
#include "core/widget.hpp"

#include <cmath>
#include <string>

using namespace typelock;

// ============================================================================
//  Strong<T, Tag> — phantom-typed wrappers
// ============================================================================

TEST(strong_types_are_distinct) {
    Px a{10.0f};
    Px b{20.0f};
    auto c = a + b;
    EXPECT(c.value == 30.0f);

    Seconds s{1.5};
    auto s2 = s * 2.0;
    EXPECT(s2.value == 3.0);
}

TEST(strong_type_subtraction) {
    Px a{30.0f};
    Px b{10.0f};
    auto c = a - b;
    EXPECT(c.value == 20.0f);
}

TEST(strong_type_division) {
    Seconds a{6.0};
    auto b = a / 2.0;
    EXPECT(b.value == 3.0);
}

TEST(strong_type_comparison) {
    Px a{10.0f};
    Px b{20.0f};
    EXPECT(a < b);
    EXPECT(b > a);
    EXPECT(a == Px{10.0f});
    EXPECT(a != b);
}

// ============================================================================
//  Color — constexpr RGBA
// ============================================================================

TEST(color_hex_parsing) {
    constexpr auto c = Color::hex(0xFF8040);
    EXPECT(c.r == 0xFF);
    EXPECT(c.g == 0x80);
    EXPECT(c.b == 0x40);
    EXPECT(c.a == 255);
}

TEST(color_with_alpha) {
    constexpr auto c = Color::hex(0xFFFFFF).with_alpha(128);
    EXPECT(c.r == 255);
    EXPECT(c.a == 128);
}

TEST(color_black) {
    constexpr auto c = Color::hex(0x000000);
    EXPECT(c.r == 0);
    EXPECT(c.g == 0);
    EXPECT(c.b == 0);
    EXPECT(c.a == 255);
}

TEST(color_equality) {
    constexpr auto a = Color::hex(0xFF8040);
    constexpr auto b = Color::hex(0xFF8040);
    constexpr auto c = Color::hex(0x112233);
    EXPECT(a == b);
    EXPECT(!(a == c));
}

TEST(color_float_accessors) {
    constexpr auto c = Color::rgba(255, 0, 128, 255);
    EXPECT(c.rf() == 1.0f);
    EXPECT(c.gf() == 0.0f);
    float bf = c.bf();
    EXPECT(bf > 0.49f && bf < 0.51f);
}

TEST(palette_colors_exist) {
    EXPECT(palette::bg_dark == Color::hex(0x141420));
    EXPECT(palette::text_white == Color::hex(0xDDDDE0));
    EXPECT(palette::error_red == Color::hex(0xE04040));
}

// ============================================================================
//  Easing functions
// ============================================================================

TEST(easing_bounds) {
    EXPECT(Linear::ease(0.0f) == 0.0f);
    EXPECT(Linear::ease(1.0f) == 1.0f);
    EXPECT(EaseOut::ease(0.0f) == 0.0f);
    EXPECT(EaseOut::ease(1.0f) == 1.0f);
    EXPECT(EaseIn::ease(0.0f) == 0.0f);
    EXPECT(EaseIn::ease(1.0f) == 1.0f);
    EXPECT(EaseOut::ease(0.5f) > 0.5f);
    EXPECT(EaseIn::ease(0.5f) < 0.5f);
}

TEST(easing_ease_in_out_bounds) {
    EXPECT(EaseInOut::ease(0.0f) == 0.0f);
    EXPECT(EaseInOut::ease(1.0f) == 1.0f);
}

TEST(easing_ease_out_back_bounds) {
    EXPECT(EaseOutBack::ease(0.0f) == 0.0f);
    EXPECT(EaseOutBack::ease(1.0f) == 1.0f);
    EXPECT(EaseOutBack::ease(0.5f) > 0.5f);
}

TEST(easing_linear_is_identity) {
    EXPECT(Linear::ease(0.25f) == 0.25f);
    EXPECT(Linear::ease(0.5f) == 0.5f);
    EXPECT(Linear::ease(0.75f) == 0.75f);
}

TEST(easing_monotonicity) {
    float prev = 0.0f;
    for (int i = 1; i <= 10; i++) {
        float t = static_cast<float>(i) / 10.0f;
        float v = EaseOut::ease(t);
        EXPECT(v >= prev);
        prev = v;
    }
}

// ============================================================================
//  Shake — damped oscillation
// ============================================================================

TEST(shake_offset_at_zero) {
    float offset = shake_offset(0.0f, 10.0f);
    EXPECT(offset == 0.0f);
}

TEST(shake_offset_at_one) {
    float offset = shake_offset(1.0f, 10.0f);
    EXPECT(offset > -1.0f && offset < 1.0f);
}

// ============================================================================
//  Config defaults
// ============================================================================

TEST(default_config_values) {
    Config cfg;
    EXPECT(cfg.general.grace_period.value == 5.0);
    EXPECT(cfg.background.blur_radius == 20);
    EXPECT(cfg.background.color == palette::bg_dark);
    EXPECT(cfg.clock.enabled == true);
    EXPECT(cfg.general.fingerprint == false);
}

TEST(default_config_background) {
    Config cfg;
    EXPECT(cfg.background.screenshot == true);
    EXPECT(cfg.background.blur_radius == 20);
}

TEST(default_config_input) {
    Config cfg;
    EXPECT(cfg.input.dot_size == 8.0f);
    EXPECT(cfg.input.dot_gap == 20.0f);
    EXPECT(cfg.input.field_width == 300.0f);
    EXPECT(cfg.input.field_height == 50.0f);
    EXPECT(cfg.input.field_radius == 10.0f);
}

TEST(default_config_clock) {
    Config cfg;
    EXPECT(cfg.clock.time_format == "%H:%M");
    EXPECT(cfg.clock.time_color == palette::clock_white);
}

TEST(default_config_dpms) {
    Config cfg;
    EXPECT(cfg.general.dpms_timeout.value == 30.0);
}

// ============================================================================
//  Geometry (widget value types)
// ============================================================================

TEST(rect_center) {
    typelock::widget::Rect r{10.0f, 20.0f, 100.0f, 50.0f};
    EXPECT(r.center_x() == 60.0f);
    EXPECT(r.center_y() == 45.0f);
}

TEST(size_addition) {
    typelock::widget::Size a{10.0f, 20.0f};
    typelock::widget::Size b{30.0f, 40.0f};
    auto c = a + b;
    EXPECT(c.w == 40.0f);
    EXPECT(c.h == 60.0f);
}
