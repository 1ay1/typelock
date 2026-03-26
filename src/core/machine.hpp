#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace typelock {

// ============================================================================
//  Meta — compile-time type-level machinery
// ============================================================================

template <typename... Ts>
struct type_list {
    static constexpr std::size_t size = sizeof...(Ts);
};

template <typename List, typename T>
struct contains : std::false_type {};

template <typename T, typename... Ts>
struct contains<type_list<T, Ts...>, T> : std::true_type {};

template <typename T, typename U, typename... Ts>
struct contains<type_list<U, Ts...>, T> : contains<type_list<Ts...>, T> {};

template <typename List, typename T>
constexpr bool contains_v = contains<List, T>::value;

template <typename List, template <typename> class Pred>
struct count_if;

template <template <typename> class Pred>
struct count_if<type_list<>, Pred> {
    static constexpr std::size_t value = 0;
};

template <template <typename> class Pred, typename T, typename... Ts>
struct count_if<type_list<T, Ts...>, Pred> {
    static constexpr std::size_t value =
        (Pred<T>::value ? 1 : 0) + count_if<type_list<Ts...>, Pred>::value;
};

template <typename List, template <typename> class Pred>
constexpr std::size_t count_if_v = count_if<List, Pred>::value;

template <typename List, typename F>
constexpr void for_each_type(F&& f) {
    []<typename... Ts>(type_list<Ts...>, F&& fn) {
        (fn.template operator()<Ts>(), ...);
    }(List{}, std::forward<F>(f));
}

template <typename... Fs>
struct overloaded : Fs... {
    using Fs::operator()...;
};

// ============================================================================
//  States — each type is a proposition; its values are proofs
//
//  Idle            — locked screen, awaiting first input
//  Typing          — password entry in progress (buffer non-empty)
//  Authenticating  — PAM is verifying (password non-empty)
//  Unlocked        — terminal: authentication succeeded
//  AuthError       — authentication failed, showing error
// ============================================================================

struct Idle {
    constexpr bool operator==(const Idle&) const = default;
};

struct Typing {
    std::string buffer;
};

struct Authenticating {
    std::string password;
};

struct Unlocked {
    constexpr bool operator==(const Unlocked&) const = default;
};

struct AuthError {
    std::string message;
};

using States = type_list<Idle, Typing, Authenticating, Unlocked, AuthError>;
using State  = std::variant<Idle, Typing, Authenticating, Unlocked, AuthError>;

template <typename S>
concept IsState = contains_v<States, S>;

// ============================================================================
//  Events — inputs to the state machine
//
//  Now includes fingerprint events for biometric auth path.
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
struct FingerprintMatch {};
struct FingerprintNoMatch {};

using Events = type_list<KeyPress, Backspace, Submit, AuthSuccess, AuthFail,
                         Timeout, FingerprintMatch, FingerprintNoMatch>;
using Event  = std::variant<KeyPress, Backspace, Submit, AuthSuccess, AuthFail,
                            Timeout, FingerprintMatch, FingerprintNoMatch>;

template <typename E>
concept IsEvent = contains_v<Events, E>;

// ============================================================================
//  Effects — reified side effects (descriptions, not executions)
//
//  The core produces Effect values. The impure shell interprets them.
//  This is the free monad pattern: separate description from execution.
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
//  Transition — compile-time transition table via template specialization
// ============================================================================

template <IsState S, IsEvent E>
struct Transition {
    static constexpr bool valid = false;
};

template <typename S, typename E>
concept ValidTransition =
    IsState<std::decay_t<S>> &&
    IsEvent<std::decay_t<E>> &&
    Transition<std::decay_t<S>, std::decay_t<E>>::valid;

template <IsState From, IsEvent On, IsState To>
struct rule {
    using from  = From;
    using event = On;
    using to    = To;
};

// ============================================================================
//  Transition implementations — password auth path
// ============================================================================

// Idle + KeyPress --> Typing
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

// Typing + KeyPress --> Typing
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

// Typing + Backspace --> Typing | Idle
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

// Typing + Submit --> Authenticating (effect: StartAuth)
template <>
struct Transition<Typing, Submit> {
    static constexpr bool valid = true;
    static auto apply(const Typing& t, const Submit&) -> TransitionResult {
        return {Authenticating{t.buffer}, StartAuth{t.buffer}};
    }
};

// Authenticating + AuthSuccess --> Unlocked (effect: ExitProgram)
template <>
struct Transition<Authenticating, AuthSuccess> {
    static constexpr bool valid = true;
    static auto apply(const Authenticating&, const AuthSuccess&) -> TransitionResult {
        return {Unlocked{}, ExitProgram{}};
    }
};

// Authenticating + AuthFail --> AuthError
template <>
struct Transition<Authenticating, AuthFail> {
    static constexpr bool valid = true;
    static auto apply(const Authenticating&, const AuthFail& e) -> TransitionResult {
        return {AuthError{e.reason}, NoEffect{}};
    }
};

// AuthError + KeyPress --> Typing (start over)
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

// AuthError + Timeout --> Idle
template <>
struct Transition<AuthError, Timeout> {
    static constexpr bool valid = true;
    static auto apply(const AuthError&, const Timeout&) -> TransitionResult {
        return {Idle{}, NoEffect{}};
    }
};

// ============================================================================
//  Transition implementations — fingerprint auth path
//
//  Fingerprint is a parallel authentication channel. A successful scan
//  from any non-terminal state transitions directly to Unlocked.
//  A failed scan shows an error without losing typed password.
// ============================================================================

// Idle + FingerprintMatch --> Unlocked (effect: ExitProgram)
template <>
struct Transition<Idle, FingerprintMatch> {
    static constexpr bool valid = true;
    static auto apply(const Idle&, const FingerprintMatch&) -> TransitionResult {
        return {Unlocked{}, ExitProgram{}};
    }
};

// Typing + FingerprintMatch --> Unlocked (effect: ExitProgram)
template <>
struct Transition<Typing, FingerprintMatch> {
    static constexpr bool valid = true;
    static auto apply(const Typing&, const FingerprintMatch&) -> TransitionResult {
        return {Unlocked{}, ExitProgram{}};
    }
};

// AuthError + FingerprintMatch --> Unlocked (effect: ExitProgram)
template <>
struct Transition<AuthError, FingerprintMatch> {
    static constexpr bool valid = true;
    static auto apply(const AuthError&, const FingerprintMatch&) -> TransitionResult {
        return {Unlocked{}, ExitProgram{}};
    }
};

// Idle + FingerprintNoMatch --> AuthError
template <>
struct Transition<Idle, FingerprintNoMatch> {
    static constexpr bool valid = true;
    static auto apply(const Idle&, const FingerprintNoMatch&) -> TransitionResult {
        return {AuthError{"Fingerprint not recognized"}, NoEffect{}};
    }
};

// AuthError + FingerprintNoMatch --> AuthError (update message)
template <>
struct Transition<AuthError, FingerprintNoMatch> {
    static constexpr bool valid = true;
    static auto apply(const AuthError&, const FingerprintNoMatch&) -> TransitionResult {
        return {AuthError{"Fingerprint not recognized"}, NoEffect{}};
    }
};

// ============================================================================
//  Declarative transition table — the complete state diagram as a type
//
//      Idle ----KeyPress-----------> Typing
//      Idle ----FingerprintMatch---> Unlocked         [biometric]
//      Idle ----FingerprintNoMatch-> AuthError         [biometric]
//      Typing --KeyPress-----------> Typing
//      Typing --Backspace----------> Typing (or Idle)
//      Typing --Submit-------------> Authenticating
//      Typing --FingerprintMatch---> Unlocked          [biometric]
//      Auth   --AuthSuccess--------> Unlocked          [terminal]
//      Auth   --AuthFail-----------> AuthError
//      Error  --KeyPress-----------> Typing
//      Error  --Timeout------------> Idle
//      Error  --FingerprintMatch---> Unlocked          [biometric]
//      Error  --FingerprintNoMatch-> AuthError         [biometric]
// ============================================================================

using TransitionTable = type_list<
    rule<Idle,           KeyPress,           Typing>,
    rule<Idle,           FingerprintMatch,   Unlocked>,
    rule<Idle,           FingerprintNoMatch, AuthError>,
    rule<Typing,         KeyPress,           Typing>,
    rule<Typing,         Backspace,          Typing>,
    rule<Typing,         Submit,             Authenticating>,
    rule<Typing,         FingerprintMatch,   Unlocked>,
    rule<Authenticating, AuthSuccess,        Unlocked>,
    rule<Authenticating, AuthFail,           AuthError>,
    rule<AuthError,      KeyPress,           Typing>,
    rule<AuthError,      Timeout,            Idle>,
    rule<AuthError,      FingerprintMatch,   Unlocked>,
    rule<AuthError,      FingerprintNoMatch, AuthError>
>;

// ============================================================================
//  Compile-time graph analysis
// ============================================================================

template <typename Table, typename S>
struct outgoing_count;

template <typename S>
struct outgoing_count<type_list<>, S> {
    static constexpr std::size_t value = 0;
};

template <typename S, typename R, typename... Rs>
struct outgoing_count<type_list<R, Rs...>, S> {
    static constexpr std::size_t value =
        (std::is_same_v<typename R::from, S> ? 1 : 0) +
        outgoing_count<type_list<Rs...>, S>::value;
};

template <typename Table, typename S>
constexpr std::size_t outgoing_v = outgoing_count<Table, S>::value;

template <typename Table, typename S>
struct incoming_count;

template <typename S>
struct incoming_count<type_list<>, S> {
    static constexpr std::size_t value = 0;
};

template <typename S, typename R, typename... Rs>
struct incoming_count<type_list<R, Rs...>, S> {
    static constexpr std::size_t value =
        (std::is_same_v<typename R::to, S> ? 1 : 0) +
        incoming_count<type_list<Rs...>, S>::value;
};

template <typename Table, typename S>
constexpr std::size_t incoming_v = incoming_count<Table, S>::value;

template <typename S>
concept TerminalState = IsState<S> && (outgoing_v<TransitionTable, S> == 0);

template <typename S>
concept InitialState = IsState<S> && std::is_same_v<S, Idle>;

template <typename Table>
struct table_matches_specializations;

template <>
struct table_matches_specializations<type_list<>> : std::true_type {};

template <typename R, typename... Rs>
struct table_matches_specializations<type_list<R, Rs...>> {
    static constexpr bool value =
        Transition<typename R::from, typename R::event>::valid &&
        table_matches_specializations<type_list<Rs...>>::value;
};

template <typename Table, typename R>
struct count_matching_rules;

template <typename R>
struct count_matching_rules<type_list<>, R> {
    static constexpr std::size_t value = 0;
};

template <typename R, typename R2, typename... Rs>
struct count_matching_rules<type_list<R2, Rs...>, R> {
    static constexpr bool same_trigger =
        std::is_same_v<typename R::from, typename R2::from> &&
        std::is_same_v<typename R::event, typename R2::event>;
    static constexpr std::size_t value =
        (same_trigger ? 1 : 0) + count_matching_rules<type_list<Rs...>, R>::value;
};

template <typename Table>
struct is_deterministic;

template <>
struct is_deterministic<type_list<>> : std::true_type {};

template <typename R, typename... Rs>
struct is_deterministic<type_list<R, Rs...>> {
    static constexpr bool value =
        (count_matching_rules<type_list<R, Rs...>, R>::value == 1) &&
        is_deterministic<type_list<Rs...>>::value;
};

// ============================================================================
//  Compile-time proofs
// ============================================================================

static_assert(table_matches_specializations<TransitionTable>::value,
    "transition table has a rule with no matching Transition<> specialization");

static_assert(is_deterministic<TransitionTable>::value,
    "transition table is non-deterministic: duplicate (State, Event) pair");

static_assert(TerminalState<Unlocked>, "Unlocked must be terminal");
static_assert(!TerminalState<Idle>,    "Idle must not be terminal");
static_assert(!TerminalState<Typing>,  "Typing must not be terminal");

static_assert(InitialState<Idle>, "Idle must be the initial state");

// Edge counts — includes fingerprint paths
static_assert(outgoing_v<TransitionTable, Idle>           == 3);  // KeyPress, FP+, FP-
static_assert(outgoing_v<TransitionTable, Typing>         == 4);  // Key, BS, Submit, FP+
static_assert(outgoing_v<TransitionTable, Authenticating> == 2);  // Success, Fail
static_assert(outgoing_v<TransitionTable, AuthError>      == 4);  // Key, Timeout, FP+, FP-
static_assert(outgoing_v<TransitionTable, Unlocked>       == 0);

static_assert(TransitionTable::size == 13, "expected exactly 13 transition rules");

// Forbidden transitions
static_assert(!Transition<Idle, Submit>::valid,              "can't submit empty");
static_assert(!Transition<Idle, Backspace>::valid,           "nothing to delete");
static_assert(!Transition<Unlocked, KeyPress>::valid,        "terminal: no input");
static_assert(!Transition<Unlocked, FingerprintMatch>::valid,"terminal: no input");
static_assert(!Transition<Authenticating, KeyPress>::valid,  "locked during auth");
static_assert(!Transition<Typing, FingerprintNoMatch>::valid,"don't lose typed password");

// ============================================================================
//  Runtime dispatch
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
//  ViewModel — pure projection: (State, ViewContext) -> ViewModel
// ============================================================================

struct ViewContext {
    const char* time_text            = "";
    const char* date_text            = "";
    bool        fingerprint_listening = false;
};

struct ViewModel {
    std::string status_text;
    std::string input_display;
    std::size_t input_length   = 0;
    bool        show_error     = false;
    std::string error_text;
    const char* time_text      = "";
    const char* date_text      = "";
    bool        fingerprint_listening = false;
};

inline auto view(const State& state, const ViewContext& ctx = {}) -> ViewModel {
    auto vm = std::visit(
        overloaded{
            [](const Idle&) -> ViewModel {
                return {"Enter password", "", 0, false, "", "", "", false};
            },
            [](const Typing& s) -> ViewModel {
                return {"Enter password", std::string(s.buffer.size(), '*'),
                        s.buffer.size(), false, "", "", "", false};
            },
            [](const Authenticating&) -> ViewModel {
                return {"Authenticating...", "", 0, false, "", "", "", false};
            },
            [](const Unlocked&) -> ViewModel {
                return {"", "", 0, false, "", "", "", false};
            },
            [](const AuthError& s) -> ViewModel {
                return {"Enter password", "", 0, true, s.message, "", "", false};
            },
        },
        state);

    vm.time_text             = ctx.time_text;
    vm.date_text             = ctx.date_text;
    vm.fingerprint_listening = ctx.fingerprint_listening;
    return vm;
}

}  // namespace typelock
