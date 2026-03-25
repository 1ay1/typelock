#include "renderer.hpp"

#include <cmath>
#include <cstring>

namespace typelock::render {

Renderer::Renderer() {
    font_status_ = pango_font_description_from_string("Sans 24");
    font_input_  = pango_font_description_from_string("Sans Bold 32");
    font_error_  = pango_font_description_from_string("Sans 16");
}

Renderer::~Renderer() {
    if (font_status_) pango_font_description_free(font_status_);
    if (font_input_)  pango_font_description_free(font_input_);
    if (font_error_)  pango_font_description_free(font_error_);
}

void Renderer::draw(wl::ShmBuffer& buf, const ViewModel& vm) {
    if (!buf.data || buf.width <= 0 || buf.height <= 0)
        return;

    auto* surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(buf.data),
        CAIRO_FORMAT_ARGB32, buf.width, buf.height, buf.width * 4);

    auto* cr = cairo_create(surface);

    // Background — dark
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
    cairo_paint(cr);

    double cx = buf.width / 2.0;
    double cy = buf.height / 2.0;

    auto* layout = pango_cairo_create_layout(cr);

    // Status text
    if (!vm.status_text.empty()) {
        pango_layout_set_font_description(layout, font_status_);
        pango_layout_set_text(layout, vm.status_text.c_str(), -1);
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);

        cairo_move_to(cr, cx - tw / 2.0, cy - 60);
        cairo_set_source_rgb(cr, 0.85, 0.85, 0.90);
        pango_cairo_show_layout(cr, layout);
    }

    // Password dots / mask
    if (!vm.input_display.empty()) {
        // Draw a subtle input field background
        double field_w = 300;
        double field_h = 50;
        double field_x = cx - field_w / 2.0;
        double field_y = cy - field_h / 2.0;
        double radius  = 10;

        // Rounded rectangle
        cairo_new_sub_path(cr);
        cairo_arc(cr, field_x + field_w - radius, field_y + radius, radius, -M_PI / 2, 0);
        cairo_arc(cr, field_x + field_w - radius, field_y + field_h - radius, radius, 0, M_PI / 2);
        cairo_arc(cr, field_x + radius, field_y + field_h - radius, radius, M_PI / 2, M_PI);
        cairo_arc(cr, field_x + radius, field_y + radius, radius, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
        cairo_fill(cr);

        // Draw dots instead of asterisks
        size_t n = vm.input_display.size();
        double dot_radius = 6;
        double dot_gap    = 20;
        double dots_w     = n * dot_gap - dot_gap + dot_radius * 2;
        double start_x    = cx - dots_w / 2.0 + dot_radius;

        cairo_set_source_rgb(cr, 0.9, 0.9, 0.95);
        for (size_t i = 0; i < n; ++i) {
            cairo_arc(cr, start_x + i * dot_gap, cy, dot_radius, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    // Error text
    if (vm.show_error && !vm.error_text.empty()) {
        pango_layout_set_font_description(layout, font_error_);
        pango_layout_set_text(layout, vm.error_text.c_str(), -1);
        pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);

        cairo_move_to(cr, cx - tw / 2.0, cy + 50);
        cairo_set_source_rgb(cr, 0.90, 0.30, 0.30);
        pango_cairo_show_layout(cr, layout);
    }

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

}  // namespace typelock::render
