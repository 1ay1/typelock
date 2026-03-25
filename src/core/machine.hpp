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

// Type list — the fundamental compile-time container
template <typename... Ts>
struct type_list {
    static constexpr std::size_t size = sizeof...(Ts);
};

// Check if a type list contains a type
template <typename List, typename T>
struct contains : std::false_type {};

template <typename T, typename... Ts>
struct contains<type_list<T, Ts...>, T> : std::true_type {};

template <typename T, typename U, typename... Ts>
struct contains<type_list<U, Ts...>, T> : contains<type_list<Ts...>, T> {};

template <typename List, typename T>
constexpr bool contains_v = contains<List, T>::value;

// Apply a predicate to each element and count how many match
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

// For-each at compile time — fold over a type list
template <typename List, typename F>
constexpr void for_each_type(F&& f) {
    []<typename... Ts>(type_list<Ts...>, F&& fn) {
        (fn.template operator()<Ts>(), ...);
    }(List{}, std::forward<F>(f));
}

// Overloaded visitor — clean std::visit syntax
template <typename... Fs>
struct overloaded : Fs... {
    using Fs::operator()...;
};

// ============================================================================
//  States — each type is a proposition; its values are proofs
//
//  Idle            — the screen is locked, no input yet
//  Typing          — user is entering a password (buffer is non-empty)
//  Authenticating  — PAM is checking the password (password is non-empty)
//  Unlocked        — authentication succeeded (terminal state)
//  AuthError       — authentication failed, showing error message
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

using Events = type_list<KeyPress, Backspace, Submit, AuthSuccess, AuthFail, Timeout>;
using Event  = std::variant<KeyPress, Backspace, Submit, AuthSuccess, AuthFail, Timeout>;

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
//
//  The primary template defines the "no transition" case.
//  Each specialization is an arrow in the state diagram:
//
//      Transition<S, E>::valid   = true   iff  S --E--> _ exists
//      Transition<S, E>::apply() = the transition function
//
//  An undefined transition is a type error at compile time when accessed
//  through the ValidTransition concept.
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

// ============================================================================
//  Transition rule — a reified type-level arrow: From --Event--> To
//
//  Used to build a declarative transition table as a type list, enabling
//  compile-time graph analysis (determinism, reachability, terminal states).
// ============================================================================

template <IsState From, IsEvent On, IsState To>
struct rule {
    using from  = From;
    using event = On;
    using to    = To;
};

// ============================================================================
//  Transition implementations
// ============================================================================

// --- Idle + KeyPress --> Typing ---
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

// --- Typing + KeyPress --> Typing ---
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

// --- Typing + Backspace --> Typing | Idle ---
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

// --- Typing + Submit --> Authenticating  (effect: StartAuth) ---
template <>
struct Transition<Typing, Submit> {
    static constexpr bool valid = true;
    static auto apply(const Typing& t, const Submit&) -> TransitionResult {
        return {Authenticating{t.buffer}, StartAuth{t.buffer}};
    }
};

// --- Authenticating + AuthSuccess --> Unlocked  (effect: ExitProgram) ---
template <>
struct Transition<Authenticating, AuthSuccess> {
    static constexpr bool valid = true;
    static auto apply(const Authenticating&, const AuthSuccess&) -> TransitionResult {
        return {Unlocked{}, ExitProgram{}};
    }
};

// --- Authenticating + AuthFail --> AuthError ---
template <>
struct Transition<Authenticating, AuthFail> {
    static constexpr bool valid = true;
    static auto apply(const Authenticating&, const AuthFail& e) -> TransitionResult {
        return {AuthError{e.reason}, NoEffect{}};
    }
};

// --- AuthError + KeyPress --> Typing (start over) ---
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

// --- AuthError + Timeout --> Idle ---
template <>
struct Transition<AuthError, Timeout> {
    static constexpr bool valid = true;
    static auto apply(const AuthError&, const Timeout&) -> TransitionResult {
        return {Idle{}, NoEffect{}};
    }
};

// ============================================================================
//  Declarative transition table — the state diagram as a type
//
//      Idle ----KeyPress----> Typing
//      Typing --KeyPress----> Typing
//      Typing --Backspace---> Typing (or Idle at runtime)
//      Typing --Submit------> Authenticating
//      Auth   --AuthSuccess-> Unlocked       [terminal]
//      Auth   --AuthFail----> AuthError
//      Error  --KeyPress----> Typing
//      Error  --Timeout-----> Idle
// ============================================================================

using TransitionTable = type_list<
    rule<Idle,           KeyPress,    Typing>,
    rule<Typing,         KeyPress,    Typing>,
    rule<Typing,         Backspace,   Typing>,       // or Idle — runtime decision
    rule<Typing,         Submit,      Authenticating>,
    rule<Authenticating, AuthSuccess, Unlocked>,
    rule<Authenticating, AuthFail,    AuthError>,
    rule<AuthError,      KeyPress,    Typing>,
    rule<AuthError,      Timeout,     Idle>
>;

// ============================================================================
//  Compile-time graph analysis — the compiler is our theorem prover
// ============================================================================

// --- Count outgoing edges from a state ---
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

// --- Count incoming edges to a state ---
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

// --- Terminal state: no outgoing edges ---
template <typename S>
concept TerminalState = IsState<S> && (outgoing_v<TransitionTable, S> == 0);

// --- Source state: can be entered only from itself or the start ---
template <typename S>
concept InitialState = IsState<S> && std::is_same_v<S, Idle>;

// --- Check that the table matches the specializations ---
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

// --- Determinism: no two rules share (From, Event) ---
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
//  Compile-time proofs — these are theorems about our state machine
// ============================================================================

// The transition table is consistent with the specializations
static_assert(table_matches_specializations<TransitionTable>::value,
    "transition table has a rule with no matching Transition<> specialization");

// The machine is deterministic: each (State, Event) pair has at most one rule
static_assert(is_deterministic<TransitionTable>::value,
    "transition table is non-deterministic: duplicate (State, Event) pair");

// Unlocked is the only terminal state (no outgoing transitions)
static_assert(TerminalState<Unlocked>, "Unlocked must be terminal");
static_assert(!TerminalState<Idle>,    "Idle must not be terminal");
static_assert(!TerminalState<Typing>,  "Typing must not be terminal");

// Idle is the only initial state (no incoming transitions from the table)
static_assert(InitialState<Idle>, "Idle must be the initial state");
static_assert(!InitialState<Typing>, "Typing must not be initial");

// Exact edge counts — change a transition, break a proof
static_assert(outgoing_v<TransitionTable, Idle>           == 1);
static_assert(outgoing_v<TransitionTable, Typing>         == 3);
static_assert(outgoing_v<TransitionTable, Authenticating> == 2);
static_assert(outgoing_v<TransitionTable, AuthError>      == 2);
static_assert(outgoing_v<TransitionTable, Unlocked>       == 0);

static_assert(TransitionTable::size == 8, "expected exactly 8 transition rules");

// Forbidden transitions — the compiler rejects these at the type level
static_assert(!Transition<Idle, Submit>::valid,            "can't submit with nothing typed");
static_assert(!Transition<Idle, Backspace>::valid,         "nothing to delete");
static_assert(!Transition<Unlocked, KeyPress>::valid,      "terminal state accepts no input");
static_assert(!Transition<Authenticating, KeyPress>::valid, "can't type while authenticating");

// ============================================================================
//  Runtime dispatch — the impure boundary
//
//  Inside: compile-time validated, total, pure transitions.
//  Outside: std::variant erases the state type for runtime polymorphism.
//  The if-constexpr ensures only valid transitions generate code.
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
//  ViewModel — pure projection:  State -> ViewModel
//
//  The renderer never sees raw state. This is a natural transformation
//  from the State functor to the ViewModel functor — a morphism between
//  representations that preserves structure.
// ============================================================================

struct ViewModel {
    std::string status_text;
    std::string input_display;
    bool show_error  = false;
    std::string error_text;
};

inline auto view(const State& state) -> ViewModel {
    return std::visit(
        overloaded{
            [](const Idle&) -> ViewModel {
                return {"Enter password", "", false, ""};
            },
            [](const Typing& s) -> ViewModel {
                return {"Enter password", std::string(s.buffer.size(), '*'), false, ""};
            },
            [](const Authenticating&) -> ViewModel {
                return {"Authenticating...", "", false, ""};
            },
            [](const Unlocked&) -> ViewModel {
                return {"", "", false, ""};
            },
            [](const AuthError& s) -> ViewModel {
                return {"Enter password", "", true, s.message};
            },
        },
        state);
}

}  // namespace typelock
