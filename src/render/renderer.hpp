#pragma once

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include "../core/machine.hpp"
#include "../wayland/client.hpp"

namespace typelock::render {

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void draw(wl::ShmBuffer& buf, const ViewModel& vm);

private:
    PangoFontDescription* font_status_ = nullptr;
    PangoFontDescription* font_input_  = nullptr;
    PangoFontDescription* font_error_  = nullptr;
};

}  // namespace typelock::render
