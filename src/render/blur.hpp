#pragma once

#include <cstdint>

namespace typelock::render {

// ============================================================================
//  Stackblur — O(w*h) blur approximation
//
//  Stackblur produces results visually indistinguishable from Gaussian blur
//  but runs in linear time regardless of radius. It works by maintaining a
//  "stack" of pixel values and sliding it across the image, which avoids
//  the O(r^2) cost of a true convolution kernel.
//
//  Reference: http://underdestruction.com/2004/02/25/stackblur-2004/
//
//  The radius parameter controls blur strength. radius=0 is no-op.
//  Typical values: 10-30 for a lock screen background.
// ============================================================================

void stackblur(uint32_t* pixels, int width, int height, int radius);

}  // namespace typelock::render
