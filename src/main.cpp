#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <queue>
#include <sys/mman.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "auth/fingerprint.hpp"
#include "auth/pam.hpp"
#include "config/parser.hpp"
#include "core/machine.hpp"
#include "core/types.hpp"
#include "render/renderer.hpp"
#include "wayland/client.hpp"

using namespace typelock;
using SteadyClock = std::chrono::steady_clock;

// ============================================================================
//  Thread-safe event queue — the bridge between async auth and the pure core
// ============================================================================

static std::mutex event_mutex;
static std::queue<Event> pending_events;

static void push_event(Event e) {
    std::lock_guard lock(event_mutex);
    pending_events.push(std::move(e));
}

static auto drain_events() -> std::vector<Event> {
    std::lock_guard lock(event_mutex);
    std::vector<Event> events;
    while (!pending_events.empty()) {
        events.push_back(std::move(pending_events.front()));
        pending_events.pop();
    }
    return events;
}

// ============================================================================
//  Effect interpreter — the impure shell that executes effect descriptions
// ============================================================================

static void execute_effect(const Effect& effect, auth::PamAuth& pam,
                           wl::Client& client) {
    std::visit(
        overloaded{
            [](const NoEffect&) {},
            [&](const StartAuth& e) {
                pam.authenticate_async(e.password,
                    [](auth::AuthResult result, std::string reason) {
                        if (result == auth::AuthResult::Success) {
                            push_event(AuthSuccess{});
                        } else {
                            push_event(AuthFail{reason.empty()
                                ? "Authentication failed" : std::move(reason)});
                        }
                    });
            },
            [&](const ExitProgram&) {
                client.unlock_session();
            },
        },
        effect);
}

// ============================================================================
//  Grace period — skip auth if locked recently
// ============================================================================

static const char* GRACE_FILE = "/tmp/typelock-last-unlock";

static void write_grace_timestamp() {
    std::ofstream f(GRACE_FILE);
    if (f.is_open()) {
        f << std::chrono::system_clock::now().time_since_epoch().count();
    }
}

static bool within_grace_period(double grace_seconds) {
    if (grace_seconds <= 0) return false;

    std::ifstream f(GRACE_FILE);
    if (!f.is_open()) return false;

    long long ts = 0;
    f >> ts;
    if (ts == 0) return false;

    auto then = std::chrono::system_clock::time_point(
        std::chrono::system_clock::duration(ts));
    auto elapsed = std::chrono::system_clock::now() - then;
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

    return secs < static_cast<long long>(grace_seconds);
}

// ============================================================================
//  Clock formatting
// ============================================================================

static auto format_time(const char* fmt) -> std::string {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    char buf[128];
    std::strftime(buf, sizeof(buf), fmt, tm);
    return buf;
}

// ============================================================================
//  Render helper
// ============================================================================

static void render_all(wl::Client& client, render::Renderer& renderer,
                       const ViewModel& vm,
                       std::vector<wl::ShmBuffer>& buffers,
                       float opacity, float error_shake, float error_opacity,
                       bool fingerprint_active) {
    auto& surfaces = client.lock_surfaces();

    while (buffers.size() < surfaces.size())
        buffers.emplace_back();

    for (size_t i = 0; i < surfaces.size(); ++i) {
        auto& surf = surfaces[i];
        if (!surf.needs_redraw || surf.width == 0 || surf.height == 0)
            continue;

        auto& buf = buffers[i];

        if (buf.width != static_cast<int32_t>(surf.width) ||
            buf.height != static_cast<int32_t>(surf.height)) {
            if (buf.data) {
                munmap(buf.data, buf.size);
                buf.data = nullptr;
            }
            if (buf.buffer) {
                wl_buffer_destroy(buf.buffer);
                buf.buffer = nullptr;
            }
            if (buf.fd >= 0) {
                close(buf.fd);
                buf.fd = -1;
            }
            buf = client.create_shm_buffer(
                static_cast<int32_t>(surf.width),
                static_cast<int32_t>(surf.height));
        }

        if (!buf.data) continue;

        renderer.draw(buf, vm, opacity, error_shake, error_opacity,
                      fingerprint_active, static_cast<int>(i));
        client.attach_buffer(surf, buf);
        surf.needs_redraw = false;
    }
}

// ============================================================================
//  main — the event loop
// ============================================================================

int main() {
    // -- Load config --
    auto config_path = config::default_config_path();
    Config config = config::parse(config_path);

    // -- Grace period check --
    if (within_grace_period(config.general.grace_period.value)) {
        write_grace_timestamp();
        return 0;
    }

    // -- Connect to Wayland --
    wl::Client client;

    if (!client.connect()) {
        fprintf(stderr, "typelock: failed to connect to Wayland compositor\n");
        return 1;
    }

    // -- Capture screenshots before locking (for blur background) --
    std::vector<wl::Screenshot> screenshots;
    if (config.background.screenshot) {
        screenshots = client.capture_outputs();
    }

    // -- Lock the session --
    if (!client.lock_session()) {
        fprintf(stderr, "typelock: failed to lock session\n");
        return 1;
    }

    // -- Initialize subsystems --
    auth::PamAuth pam;
    render::Renderer renderer(config);
    renderer.set_backgrounds(std::move(screenshots));

    State state = Idle{};
    std::vector<wl::ShmBuffer> buffers;
    bool running = true;

    // -- Animations --
    FadeIn fade_in;
    fade_in.begin(std::chrono::milliseconds(400));

    ShakeAnim shake_anim;
    FadeIn error_fade;

    // -- DPMS state --
    auto last_input_time = SteadyClock::now();
    bool dpms_off = false;
    auto dpms_timeout = std::chrono::duration_cast<SteadyClock::duration>(
        std::chrono::duration<double>(config.general.dpms_timeout.value));

    // -- Key repeat state --
    bool repeating = false;
    xkb_keysym_t repeat_sym = 0;
    char32_t repeat_codepoint = 0;
    auto repeat_start = SteadyClock::now();
    auto repeat_last  = SteadyClock::now();

    // -- Fingerprint --
    auth::FingerprintAuth fingerprint;
    bool fingerprint_active = false;

    if (config.general.fingerprint && auth::FingerprintAuth::is_available()) {
        fingerprint_active = fingerprint.start(
            [](auth::FingerprintResult result, std::string) {
                if (result == auth::FingerprintResult::Match) {
                    push_event(FingerprintMatch{});
                } else if (result == auth::FingerprintResult::NoMatch) {
                    push_event(FingerprintNoMatch{});
                }
            });
    }

    // -- Key handler --
    client.set_key_callback([&](xkb_keysym_t sym, char32_t codepoint) {
        // Wake DPMS
        if (dpms_off) {
            client.set_dpms(true);
            dpms_off = false;
        }
        last_input_time = SteadyClock::now();

        Event event = KeyPress{codepoint};

        switch (sym) {
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                event = Submit{};
                repeating = false;
                break;
            case XKB_KEY_BackSpace:
                event = Backspace{};
                // Enable repeat for backspace
                repeating = true;
                repeat_sym = sym;
                repeat_codepoint = 0;
                repeat_start = SteadyClock::now();
                repeat_last  = repeat_start;
                break;
            case XKB_KEY_Escape:
                event = Timeout{};
                repeating = false;
                break;
            default:
                if (codepoint == 0 || codepoint > 0x10FFFF) return;
                // Enable repeat for printable keys
                repeating = true;
                repeat_sym = sym;
                repeat_codepoint = codepoint;
                repeat_start = SteadyClock::now();
                repeat_last  = repeat_start;
                break;
        }

        auto [next, effect] = dispatch(state, event);

        // Trigger error animation on auth fail
        if (std::holds_alternative<AuthError>(next) &&
            !std::holds_alternative<AuthError>(state)) {
            shake_anim.begin(std::chrono::milliseconds(500));
            error_fade.begin(std::chrono::milliseconds(300));
        }

        state = std::move(next);
        execute_effect(effect, pam, client);

        for (auto& s : client.lock_surfaces())
            s.needs_redraw = true;
    });

    // Initial dispatch to get configure events
    client.dispatch();

    // -- Main event loop --
    struct pollfd fds[1];
    fds[0].fd     = client.get_fd();
    fds[0].events = POLLIN;

    while (running) {
        // 1. Process auth events from background threads
        for (auto& event : drain_events()) {
            auto [next, effect] = dispatch(state, event);

            if (std::holds_alternative<AuthError>(next) &&
                !std::holds_alternative<AuthError>(state)) {
                shake_anim.begin(std::chrono::milliseconds(500));
                error_fade.begin(std::chrono::milliseconds(300));
            }

            state = std::move(next);
            execute_effect(effect, pam, client);

            if (std::holds_alternative<Unlocked>(state)) {
                running = false;
                break;
            }

            for (auto& s : client.lock_surfaces())
                s.needs_redraw = true;
        }

        if (!running) break;

        // 2. Key repeat
        if (repeating) {
            auto now = SteadyClock::now();
            auto since_start = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - repeat_start).count();
            auto since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - repeat_last).count();

            int delay_ms = client.repeat_delay();
            int rate_interval = client.repeat_rate() > 0
                ? 1000 / client.repeat_rate() : 0;

            if (since_start >= delay_ms && rate_interval > 0 &&
                since_last >= rate_interval) {
                repeat_last = now;

                Event event = (repeat_sym == XKB_KEY_BackSpace)
                    ? Event{Backspace{}}
                    : Event{KeyPress{repeat_codepoint}};

                auto [next, effect] = dispatch(state, event);
                state = std::move(next);
                execute_effect(effect, pam, client);

                for (auto& s : client.lock_surfaces())
                    s.needs_redraw = true;
            }
        }

        // 3. DPMS — turn off displays after idle
        if (client.has_dpms() && !dpms_off &&
            config.general.dpms_timeout.value > 0) {
            auto idle_time = SteadyClock::now() - last_input_time;
            if (idle_time >= dpms_timeout) {
                client.set_dpms(false);
                dpms_off = true;
            }
        }

        // 4. Compute animations
        float opacity       = fade_in.progress();
        float error_shake_v = shake_anim.active ? shake_offset(shake_anim.progress()) : 0.0f;
        float error_opacity = error_fade.active ? error_fade.progress() : 1.0f;

        if (shake_anim.done()) shake_anim.active = false;
        if (error_fade.done()) error_fade.active = false;

        // 5. Clock
        std::string time_str = format_time(config.clock.time_format.c_str());
        std::string date_str = format_time(config.clock.date_format.c_str());

        ViewContext ctx{
            .time_text             = time_str.c_str(),
            .date_text             = date_str.c_str(),
            .fingerprint_listening = fingerprint_active,
        };
        auto vm = view(state, ctx);

        // 6. Render all surfaces
        bool needs_anim = !fade_in.done() || shake_anim.active || error_fade.active;
        if (needs_anim) {
            for (auto& s : client.lock_surfaces())
                s.needs_redraw = true;
        }

        render_all(client, renderer, vm, buffers,
                   opacity, error_shake_v, error_opacity, fingerprint_active);

        // 7. Flush and wait
        client.flush();

        // Short poll timeout during animations for smooth 60fps,
        // longer timeout when idle to save CPU
        int timeout_ms = needs_anim ? 16 : (repeating ? 16 : 100);

        int ret = poll(fds, 1, timeout_ms);

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            if (client.dispatch() < 0) {
                fprintf(stderr, "typelock: Wayland connection lost\n");
                running = false;
            }
        } else if (ret == 0) {
            // Timeout — run dispatch_pending to process any buffered events
            client.dispatch_pending();
        }
    }

    // Stop fingerprint listener
    if (fingerprint_active)
        fingerprint.stop();

    // Write grace timestamp
    write_grace_timestamp();

    // Cleanup buffers
    for (auto& buf : buffers) {
        if (buf.data) munmap(buf.data, buf.size);
        if (buf.buffer) wl_buffer_destroy(buf.buffer);
        if (buf.fd >= 0) close(buf.fd);
    }

    return 0;
}
