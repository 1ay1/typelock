#include "client.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace typelock::wl {

// ============================================================================
// C callback trampolines
// ============================================================================

static void registry_global(void* data, wl_registry*, uint32_t name,
                            const char* interface, uint32_t version) {
    static_cast<Client*>(data)->handle_registry_global(name, interface, version);
}

static void registry_remove(void* data, wl_registry*, uint32_t name) {
    static_cast<Client*>(data)->handle_registry_remove(name);
}

static const wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_remove,
};

// -- Output --

static void output_geometry(void*, wl_output*, int32_t, int32_t, int32_t,
                            int32_t, int32_t, const char*, const char*, int32_t) {}

static void output_mode(void* data, wl_output* out, uint32_t,
                        int32_t w, int32_t h, int32_t) {
    static_cast<Client*>(data)->handle_output_geometry(out, w, h);
}

static void output_done(void* data, wl_output* out) {
    static_cast<Client*>(data)->handle_output_done(out);
}

static void output_scale(void*, wl_output*, int32_t) {}
static void output_name(void*, wl_output*, const char*) {}
static void output_description(void*, wl_output*, const char*) {}

static const wl_output_listener output_listener = {
    .geometry    = output_geometry,
    .mode        = output_mode,
    .done        = output_done,
    .scale       = output_scale,
    .name        = output_name,
    .description = output_description,
};

// -- Session lock --

static void lock_locked(void* data, ext_session_lock_v1*) {
    static_cast<Client*>(data)->handle_lock_locked();
}

static void lock_finished(void* data, ext_session_lock_v1*) {
    static_cast<Client*>(data)->handle_lock_finished();
}

static const ext_session_lock_v1_listener lock_listener = {
    .locked   = lock_locked,
    .finished = lock_finished,
};

// -- Lock surface --

static void lock_surface_configure(void* data, ext_session_lock_surface_v1* surf,
                                   uint32_t serial, uint32_t w, uint32_t h) {
    static_cast<Client*>(data)->handle_lock_surface_configure(surf, serial, w, h);
}

static const ext_session_lock_surface_v1_listener lock_surface_listener = {
    .configure = lock_surface_configure,
};

// -- Seat --

static void seat_capabilities(void* data, wl_seat*, uint32_t caps) {
    static_cast<Client*>(data)->handle_seat_capabilities(caps);
}

static void seat_name(void*, wl_seat*, const char*) {}

static const wl_seat_listener seat_listener_def = {
    .capabilities = seat_capabilities,
    .name         = seat_name,
};

// -- Keyboard --

static void kb_keymap(void* data, wl_keyboard*, uint32_t fmt, int fd, uint32_t sz) {
    static_cast<Client*>(data)->handle_keyboard_keymap(fmt, fd, sz);
}

static void kb_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
static void kb_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

static void kb_key(void* data, wl_keyboard*, uint32_t serial, uint32_t time,
                   uint32_t key, uint32_t state) {
    static_cast<Client*>(data)->handle_keyboard_key(serial, time, key, state);
}

static void kb_modifiers(void* data, wl_keyboard*, uint32_t serial,
                         uint32_t depressed, uint32_t latched,
                         uint32_t locked, uint32_t group) {
    static_cast<Client*>(data)->handle_keyboard_modifiers(serial, depressed,
                                                          latched, locked, group);
}

static void kb_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

static const wl_keyboard_listener kb_listener = {
    .keymap      = kb_keymap,
    .enter       = kb_enter,
    .leave       = kb_leave,
    .key         = kb_key,
    .modifiers   = kb_modifiers,
    .repeat_info = kb_repeat_info,
};

// -- Buffer --

static void buffer_release(void* data, wl_buffer*) {
    static_cast<ShmBuffer*>(data)->busy = false;
}

static const wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

// ============================================================================
// Client implementation
// ============================================================================

Client::Client() {
    xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx_)
        throw std::runtime_error("failed to create xkb context");
}

Client::~Client() {
    if (xkb_state_)   xkb_state_unref(xkb_state_);
    if (xkb_keymap_)  xkb_keymap_unref(xkb_keymap_);
    if (xkb_ctx_)     xkb_context_unref(xkb_ctx_);

    for (auto& ls : lock_surfaces_) {
        if (ls.lock_surf) ext_session_lock_surface_v1_destroy(ls.lock_surf);
        if (ls.surface)   wl_surface_destroy(ls.surface);
    }

    if (lock_)         ext_session_lock_v1_destroy(lock_);
    if (lock_manager_) ext_session_lock_manager_v1_destroy(lock_manager_);
    if (keyboard_)     wl_keyboard_destroy(keyboard_);
    if (seat_)         wl_seat_destroy(seat_);
    if (shm_)          wl_shm_destroy(shm_);
    if (compositor_)   wl_compositor_destroy(compositor_);
    for (auto& o : outputs_) {
        if (o.output) wl_output_destroy(o.output);
    }
    if (registry_)     wl_registry_destroy(registry_);
    if (display_)      wl_display_disconnect(display_);
}

bool Client::connect() {
    display_ = wl_display_connect(nullptr);
    if (!display_) return false;

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &registry_listener, this);

    // Round-trip to get globals
    wl_display_roundtrip(display_);
    // Second round-trip for output modes
    wl_display_roundtrip(display_);

    if (!compositor_ || !shm_ || !seat_ || !lock_manager_) {
        return false;
    }

    return true;
}

bool Client::lock_session() {
    lock_ = ext_session_lock_manager_v1_lock(lock_manager_);
    if (!lock_) return false;

    ext_session_lock_v1_add_listener(lock_, &lock_listener, this);

    // Create a lock surface for each output
    for (auto& output : outputs_) {
        LockSurface ls;
        ls.output   = &output;
        ls.surface  = wl_compositor_create_surface(compositor_);
        ls.lock_surf = ext_session_lock_v1_get_lock_surface(lock_, ls.surface, output.output);

        ext_session_lock_surface_v1_add_listener(ls.lock_surf, &lock_surface_listener, this);

        lock_surfaces_.push_back(std::move(ls));
    }

    wl_display_roundtrip(display_);
    return true;
}

void Client::unlock_session() {
    if (lock_ && locked_) {
        for (auto& ls : lock_surfaces_) {
            if (ls.lock_surf) {
                ext_session_lock_surface_v1_destroy(ls.lock_surf);
                ls.lock_surf = nullptr;
            }
            if (ls.surface) {
                wl_surface_destroy(ls.surface);
                ls.surface = nullptr;
            }
        }
        lock_surfaces_.clear();
        ext_session_lock_v1_unlock_and_destroy(lock_);
        lock_   = nullptr;
        locked_ = false;
    }
}

int Client::dispatch() {
    return wl_display_dispatch(display_);
}

void Client::flush() {
    wl_display_flush(display_);
}

int Client::get_fd() const {
    return wl_display_get_fd(display_);
}

// -- Shared memory buffer --

static int create_shm_file(size_t size) {
    const char* name = "/typelock-shm-XXXXXX";
    int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

ShmBuffer Client::create_shm_buffer(int32_t width, int32_t height) {
    ShmBuffer buf;
    buf.width  = width;
    buf.height = height;

    int32_t stride = width * 4;
    buf.size       = static_cast<size_t>(stride * height);

    buf.fd = create_shm_file(buf.size);
    if (buf.fd < 0) return buf;

    buf.data = mmap(nullptr, buf.size, PROT_READ | PROT_WRITE, MAP_SHARED, buf.fd, 0);
    if (buf.data == MAP_FAILED) {
        close(buf.fd);
        buf.data = nullptr;
        return buf;
    }

    wl_shm_pool* pool = wl_shm_create_pool(shm_, buf.fd, static_cast<int32_t>(buf.size));
    buf.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                            WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    wl_buffer_add_listener(buf.buffer, &buffer_listener, &buf);

    return buf;
}

void Client::attach_buffer(LockSurface& surf, ShmBuffer& buf) {
    wl_surface_attach(surf.surface, buf.buffer, 0, 0);
    wl_surface_damage_buffer(surf.surface, 0, 0, buf.width, buf.height);
    wl_surface_commit(surf.surface);
    buf.busy = true;
}

// ============================================================================
// Event handlers
// ============================================================================

void Client::handle_registry_global(uint32_t name, const char* interface, [[maybe_unused]] uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry_, name, &wl_compositor_interface, 4));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm_ = static_cast<wl_shm*>(
            wl_registry_bind(registry_, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat_ = static_cast<wl_seat*>(
            wl_registry_bind(registry_, name, &wl_seat_interface, 5));
        wl_seat_add_listener(seat_, &seat_listener_def, this);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto* out = static_cast<wl_output*>(
            wl_registry_bind(registry_, name, &wl_output_interface, 4));
        Output o;
        o.output = out;
        o.name   = name;
        outputs_.push_back(o);
        wl_output_add_listener(out, &output_listener, this);
    } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        lock_manager_ = static_cast<ext_session_lock_manager_v1*>(
            wl_registry_bind(registry_, name, &ext_session_lock_manager_v1_interface, 1));
    }
}

void Client::handle_registry_remove(uint32_t name) {
    // Handle output removal if needed
    (void)name;
}

void Client::handle_output_geometry(wl_output* out, int32_t w, int32_t h) {
    for (auto& o : outputs_) {
        if (o.output == out) {
            o.width  = w;
            o.height = h;
            break;
        }
    }
}

void Client::handle_output_done(wl_output* out) {
    for (auto& o : outputs_) {
        if (o.output == out) {
            o.configured = true;
            break;
        }
    }
}

void Client::handle_lock_surface_configure(ext_session_lock_surface_v1* surf,
                                            uint32_t serial, uint32_t w, uint32_t h) {
    ext_session_lock_surface_v1_ack_configure(surf, serial);

    for (auto& ls : lock_surfaces_) {
        if (ls.lock_surf == surf) {
            ls.width        = w;
            ls.height       = h;
            ls.needs_redraw = true;
            break;
        }
    }
}

void Client::handle_lock_locked() {
    locked_ = true;
}

void Client::handle_lock_finished() {
    finished_ = true;
}

void Client::handle_seat_capabilities(uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard_) {
        keyboard_ = wl_seat_get_keyboard(seat_);
        wl_keyboard_add_listener(keyboard_, &kb_listener, this);
    }
}

void Client::handle_keyboard_keymap(uint32_t format, int fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_str = static_cast<char*>(
        mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);

    if (map_str == MAP_FAILED) return;

    if (xkb_keymap_) xkb_keymap_unref(xkb_keymap_);
    xkb_keymap_ = xkb_keymap_new_from_string(xkb_ctx_, map_str,
                                               XKB_KEYMAP_FORMAT_TEXT_V1,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);

    if (!xkb_keymap_) return;

    if (xkb_state_) xkb_state_unref(xkb_state_);
    xkb_state_ = xkb_state_new(xkb_keymap_);
}

void Client::handle_keyboard_key(uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !xkb_state_)
        return;

    // evdev offset
    uint32_t keycode = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state_, keycode);

    // Get UTF-32 codepoint
    char32_t codepoint = 0;
    char buf[8];
    int len = xkb_state_key_get_utf8(xkb_state_, keycode, buf, sizeof(buf));
    if (len > 0 && len <= 4) {
        // Decode first UTF-8 character to UTF-32
        auto* u = reinterpret_cast<unsigned char*>(buf);
        if (u[0] < 0x80)      codepoint = u[0];
        else if (u[0] < 0xE0) codepoint = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        else if (u[0] < 0xF0) codepoint = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        else                   codepoint = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    }

    if (key_callback_)
        key_callback_(sym, codepoint);
}

void Client::handle_keyboard_modifiers(uint32_t, uint32_t depressed,
                                        uint32_t latched, uint32_t locked,
                                        uint32_t group) {
    if (xkb_state_)
        xkb_state_update_mask(xkb_state_, depressed, latched, locked, 0, 0, group);
}

}  // namespace typelock::wl
