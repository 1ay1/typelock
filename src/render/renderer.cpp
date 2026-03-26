#include "renderer.hpp"
#include "blur.hpp"

#include <cmath>
#include <cstring>
#include <numbers>

namespace typelock::render {

// ============================================================================
//  Widget rendering implementations
//
//  Each widget's render() is defined here because it needs Cairo/Pango.
//  The widget declarations in widget.hpp are kept header-only and free
//  of rendering dependencies — the core stays pure.
// ============================================================================

static void set_color(cairo_t* cr, const Color& c) {
    cairo_set_source_rgba(cr, c.rf(), c.gf(), c.bf(), c.af());
}

static void draw_text_centered(cairo_t* cr, PangoFontDescription* font,
                               const char* text, const Color& color,
                               float cx, float y, int* out_w = nullptr,
                               int* out_h = nullptr) {
    if (!text || !text[0]) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }

    auto* layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    set_color(cr, color);
    cairo_move_to(cr, cx - tw / 2.0, y);
    pango_cairo_show_layout(cr, layout);

    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;

    g_object_unref(layout);
}

static void draw_rounded_rect(cairo_t* cr, float x, float y,
                               float w, float h, float r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

// ============================================================================
//  Renderer
// ============================================================================

Renderer::Renderer(const Config& config) : config_(config) {
    font_clock_ = pango_font_description_from_string(config.clock.time_font.c_str());
    font_date_  = pango_font_description_from_string(config.clock.date_font.c_str());
    font_label_ = pango_font_description_from_string(config.label.font.c_str());
    font_error_ = pango_font_description_from_string(config.error.font.c_str());
}

Renderer::~Renderer() {
    if (font_clock_) pango_font_description_free(font_clock_);
    if (font_date_)  pango_font_description_free(font_date_);
    if (font_label_) pango_font_description_free(font_label_);
    if (font_error_) pango_font_description_free(font_error_);

    for (auto* bg : backgrounds_) {
        if (bg) cairo_surface_destroy(bg);
    }
}

void Renderer::set_backgrounds(std::vector<wl::Screenshot> shots) {
    for (auto* bg : backgrounds_) {
        if (bg) cairo_surface_destroy(bg);
    }
    backgrounds_.clear();

    for (auto& shot : shots) {
        if (shot.surface) {
            // Apply blur to the screenshot
            int w = cairo_image_surface_get_width(shot.surface);
            int h = cairo_image_surface_get_height(shot.surface);

            cairo_surface_flush(shot.surface);
            auto* data = reinterpret_cast<uint32_t*>(
                cairo_image_surface_get_data(shot.surface));

            if (data && config_.background.blur_radius > 0) {
                stackblur(data, w, h, config_.background.blur_radius);
                // Apply a second pass for stronger blur
                stackblur(data, w, h, config_.background.blur_radius);
            }

            cairo_surface_mark_dirty(shot.surface);
            backgrounds_.push_back(shot.surface);
        }
    }
}

void Renderer::draw(wl::ShmBuffer& buf, const ViewModel& vm,
                    float opacity, float error_shake, float error_opacity,
                    bool fingerprint_active, int output_index) {
    if (!buf.data || buf.width <= 0 || buf.height <= 0)
        return;

    auto* surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(buf.data),
        CAIRO_FORMAT_ARGB32, buf.width, buf.height, buf.width * 4);

    auto* cr = cairo_create(surface);

    // -- Background --
    if (output_index >= 0 &&
        static_cast<size_t>(output_index) < backgrounds_.size() &&
        backgrounds_[static_cast<size_t>(output_index)]) {

        auto* bg = backgrounds_[static_cast<size_t>(output_index)];
        int bg_w = cairo_image_surface_get_width(bg);
        int bg_h = cairo_image_surface_get_height(bg);

        // Scale background to fill the buffer
        double sx = static_cast<double>(buf.width) / bg_w;
        double sy = static_cast<double>(buf.height) / bg_h;
        double scale = std::max(sx, sy);

        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, bg, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);

        // Darken overlay for readability
        cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
        cairo_paint(cr);
    } else {
        // Solid color fallback
        set_color(cr, config_.background.color);
        cairo_paint(cr);
    }

    // Apply global opacity (for fade-in animation)
    if (opacity < 1.0f) {
        // We've already painted the background, now apply opacity
        // to the UI elements by setting alpha on everything below
    }

    double cx = buf.width / 2.0;
    double cy = buf.height / 2.0;

    // Vertical layout: clock, date, gap, label, gap, input, gap, error, gap, fingerprint
    double y_cursor = cy - 180;

    // -- Clock --
    if (config_.clock.enabled && vm.time_text && vm.time_text[0]) {
        Color tc = config_.clock.time_color.with_alpha(
            static_cast<uint8_t>(config_.clock.time_color.a * opacity));

        int th = 0;
        draw_text_centered(cr, font_clock_, vm.time_text, tc,
                           cx, y_cursor, nullptr, &th);
        y_cursor += th + 4;
    }

    // -- Date --
    if (config_.clock.enabled && vm.date_text && vm.date_text[0]) {
        Color dc = config_.clock.date_color.with_alpha(
            static_cast<uint8_t>(config_.clock.date_color.a * opacity));

        int th = 0;
        draw_text_centered(cr, font_date_, vm.date_text, dc,
                           cx, y_cursor, nullptr, &th);
        y_cursor += th + 40;
    }

    // -- Status label --
    if (!vm.status_text.empty()) {
        Color lc = config_.label.color.with_alpha(
            static_cast<uint8_t>(config_.label.color.a * opacity));

        int th = 0;
        draw_text_centered(cr, font_label_, vm.status_text.c_str(), lc,
                           cx + error_shake, y_cursor, nullptr, &th);
        y_cursor += th + 16;
    }

    // -- Password input field + dots --
    if (vm.input_length > 0) {
        float fw = config_.input.field_width;
        float fh = config_.input.field_height;
        float fx = static_cast<float>(cx) - fw / 2.0f + error_shake;
        float fy = static_cast<float>(y_cursor);
        float fr = config_.input.field_radius;

        // Field background
        draw_rounded_rect(cr, fx, fy, fw, fh, fr);
        Color fc = config_.input.field_color;
        fc = fc.with_alpha(static_cast<uint8_t>(fc.a * opacity));
        set_color(cr, fc);
        cairo_fill(cr);

        // Dots
        float dot_r   = config_.input.dot_size;
        float dot_gap  = config_.input.dot_gap;
        float dots_w   = static_cast<float>(vm.input_length) * dot_gap - dot_gap + dot_r * 2;
        float start_x  = static_cast<float>(cx) - dots_w / 2.0f + dot_r + error_shake;
        float dot_cy   = fy + fh / 2.0f;

        Color dc = config_.input.dot_color.with_alpha(
            static_cast<uint8_t>(config_.input.dot_color.a * opacity));
        set_color(cr, dc);

        for (std::size_t i = 0; i < vm.input_length; ++i) {
            cairo_arc(cr, start_x + static_cast<float>(i) * dot_gap,
                      dot_cy, dot_r, 0, 2 * M_PI);
            cairo_fill(cr);
        }

        y_cursor += static_cast<double>(fh) + 12;
    } else {
        // Empty input field outline
        float fw = config_.input.field_width;
        float fh = config_.input.field_height;
        float fx = static_cast<float>(cx) - fw / 2.0f + error_shake;
        float fy = static_cast<float>(y_cursor);
        float fr = config_.input.field_radius;

        draw_rounded_rect(cr, fx, fy, fw, fh, fr);
        Color fc = config_.input.field_color;
        fc = fc.with_alpha(static_cast<uint8_t>(fc.a * 0.5f * opacity));
        set_color(cr, fc);
        cairo_fill(cr);

        y_cursor += static_cast<double>(fh) + 12;
    }

    // -- Error text --
    if (vm.show_error && !vm.error_text.empty() && error_opacity > 0.01f) {
        Color ec = config_.error.color.with_alpha(
            static_cast<uint8_t>(config_.error.color.a * error_opacity * opacity));

        draw_text_centered(cr, font_error_, vm.error_text.c_str(), ec,
                           cx + error_shake, y_cursor, nullptr, nullptr);
        y_cursor += 24;
    } else {
        y_cursor += 24;
    }

    // -- Fingerprint indicator --
    if (fingerprint_active) {
        const char* fp_text = "Touch fingerprint sensor to unlock";
        Color fpc = config_.label.color.with_alpha(
            static_cast<uint8_t>(100 * opacity));
        int th = 0;
        draw_text_centered(cr, font_error_, fp_text, fpc,
                           cx, y_cursor + 16, nullptr, &th);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

}  // namespace typelock::render
