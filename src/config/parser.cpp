#include "parser.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace typelock::config {

static auto trim(const std::string& s) -> std::string {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static auto parse_color(const std::string& s) -> Color {
    std::string hex = s;
    if (!hex.empty() && hex[0] == '#')
        hex = hex.substr(1);

    uint32_t val = 0;
    std::istringstream iss(hex);
    iss >> std::hex >> val;

    if (hex.size() == 8) {
        // RRGGBBAA
        return Color::rgba(
            static_cast<uint8_t>((val >> 24) & 0xFF),
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF));
    }
    // RRGGBB
    return Color::hex(val);
}

static auto parse_bool(const std::string& s) -> bool {
    return s == "true" || s == "1" || s == "yes";
}

auto default_config_path() -> std::filesystem::path {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) return std::filesystem::path(xdg) / "typelock" / "config.ini";

    const char* home = std::getenv("HOME");
    if (home) return std::filesystem::path(home) / ".config" / "typelock" / "config.ini";

    return "typelock.ini";
}

auto parse(const std::filesystem::path& path) -> Config {
    Config cfg;

    std::ifstream file(path);
    if (!file.is_open())
        return cfg;  // Return defaults if no config file

    std::string line;
    std::string section;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos)
                section = line.substr(1, end - 1);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key   = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (section == "general") {
            if (key == "grace_period")
                cfg.general.grace_period = Seconds{std::stod(value)};
            else if (key == "fingerprint")
                cfg.general.fingerprint = parse_bool(value);
            else if (key == "dpms_timeout")
                cfg.general.dpms_timeout = Seconds{std::stod(value)};
        } else if (section == "background") {
            if (key == "screenshot")
                cfg.background.screenshot = parse_bool(value);
            else if (key == "blur_radius")
                cfg.background.blur_radius = std::stoi(value);
            else if (key == "color")
                cfg.background.color = parse_color(value);
        } else if (section == "clock") {
            if (key == "enabled")
                cfg.clock.enabled = parse_bool(value);
            else if (key == "time_format")
                cfg.clock.time_format = value;
            else if (key == "time_font")
                cfg.clock.time_font = value;
            else if (key == "time_color")
                cfg.clock.time_color = parse_color(value);
            else if (key == "date_format")
                cfg.clock.date_format = value;
            else if (key == "date_font")
                cfg.clock.date_font = value;
            else if (key == "date_color")
                cfg.clock.date_color = parse_color(value);
        } else if (section == "input") {
            if (key == "dot_size")
                cfg.input.dot_size = std::stof(value);
            else if (key == "dot_gap")
                cfg.input.dot_gap = std::stof(value);
            else if (key == "dot_color")
                cfg.input.dot_color = parse_color(value);
            else if (key == "field_width")
                cfg.input.field_width = std::stof(value);
            else if (key == "field_height")
                cfg.input.field_height = std::stof(value);
            else if (key == "field_color")
                cfg.input.field_color = parse_color(value);
            else if (key == "field_radius")
                cfg.input.field_radius = std::stof(value);
        } else if (section == "label") {
            if (key == "font")
                cfg.label.font = value;
            else if (key == "color")
                cfg.label.color = parse_color(value);
        } else if (section == "error") {
            if (key == "font")
                cfg.error.font = value;
            else if (key == "color")
                cfg.error.color = parse_color(value);
        }
    }

    return cfg;
}

}  // namespace typelock::config
