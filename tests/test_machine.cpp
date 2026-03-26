#include "harness.hpp"
#include "core/machine.hpp"

using namespace typelock;

// ============================================================================
//  State machine transitions
// ============================================================================

TEST(idle_keypress_transitions_to_typing) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, KeyPress{'a'});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == "a");
    EXPECT(std::holds_alternative<NoEffect>(eff));
}

TEST(typing_keypress_appends) {
    State s = Typing{"abc"};
    auto [next, eff] = dispatch(s, KeyPress{'d'});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == "abcd");
}

TEST(typing_backspace_removes_last) {
    State s = Typing{"abc"};
    auto [next, eff] = dispatch(s, Backspace{});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == "ab");
}

TEST(typing_backspace_single_char_returns_idle) {
    State s = Typing{"a"};
    auto [next, eff] = dispatch(s, Backspace{});
    EXPECT(std::holds_alternative<Idle>(next));
}

TEST(typing_submit_starts_auth) {
    State s = Typing{"secret"};
    auto [next, eff] = dispatch(s, Submit{});
    EXPECT(std::holds_alternative<Authenticating>(next));
    EXPECT(std::get<Authenticating>(next).password == "secret");
    EXPECT(std::holds_alternative<StartAuth>(eff));
    EXPECT(std::get<StartAuth>(eff).password == "secret");
}

TEST(auth_success_unlocks) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, AuthSuccess{});
    EXPECT(std::holds_alternative<Unlocked>(next));
    EXPECT(std::holds_alternative<ExitProgram>(eff));
}

TEST(auth_fail_shows_error) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, AuthFail{"wrong"});
    EXPECT(std::holds_alternative<AuthError>(next));
    EXPECT(std::get<AuthError>(next).message == "wrong");
    EXPECT(std::holds_alternative<NoEffect>(eff));
}

TEST(error_keypress_starts_typing) {
    State s = AuthError{"fail"};
    auto [next, eff] = dispatch(s, KeyPress{'x'});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == "x");
}

TEST(error_timeout_returns_idle) {
    State s = AuthError{"fail"};
    auto [next, eff] = dispatch(s, Timeout{});
    EXPECT(std::holds_alternative<Idle>(next));
}

// ============================================================================
//  Fingerprint path
// ============================================================================

TEST(idle_fingerprint_match_unlocks) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, FingerprintMatch{});
    EXPECT(std::holds_alternative<Unlocked>(next));
    EXPECT(std::holds_alternative<ExitProgram>(eff));
}

TEST(typing_fingerprint_match_unlocks) {
    State s = Typing{"partial"};
    auto [next, eff] = dispatch(s, FingerprintMatch{});
    EXPECT(std::holds_alternative<Unlocked>(next));
    EXPECT(std::holds_alternative<ExitProgram>(eff));
}

TEST(error_fingerprint_match_unlocks) {
    State s = AuthError{"bad"};
    auto [next, eff] = dispatch(s, FingerprintMatch{});
    EXPECT(std::holds_alternative<Unlocked>(next));
}

TEST(idle_fingerprint_nomatch_shows_error) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, FingerprintNoMatch{});
    EXPECT(std::holds_alternative<AuthError>(next));
}

TEST(typing_fingerprint_nomatch_stays_typing) {
    State s = Typing{"keep"};
    auto [next, eff] = dispatch(s, FingerprintNoMatch{});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == "keep");
}

TEST(error_fingerprint_nomatch_updates_message) {
    State s = AuthError{"old error"};
    auto [next, eff] = dispatch(s, FingerprintNoMatch{});
    EXPECT(std::holds_alternative<AuthError>(next));
    EXPECT(std::get<AuthError>(next).message == "Fingerprint not recognized");
    EXPECT(std::holds_alternative<NoEffect>(eff));
}

TEST(authenticating_fingerprint_match_is_noop) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, FingerprintMatch{});
    EXPECT(std::holds_alternative<Authenticating>(next));
    EXPECT(std::holds_alternative<NoEffect>(eff));
}

// ============================================================================
//  Invalid transitions (no-ops)
// ============================================================================

TEST(idle_submit_is_noop) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, Submit{});
    EXPECT(std::holds_alternative<Idle>(next));
    EXPECT(std::holds_alternative<NoEffect>(eff));
}

TEST(idle_backspace_is_noop) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, Backspace{});
    EXPECT(std::holds_alternative<Idle>(next));
}

TEST(unlocked_keypress_is_noop) {
    State s = Unlocked{};
    auto [next, eff] = dispatch(s, KeyPress{'a'});
    EXPECT(std::holds_alternative<Unlocked>(next));
}

TEST(authenticating_keypress_is_noop) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, KeyPress{'a'});
    EXPECT(std::holds_alternative<Authenticating>(next));
}

TEST(unlocked_submit_is_noop) {
    State s = Unlocked{};
    auto [next, eff] = dispatch(s, Submit{});
    EXPECT(std::holds_alternative<Unlocked>(next));
    EXPECT(std::holds_alternative<NoEffect>(eff));
}

TEST(unlocked_backspace_is_noop) {
    State s = Unlocked{};
    auto [next, eff] = dispatch(s, Backspace{});
    EXPECT(std::holds_alternative<Unlocked>(next));
}

TEST(unlocked_auth_success_is_noop) {
    State s = Unlocked{};
    auto [next, eff] = dispatch(s, AuthSuccess{});
    EXPECT(std::holds_alternative<Unlocked>(next));
}

TEST(idle_auth_success_is_noop) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, AuthSuccess{});
    EXPECT(std::holds_alternative<Idle>(next));
}

TEST(idle_auth_fail_is_noop) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, AuthFail{"nope"});
    EXPECT(std::holds_alternative<Idle>(next));
}

TEST(idle_timeout_is_noop) {
    State s = Idle{};
    auto [next, eff] = dispatch(s, Timeout{});
    EXPECT(std::holds_alternative<Idle>(next));
}

TEST(typing_timeout_is_noop) {
    State s = Typing{"abc"};
    auto [next, eff] = dispatch(s, Timeout{});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == "abc");
}

TEST(typing_auth_success_is_noop) {
    State s = Typing{"abc"};
    auto [next, eff] = dispatch(s, AuthSuccess{});
    EXPECT(std::holds_alternative<Typing>(next));
}

TEST(authenticating_backspace_is_noop) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, Backspace{});
    EXPECT(std::holds_alternative<Authenticating>(next));
}

TEST(authenticating_submit_is_noop) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, Submit{});
    EXPECT(std::holds_alternative<Authenticating>(next));
}

TEST(authenticating_timeout_is_noop) {
    State s = Authenticating{"pw"};
    auto [next, eff] = dispatch(s, Timeout{});
    EXPECT(std::holds_alternative<Authenticating>(next));
}

// ============================================================================
//  ViewModel projection
// ============================================================================

TEST(view_idle_shows_prompt) {
    State s = Idle{};
    auto vm = view(s);
    EXPECT(vm.status_text == "Enter password");
    EXPECT(vm.input_length == 0);
    EXPECT(!vm.show_error);
}

TEST(view_typing_masks_input) {
    State s = Typing{"hello"};
    auto vm = view(s);
    EXPECT(vm.input_display == "*****");
    EXPECT(vm.input_length == 5);
}

TEST(view_typing_single_char) {
    State s = Typing{"x"};
    auto vm = view(s);
    EXPECT(vm.input_display == "*");
    EXPECT(vm.input_length == 1);
    EXPECT(vm.status_text == "Enter password");
}

TEST(view_error_shows_message) {
    State s = AuthError{"denied"};
    auto vm = view(s);
    EXPECT(vm.show_error);
    EXPECT(vm.error_text == "denied");
}

TEST(view_authenticating) {
    State s = Authenticating{"pw"};
    auto vm = view(s);
    EXPECT(vm.status_text == "Authenticating...");
    EXPECT(vm.input_length == 0);
    EXPECT(!vm.show_error);
}

TEST(view_unlocked) {
    State s = Unlocked{};
    auto vm = view(s);
    EXPECT(vm.status_text.empty());
    EXPECT(vm.input_length == 0);
    EXPECT(!vm.show_error);
}

TEST(view_with_context_passes_clock) {
    State s = Idle{};
    ViewContext ctx{.time_text = "14:32", .date_text = "Monday"};
    auto vm = view(s, ctx);
    EXPECT(std::string(vm.time_text) == "14:32");
    EXPECT(std::string(vm.date_text) == "Monday");
}

TEST(view_with_context_fingerprint_flag) {
    State s = Idle{};
    ViewContext ctx{.fingerprint_listening = true};
    auto vm = view(s, ctx);
    EXPECT(vm.fingerprint_listening == true);
}

TEST(view_default_context) {
    State s = Idle{};
    auto vm = view(s);
    EXPECT(std::string(vm.time_text).empty());
    EXPECT(std::string(vm.date_text).empty());
    EXPECT(vm.fingerprint_listening == false);
}

// ============================================================================
//  Full flow integration
// ============================================================================

TEST(full_password_flow) {
    State s = Idle{};

    auto [s1, e1] = dispatch(s,  KeyPress{'h'});
    auto [s2, e2] = dispatch(s1, KeyPress{'i'});
    EXPECT(std::get<Typing>(s2).buffer == "hi");

    auto [s3, e3] = dispatch(s2, Submit{});
    EXPECT(std::holds_alternative<Authenticating>(s3));
    EXPECT(std::holds_alternative<StartAuth>(e3));

    auto [s4, e4] = dispatch(s3, AuthFail{"nope"});
    EXPECT(std::holds_alternative<AuthError>(s4));

    auto [s5, e5] = dispatch(s4, KeyPress{'p'});
    auto [s6, e6] = dispatch(s5, KeyPress{'w'});
    auto [s7, e7] = dispatch(s6, Submit{});
    EXPECT(std::holds_alternative<Authenticating>(s7));

    auto [s8, e8] = dispatch(s7, AuthSuccess{});
    EXPECT(std::holds_alternative<Unlocked>(s8));
    EXPECT(std::holds_alternative<ExitProgram>(e8));
}

TEST(full_fingerprint_during_typing) {
    State s = Idle{};
    auto [s1, _1] = dispatch(s, KeyPress{'a'});
    auto [s2, _2] = dispatch(s1, KeyPress{'b'});

    auto [s3, e3] = dispatch(s2, FingerprintMatch{});
    EXPECT(std::holds_alternative<Unlocked>(s3));
    EXPECT(std::holds_alternative<ExitProgram>(e3));
}

TEST(full_backspace_to_idle_then_retype) {
    State s = Idle{};
    auto [s1, _1] = dispatch(s, KeyPress{'a'});
    EXPECT(std::holds_alternative<Typing>(s1));

    auto [s2, _2] = dispatch(s1, Backspace{});
    EXPECT(std::holds_alternative<Idle>(s2));

    auto [s3, _3] = dispatch(s2, KeyPress{'b'});
    EXPECT(std::holds_alternative<Typing>(s3));
    EXPECT(std::get<Typing>(s3).buffer == "b");
}

TEST(full_error_timeout_then_retype) {
    State s = AuthError{"bad password"};
    auto [s1, _1] = dispatch(s, Timeout{});
    EXPECT(std::holds_alternative<Idle>(s1));

    auto [s2, _2] = dispatch(s1, KeyPress{'n'});
    EXPECT(std::holds_alternative<Typing>(s2));
    EXPECT(std::get<Typing>(s2).buffer == "n");
}

TEST(full_multiple_failures_then_success) {
    State s = Idle{};

    auto [s1, _1] = dispatch(s,  KeyPress{'a'});
    auto [s2, _2] = dispatch(s1, Submit{});
    auto [s3, _3] = dispatch(s2, AuthFail{"wrong"});
    EXPECT(std::holds_alternative<AuthError>(s3));

    auto [s4, _4] = dispatch(s3, KeyPress{'b'});
    auto [s5, _5] = dispatch(s4, Submit{});
    auto [s6, _6] = dispatch(s5, AuthFail{"still wrong"});
    EXPECT(std::holds_alternative<AuthError>(s6));

    auto [s7, _7] = dispatch(s6, KeyPress{'c'});
    auto [s8, _8] = dispatch(s7, Submit{});
    auto [s9, e9] = dispatch(s8, AuthSuccess{});
    EXPECT(std::holds_alternative<Unlocked>(s9));
    EXPECT(std::holds_alternative<ExitProgram>(e9));
}
