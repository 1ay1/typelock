# Testing: Compile-Time Proofs Meet Runtime Verification

Typelock's test suite validates both the runtime behavior of the state machine and the compile-time properties of the type system. The pure core has zero I/O dependencies, so every test is a fast, deterministic, in-process check — no mocks, no stubs, no Wayland, no PAM.

## Running Tests

```bash
make test
```

This compiles the test binary to `build/test_core` and runs it. The output shows each test with a colored PASS/FAIL indicator and a summary.

## Test Structure

```
tests/
├── harness.hpp        Shared macros: TEST, EXPECT, global counters
├── main.cpp           Entry point — prints summary after auto-registered tests run
├── test_machine.cpp   State machine transitions, ViewModel projection, integration flows
├── test_types.cpp     Strong<T,Tag>, Color, Easing, Shake, Config defaults, Geometry
└── test_proofs.cpp    Type-theoretic proofs: type_list, concepts, graph theory,
                       phantom types, constexpr algebra, variant exhaustiveness,
                       dispatch totality, Widget HKTs, Animation parameterization
```

Each test file is compiled as a separate translation unit and linked together. Tests auto-register via static constructors — there's no central test list to maintain.

## Test Harness

The framework is a single header with two macros:

```cpp
#define TEST(name)     // declares and auto-registers a test function
#define EXPECT(cond)   // assertion with file/line on failure
```

`TEST(name)` expands to a static constructor that runs the test at program startup and records pass/fail. By the time `main()` executes, all tests have already run. This is the simplest possible framework — no allocation, no virtual dispatch, no dependencies.

## Test Categories

### State Machine (`test_machine.cpp`)

Tests the `dispatch(State, Event) -> (State, Effect)` function — the heart of the pure core.

**Transitions** — every valid edge in the state diagram:

```cpp
TEST(idle_keypress_transitions_to_typing) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, KeyPress{'a'});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == "a");
    EXPECT(std::holds_alternative<NoEffect>(eff));
}
```

**No-ops** — every invalid `(State, Event)` pair produces the same state back with `NoEffect`:

```cpp
TEST(authenticating_keypress_is_noop) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, KeyPress{'a'});
    EXPECT(std::holds_alternative<Authenticating>(next));
}
```

**ViewModel projection** — `view(State) -> ViewModel` maps machine state to display-safe data:

```cpp
TEST(view_typing_masks_input) {
    State s = Typing{"hello"};
    auto vm = view(s);
    EXPECT(vm.input_display == "*****");
    EXPECT(vm.input_length == 5);
}
```

**Full flow integration** — multi-step sequences that exercise the complete lifecycle:

```cpp
TEST(full_password_flow) {
    State s = Idle{};
    auto [s1, e1] = dispatch(s,  KeyPress{'h'});
    auto [s2, e2] = dispatch(s1, KeyPress{'i'});
    auto [s3, e3] = dispatch(s2, Submit{});
    // ... fail, retry, succeed
    auto [s8, e8] = dispatch(s7, AuthSuccess{});
    EXPECT(std::holds_alternative<Unlocked>(s8));
    EXPECT(std::holds_alternative<ExitProgram>(e8));
}
```

### Types & Values (`test_types.cpp`)

Tests the value-level primitives — the types you use at runtime.

- **`Strong<T, Tag>`** (phantom types) — arithmetic, comparison, division, compound assignment
- **`Color`** — `hex()` parsing, `with_alpha()`, `rgba()`, float accessors, palette correctness
- **Easing functions** — boundary conditions, monotonicity, identity (Linear)
- **`shake_offset`** — fixed point at origin, amplitude damping
- **`Config`** — default values for all config sections
- **Geometry** — `Rect::center_x/y`, `Size::operator+`

### Type-Theoretic Proofs (`test_proofs.cpp`)

This is where it gets interesting. These tests verify properties of the type system itself — not just "does the code work" but "does the type-level structure have the right shape."

#### Type-Level Set Theory

```cpp
TEST(states_and_events_are_disjoint_universes) {
    // No type inhabits both States and Events
    bool disjoint = true;
    for_each_type<States>([&]<typename S>() {
        if (contains_v<Events, S>) disjoint = false;
    });
    for_each_type<Events>([&]<typename E>() {
        if (contains_v<States, E>) disjoint = false;
    });
    EXPECT(disjoint);
}
```

`type_list`, `contains_v`, `count_if_v`, and `for_each_type` are verified as a consistent type-level set library. Membership is decidable, cardinalities are correct, and the `States`/`Events` universes are provably disjoint.

#### Concept Classification

```cpp
TEST(concept_IsState_exactly_classifies_states) {
    EXPECT(IsState<Idle>);
    EXPECT(IsState<Typing>);
    EXPECT(!IsState<KeyPress>);  // events are not states
    EXPECT(!IsState<NoEffect>);  // effects are not states
}
```

`IsState`, `IsEvent`, and `Easing` are tested for both positive and negative membership — they classify exactly the types they should.

#### Transition Table Graph Theory

The transition table is a compile-time directed graph. Tests verify its structural properties:

| Property | What It Proves |
|---|---|
| `TransitionTable::size == 13` | Exactly 13 rules |
| `is_deterministic<TransitionTable>` | No duplicate `(State, Event)` pairs |
| `table_matches_specializations<TransitionTable>` | Every rule has a `Transition<>` impl |
| `valid_transitions_are_exactly_13` | Enumeration over `States x Events` confirms no hidden edges |
| `state_event_product_space_is_40` | 5 states x 8 events = 40 pairs; 13 valid + 27 forbidden |
| `unlocked_is_a_sink_node` | 0 outgoing, 4 incoming edges |
| `every_non_terminal_state_has_outgoing_edges` | No dead ends |
| `incoming_edge_counts_all_states` | Per-state incoming edge verification |

The forbidden transitions test enumerates all 27 invalid pairs explicitly — proving the guard is airtight:

```cpp
TEST(forbidden_transitions_form_a_complete_set) {
    EXPECT(!(Transition<Idle, Submit>::valid));        // can't submit empty
    EXPECT(!(Transition<Authenticating, KeyPress>::valid)); // locked during auth
    EXPECT(!(Transition<Unlocked, KeyPress>::valid));  // terminal: no input
    // ... all 27 forbidden pairs
}
```

#### Dispatch Totality

```cpp
TEST(dispatch_is_total_over_all_state_event_pairs) {
    // For every (State, Event) pair, dispatch returns a valid result
    for (const auto& s : all_states) {
        for (const auto& e : all_events) {
            auto [next, eff] = dispatch(s, e);
            bool valid = std::visit(
                [](const auto& st) { return IsState<std::decay_t<decltype(st)>>; },
                next);
            EXPECT(valid);
            EXPECT(!eff.valueless_by_exception());
        }
    }
}
```

This is a totality proof: `dispatch` is a total function over the full 40-element product space.

#### Phantom Type Proofs

```cpp
TEST(phantom_types_are_zero_cost) {
    EXPECT(sizeof(Px) == sizeof(float));      // no runtime overhead
    EXPECT(sizeof(Seconds) == sizeof(double));
}

TEST(strong_type_constexpr_arithmetic) {
    constexpr Px a{10.0f};
    constexpr Px b{20.0f};
    constexpr Px sum = a + b;   // evaluated at compile time
    EXPECT(sum.value == 30.0f);
}
```

`Strong<T, Tag>` is verified to be zero-cost (same size as the underlying type) and fully constexpr.

#### Easing Morphisms

Easing functions are morphisms in `[0,1] -> [0,1]`. Tests verify mathematical properties:

- **Boundary conditions**: `ease(0) == 0`, `ease(1) == 1` for all 5 easings
- **Duality**: `EaseIn(t) + EaseOut(1-t) == 1` — they're reflections across the unit diagonal
- **Symmetry**: `EaseInOut(0.5) ≈ 0.5` — symmetric sigmoid
- **Overshoot**: `EaseOutBack` provably exceeds 1.0 mid-curve, then settles exactly to 1.0
- **Identity**: `Linear::ease(t) == t` for all sampled points

#### Widget Higher-Kinded Types

```cpp
TEST(layout_combinators_preserve_widget_concept) {
    using namespace typelock::widget;
    EXPECT((Widget<VStack<Spacer<10>, Spacer<20>>>));
    EXPECT((Widget<Center<Spacer<10>>>));
    EXPECT((Widget<Padding<5, Spacer<10>>>));
    EXPECT((Widget<Center<VStack<Padding<5, Spacer<10>>, Spacer<20>>>>));
    EXPECT((Widget<DefaultLayout>));
}
```

`VStack`, `Center`, and `Padding` are higher-kinded types — they take `Widget` types as parameters and produce new `Widget` types. The test proves the concept is preserved through arbitrary nesting.

#### Animation Type-Level Parameterization

```cpp
TEST(animation_types_encode_easing_in_type) {
    EXPECT(!(std::is_same_v<FadeIn, PulseAnim>));         // different easings
    EXPECT((std::is_same_v<FadeIn, Animation<EaseOut>>));  // FadeIn IS Animation<EaseOut>
    EXPECT((std::is_same_v<FadeIn, ShakeAnim>));           // same easing = same type
}
```

The easing function is a template parameter — a type-level choice. `FadeIn` and `PulseAnim` are distinct types even though they share the `Animation<>` template.

## Design Decisions

### Why a Custom Framework?

The test suite has zero external dependencies — it compiles with just the C++ standard library. This matters because:

1. The core headers are dependency-free, and tests should stay that way
2. The auto-registration pattern is 10 lines of macro; pulling in GoogleTest or Catch2 for that would be absurd
3. The tests run in <50ms total — no framework overhead matters at this scale

### Why Prove Things at Compile Time AND Runtime?

Many properties in `test_proofs.cpp` are already enforced by `static_assert` in the headers. The tests verify them again at runtime for two reasons:

1. **Documentation**: The test names describe what the type system guarantees in plain English
2. **Defense in depth**: If someone accidentally removes a `static_assert`, the test still catches it
3. **Visibility**: `make test` shows all 123 properties in one output — a static_assert failure is a cryptic compiler error

### What's NOT Tested

The impure shell — Wayland, PAM, Cairo rendering — is not tested here. Those require a running compositor and authentication context. The architecture (see [architecture.md](architecture.md)) ensures the pure core handles all logic, so testing the shell is mostly integration/smoke testing.
