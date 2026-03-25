#pragma once

#include <functional>
#include <string>
#include <thread>

namespace typelock::auth {

enum class AuthResult { Success, Failure };

using AuthCallback = std::function<void(AuthResult, std::string)>;

class PamAuth {
public:
    PamAuth();
    ~PamAuth();

    PamAuth(const PamAuth&)            = delete;
    PamAuth& operator=(const PamAuth&) = delete;

    void authenticate_async(const std::string& password, AuthCallback callback);
    bool is_busy() const { return busy_; }

private:
    static auto authenticate(const std::string& password) -> std::pair<AuthResult, std::string>;

    std::thread thread_;
    bool busy_ = false;
};

}  // namespace typelock::auth
