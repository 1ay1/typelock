#pragma once

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <vector>

#include "../core/config.hpp"
#include "../core/machine.hpp"
#include "../core/widget.hpp"
#include "../wayland/client.hpp"

namespace typelock::render {

class Renderer {
public:
    explicit Renderer(const Config& config);
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void set_backgrounds(std::vector<wl::Screenshot> shots);

    void draw(wl::ShmBuffer& buf, const ViewModel& vm,
              float opacity, float error_shake, float error_opacity,
              bool fingerprint_active, int output_index);

private:
    const Config&                   config_;
    PangoFontDescription*           font_clock_    = nullptr;
    PangoFontDescription*           font_date_     = nullptr;
    PangoFontDescription*           font_label_    = nullptr;
    PangoFontDescription*           font_error_    = nullptr;
    std::vector<cairo_surface_t*>   backgrounds_;
};

}  // namespace typelock::render
