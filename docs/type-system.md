# Type-Theoretic C++ in Typelock

This document explains every type-theoretic pattern used in typelock and why it matters. The ideas come from programming language theory — dependent types, the Curry-Howard correspondence, algebraic data types — but are implemented in standard C++23 with no external libraries.

## The Core Idea

In most C++ programs, correctness is a runtime property. You write tests, add assertions, and hope nothing slips through. Type-theoretic programming inverts this: **correctness is a compile-time property**. The type system becomes a proof system, and if your program compiles, entire categories of bugs are impossible.

Typelock applies this to a lock screen state machine. The compiler proves:
- Only valid state transitions can be coded
- The machine is deterministic
- Terminal and initial states are structurally correct
- The transition table is consistent with its implementations

None of this requires runtime checks. The proofs are erased at compile time — zero cost.

---

## 1. States as Types (Typestate Pattern)

### The Problem with Enums

A typical state machine uses an enum:

```cpp
enum class State { Idle, Typing, Authenticating, Unlocked, Error };

struct Machine {
    State state;
    std::string buffer;   // only valid when Typing
    std::string password; // only valid when Authenticating
    std::string error;    // only valid when Error
};
```

This is a **product type**: every `Machine` carries all fields regardless of state. A `Machine` in `Idle` state still has a `buffer` field — it's just meaningless. Nothing prevents you from reading `password` while in `Typing` state. The type system can't help you.

### The Solution: Sum Types

Typelock uses **sum types** (tagged unions). Each state is its own type carrying exactly the data valid in that state:

```cpp
struct Idle {};                          // no data needed
struct Typing { std::string buffer; };   // buffer exists iff Typing
struct Authenticating { std::string password; };
struct Unlocked {};                      // terminal, no data
struct AuthError { std::string message; };
```

The state is their discriminated union:

```cpp
using State = std::variant<Idle, Typing, Authenticating, Unlocked, AuthError>;
```

Now **invalid states are unrepresentable**:
- There is no `Idle` with a buffer. The type doesn't have that field.
- There is no `Authenticating` without a password. The constructor requires one.
- You cannot access `.buffer` on an `Unlocked` value. It doesn't exist.

This is the same idea as Rust's enums, Haskell's ADTs, or ML's datatypes — but in C++ using `std::variant`.

### Connection to Type Theory

In type theory, a sum type `A + B` is a type whose values are *either* an `A` or a `B`, never both. This is the **coproduct** in category theory. `std::variant<A, B, C>` is the C++ encoding of `A + B + C`.

A product type `A × B` carries both an `A` and a `B`. A `struct { A a; B b; }` is a product. The enum approach creates a giant product type where most fields are meaningless — the type is too large. The variant approach creates a sum type where each alternative carries exactly what it needs — the type is precise.

**Precision** means the set of values the type permits is as close as possible to the set of values that are actually valid. Narrowing this gap is the entire point.

---

## 2. Transitions as Template Specializations

### The Pattern

Each valid transition is a template specialization:

```cpp
// Primary template: no transition exists (the default)
template <IsState S, IsEvent E>
struct Transition {
    static constexpr bool valid = false;
};

// Specialization: Idle + KeyPress --> Typing
template <>
struct Transition<Idle, KeyPress> {
    static constexpr bool valid = true;
    static auto apply(const Idle&, const KeyPress& e) -> TransitionResult;
};
```

The **primary template** says "by default, no transition exists." Each specialization **opts in** to a specific `(State, Event)` pair. If you try to use a transition that hasn't been specialized, the compiler sees `valid = false`.

### Why This Works

Template specialization is **pattern matching on types**. The compiler selects the most specific template for the given type arguments. This is analogous to Haskell's type class instances or Rust's trait impls — you're defining behavior indexed by types.

The key property: **omission is denial**. You don't write `Transition<Unlocked, KeyPress>` → it doesn't exist → the compiler rejects it. You never need to write "this transition is forbidden." The absence of the specialization *is* the proof that it's forbidden.

### The ValidTransition Concept

```cpp
template <typename S, typename E>
concept ValidTransition =
    IsState<std::decay_t<S>> &&
    IsEvent<std::decay_t<E>> &&
    Transition<std::decay_t<S>, std::decay_t<E>>::valid;
```

This C++20 concept wraps the check into a constraint. Any function using `requires ValidTransition<S, E>` will fail to compile if the transition doesn't exist. The error message will say the constraint wasn't satisfied — much clearer than a template instantiation failure deep in implementation code.

---

## 3. Type Lists and Compile-Time Computation

### Type List

```cpp
template <typename... Ts>
struct type_list {
    static constexpr std::size_t size = sizeof...(Ts);
};
```

A `type_list` is a compile-time container of types. It has no runtime representation — it exists purely in the type system. Think of it as a tuple where you only care about the types, not any values.

### Contains

```cpp
template <typename List, typename T>
struct contains;

template <typename T, typename... Ts>
struct contains<type_list<T, Ts...>, T> : std::true_type {};

template <typename T, typename U, typename... Ts>
struct contains<type_list<U, Ts...>, T> : contains<type_list<Ts...>, T> {};
```

This is **structural recursion** over a type list. The first specialization matches when the head of the list is `T` (base case: found). The second matches when the head is something else (recursive case: keep looking). The unspecialized base case inherits from `std::false_type` (not found).

This is the type-level equivalent of:

```haskell
contains [] _ = False
contains (x:xs) x = True
contains (_:xs) t = contains xs t
```

### Count If

```cpp
template <typename List, template <typename> class Pred>
struct count_if;

template <template <typename> class Pred, typename T, typename... Ts>
struct count_if<type_list<T, Ts...>, Pred> {
    static constexpr std::size_t value =
        (Pred<T>::value ? 1 : 0) + count_if<type_list<Ts...>, Pred>::value;
};
```

A **higher-kinded fold** — `Pred` is a template template parameter (a type-level function from types to booleans). This counts how many types in the list satisfy the predicate. Everything evaluates at compile time.

### For-Each Type

```cpp
template <typename List, typename F>
constexpr void for_each_type(F&& f) {
    []<typename... Ts>(type_list<Ts...>, F&& fn) {
        (fn.template operator()<Ts>(), ...);
    }(List{}, std::forward<F>(f));
}
```

This uses a C++20 **generic lambda with explicit template parameters** and a **fold expression** to call `f<T>()` for each type in the list. The lambda itself is immediately invoked — a compile-time iteration pattern.

---

## 4. Reified Transition Rules

### Rule as a Type

```cpp
template <IsState From, IsEvent On, IsState To>
struct rule {
    using from  = From;
    using event = On;
    using to    = To;
};
```

A `rule` is a **reified arrow** in the state diagram. It exists only as a type — no runtime cost. By collecting rules into a type list:

```cpp
using TransitionTable = type_list<
    rule<Idle,           KeyPress,    Typing>,
    rule<Typing,         KeyPress,    Typing>,
    rule<Typing,         Backspace,   Typing>,
    rule<Typing,         Submit,      Authenticating>,
    rule<Authenticating, AuthSuccess, Unlocked>,
    rule<Authenticating, AuthFail,    AuthError>,
    rule<AuthError,      KeyPress,    Typing>,
    rule<AuthError,      Timeout,     Idle>
>;
```

...you get a **declarative, type-level representation of the entire state diagram**. This is data that the compiler can analyze.

### Why Reify?

The `Transition<S, E>` specializations are *opaque* to the compiler — you can check if one exists, but you can't iterate over all of them. The `TransitionTable` makes the graph *transparent*: you can count edges, check properties, and prove theorems about the structure.

This is the difference between **intensional** and **extensional** definitions:
- Intensional: "a transition exists if a specialization is defined" (opaque)
- Extensional: "here is the complete list of transitions" (transparent)

Having both lets you prove that they agree (via `table_matches_specializations`).

---

## 5. Compile-Time Graph Analysis

### Outgoing/Incoming Edge Counts

```cpp
template <typename Table, typename S>
struct outgoing_count;

template <typename S, typename R, typename... Rs>
struct outgoing_count<type_list<R, Rs...>, S> {
    static constexpr std::size_t value =
        (std::is_same_v<typename R::from, S> ? 1 : 0) +
        outgoing_count<type_list<Rs...>, S>::value;
};
```

This recursively walks the transition table and counts rules whose `from` type matches `S`. The result is a `constexpr` value available at compile time. The same pattern works for incoming edges (matching `to`).

### Terminal and Initial State Concepts

```cpp
template <typename S>
concept TerminalState = IsState<S> && (outgoing_v<TransitionTable, S> == 0);
```

A terminal state is one with no outgoing edges. This is a **structural property** — it's derived from the graph, not declared by annotation. If you add a transition out of `Unlocked`, the `TerminalState<Unlocked>` assertion breaks. The proof tracks the code.

### Determinism Check

```cpp
template <typename Table>
struct is_deterministic;

template <typename R, typename... Rs>
struct is_deterministic<type_list<R, Rs...>> {
    static constexpr bool value =
        (count_matching_rules<type_list<R, Rs...>, R>::value == 1) &&
        is_deterministic<type_list<Rs...>>::value;
};
```

For each rule, count how many rules in the table share the same `(from, event)` pair. If any count exceeds 1, the machine is non-deterministic. This is a **quadratic compile-time check** (each rule checks against all others), but the table is small so it's instant.

### Table-Specialization Consistency

```cpp
template <typename R, typename... Rs>
struct table_matches_specializations<type_list<R, Rs...>> {
    static constexpr bool value =
        Transition<typename R::from, typename R::event>::valid &&
        table_matches_specializations<type_list<Rs...>>::value;
};
```

This proves that every `rule<A, B, C>` in the table has a corresponding `Transition<A, B>` specialization with `valid = true`. If you add a rule to the table but forget to implement the transition, compilation fails.

---

## 6. Concepts as Propositions (Curry-Howard)

The **Curry-Howard correspondence** says:
- Types are propositions
- Values are proofs
- A type is inhabited (has a value) iff the proposition is true

In C++, `static_assert` is the bridge:

```cpp
static_assert(TerminalState<Unlocked>, "Unlocked must be terminal");
```

This says: "the proposition `TerminalState<Unlocked>` is true." If the concept's constraints aren't satisfied, compilation fails — the proof doesn't go through.

C++20 concepts are the closest C++ gets to propositions:

```cpp
template <typename S>
concept TerminalState = IsState<S> && (outgoing_v<TransitionTable, S> == 0);
```

This reads: "S is a TerminalState if S is a state AND S has zero outgoing edges." It's a predicate on types, checked at compile time.

The `static_assert` block in typelock is essentially a **proof script**:

```cpp
// Structural proofs
static_assert(is_deterministic<TransitionTable>::value);
static_assert(table_matches_specializations<TransitionTable>::value);
static_assert(TerminalState<Unlocked>);
static_assert(TransitionTable::size == 8);

// Forbidden transitions
static_assert(!Transition<Idle, Submit>::valid);
static_assert(!Transition<Unlocked, KeyPress>::valid);
```

Each line is a theorem. If the code compiles, all theorems hold.

---

## 7. The Overloaded Visitor

```cpp
template <typename... Fs>
struct overloaded : Fs... {
    using Fs::operator()...;
};
```

This combines multiple lambdas into one callable using **multiple inheritance** and a **using declaration pack expansion** (C++17). It enables clean exhaustive pattern matching on variants:

```cpp
std::visit(overloaded{
    [](const Idle&)           -> ViewModel { ... },
    [](const Typing& s)      -> ViewModel { ... },
    [](const Authenticating&) -> ViewModel { ... },
    [](const Unlocked&)       -> ViewModel { ... },
    [](const AuthError& s)   -> ViewModel { ... },
}, state);
```

If you forget a case, the compiler errors — exhaustiveness is enforced. This is the C++ equivalent of Haskell's pattern matching or Rust's `match`.

In C++23, CTAD (class template argument deduction) automatically deduces the template arguments from the lambda types, so no deduction guide is needed.

---

## 8. if constexpr and Compile-Time Dispatch

The runtime dispatch function uses `if constexpr` to generate only the valid paths:

```cpp
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
```

`std::visit` with a generic lambda instantiates the lambda for **every combination** of `State × Event` alternatives (5 states × 6 events = 30 instantiations). The `if constexpr` prunes at compile time: only the 8 valid transitions generate `apply()` calls. The other 22 instantiations compile to a simple copy of the current state.

This is the boundary between the type-level world (where transitions are types) and the value-level world (where the state is a `variant`). Inside `if constexpr (ValidTransition<S, E>)`, the compiler knows the exact types — you're back in the pure, compile-time-validated core.

---

## Summary of Patterns Used

| Pattern | C++ Feature | Type Theory Concept |
|---|---|---|
| States as types | `struct` per state | Sum types / coproducts |
| State variant | `std::variant` | Tagged union / ADT |
| Transition table | Template specialization | Type-indexed functions |
| Valid transition check | C++20 concepts | Propositions / predicates |
| Type list | Variadic templates | Compile-time sequences |
| Graph analysis | Recursive templates | Structural recursion |
| Compile-time proofs | `static_assert` | Curry-Howard |
| Exhaustive matching | `std::visit` + `overloaded` | Pattern matching |
| Branch pruning | `if constexpr` | Compile-time evaluation |
| Reified rules | `rule<From, Event, To>` | Type-level data |
| Effect descriptions | `std::variant<NoEffect, ...>` | Free monad (simplified) |
