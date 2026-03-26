#pragma once

#include "config.hpp"
#include "types.hpp"
#include <cmath>
#include <concepts>
#include <numbers>
#include <tuple>

// Forward declarations for Cairo/Pango (avoid header dependency in core)
typedef struct _cairo cairo_t;
typedef struct _PangoLayout PangoLayout;

namespace typelock::widget {

// ============================================================================
//  Geometry — value types for layout
// ============================================================================

struct Rect {
    float x, y, w, h;

    constexpr auto center_x() const -> float { return x + w / 2.0f; }
    constexpr auto center_y() const -> float { return y + h / 2.0f; }
};

struct Size {
    float w, h;
    constexpr auto operator+(Size o) const -> Size { return {w + o.w, h + o.h}; }
};

// ============================================================================
//  RenderContext — everything a widget needs to draw itself
//
//  This is the "environment" passed through the widget tree. By bundling
//  all rendering state into a single value, widgets remain pure functions
//  of (context, bounds) → drawing commands.
// ============================================================================

struct RenderContext {
    cairo_t*        cr;
    Rect            bounds;
    const Config&   config;

    // ViewModel fields (flattened for widget access)
    const char*     status_text;
    const char*     input_display;
    std::size_t     input_length;
    bool            show_error;
    const char*     error_text;
    const char*     time_text;
    const char*     date_text;
    bool            fingerprint_active;

    // Animation state
    float           opacity;
    float           error_shake;
    float           error_opacity;
};

// ============================================================================
//  Widget concept — the fundamental abstraction
//
//  A Widget is any type that can:
//    1. Report its preferred size given a context
//    2. Render itself given a context with bounds
//
//  This is a type class (in Haskell terms) or a trait (in Rust terms),
//  expressed as a C++20 concept. Widgets are composed at the type level
//  using layout combinators — the compiler verifies that every node in
//  the widget tree satisfies this concept.
// ============================================================================

template <typename W>
concept Widget = requires(const W& w, RenderContext& ctx) {
    { w.size_hint(ctx) } -> std::convertible_to<Size>;
    { w.render(ctx) } -> std::same_as<void>;
};

// ============================================================================
//  Concrete widgets — leaf nodes in the widget tree
// ============================================================================

struct ClockWidget {
    auto size_hint(RenderContext& ctx) const -> Size;
    void render(RenderContext& ctx) const;
};

struct DateWidget {
    auto size_hint(RenderContext& ctx) const -> Size;
    void render(RenderContext& ctx) const;
};

struct StatusLabel {
    auto size_hint(RenderContext& ctx) const -> Size;
    void render(RenderContext& ctx) const;
};

struct PasswordDots {
    auto size_hint(RenderContext& ctx) const -> Size;
    void render(RenderContext& ctx) const;
};

struct ErrorLabel {
    auto size_hint(RenderContext& ctx) const -> Size;
    void render(RenderContext& ctx) const;
};

struct FingerprintIndicator {
    auto size_hint(RenderContext& ctx) const -> Size;
    void render(RenderContext& ctx) const;
};

// ============================================================================
//  Spacer<N> — compile-time fixed-height spacing
//
//  The height is a template parameter — a type-level natural number.
//  Spacer<20> and Spacer<40> are different types. The compiler can
//  constant-fold the layout computation entirely.
// ============================================================================

template <int N>
struct Spacer {
    auto size_hint(RenderContext&) const -> Size { return {0, static_cast<float>(N)}; }
    void render(RenderContext&) const {}
};

static_assert(Widget<Spacer<10>>);

// ============================================================================
//  Layout combinators — compose widgets at the type level
//
//  VStack<A, B, C> stacks widgets vertically.
//  Center<W> centers a widget in its bounds.
//  Padding<N, W> adds padding around a widget.
//
//  These are higher-kinded types: they take Widget types as parameters
//  and produce new Widget types. The entire layout tree is a single
//  type — the compiler can see through all the composition.
// ============================================================================

// --- VStack: vertical composition ---

template <Widget... Children>
struct VStack {
    std::tuple<Children...> children;
    float gap = 0;

    constexpr VStack(Children... cs, float g = 10.0f)
        : children{std::move(cs)...}, gap(g) {}

    constexpr VStack() : gap(10.0f) {}

    auto size_hint(RenderContext& ctx) const -> Size {
        float total_h = 0;
        float max_w   = 0;
        std::apply([&](const auto&... child) {
            ((void)[&] {
                auto s = child.size_hint(ctx);
                total_h += s.h + gap;
                max_w = std::max(max_w, s.w);
            }(), ...);
        }, children);
        if constexpr (sizeof...(Children) > 0)
            total_h -= gap;
        return {max_w, total_h};
    }

    void render(RenderContext& ctx) const {
        auto total = size_hint(ctx);
        float y = ctx.bounds.center_y() - total.h / 2.0f;

        std::apply([&](const auto&... child) {
            ((void)[&] {
                auto s = child.size_hint(ctx);
                RenderContext child_ctx = ctx;
                child_ctx.bounds = {
                    ctx.bounds.x,
                    y,
                    ctx.bounds.w,
                    s.h
                };
                child.render(child_ctx);
                y += s.h + gap;
            }(), ...);
        }, children);
    }
};

// --- Center: horizontal centering ---

template <Widget W>
struct Center {
    W child;

    constexpr Center() = default;
    constexpr explicit Center(W c) : child(std::move(c)) {}

    auto size_hint(RenderContext& ctx) const -> Size {
        return child.size_hint(ctx);
    }

    void render(RenderContext& ctx) const {
        auto s = child.size_hint(ctx);
        RenderContext centered = ctx;
        centered.bounds = {
            ctx.bounds.center_x() - s.w / 2.0f,
            ctx.bounds.y,
            s.w,
            ctx.bounds.h
        };
        child.render(centered);
    }
};

// --- Padding: add space around a widget ---

template <int N, Widget W>
struct Padding {
    W child;

    constexpr Padding() = default;
    constexpr explicit Padding(W c) : child(std::move(c)) {}

    auto size_hint(RenderContext& ctx) const -> Size {
        auto s = child.size_hint(ctx);
        return {s.w + 2.0f * N, s.h + 2.0f * N};
    }

    void render(RenderContext& ctx) const {
        RenderContext padded = ctx;
        padded.bounds = {
            ctx.bounds.x + N,
            ctx.bounds.y + N,
            ctx.bounds.w - 2.0f * N,
            ctx.bounds.h - 2.0f * N
        };
        child.render(padded);
    }
};

// ============================================================================
//  DefaultLayout — the lock screen widget tree as a type
//
//  This is the entire UI structure encoded in the type system. The compiler
//  verifies that every node is a valid Widget. Changing the layout is a
//  type-level refactor — the compiler guides you.
//
//  Center<VStack<
//      ClockWidget,
//      DateWidget,
//      Spacer<40>,
//      StatusLabel,
//      Spacer<16>,
//      PasswordDots,
//      Spacer<12>,
//      ErrorLabel,
//      Spacer<16>,
//      FingerprintIndicator
//  >>
// ============================================================================

using DefaultLayout = Center<VStack<
    ClockWidget,
    DateWidget,
    Spacer<40>,
    StatusLabel,
    Spacer<16>,
    PasswordDots,
    Spacer<12>,
    ErrorLabel,
    Spacer<16>,
    FingerprintIndicator
>>;

// Compile-time proof: the layout is a valid widget tree
static_assert(Widget<ClockWidget>);
static_assert(Widget<PasswordDots>);
static_assert(Widget<ErrorLabel>);
static_assert(Widget<VStack<ClockWidget, Spacer<10>, PasswordDots>>);
static_assert(Widget<Center<VStack<ClockWidget, PasswordDots>>>);
static_assert(Widget<DefaultLayout>);

}  // namespace typelock::widget
