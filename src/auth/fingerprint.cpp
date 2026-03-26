#include "fingerprint.hpp"

#include <cstring>
#include <systemd/sd-bus.h>

namespace typelock::auth {

static constexpr const char* FPRINT_SERVICE   = "net.reactivated.Fprint";
static constexpr const char* FPRINT_MANAGER   = "/net/reactivated/Fprint/Manager";
static constexpr const char* FPRINT_MANAGER_IF = "net.reactivated.Fprint.Manager";
static constexpr const char* FPRINT_DEVICE_IF = "net.reactivated.Fprint.Device";

FingerprintAuth::FingerprintAuth() = default;

FingerprintAuth::~FingerprintAuth() {
    stop();
}

bool FingerprintAuth::is_available() {
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0)
        return false;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus,
        FPRINT_SERVICE, FPRINT_MANAGER, FPRINT_MANAGER_IF,
        "GetDefaultDevice", &error, &reply, "");

    bool available = (r >= 0);

    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    sd_bus_unref(bus);

    return available;
}

bool FingerprintAuth::start(FingerprintCallback callback) {
    if (running_.load())
        return false;

    stop_requested_.store(false);
    running_.store(true);

    if (thread_.joinable())
        thread_.join();

    thread_ = std::thread([this, cb = std::move(callback)]() {
        listen_loop(cb);
        running_.store(false);
    });

    return true;
}

void FingerprintAuth::stop() {
    stop_requested_.store(true);
    if (thread_.joinable())
        thread_.join();
}

void FingerprintAuth::listen_loop(FingerprintCallback callback) {
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0) {
        callback(FingerprintResult::Error, "Failed to connect to system bus");
        return;
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    // Get default device path
    int r = sd_bus_call_method(bus,
        FPRINT_SERVICE, FPRINT_MANAGER, FPRINT_MANAGER_IF,
        "GetDefaultDevice", &error, &reply, "");

    if (r < 0) {
        callback(FingerprintResult::Error, "No fingerprint device found");
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return;
    }

    const char* device_path = nullptr;
    sd_bus_message_read(reply, "o", &device_path);
    std::string dev_path(device_path);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    // Claim device for current user
    error = SD_BUS_ERROR_NULL;
    r = sd_bus_call_method(bus,
        FPRINT_SERVICE, dev_path.c_str(), FPRINT_DEVICE_IF,
        "Claim", &error, &reply, "s", "");

    if (r < 0) {
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        callback(FingerprintResult::Error, "Failed to claim fingerprint device");
        return;
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    // Continuous verification loop
    while (!stop_requested_.load()) {
        // Start verification
        error = SD_BUS_ERROR_NULL;
        reply = nullptr;
        r = sd_bus_call_method(bus,
            FPRINT_SERVICE, dev_path.c_str(), FPRINT_DEVICE_IF,
            "VerifyStart", &error, &reply, "s", "any");

        if (r < 0) {
            sd_bus_error_free(&error);
            break;
        }
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);

        // Wait for VerifyStatus signal
        bool got_result = false;
        while (!stop_requested_.load() && !got_result) {
            r = sd_bus_process(bus, nullptr);
            if (r > 0) continue;

            // Check for VerifyStatus signal via property or wait
            sd_bus_message* sig = nullptr;
            r = sd_bus_wait(bus, 500000);  // 500ms timeout
            if (r < 0) continue;

            r = sd_bus_process(bus, &sig);
            if (r <= 0 || !sig) continue;

            if (sd_bus_message_is_signal(sig, FPRINT_DEVICE_IF, "VerifyStatus")) {
                const char* result_str = nullptr;
                int done = 0;
                sd_bus_message_read(sig, "sb", &result_str, &done);

                if (result_str) {
                    if (strcmp(result_str, "verify-match") == 0) {
                        callback(FingerprintResult::Match, "");
                        got_result = true;
                    } else if (strcmp(result_str, "verify-no-match") == 0) {
                        callback(FingerprintResult::NoMatch, "Fingerprint not recognized");
                        got_result = true;
                    } else if (strcmp(result_str, "verify-swipe-too-short") == 0 ||
                               strcmp(result_str, "verify-finger-not-centered") == 0 ||
                               strcmp(result_str, "verify-remove-and-retry") == 0) {
                        // Retry — don't report, just continue
                    } else {
                        callback(FingerprintResult::NoMatch, result_str);
                        got_result = true;
                    }
                }
            }
            sd_bus_message_unref(sig);
        }

        // Stop verification before next round
        error = SD_BUS_ERROR_NULL;
        reply = nullptr;
        sd_bus_call_method(bus,
            FPRINT_SERVICE, dev_path.c_str(), FPRINT_DEVICE_IF,
            "VerifyStop", &error, &reply, "");
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);

        // If matched, stop the loop
        if (stop_requested_.load())
            break;
    }

    // Release device
    error = SD_BUS_ERROR_NULL;
    reply = nullptr;
    sd_bus_call_method(bus,
        FPRINT_SERVICE, dev_path.c_str(), FPRINT_DEVICE_IF,
        "Release", &error, &reply, "");
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    sd_bus_unref(bus);
}

}  // namespace typelock::auth
