# Architecture: Pure Core / Impure Shell

Typelock follows a functional architecture where the program is split into two layers:

1. **Pure core** — the state machine logic, with no side effects
2. **Impure shell** — Wayland I/O, PAM authentication, rendering

This is sometimes called the "functional core, imperative shell" pattern, or the "onion architecture" in FP circles. The key insight: **push I/O to the edges, keep the center pure**.

```
┌──────────────────────────────────────────────────────┐
│                    Impure Shell                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
│  │ Wayland  │  │  Cairo   │  │  PAM (threaded)  │   │
│  │  Client  │  │ Renderer │  │  Authentication  │   │
│  └────┬─────┘  └────┬─────┘  └────────┬─────────┘   │
│       │              │                 │              │
│       │    Events    │   ViewModel     │  Auth Result │
│       ▼              ▲                 │              │
│  ┌────────────────────────────────────────────────┐  │
│  │              Effect Interpreter                 │  │
│  │         (executes described effects)            │  │
│  └─────────────────────┬──────────────────────────┘  │
│                        │                              │
│                        ▼                              │
│  ┌────────────────────────────────────────────────┐  │
│  │                 Pure Core                       │  │
│  │                                                 │  │
│  │   State ──Event──▶ dispatch() ──▶ (State, Effect) │
│  │   State ──────────▶ view()    ──▶ ViewModel     │  │
│  │                                                 │  │
│  │   compile-time: transition table, proofs        │  │
│  └─────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

## Why This Split Matters

### Testability

The pure core can be tested without any OS, display server, or PAM:

```cpp
State s = Idle{};
auto [next, eff] = dispatch(s, KeyPress{'a'});
assert(std::holds_alternative<Typing>(next));
assert(std::holds_alternative<NoEffect>(eff));
```

No mocks. No stubs. Just values in, values out.

### Reasoning

The `dispatch` function is a **total function** from `(State, Event)` to `(State, Effect)`. Given the same input, it always produces the same output. There are no hidden dependencies, no global state, no mutation. You can reason about it algebraically.

### Separation of Concerns

The renderer only sees `ViewModel` — it has no idea what state the machine is in. It just draws text and dots. The PAM module only knows about passwords — it has no idea about Wayland. Each component operates in its own domain with a minimal, typed interface to the others.

---

## The Effect System

Side effects are **described, not executed** in the core:

```cpp
struct NoEffect {};
struct StartAuth { std::string password; };
struct ExitProgram {};

using Effect = std::variant<NoEffect, StartAuth, ExitProgram>;
```

A transition returns `TransitionResult { State, Effect }`. The effect is data — a description of what should happen. The impure shell reads this description and executes it:

```cpp
void execute_effect(const Effect& effect, PamAuth& pam, Client& client) {
    std::visit(overloaded{
        [](const NoEffect&) {},
        [&](const StartAuth& e) { pam.authenticate_async(e.password, callback); },
        [&](const ExitProgram&) { client.unlock_session(); },
    }, effect);
}
```

This is a simplified version of the **free monad** pattern from Haskell:
- The core produces a **program description** (the effect)
- The shell provides an **interpreter** (the executor)
- You can swap interpreters without touching the core (e.g., a test interpreter that records effects instead of executing them)

---

## Data Flow

### Input Path

```
Wayland keyboard event
  → xkbcommon decodes keysym + codepoint
    → key_callback translates to Event (KeyPress / Submit / Backspace)
      → dispatch(state, event)
        → (new_state, effect)
          → execute_effect(effect)
          → state = new_state
```

### Auth Path (async)

```
dispatch(state, Submit)
  → (Authenticating, StartAuth{password})
    → execute_effect spawns PAM thread
      → PAM thread authenticates
        → push_event(AuthSuccess or AuthFail)  // thread-safe queue
          → main loop drains queue
            → dispatch(state, AuthSuccess)
              → (Unlocked, ExitProgram)
```

### Render Path

```
state
  → view(state)
    → ViewModel { status_text, input_display, show_error, error_text }
      → Renderer::draw(buffer, viewmodel)
        → Cairo paints to shared memory buffer
          → attach buffer to Wayland surface
            → compositor displays it
```

---

## File Map

```
src/
├── core/
│   └── machine.hpp      The pure core. States, events, transitions,
│                         compile-time proofs, dispatch, view.
│                         Zero dependencies on OS or I/O.
│
├── wayland/
│   ├── client.hpp        Wayland connection, session lock, keyboard input.
│   └── client.cpp        C callback trampolines, shared memory buffers,
│                         xkbcommon keymap handling.
│
├── render/
│   ├── renderer.hpp      Cairo/Pango renderer.
│   └── renderer.cpp      Draws ViewModel to a shared memory buffer.
│                         Dark background, centered text, password dots.
│
├── auth/
│   ├── pam.hpp           Async PAM authentication interface.
│   └── pam.cpp           PAM conversation function, threaded auth.
│
└── main.cpp              The impure shell. Event loop, effect interpreter,
                          wiring between Wayland → core → renderer → auth.
```

---

## The Event Loop

The main loop is a `poll()`-based event loop:

```
while (running) {
    1. Drain auth events from PAM thread    → dispatch into core
    2. Compute ViewModel from current state → render to all surfaces
    3. Flush Wayland connection
    4. poll() on Wayland fd with 100ms timeout
    5. Dispatch Wayland events (keyboard input → dispatch into core)
}
```

The 100ms timeout ensures auth results from the PAM thread are processed even when no Wayland events arrive. This is a pragmatic choice — an eventfd or pipe would be more efficient but adds complexity for no real benefit in a lock screen.

---

## Wayland Protocol: ext-session-lock-v1

Typelock uses the `ext-session-lock-v1` protocol, the standard Wayland protocol for session locking. This is more secure than the `wlr-layer-shell` approach because:

1. **Exclusive access**: The compositor guarantees no other surfaces are visible while the lock is active
2. **Keyboard grab**: All keyboard input goes to the lock surface
3. **Atomicity**: The lock either succeeds completely or fails — no partial states where some outputs are locked and others aren't

### Protocol Flow

```
Client                          Compositor
  │                                │
  ├─ bind ext_session_lock_manager │
  ├─ lock() ──────────────────────▶│
  │                                │ compositor hides all surfaces
  ├─ get_lock_surface(output1) ───▶│
  ├─ get_lock_surface(output2) ───▶│
  │                                │
  │◀── configure(serial, w, h) ────┤  for each output
  ├─── ack_configure(serial) ─────▶│
  ├─── attach buffer + commit ────▶│
  │                                │
  │◀── locked ─────────────────────┤  all surfaces committed
  │                                │
  │  ... user authenticates ...    │
  │                                │
  ├── unlock_and_destroy() ───────▶│
  │                                │ compositor shows surfaces again
```

---

## Threading Model

Only one thread does I/O: the main thread runs the Wayland event loop, the renderer, and the effect interpreter.

The PAM authentication runs in a **separate thread** because `pam_authenticate()` blocks. Communication back to the main thread is through a mutex-protected event queue:

```
Main Thread                    PAM Thread
    │                              │
    ├── authenticate_async() ─────▶│
    │                              ├── pam_start()
    │   poll() / dispatch()        ├── pam_authenticate()  // blocks
    │   poll() / dispatch()        ├── pam_end()
    │   poll() / dispatch()        │
    │◀── push_event(AuthSuccess) ──┤
    │                              │
    ├── drain_events()             │
    ├── dispatch(state, AuthSuccess)
    ├── execute_effect(ExitProgram)
    ├── unlock_session()
```

This is the simplest correct threading model. The PAM thread never touches Wayland or the renderer. The main thread never blocks on PAM. The shared state is minimal: one event queue protected by one mutex.
