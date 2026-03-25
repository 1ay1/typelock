# Building and Running Typelock

## Requirements

### Compiler

- GCC 13+ or Clang 17+ with C++23 support
- Tested with GCC 15.2

### Dependencies

| Library | Package (Arch) | Package (Debian/Ubuntu) | Purpose |
|---|---|---|---|
| wayland-client | `wayland` | `libwayland-dev` | Wayland protocol |
| wayland-scanner | `wayland` | `libwayland-dev` | Protocol code generation |
| cairo | `cairo` | `libcairo2-dev` | 2D rendering |
| pango + pangocairo | `pango` | `libpango1.0-dev` | Text rendering |
| xkbcommon | `libxkbcommon` | `libxkbcommon-dev` | Keyboard handling |
| PAM | `pam` | `libpam0g-dev` | Authentication |

### Arch Linux

```bash
# Most of these are likely already installed on a Hyprland system
sudo pacman -S wayland cairo pango libxkbcommon pam
```

### Debian/Ubuntu

```bash
sudo apt install libwayland-dev libcairo2-dev libpango1.0-dev libxkbcommon-dev libpam0g-dev
```

## Building

```bash
make
```

The binary is placed at `build/typelock`.

### Build Options

```bash
make            # build (release, -O2)
make clean      # remove all build artifacts
make install    # install to /usr/local/bin (requires root)
```

### What Happens During Build

1. `wayland-scanner` generates C bindings from `protocols/ext-session-lock-v1.xml`
2. The protocol C code is compiled
3. All C++ sources are compiled with `-std=c++23`
4. Everything is linked against wayland-client, cairo, pangocairo, xkbcommon, pam

## Running

Typelock must be run from within a Wayland session that supports the `ext-session-lock-v1` protocol.

### Supported Compositors

- **Hyprland** (recommended)
- **Sway** 1.8+
- Any compositor implementing `ext-session-lock-v1`

### From a Terminal Inside Your Session

```bash
# Verify you're in a Wayland session:
echo $WAYLAND_DISPLAY   # should print something like "wayland-1"

# Run:
./build/typelock
```

### Testing Safely with a Nested Compositor

To avoid locking yourself out during development:

```bash
# Option 1: cage (minimal Wayland compositor)
cage -- ./build/typelock

# Option 2: nested sway
WLR_BACKENDS=wayland sway
# then run typelock inside the nested sway
```

### Integration with Idle Daemon

To lock on idle with `hypridle`, add to `~/.config/hypr/hypridle.conf`:

```
listener {
    timeout = 300   # 5 minutes
    on-timeout = /path/to/typelock
}
```

## Troubleshooting

### "failed to connect to Wayland compositor"

- `WAYLAND_DISPLAY` is not set. You're not in a Wayland session, or running from a context that doesn't inherit the variable (e.g., SSH, a TTY, or a shell spawned outside the compositor).
- Fix: run from a terminal inside your compositor.

### "failed to lock session"

- The compositor doesn't support `ext-session-lock-v1`.
- Another lock screen is already active.
- The compositor rejected the lock request.

### Authentication always fails

- Check that the `login` PAM service exists: `ls /etc/pam.d/login`
- Typelock authenticates as the current user (`getuid()`). If running via sudo, it may try to authenticate the wrong user.

### Blank screen, no text

- The surface didn't receive a configure event. This can happen if the compositor is slow or if there's a race condition.
- Check compositor logs for protocol errors.
