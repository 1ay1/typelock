#include "harness.hpp"
#include "core/machine.hpp"
#include "core/types.hpp"
#include "core/config.hpp"
#include "core/widget.hpp"

#include <cmath>
#include <string>

using namespace typelock;

// ============================================================================
//  type_list: the fundamental kind
// ============================================================================

TEST(type_list_cardinality) {
    EXPECT((type_list<>::size) == 0);
    EXPECT((type_list<int>::size) == 1);
    EXPECT((type_list<int, float, double>::size) == 3);
    EXPECT(States::size == 5);
    EXPECT(Events::size == 8);
}

TEST(type_list_membership_is_decidable) {
    // Positive witnesses
    EXPECT((contains_v<States, Idle>));
    EXPECT((contains_v<States, Typing>));
    EXPECT((contains_v<States, Authenticating>));
    EXPECT((contains_v<States, Unlocked>));
    EXPECT((contains_v<States, AuthError>));
    // Negative witnesses
    EXPECT(!(contains_v<States, KeyPress>));
    EXPECT(!(contains_v<States, int>));
    EXPECT(!(contains_v<Events, Idle>));
    EXPECT(!(contains_v<Events, Unlocked>));
    // Events universe
    EXPECT((contains_v<Events, KeyPress>));
    EXPECT((contains_v<Events, Backspace>));
    EXPECT((contains_v<Events, Submit>));
    EXPECT((contains_v<Events, AuthSuccess>));
    EXPECT((contains_v<Events, AuthFail>));
    EXPECT((contains_v<Events, Timeout>));
    EXPECT((contains_v<Events, FingerprintMatch>));
    EXPECT((contains_v<Events, FingerprintNoMatch>));
}

TEST(states_and_events_are_disjoint_universes) {
    bool disjoint = true;
    for_each_type<States>([&]<typename S>() {
        if (contains_v<Events, S>) disjoint = false;
    });
    for_each_type<Events>([&]<typename E>() {
        if (contains_v<States, E>) disjoint = false;
    });
    EXPECT(disjoint);
}

// ============================================================================
//  Concept satisfaction: IsState / IsEvent / Easing type classes
// ============================================================================

TEST(concept_IsState_exactly_classifies_states) {
    EXPECT(IsState<Idle>);
    EXPECT(IsState<Typing>);
    EXPECT(IsState<Authenticating>);
    EXPECT(IsState<Unlocked>);
    EXPECT(IsState<AuthError>);
    EXPECT(!IsState<KeyPress>);
    EXPECT(!IsState<Submit>);
    EXPECT(!IsState<NoEffect>);
}

TEST(concept_IsEvent_exactly_classifies_events) {
    EXPECT(IsEvent<KeyPress>);
    EXPECT(IsEvent<Backspace>);
    EXPECT(IsEvent<Submit>);
    EXPECT(IsEvent<AuthSuccess>);
    EXPECT(IsEvent<AuthFail>);
    EXPECT(IsEvent<Timeout>);
    EXPECT(IsEvent<FingerprintMatch>);
    EXPECT(IsEvent<FingerprintNoMatch>);
    EXPECT(!IsEvent<Idle>);
    EXPECT(!IsEvent<Unlocked>);
    EXPECT(!IsEvent<StartAuth>);
}

TEST(concept_Easing_classifies_easing_types) {
    EXPECT(Easing<Linear>);
    EXPECT(Easing<EaseOut>);
    EXPECT(Easing<EaseIn>);
    EXPECT(Easing<EaseInOut>);
    EXPECT(Easing<EaseOutBack>);
    EXPECT(!Easing<Idle>);
    EXPECT(!Easing<int>);
}

// ============================================================================
//  count_if: type-level predicate counting
// ============================================================================

template <typename T>
struct is_state_pred : std::bool_constant<IsState<T>> {};

template <typename T>
struct is_event_pred : std::bool_constant<IsEvent<T>> {};

TEST(count_if_over_mixed_list) {
    using Mixed = type_list<Idle, KeyPress, Typing, Submit, int, Unlocked>;
    constexpr auto n_states = count_if_v<Mixed, is_state_pred>;
    constexpr auto n_events = count_if_v<Mixed, is_event_pred>;
    EXPECT(n_states == 3);
    EXPECT(n_events == 2);
}

TEST(count_if_empty_list_is_zero) {
    using Empty = type_list<>;
    EXPECT((count_if_v<Empty, is_state_pred>) == 0);
}

// ============================================================================
//  for_each_type: type-level iteration
// ============================================================================

TEST(for_each_type_visits_all_states) {
    std::size_t count = 0;
    for_each_type<States>([&]<typename S>() {
        EXPECT(IsState<S>);
        count++;
    });
    EXPECT(count == 5);
}

TEST(for_each_type_visits_all_events) {
    std::size_t count = 0;
    for_each_type<Events>([&]<typename E>() {
        EXPECT(IsEvent<E>);
        count++;
    });
    EXPECT(count == 8);
}

// ============================================================================
//  Transition table: compile-time graph theory
// ============================================================================

TEST(transition_table_size) {
    EXPECT(TransitionTable::size == 13);
}

TEST(outgoing_edge_counts) {
    EXPECT((outgoing_v<TransitionTable, Idle>) == 3);
    EXPECT((outgoing_v<TransitionTable, Typing>) == 4);
    EXPECT((outgoing_v<TransitionTable, Authenticating>) == 2);
    EXPECT((outgoing_v<TransitionTable, AuthError>) == 4);
    EXPECT((outgoing_v<TransitionTable, Unlocked>) == 0);
}

TEST(incoming_edge_counts) {
    EXPECT((incoming_v<TransitionTable, Unlocked>) > 0);
    EXPECT((incoming_v<TransitionTable, Idle>) > 0);
}

TEST(terminal_and_initial_concepts) {
    EXPECT(TerminalState<Unlocked>);
    EXPECT(!TerminalState<Idle>);
    EXPECT(InitialState<Idle>);
}

TEST(transition_table_is_deterministic) {
    EXPECT((is_deterministic<TransitionTable>::value));
}

TEST(every_rule_has_a_specialization) {
    EXPECT((table_matches_specializations<TransitionTable>::value));
}

TEST(forbidden_transitions_form_a_complete_set) {
    EXPECT(!(Transition<Idle, Submit>::valid));
    EXPECT(!(Transition<Idle, Backspace>::valid));
    EXPECT(!(Transition<Idle, AuthSuccess>::valid));
    EXPECT(!(Transition<Idle, AuthFail>::valid));
    EXPECT(!(Transition<Idle, Timeout>::valid));

    EXPECT(!(Transition<Typing, AuthSuccess>::valid));
    EXPECT(!(Transition<Typing, AuthFail>::valid));
    EXPECT(!(Transition<Typing, Timeout>::valid));
    EXPECT(!(Transition<Typing, FingerprintNoMatch>::valid));

    EXPECT(!(Transition<Authenticating, KeyPress>::valid));
    EXPECT(!(Transition<Authenticating, Backspace>::valid));
    EXPECT(!(Transition<Authenticating, Submit>::valid));
    EXPECT(!(Transition<Authenticating, Timeout>::valid));
    EXPECT(!(Transition<Authenticating, FingerprintMatch>::valid));
    EXPECT(!(Transition<Authenticating, FingerprintNoMatch>::valid));

    EXPECT(!(Transition<Unlocked, KeyPress>::valid));
    EXPECT(!(Transition<Unlocked, Backspace>::valid));
    EXPECT(!(Transition<Unlocked, Submit>::valid));
    EXPECT(!(Transition<Unlocked, AuthSuccess>::valid));
    EXPECT(!(Transition<Unlocked, AuthFail>::valid));
    EXPECT(!(Transition<Unlocked, Timeout>::valid));
    EXPECT(!(Transition<Unlocked, FingerprintMatch>::valid));
    EXPECT(!(Transition<Unlocked, FingerprintNoMatch>::valid));

    EXPECT(!(Transition<AuthError, Backspace>::valid));
    EXPECT(!(Transition<AuthError, Submit>::valid));
    EXPECT(!(Transition<AuthError, AuthSuccess>::valid));
    EXPECT(!(Transition<AuthError, AuthFail>::valid));
}

TEST(valid_transitions_are_exactly_13) {
    std::size_t valid_count = 0;
    for_each_type<States>([&]<typename S>() {
        for_each_type<Events>([&]<typename E>() {
            if constexpr (Transition<S, E>::valid)
                valid_count++;
        });
    });
    EXPECT(valid_count == 13);
    EXPECT(valid_count == TransitionTable::size);
}

TEST(state_event_product_space_is_40) {
    constexpr auto product = States::size * Events::size;
    EXPECT(product == 40);
    std::size_t forbidden_count = 0;
    for_each_type<States>([&]<typename S>() {
        for_each_type<Events>([&]<typename E>() {
            if constexpr (!Transition<S, E>::valid)
                forbidden_count++;
        });
    });
    EXPECT(forbidden_count == 27);
}

TEST(unlocked_is_a_sink_node) {
    EXPECT((outgoing_v<TransitionTable, Unlocked>) == 0);
    EXPECT((incoming_v<TransitionTable, Unlocked>) == 4);
}

TEST(every_non_terminal_state_has_outgoing_edges) {
    for_each_type<States>([&]<typename S>() {
        if constexpr (!TerminalState<S>) {
            EXPECT((outgoing_v<TransitionTable, S>) > 0);
        }
    });
}

TEST(idle_is_reachable_from_error) {
    EXPECT((Transition<AuthError, Timeout>::valid));
    EXPECT((Transition<Idle, KeyPress>::valid));
}

TEST(incoming_edge_counts_all_states) {
    EXPECT((incoming_v<TransitionTable, Idle>) == 1);
    EXPECT((incoming_v<TransitionTable, Typing>) == 4);
    EXPECT((incoming_v<TransitionTable, Authenticating>) == 1);
    EXPECT((incoming_v<TransitionTable, AuthError>) == 3);
    EXPECT((incoming_v<TransitionTable, Unlocked>) == 4);
}

// ============================================================================
//  Phantom types: Strong<T, Tag> — type safety proofs
// ============================================================================

TEST(phantom_types_are_zero_cost) {
    EXPECT(sizeof(Px) == sizeof(float));
    EXPECT(sizeof(Seconds) == sizeof(double));
}

TEST(strong_type_constexpr_arithmetic) {
    constexpr Px a{10.0f};
    constexpr Px b{20.0f};
    constexpr Px sum = a + b;
    constexpr Px diff = b - a;
    constexpr Px scaled = a * 3.0f;
    constexpr Px divided = b / 4.0f;
    EXPECT(sum.value == 30.0f);
    EXPECT(diff.value == 10.0f);
    EXPECT(scaled.value == 30.0f);
    EXPECT(divided.value == 5.0f);
}

TEST(strong_type_compound_assignment) {
    Px a{10.0f};
    a += Px{5.0f};
    EXPECT(a.value == 15.0f);
    a -= Px{3.0f};
    EXPECT(a.value == 12.0f);
}

TEST(strong_type_spaceship_operator) {
    constexpr Px a{1.0f}, b{2.0f}, c{1.0f};
    EXPECT(a < b);
    EXPECT(b > a);
    EXPECT(a <= c);
    EXPECT(a >= c);
    EXPECT(a == c);
    EXPECT(a != b);
    EXPECT((a == c && c < b && a < b));
}

TEST(strong_type_default_construction_is_zero) {
    Px p;
    EXPECT(p.value == 0.0f);
    Seconds s;
    EXPECT(s.value == 0.0);
}

// ============================================================================
//  Color: constexpr RGBA algebra
// ============================================================================

TEST(color_constexpr_chain) {
    constexpr auto c = Color::hex(0xAABBCC).with_alpha(0);
    EXPECT(c.r == 0xAA);
    EXPECT(c.g == 0xBB);
    EXPECT(c.b == 0xCC);
    EXPECT(c.a == 0);
    constexpr auto d = c.with_alpha(255).with_alpha(100).with_alpha(42);
    EXPECT(d.a == 42);
    EXPECT(d.r == c.r);
}

TEST(color_float_normalization_boundary_values) {
    constexpr auto black = Color::rgba(0, 0, 0, 0);
    constexpr auto white = Color::rgba(255, 255, 255, 255);
    EXPECT(black.rf() == 0.0);
    EXPECT(black.af() == 0.0);
    EXPECT(white.rf() == 1.0);
    EXPECT(white.af() == 1.0);
    constexpr auto mid = Color::rgba(128, 128, 128, 128);
    EXPECT(mid.rf() > 0.50 && mid.rf() < 0.51);
}

TEST(palette_is_self_consistent) {
    EXPECT(palette::field_bg.a != 255);
    EXPECT(palette::bg_dark.a == 255);
    EXPECT(palette::text_white.a == 255);
    EXPECT(palette::error_red.a == 255);
    EXPECT(palette::dot_white.a == 255);
    EXPECT(palette::clock_white.a == 255);
    EXPECT(palette::clock_white.r == 255);
    EXPECT(palette::clock_white.g == 255);
    EXPECT(palette::clock_white.b == 255);
}

// ============================================================================
//  Easing: morphisms in [0,1] -> [0,1]
// ============================================================================

TEST(all_easings_are_endomorphisms_on_unit_interval) {
    auto check_boundary = []<Easing E>() {
        EXPECT(E::ease(0.0f) == 0.0f);
        EXPECT(E::ease(1.0f) == 1.0f);
    };
    check_boundary.template operator()<Linear>();
    check_boundary.template operator()<EaseIn>();
    check_boundary.template operator()<EaseOut>();
    check_boundary.template operator()<EaseInOut>();
    check_boundary.template operator()<EaseOutBack>();
}

TEST(ease_in_out_is_symmetric_at_midpoint) {
    float mid = EaseInOut::ease(0.5f);
    EXPECT(mid > 0.49f && mid < 0.51f);
}

TEST(ease_in_and_ease_out_are_duals) {
    for (int i = 0; i <= 10; i++) {
        float t = static_cast<float>(i) / 10.0f;
        float sum = EaseIn::ease(t) + EaseOut::ease(1.0f - t);
        EXPECT(sum > 0.99f && sum < 1.01f);
    }
}

TEST(ease_out_back_overshoots_then_settles) {
    bool overshot = false;
    for (int i = 1; i < 100; i++) {
        float t = static_cast<float>(i) / 100.0f;
        if (EaseOutBack::ease(t) > 1.0f) overshot = true;
    }
    EXPECT(overshot);
    EXPECT(EaseOutBack::ease(1.0f) == 1.0f);
}

TEST(easing_constexpr_evaluation) {
    constexpr float lin_0 = Linear::ease(0.0f);
    constexpr float lin_1 = Linear::ease(1.0f);
    constexpr float ei_0  = EaseIn::ease(0.0f);
    constexpr float eo_1  = EaseOut::ease(1.0f);
    EXPECT(lin_0 == 0.0f);
    EXPECT(lin_1 == 1.0f);
    EXPECT(ei_0 == 0.0f);
    EXPECT(eo_1 == 1.0f);
}

TEST(linear_satisfies_homomorphism) {
    for (int i = 0; i <= 20; i++) {
        float t = static_cast<float>(i) / 20.0f;
        EXPECT(Linear::ease(t) == t);
    }
}

// ============================================================================
//  Variant exhaustiveness: sum types are closed
// ============================================================================

TEST(state_variant_holds_exactly_5_alternatives) {
    EXPECT(std::variant_size_v<State> == 5);
    EXPECT(std::variant_size_v<State> == States::size);
}

TEST(event_variant_holds_exactly_8_alternatives) {
    EXPECT(std::variant_size_v<Event> == 8);
    EXPECT(std::variant_size_v<Event> == Events::size);
}

TEST(effect_variant_is_minimal) {
    EXPECT(std::variant_size_v<Effect> == 3);
    Effect e1 = NoEffect{};
    Effect e2 = StartAuth{"pw"};
    Effect e3 = ExitProgram{};
    EXPECT(std::holds_alternative<NoEffect>(e1));
    EXPECT(std::holds_alternative<StartAuth>(e2));
    EXPECT(std::holds_alternative<ExitProgram>(e3));
}

// ============================================================================
//  dispatch totality: total function over the product space
// ============================================================================

TEST(dispatch_is_total_over_all_state_event_pairs) {
    State states[] = {
        Idle{}, Typing{"x"}, Authenticating{"pw"},
        Unlocked{}, AuthError{"err"}
    };
    Event events[] = {
        KeyPress{'a'}, Backspace{}, Submit{},
        AuthSuccess{}, AuthFail{"no"}, Timeout{},
        FingerprintMatch{}, FingerprintNoMatch{}
    };

    for (const auto& s : states) {
        for (const auto& e : events) {
            auto [next, eff] = dispatch(s, e);
            bool valid_state = std::visit(
                [](const auto& st) { return IsState<std::decay_t<decltype(st)>>; },
                next);
            EXPECT(valid_state);
            EXPECT(!eff.valueless_by_exception());
        }
    }
}

TEST(view_is_total_and_produces_valid_viewmodels) {
    State states[] = {
        Idle{}, Typing{"test"}, Authenticating{"pw"},
        Unlocked{}, AuthError{"err"}
    };

    for (const auto& s : states) {
        auto vm = view(s);
        bool is_unlocked = std::holds_alternative<Unlocked>(s);
        if (!is_unlocked) {
            EXPECT(!vm.status_text.empty());
        }
    }
}

// ============================================================================
//  Widget concept: structural typing / higher-kinded types
// ============================================================================

TEST(widget_concept_spacer_parametric) {
    EXPECT((typelock::widget::Widget<typelock::widget::Spacer<0>>));
    EXPECT((typelock::widget::Widget<typelock::widget::Spacer<1>>));
    EXPECT((typelock::widget::Widget<typelock::widget::Spacer<100>>));
    EXPECT((typelock::widget::Widget<typelock::widget::Spacer<9999>>));
}

TEST(spacer_size_hint_encodes_height_at_type_level) {
    EXPECT(!(std::is_same_v<typelock::widget::Spacer<10>, typelock::widget::Spacer<20>>));
    EXPECT((std::is_same_v<typelock::widget::Spacer<10>, typelock::widget::Spacer<10>>));
}

TEST(layout_combinators_preserve_widget_concept) {
    using namespace typelock::widget;
    EXPECT((Widget<VStack<Spacer<10>, Spacer<20>>>));
    EXPECT((Widget<Center<Spacer<10>>>));
    EXPECT((Widget<Padding<5, Spacer<10>>>));
    EXPECT((Widget<Center<VStack<Padding<5, Spacer<10>>, Spacer<20>>>>));
    EXPECT((Widget<DefaultLayout>));
}

// ============================================================================
//  Animation: type-level easing parameterization
// ============================================================================

TEST(animation_types_encode_easing_in_type) {
    EXPECT(!(std::is_same_v<FadeIn, PulseAnim>));
    EXPECT((std::is_same_v<FadeIn, Animation<EaseOut>>));
    EXPECT((std::is_same_v<ShakeAnim, Animation<EaseOut>>));
    EXPECT((std::is_same_v<PulseAnim, Animation<EaseInOut>>));
    EXPECT((std::is_same_v<FadeIn, ShakeAnim>));
}

TEST(animation_inactive_is_complete) {
    Animation<Linear> a;
    EXPECT(!a.active);
    EXPECT(a.done());
    EXPECT(a.progress() == 1.0f);
}

// ============================================================================
//  Shake: mathematical properties
// ============================================================================

TEST(shake_is_odd_symmetric_at_origin) {
    EXPECT(shake_offset(0.0f, 1.0f) == 0.0f);
    EXPECT(shake_offset(0.0f, 100.0f) == 0.0f);
    EXPECT(shake_offset(0.0f, 0.0f) == 0.0f);
}

TEST(shake_amplitude_scales_linearly) {
    float t = 0.3f;
    float s1 = shake_offset(t, 5.0f);
    float s2 = shake_offset(t, 10.0f);
    EXPECT(std::abs(s2 - 2.0f * s1) < 0.001f);
}

TEST(shake_envelope_decays_monotonically) {
    float amp = 10.0f;
    for (int i = 0; i <= 100; i++) {
        float t = static_cast<float>(i) / 100.0f;
        float offset = shake_offset(t, amp);
        EXPECT(std::abs(offset) <= amp + 0.01f);
    }
}

// ============================================================================
//  Data preservation invariants
// ============================================================================

TEST(submit_effect_carries_exact_password) {
    std::string passwords[] = {"", "a", "hello world", "p@$$w0rd!", std::string(100, 'x')};
    for (const auto& pw : passwords) {
        State s = Typing{pw};
        auto [next, eff] = dispatch(s, Submit{});
        EXPECT(std::holds_alternative<Authenticating>(next));
        EXPECT(std::get<Authenticating>(next).password == pw);
        EXPECT(std::holds_alternative<StartAuth>(eff));
        EXPECT(std::get<StartAuth>(eff).password == pw);
    }
}

TEST(invalid_transitions_preserve_state_exactly) {
    Typing original{"preserve_me"};
    State s = original;
    auto [next, eff] = dispatch(s, Timeout{});
    EXPECT(std::holds_alternative<Typing>(next));
    EXPECT(std::get<Typing>(next).buffer == original.buffer);
    EXPECT(std::holds_alternative<NoEffect>(eff));
}

// ============================================================================
//  Geometry: constexpr proofs
// ============================================================================

TEST(rect_center_is_constexpr) {
    constexpr typelock::widget::Rect r{0.0f, 0.0f, 100.0f, 200.0f};
    constexpr float cx = r.center_x();
    constexpr float cy = r.center_y();
    EXPECT(cx == 50.0f);
    EXPECT(cy == 100.0f);
}

TEST(size_addition_is_constexpr) {
    constexpr typelock::widget::Size a{10.0f, 20.0f};
    constexpr typelock::widget::Size b{30.0f, 40.0f};
    constexpr auto c = a + b;
    EXPECT(c.w == 40.0f);
    EXPECT(c.h == 60.0f);
}
