#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <poll.h>
#include <queue>
#include <sys/mman.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "auth/pam.hpp"
#include "core/machine.hpp"
#include "render/renderer.hpp"
#include "wayland/client.hpp"

using namespace typelock;

// Thread-safe event queue for auth results pushed from the PAM thread
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

// Effect interpreter — the impure shell
static void execute_effect(const Effect& effect, auth::PamAuth& pam, wl::Client& client) {
    std::visit(
        [&](const auto& eff) {
            using E = std::decay_t<decltype(eff)>;
            if constexpr (std::is_same_v<E, StartAuth>) {
                pam.authenticate_async(eff.password, [](auth::AuthResult result, std::string reason) {
                    if (result == auth::AuthResult::Success) {
                        push_event(AuthSuccess{});
                    } else {
                        push_event(AuthFail{reason.empty() ? "Authentication failed" : std::move(reason)});
                    }
                });
            } else if constexpr (std::is_same_v<E, ExitProgram>) {
                client.unlock_session();
            }
        },
        effect);
}

static void render_all(wl::Client& client, render::Renderer& renderer,
                       const ViewModel& vm,
                       std::vector<wl::ShmBuffer>& buffers) {
    auto& surfaces = client.lock_surfaces();

    // Ensure we have enough buffers
    while (buffers.size() < surfaces.size()) {
        buffers.emplace_back();
    }

    for (size_t i = 0; i < surfaces.size(); ++i) {
        auto& surf = surfaces[i];
        if (!surf.needs_redraw || surf.width == 0 || surf.height == 0)
            continue;

        auto& buf = buffers[i];

        // Recreate buffer if size changed
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

        renderer.draw(buf, vm);
        client.attach_buffer(surf, buf);
        surf.needs_redraw = false;
    }
}

int main() {
    wl::Client client;

    if (!client.connect()) {
        fprintf(stderr, "typelock: failed to connect to Wayland compositor\n");
        return 1;
    }

    if (!client.lock_session()) {
        fprintf(stderr, "typelock: failed to lock session\n");
        return 1;
    }

    auth::PamAuth pam;
    render::Renderer renderer;
    State state = Idle{};
    std::vector<wl::ShmBuffer> buffers;
    bool running = true;

    // Key handler: translate xkb events into state machine events
    client.set_key_callback([&](xkb_keysym_t sym, char32_t codepoint) {
        Event event = KeyPress{codepoint};

        switch (sym) {
            case XKB_KEY_Return:
            case XKB_KEY_KP_Enter:
                event = Submit{};
                break;
            case XKB_KEY_BackSpace:
                event = Backspace{};
                break;
            case XKB_KEY_Escape:
                // Escape clears input — transition Typing->Idle via repeated backspace
                // Or just reset: we model this as a timeout on the current state
                event = Timeout{};
                break;
            default:
                // Filter out non-printable keys
                if (codepoint == 0 || codepoint > 0x10FFFF)
                    return;
                break;
        }

        auto [next, effect] = dispatch(state, event);
        state = std::move(next);
        execute_effect(effect, pam, client);

        // Mark all surfaces for redraw
        for (auto& s : client.lock_surfaces())
            s.needs_redraw = true;
    });

    // Initial render
    auto vm = view(state);
    // Need to dispatch once to get configure events
    client.dispatch();

    render_all(client, renderer, vm, buffers);

    // Main event loop
    struct pollfd fds[1];
    fds[0].fd     = client.get_fd();
    fds[0].events = POLLIN;

    while (running) {
        // Process pending auth events from PAM thread
        for (auto& event : drain_events()) {
            auto [next, effect] = dispatch(state, event);
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

        // Render
        vm = view(state);
        render_all(client, renderer, vm, buffers);

        // Wait for Wayland events or timeout (for auth polling)
        client.flush();
        int ret = poll(fds, 1, 100);  // 100ms timeout for auth event polling

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            if (client.dispatch() < 0) {
                fprintf(stderr, "typelock: Wayland connection lost\n");
                running = false;
            }
        }
    }

    // Cleanup buffers
    for (auto& buf : buffers) {
        if (buf.data) munmap(buf.data, buf.size);
        if (buf.buffer) wl_buffer_destroy(buf.buffer);
        if (buf.fd >= 0) close(buf.fd);
    }

    return 0;
}
