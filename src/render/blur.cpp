#include "blur.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace typelock::render {

// Stackblur lookup tables (precomputed divisors for the given radius)
static constexpr uint16_t stackblur_mul[256] = {
    512,512,456,512,328,456,335,512,405,328,271,456,388,335,292,512,
    454,405,364,328,298,271,496,456,420,388,360,335,312,292,273,512,
    482,454,428,405,383,364,345,328,312,298,284,271,259,496,475,456,
    437,420,404,388,374,360,347,335,323,312,302,292,282,273,265,512,
    497,482,468,454,441,428,417,405,394,383,373,364,354,345,337,328,
    320,312,305,298,291,284,278,271,265,259,507,496,485,475,465,456,
    446,437,428,420,412,404,396,388,381,374,367,360,354,347,341,335,
    329,323,318,312,307,302,297,292,287,282,278,273,269,265,261,512,
    505,497,489,482,475,468,461,454,447,441,435,428,422,417,411,405,
    399,394,389,383,378,373,368,364,359,354,350,345,341,337,332,328,
    324,320,316,312,309,305,301,298,294,291,287,284,281,278,274,271,
    268,265,262,259,257,507,501,496,491,485,480,475,470,465,460,456,
    451,446,442,437,433,428,424,420,416,412,408,404,400,396,392,388,
    385,381,377,374,370,367,363,360,357,354,350,347,344,341,338,335,
    332,329,326,323,320,318,315,312,310,307,304,302,299,297,294,292,
    289,287,285,282,280,278,275,273,271,269,267,265,263,261,259
};

static constexpr uint8_t stackblur_shr[256] = {
     9, 11, 12, 13, 13, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17,
    17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19,
    19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24
};

void stackblur(uint32_t* pixels, int width, int height, int radius) {
    if (radius < 1 || !pixels || width < 1 || height < 1)
        return;

    radius = std::min(radius, 254);

    int div     = radius * 2 + 1;

    std::vector<uint32_t> r_sum(static_cast<size_t>(std::max(width, height)));
    std::vector<uint32_t> g_sum(r_sum.size());
    std::vector<uint32_t> b_sum(r_sum.size());
    std::vector<uint32_t> a_sum(r_sum.size());

    uint16_t mul_val = stackblur_mul[static_cast<size_t>(radius)];
    uint8_t  shr_val = stackblur_shr[static_cast<size_t>(radius)];

    // Stack for each scanline
    std::vector<uint32_t> stack(static_cast<size_t>(div));

    // Horizontal pass
    for (int y = 0; y < height; ++y) {
        uint32_t rs = 0, gs = 0, bs = 0, as_ = 0;
        uint32_t ri = 0, gi = 0, bi = 0, ai = 0;
        uint32_t ro = 0, go = 0, bo = 0, ao = 0;

        uint32_t* row = pixels + y * width;

        for (int i = -radius; i <= radius; ++i) {
            int idx = std::clamp(i, 0, width - 1);
            uint32_t px = row[idx];
            uint8_t pr = (px >> 16) & 0xFF;
            uint8_t pg = (px >> 8)  & 0xFF;
            uint8_t pb =  px        & 0xFF;
            uint8_t pa = (px >> 24) & 0xFF;

            int w = radius + 1 - std::abs(i);
            rs += pr * static_cast<uint32_t>(w);
            gs += pg * static_cast<uint32_t>(w);
            bs += pb * static_cast<uint32_t>(w);
            as_ += pa * static_cast<uint32_t>(w);

            if (i > 0) {
                ri += pr; gi += pg; bi += pb; ai += pa;
            } else {
                ro += pr; go += pg; bo += pb; ao += pa;
            }
            stack[static_cast<size_t>(i + radius)] = px;
        }

        for (int x = 0; x < width; ++x) {
            row[x] = ((as_ * mul_val) >> shr_val) << 24 |
                      ((rs * mul_val) >> shr_val) << 16 |
                      ((gs * mul_val) >> shr_val) << 8  |
                      ((bs * mul_val) >> shr_val);

            // Remove outgoing pixel
            int si_out = x;
            uint32_t px_out = stack[static_cast<size_t>(si_out % div)];
            uint8_t r_out = (px_out >> 16) & 0xFF;
            uint8_t g_out = (px_out >> 8) & 0xFF;
            uint8_t b_out = px_out & 0xFF;
            uint8_t a_out = (px_out >> 24) & 0xFF;

            rs -= ro; gs -= go; bs -= bo; as_ -= ao;
            ro -= r_out; go -= g_out; bo -= b_out; ao -= a_out;

            // Add incoming pixel
            int in_idx = std::min(x + radius + 1, width - 1);
            uint32_t px_in = row[in_idx];
            uint8_t r_in = (px_in >> 16) & 0xFF;
            uint8_t g_in = (px_in >> 8) & 0xFF;
            uint8_t b_in = px_in & 0xFF;
            uint8_t a_in = (px_in >> 24) & 0xFF;

            ri += r_in; gi += g_in; bi += b_in; ai += a_in;
            rs += ri; gs += gi; bs += bi; as_ += ai;

            stack[static_cast<size_t>(si_out % div)] = px_in;

            int si_in = (x + radius + 1);
            if (si_in < div) {
                // Still filling initial stack
            }

            // Next outgoing
            int next_out = std::clamp(x + 1 - radius, 0, width - 1);
            uint32_t px_next_out = row[next_out];
            ro += (px_next_out >> 16) & 0xFF;
            go += (px_next_out >> 8) & 0xFF;
            bo += px_next_out & 0xFF;
            ao += (px_next_out >> 24) & 0xFF;

            ri -= r_in; gi -= g_in; bi -= b_in; ai -= a_in;
        }
    }

    // Vertical pass
    for (int x = 0; x < width; ++x) {
        uint32_t rs = 0, gs = 0, bs = 0, as_ = 0;
        uint32_t ri = 0, gi = 0, bi = 0, ai = 0;
        uint32_t ro = 0, go = 0, bo = 0, ao = 0;

        for (int i = -radius; i <= radius; ++i) {
            int idx = std::clamp(i, 0, height - 1);
            uint32_t px = pixels[idx * width + x];
            uint8_t pr = (px >> 16) & 0xFF;
            uint8_t pg = (px >> 8)  & 0xFF;
            uint8_t pb =  px        & 0xFF;
            uint8_t pa = (px >> 24) & 0xFF;

            int w = radius + 1 - std::abs(i);
            rs += pr * static_cast<uint32_t>(w);
            gs += pg * static_cast<uint32_t>(w);
            bs += pb * static_cast<uint32_t>(w);
            as_ += pa * static_cast<uint32_t>(w);

            if (i > 0) {
                ri += pr; gi += pg; bi += pb; ai += pa;
            } else {
                ro += pr; go += pg; bo += pb; ao += pa;
            }
            stack[static_cast<size_t>(i + radius)] = px;
        }

        for (int y = 0; y < height; ++y) {
            pixels[y * width + x] =
                ((as_ * mul_val) >> shr_val) << 24 |
                ((rs * mul_val) >> shr_val) << 16 |
                ((gs * mul_val) >> shr_val) << 8  |
                ((bs * mul_val) >> shr_val);

            int si_out = y;
            uint32_t px_out = stack[static_cast<size_t>(si_out % div)];
            uint8_t r_out = (px_out >> 16) & 0xFF;
            uint8_t g_out = (px_out >> 8) & 0xFF;
            uint8_t b_out = px_out & 0xFF;
            uint8_t a_out = (px_out >> 24) & 0xFF;

            rs -= ro; gs -= go; bs -= bo; as_ -= ao;
            ro -= r_out; go -= g_out; bo -= b_out; ao -= a_out;

            int in_idx = std::min(y + radius + 1, height - 1);
            uint32_t px_in = pixels[in_idx * width + x];
            uint8_t r_in = (px_in >> 16) & 0xFF;
            uint8_t g_in = (px_in >> 8) & 0xFF;
            uint8_t b_in = px_in & 0xFF;
            uint8_t a_in = (px_in >> 24) & 0xFF;

            ri += r_in; gi += g_in; bi += b_in; ai += a_in;
            rs += ri; gs += gi; bs += bi; as_ += ai;

            stack[static_cast<size_t>(si_out % div)] = px_in;

            int next_out = std::clamp(y + 1 - radius, 0, height - 1);
            uint32_t px_next_out = pixels[next_out * width + x];
            ro += (px_next_out >> 16) & 0xFF;
            go += (px_next_out >> 8) & 0xFF;
            bo += px_next_out & 0xFF;
            ao += (px_next_out >> 24) & 0xFF;

            ri -= r_in; gi -= g_in; bi -= b_in; ai -= a_in;
        }
    }
}

}  // namespace typelock::render
