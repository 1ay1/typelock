#pragma once

#include "../core/config.hpp"
#include <filesystem>
#include <string>

namespace typelock::config {

// ============================================================================
//  Config parser — reads an INI-style config file into a typed Config value
//
//  The parser produces a Config (a product type with strong-typed fields).
//  Invalid values are rejected at parse time — if parse() succeeds, the
//  Config is valid by construction.
//
//  Config file format:
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
//
//      [clock]
//      enabled = true
//      time_format = %H:%M
//      time_font = Sans Bold 64
//      time_color = #FFFFFF
//      date_format = %A, %B %d
//      date_font = Sans 20
//      date_color = #AAAAAA
//
//      [input]
//      dot_size = 8
//      dot_gap = 20
//      dot_color = #F0F0F5
//      field_width = 300
//      field_height = 50
//      field_color = #FFFFFF14
//      field_radius = 10
//
//      [label]
//      font = Sans 24
//      color = #DDDDE0
//
//      [error]
//      font = Sans 16
//      color = #E04040
// ============================================================================

auto parse(const std::filesystem::path& path) -> Config;

auto default_config_path() -> std::filesystem::path;

}  // namespace typelock::config
