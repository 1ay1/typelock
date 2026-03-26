#pragma once

#include "types.hpp"
#include <string>

namespace typelock {

// ============================================================================
//  Config — the lock screen configuration as a product type
//
//  Every field has a strong type and a compile-time default. The config
//  file parser produces a Config value; the rest of the program is
//  parameterized over it. Changing a default here changes the behavior
//  everywhere — single source of truth.
//
//  The nested struct layout mirrors the config file sections:
//
//      [general]
//      grace_period = 5
//      fingerprint = true
//      dpms_timeout = 30
//
//      [background]
//      screenshot = true
//      blur_radius = 20
//      color = #141420
//      ...
// ============================================================================

struct Config {

    struct General {
        Seconds grace_period  = Seconds{5.0};
        bool    fingerprint   = false;
        Seconds dpms_timeout  = Seconds{30.0};
    } general;

    struct Background {
        bool  screenshot   = true;
        int   blur_radius  = 20;
        Color color        = palette::bg_dark;
    } background;

    struct ClockConfig {
        bool        enabled     = true;
        std::string time_format = "%H:%M";
        std::string time_font   = "Sans Bold 64";
        Color       time_color  = palette::clock_white;
        std::string date_format = "%A, %B %d";
        std::string date_font   = "Sans 20";
        Color       date_color  = palette::text_dim;
    } clock;

    struct Input {
        float dot_size      = 8.0f;
        float dot_gap       = 20.0f;
        Color dot_color     = palette::dot_white;
        float field_width   = 300.0f;
        float field_height  = 50.0f;
        Color field_color   = palette::field_bg;
        float field_radius  = 10.0f;
    } input;

    struct Label {
        std::string font  = "Sans 24";
        Color       color = palette::text_white;
    } label;

    struct Error {
        std::string font  = "Sans 16";
        Color       color = palette::error_red;
    } error;
};

// Default config — all fields at their declared defaults
inline constexpr auto default_config_general_grace = Seconds{5.0};
inline constexpr auto default_config_bg_color      = palette::bg_dark;

}  // namespace typelock
