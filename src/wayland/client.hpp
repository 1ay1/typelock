#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "ext-session-lock-v1-client.h"
#include "wlr-screencopy-unstable-v1-client.h"
#include "wlr-output-power-management-unstable-v1-client.h"

namespace typelock::wl {

struct Output {
    wl_output*              output     = nullptr;
    zwlr_output_power_v1*   power      = nullptr;
    uint32_t                name       = 0;
    int32_t                 width      = 0;
    int32_t                 height     = 0;
    int32_t                 scale      = 1;
    bool                    configured = false;
};

struct LockSurface {
    Output*                         output    = nullptr;
    wl_surface*                     surface   = nullptr;
    ext_session_lock_surface_v1*    lock_surf = nullptr;
    uint32_t                        width     = 0;
    uint32_t                        height    = 0;
    bool                            needs_redraw = true;
};

struct ShmBuffer {
    wl_buffer* buffer = nullptr;
    void*      data   = nullptr;
    int32_t    width  = 0;
    int32_t    height = 0;
    size_t     size   = 0;
    int        fd     = -1;
    bool       busy   = false;
};

struct Screenshot {
    cairo_surface_t* surface = nullptr;
    int32_t          width   = 0;
    int32_t          height  = 0;
};

using KeyCallback = std::function<void(xkb_keysym_t sym, char32_t codepoint)>;

class Client {
public:
    Client();
    ~Client();

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;

    bool connect();
    bool lock_session();
    void unlock_session();
    int  dispatch();
    int  dispatch_pending();
    void flush();
    int  get_fd() const;

    void set_key_callback(KeyCallback cb) { key_callback_ = std::move(cb); }

    auto& lock_surfaces() { return lock_surfaces_; }
    auto& outputs() { return outputs_; }
    bool  is_locked() const { return locked_; }

    ShmBuffer create_shm_buffer(int32_t width, int32_t height);
    void      attach_buffer(LockSurface& surf, ShmBuffer& buf);

    // Screencopy — capture all outputs before locking
    auto capture_outputs() -> std::vector<Screenshot>;

    // DPMS — display power management
    void set_dpms(bool on);
    bool has_dpms() const { return power_manager_ != nullptr; }

    // Key repeat info
    int32_t repeat_rate()  const { return repeat_rate_; }
    int32_t repeat_delay() const { return repeat_delay_; }

    // Event handlers (public for C callback trampolines)
    void handle_registry_global(uint32_t name, const char* interface, uint32_t version);
    void handle_registry_remove(uint32_t name);
    void handle_output_geometry(wl_output* out, int32_t w, int32_t h);
    void handle_output_done(wl_output* out);
    void handle_lock_surface_configure(ext_session_lock_surface_v1* surf,
                                       uint32_t serial, uint32_t w, uint32_t h);
    void handle_lock_locked();
    void handle_lock_finished();
    void handle_seat_capabilities(uint32_t caps);
    void handle_keyboard_keymap(uint32_t format, int fd, uint32_t size);
    void handle_keyboard_key(uint32_t serial, uint32_t time,
                             uint32_t key, uint32_t state);
    void handle_keyboard_modifiers(uint32_t serial, uint32_t mods_depressed,
                                   uint32_t mods_latched, uint32_t mods_locked,
                                   uint32_t group);
    void handle_keyboard_repeat_info(int32_t rate, int32_t delay);

    // Screencopy frame handlers
    void handle_frame_buffer(zwlr_screencopy_frame_v1* frame,
                             uint32_t format, uint32_t width,
                             uint32_t height, uint32_t stride);
    void handle_frame_ready(zwlr_screencopy_frame_v1* frame);
    void handle_frame_failed(zwlr_screencopy_frame_v1* frame);

private:
    wl_display*                     display_       = nullptr;
    wl_registry*                    registry_      = nullptr;
    wl_compositor*                  compositor_    = nullptr;
    wl_shm*                         shm_           = nullptr;
    wl_seat*                        seat_          = nullptr;
    wl_keyboard*                    keyboard_      = nullptr;
    ext_session_lock_manager_v1*    lock_manager_  = nullptr;
    ext_session_lock_v1*            lock_          = nullptr;
    zwlr_screencopy_manager_v1*     screencopy_    = nullptr;
    zwlr_output_power_manager_v1*   power_manager_ = nullptr;

    xkb_context*                    xkb_ctx_       = nullptr;
    xkb_keymap*                     xkb_keymap_    = nullptr;
    xkb_state*                      xkb_state_     = nullptr;

    std::vector<Output>             outputs_;
    std::vector<LockSurface>        lock_surfaces_;

    KeyCallback                     key_callback_;
    bool                            locked_      = false;
    bool                            finished_    = false;
    int32_t                         repeat_rate_  = 25;
    int32_t                         repeat_delay_ = 600;

    // Screencopy state
    struct FrameCapture {
        ShmBuffer       buf;
        uint32_t        format = 0;
        uint32_t        width  = 0;
        uint32_t        height = 0;
        uint32_t        stride = 0;
        bool            ready  = false;
        bool            failed = false;
    };
    FrameCapture* active_capture_ = nullptr;
};

}  // namespace typelock::wl
