#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace typelock {

// ============================================================================
// States — each type carries exactly the data valid in that state.
// Invalid states are unrepresentable by construction.
// ============================================================================

struct Idle {
    constexpr bool operator==(const Idle&) const = default;
};

struct Typing {
    std::string buffer;
    // Invariant: buffer.size() > 0 (enforced by transitions)
};

struct Authenticating {
    std::string password;
    // Invariant: password.size() > 0 (comes from Typing)
};

struct Unlocked {
    constexpr bool operator==(const Unlocked&) const = default;
};

struct AuthError {
    std::string message;
};

using State = std::variant<Idle, Typing, Authenticating, Unlocked, AuthError>;

// ============================================================================
// Events — inputs to the state machine
// ============================================================================

struct KeyPress {
    char32_t codepoint;
};

struct Backspace {};
struct Submit {};
struct AuthSuccess {};

struct AuthFail {
    std::string reason;
};

struct Timeout {};

using Event = std::variant<KeyPress, Backspace, Submit, AuthSuccess, AuthFail, Timeout>;

// ============================================================================
// Effects — pure descriptions of side effects, never executed inside the core
// ============================================================================

struct NoEffect {};

struct StartAuth {
    std::string password;
};

struct ExitProgram {};

using Effect = std::variant<NoEffect, StartAuth, ExitProgram>;

struct TransitionResult {
    State state;
    Effect effect;
};

// ============================================================================
// Compile-time transition table
//
// Primary template is undefined (valid = false).
// Each specialization is one arrow in the state diagram.
// Attempting to use an undefined transition is a compile-time error
// when accessed through the `ValidTransition` concept.
// ============================================================================

template <typename S, typename E>
struct Transition {
    static constexpr bool valid = false;
};

template <typename S, typename E>
concept ValidTransition = Transition<std::decay_t<S>, std::decay_t<E>>::valid;

// --- Idle + KeyPress → Typing ---
template <>
struct Transition<Idle, KeyPress> {
    static constexpr bool valid = true;
    static auto apply(const Idle&, const KeyPress& e) -> TransitionResult {
        std::string buf;
        if (e.codepoint < 0x80)
            buf += static_cast<char>(e.codepoint);
        return {Typing{std::move(buf)}, NoEffect{}};
    }
};

// --- Typing + KeyPress → Typing ---
template <>
struct Transition<Typing, KeyPress> {
    static constexpr bool valid = true;
    static auto apply(const Typing& t, const KeyPress& e) -> TransitionResult {
        auto buf = t.buffer;
        if (e.codepoint < 0x80)
            buf += static_cast<char>(e.codepoint);
        return {Typing{std::move(buf)}, NoEffect{}};
    }
};

// --- Typing + Backspace → Typing | Idle ---
template <>
struct Transition<Typing, Backspace> {
    static constexpr bool valid = true;
    static auto apply(const Typing& t, const Backspace&) -> TransitionResult {
        if (t.buffer.size() <= 1)
            return {Idle{}, NoEffect{}};
        auto buf = t.buffer;
        buf.pop_back();
        return {Typing{std::move(buf)}, NoEffect{}};
    }
};

// --- Typing + Submit → Authenticating  (effect: StartAuth) ---
template <>
struct Transition<Typing, Submit> {
    static constexpr bool valid = true;
    static auto apply(const Typing& t, const Submit&) -> TransitionResult {
        return {Authenticating{t.buffer}, StartAuth{t.buffer}};
    }
};

// --- Authenticating + AuthSuccess → Unlocked  (effect: ExitProgram) ---
template <>
struct Transition<Authenticating, AuthSuccess> {
    static constexpr bool valid = true;
    static auto apply(const Authenticating&, const AuthSuccess&) -> TransitionResult {
        return {Unlocked{}, ExitProgram{}};
    }
};

// --- Authenticating + AuthFail → AuthError ---
template <>
struct Transition<Authenticating, AuthFail> {
    static constexpr bool valid = true;
    static auto apply(const Authenticating&, const AuthFail& e) -> TransitionResult {
        return {AuthError{e.reason}, NoEffect{}};
    }
};

// --- AuthError + KeyPress → Typing (start over) ---
template <>
struct Transition<AuthError, KeyPress> {
    static constexpr bool valid = true;
    static auto apply(const AuthError&, const KeyPress& e) -> TransitionResult {
        std::string buf;
        if (e.codepoint < 0x80)
            buf += static_cast<char>(e.codepoint);
        return {Typing{std::move(buf)}, NoEffect{}};
    }
};

// --- AuthError + Timeout → Idle ---
template <>
struct Transition<AuthError, Timeout> {
    static constexpr bool valid = true;
    static auto apply(const AuthError&, const Timeout&) -> TransitionResult {
        return {Idle{}, NoEffect{}};
    }
};

// ============================================================================
// Compile-time invariants — if any of these fail, the transition table is wrong
// ============================================================================

template <typename S, typename E>
constexpr bool has_transition = Transition<S, E>::valid;

static_assert(has_transition<Idle, KeyPress>);
static_assert(!has_transition<Idle, Submit>);
static_assert(!has_transition<Idle, Backspace>);
static_assert(!has_transition<Unlocked, KeyPress>);
static_assert(!has_transition<Unlocked, Submit>);
static_assert(has_transition<Typing, KeyPress>);
static_assert(has_transition<Typing, Backspace>);
static_assert(has_transition<Typing, Submit>);
static_assert(!has_transition<Typing, AuthSuccess>);
static_assert(has_transition<Authenticating, AuthSuccess>);
static_assert(has_transition<Authenticating, AuthFail>);
static_assert(!has_transition<Authenticating, KeyPress>);
static_assert(!has_transition<Authenticating, Submit>);
static_assert(has_transition<AuthError, KeyPress>);
static_assert(has_transition<AuthError, Timeout>);

// ============================================================================
// Runtime dispatch — the impure boundary where variant meets the pure core
// ============================================================================

inline auto dispatch(const State& current, const Event& event) -> TransitionResult {
    return std::visit(
        [](const auto& state, const auto& evt) -> TransitionResult {
            using S = std::decay_t<decltype(state)>;
            using E = std::decay_t<decltype(evt)>;
            if constexpr (ValidTransition<S, E>) {
                return Transition<S, E>::apply(state, evt);
            } else {
                return {state, NoEffect{}};
            }
        },
        current, event);
}

// ============================================================================
// ViewModel — pure projection from State to renderable data.
// The renderer never sees raw state, only this.
// ============================================================================

struct ViewModel {
    std::string status_text;
    std::string input_display;
    bool show_error  = false;
    std::string error_text;
};

inline auto view(const State& state) -> ViewModel {
    return std::visit(
        [](const auto& s) -> ViewModel {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, Idle>) {
                return {.status_text = "Enter password", .input_display = "", .show_error = false, .error_text = ""};
            } else if constexpr (std::is_same_v<S, Typing>) {
                return {.status_text = "Enter password",
                        .input_display = std::string(s.buffer.size(), '*'),
                        .show_error = false, .error_text = ""};
            } else if constexpr (std::is_same_v<S, Authenticating>) {
                return {.status_text = "Authenticating...", .input_display = "", .show_error = false, .error_text = ""};
            } else if constexpr (std::is_same_v<S, Unlocked>) {
                return {.status_text = "", .input_display = "", .show_error = false, .error_text = ""};
            } else if constexpr (std::is_same_v<S, AuthError>) {
                return {.status_text = "Enter password",
                        .input_display = "",
                        .show_error   = true,
                        .error_text   = s.message};
            } else {
                static_assert(std::is_same_v<S, void>, "non-exhaustive visitor");
            }
        },
        state);
}

}  // namespace typelock
