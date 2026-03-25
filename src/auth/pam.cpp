#include "pam.hpp"

#include <cstring>
#include <pwd.h>
#include <security/pam_appl.h>
#include <unistd.h>

namespace typelock::auth {

// PAM conversation function — provides password non-interactively
static int pam_conversation(int num_msg, const struct pam_message** msg,
                            struct pam_response** resp, void* appdata) {
    const auto* password = static_cast<const std::string*>(appdata);

    auto* replies = static_cast<pam_response*>(
        calloc(static_cast<size_t>(num_msg), sizeof(pam_response)));
    if (!replies) return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; ++i) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            replies[i].resp = strdup(password->c_str());
            if (!replies[i].resp) {
                for (int j = 0; j < i; ++j) free(replies[j].resp);
                free(replies);
                return PAM_BUF_ERR;
            }
        }
    }

    *resp = replies;
    return PAM_SUCCESS;
}

PamAuth::PamAuth() = default;

PamAuth::~PamAuth() {
    if (thread_.joinable())
        thread_.join();
}

auto PamAuth::authenticate(const std::string& password) -> std::pair<AuthResult, std::string> {
    // Get current username
    struct passwd* pw = getpwuid(getuid());
    if (!pw) return {AuthResult::Failure, "Failed to get username"};

    pam_conv conv = {
        .conv      = pam_conversation,
        .appdata_ptr = const_cast<void*>(static_cast<const void*>(&password)),
    };

    pam_handle_t* handle = nullptr;
    int ret = pam_start("login", pw->pw_name, &conv, &handle);
    if (ret != PAM_SUCCESS) {
        return {AuthResult::Failure, pam_strerror(handle, ret)};
    }

    ret = pam_authenticate(handle, 0);
    auto result = (ret == PAM_SUCCESS) ? AuthResult::Success : AuthResult::Failure;
    std::string reason = (ret != PAM_SUCCESS) ? pam_strerror(handle, ret) : "";

    pam_end(handle, ret);

    // Clear password from stack
    // (the string is passed by value, destroyed on return)

    return {result, reason};
}

void PamAuth::authenticate_async(const std::string& password, AuthCallback callback) {
    if (thread_.joinable())
        thread_.join();

    busy_ = true;

    thread_ = std::thread([this, password, cb = std::move(callback)]() {
        auto [result, reason] = authenticate(password);
        busy_ = false;
        cb(result, std::move(reason));
    });
}

}  // namespace typelock::auth
