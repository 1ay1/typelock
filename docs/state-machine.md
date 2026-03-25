# State Machine: Compile-Time Verified

This document details the state machine at the heart of typelock — its states, transitions, and the compile-time proofs that verify its correctness.

## State Diagram

```
                    ┌─────────────────────────────┐
                    │                             │
                    ▼          KeyPress           │
              ┌──────────┐ ──────────────▶ ┌──────────┐
              │          │                 │          │──┐
  ────────▶   │   Idle   │                 │  Typing  │  │ KeyPress
   (start)    │          │ ◀────────────── │          │◀─┘ Backspace
              └──────────┘   Backspace     └──────────┘
                    ▲        (empty)            │
                    │                           │ Submit
                    │                           ▼
              ┌──────────┐              ┌────────────────┐
              │          │   AuthFail   │                │
              │  Auth    │ ◀─────────── │ Authenticating │
              │  Error   │              │                │
              └──────────┘              └────────────────┘
                    │                           │
                    │ KeyPress                  │ AuthSuccess
                    │                           ▼
                    │                    ┌──────────┐
                    └───────────────────▶│          │
                          (to Typing)    │ Unlocked │
                                         │          │
                                         └──────────┘
                                          (terminal)
```

## States

| State | Data Carried | Meaning |
|---|---|---|
| `Idle` | none | Screen locked, waiting for first keypress |
| `Typing` | `buffer: string` | User is entering password. Buffer is non-empty (invariant enforced by transitions) |
| `Authenticating` | `password: string` | PAM is checking the password. Non-empty (comes from Typing) |
| `Unlocked` | none | Authentication succeeded. Terminal state — no outgoing transitions |
| `AuthError` | `message: string` | Authentication failed. Shows error, waits for new input or timeout |

### Data Invariants

These invariants aren't checked at runtime — they're **enforced by construction**:

- **Typing::buffer is non-empty**: The only way to enter `Typing` is via `KeyPress`, which adds a character. The only way `Backspace` produces `Idle` is when `buffer.size() <= 1`, so `Typing` is never constructed with an empty buffer.

- **Authenticating::password is non-empty**: The only way to enter `Authenticating` is via `Submit` from `Typing`, which requires a non-empty buffer. The password is copied from `Typing::buffer`.

- **Unlocked has no data**: Once unlocked, the password is gone. It was consumed by the `Authenticating → Unlocked` transition and never stored.

---

## Events

| Event | Data | Source |
|---|---|---|
| `KeyPress` | `codepoint: char32_t` | Keyboard input (printable character) |
| `Backspace` | none | Keyboard input |
| `Submit` | none | Enter key pressed |
| `AuthSuccess` | none | PAM thread reports success |
| `AuthFail` | `reason: string` | PAM thread reports failure |
| `Timeout` | none | Error display timer / Escape key |

---

## Transitions

### Complete Transition Table

| From | Event | To | Effect | Notes |
|---|---|---|---|---|
| Idle | KeyPress | Typing | none | Start entering password |
| Typing | KeyPress | Typing | none | Append character to buffer |
| Typing | Backspace | Typing or Idle | none | Remove last char; Idle if buffer empties |
| Typing | Submit | Authenticating | StartAuth | Send password to PAM |
| Authenticating | AuthSuccess | Unlocked | ExitProgram | Unlock the session |
| Authenticating | AuthFail | AuthError | none | Show error message |
| AuthError | KeyPress | Typing | none | Start over with new password |
| AuthError | Timeout | Idle | none | Clear error, return to idle |

### Forbidden Transitions (compile-time rejected)

These are **not coded anywhere**. They simply don't exist as template specializations, so any attempt to use them is a type error:

| From | Event | Why Forbidden |
|---|---|---|
| Idle | Submit | Nothing typed yet |
| Idle | Backspace | Nothing to delete |
| Idle | AuthSuccess/Fail | Not authenticating |
| Typing | AuthSuccess/Fail | Not authenticating |
| Authenticating | KeyPress | Can't type during auth |
| Authenticating | Submit | Already submitted |
| Authenticating | Backspace | Can't edit during auth |
| Unlocked | (anything) | Terminal state — no exit |
| AuthError | Submit | Nothing typed yet |
| AuthError | Backspace | Nothing to delete |

---

## Effects

Effects are **values**, not actions. The core describes what should happen; the shell decides how.

```cpp
struct NoEffect {};                        // nothing to do
struct StartAuth { std::string password; }; // kick off PAM
struct ExitProgram {};                     // unlock and exit
```

Only two transitions produce effects:
1. `Typing + Submit → Authenticating` produces `StartAuth`
2. `Authenticating + AuthSuccess → Unlocked` produces `ExitProgram`

All other transitions produce `NoEffect`.

---

## Compile-Time Proofs

The following properties are verified by `static_assert` at compile time. If any fail, the program does not compile.

### 1. Table-Specialization Consistency

```cpp
static_assert(table_matches_specializations<TransitionTable>::value);
```

**Theorem**: Every `rule<A, B, C>` in the `TransitionTable` has a corresponding `Transition<A, B>` specialization with `valid = true`.

**Why it matters**: The `TransitionTable` (used for graph analysis) and the `Transition<>` specializations (used for runtime dispatch) could diverge. This proof ensures they agree. If you add a rule to the table but forget the implementation, or implement a transition not in the table, compilation fails.

### 2. Determinism

```cpp
static_assert(is_deterministic<TransitionTable>::value);
```

**Theorem**: No two rules in the table share the same `(from, event)` pair.

**Why it matters**: A non-deterministic state machine would mean the same input in the same state could go to different states. For a lock screen, this would be a security bug — the behavior must be predictable.

### 3. Terminal State

```cpp
static_assert(TerminalState<Unlocked>);
static_assert(!TerminalState<Idle>);
static_assert(!TerminalState<Typing>);
```

**Theorem**: `Unlocked` has zero outgoing transitions. All other states have at least one.

**Why it matters**: `Unlocked` being terminal means once you're unlocked, you're unlocked — there's no transition back to locked. The other states being non-terminal means you're never stuck (there's always a way forward).

### 4. Edge Counts

```cpp
static_assert(outgoing_v<TransitionTable, Idle>           == 1);
static_assert(outgoing_v<TransitionTable, Typing>         == 3);
static_assert(outgoing_v<TransitionTable, Authenticating> == 2);
static_assert(outgoing_v<TransitionTable, AuthError>      == 2);
static_assert(outgoing_v<TransitionTable, Unlocked>       == 0);
static_assert(TransitionTable::size == 8);
```

**Theorem**: Each state has exactly the expected number of outgoing transitions, and the table has exactly 8 rules.

**Why it matters**: These are **regression proofs**. If someone adds or removes a transition, these assertions catch it immediately. The programmer is forced to update the proofs, acknowledging the change was intentional.

### 5. Forbidden Transitions

```cpp
static_assert(!Transition<Idle, Submit>::valid);
static_assert(!Transition<Idle, Backspace>::valid);
static_assert(!Transition<Unlocked, KeyPress>::valid);
static_assert(!Transition<Authenticating, KeyPress>::valid);
```

**Theorem**: These specific transitions do not exist.

**Why it matters**: These are the **security-critical** forbidden transitions. Submitting with no password, typing while authenticating, or accepting input after unlocking would all be bugs. The assertions make the intent explicit and guard against accidental introduction.

---

## How to Add a New Transition

1. Add the `Transition<>` specialization:

```cpp
template <>
struct Transition<AuthError, Backspace> {
    static constexpr bool valid = true;
    static auto apply(const AuthError&, const Backspace&) -> TransitionResult {
        return {Idle{}, NoEffect{}};
    }
};
```

2. Add the rule to `TransitionTable`:

```cpp
using TransitionTable = type_list<
    // ... existing rules ...
    rule<AuthError, Backspace, Idle>   // new
>;
```

3. Update the compile-time proofs:

```cpp
static_assert(outgoing_v<TransitionTable, AuthError> == 3);  // was 2
static_assert(TransitionTable::size == 9);                    // was 8
```

If you forget step 2, `table_matches_specializations` won't catch it (it only checks table → specialization, not the reverse). But the edge count assertions will fail, forcing you to reconcile.

If you forget step 1, `table_matches_specializations` will fail immediately.

If you forget step 3, the exact count assertions fail, telling you exactly which number changed.

The proofs form a **safety net** that forces you to think about every change to the state machine.

---

## How to Add a New State

1. Define the state type with its data
2. Add it to `States` type list and `State` variant
3. Add transitions (specializations + rules)
4. Add a `view()` case in the `overloaded` visitor
5. Update all `static_assert` proofs

The `overloaded` visitor in `view()` will give a compile error if you add a variant alternative but don't handle it — the visitor must be exhaustive. This is your second safety net.
