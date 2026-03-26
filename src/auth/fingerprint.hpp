#pragma once

#include <functional>
#include <string>
#include <thread>
#include <atomic>

namespace typelock::auth {

// ============================================================================
//  Fingerprint authentication via fprintd (D-Bus)
//
//  Communicates with fprintd over the system D-Bus using sd-bus.
//  Runs in a separate thread to avoid blocking the Wayland event loop.
//
//  The callback reports either FingerprintMatch or FingerprintNoMatch,
//  which the main loop injects into the state machine as events.
// ============================================================================

enum class FingerprintResult { Match, NoMatch, Error };

using FingerprintCallback = std::function<void(FingerprintResult, std::string)>;

class FingerprintAuth {
public:
    FingerprintAuth();
    ~FingerprintAuth();

    FingerprintAuth(const FingerprintAuth&)            = delete;
    FingerprintAuth& operator=(const FingerprintAuth&) = delete;

    bool start(FingerprintCallback callback);
    void stop();
    bool is_running() const { return running_.load(); }

    static bool is_available();

private:
    void listen_loop(FingerprintCallback callback);

    std::thread     thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
};

}  // namespace typelock::auth
