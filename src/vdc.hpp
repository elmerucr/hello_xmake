// ---------------------------------------------------------------------
//
//
// ---------------------------------------------------------------------

#ifndef VDC_HPP
#define VDC_HPP

#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include "common.hpp"

class vdc_t {
public:
    alignas(64) std::array<uint8_t, PIXELS_PER_SCANLINE * SCANLINES> buffer;
    alignas(64) std::vector<uint8_t> ram;

    vdc_t() : ram (RAM_SIZE, 0)
    {
        //
    }

    ~vdc_t()
    {
        //
    }

    void reset()
    {
        clear(0);
    }

    void clear(uint8_t color)
    {
        std::memset(buffer.data(), color, buffer.size());
    }

    void blit(uint32_t base_address, int16_t x_dst, int16_t y_dst, uint8_t w_src, uint8_t h_src, uint8_t index)
    {
        // 1. Early rejection if completely outside the 320x180 screen
        if (w_src <= 0 || h_src <= 0 ||
            x_dst >= PIXELS_PER_SCANLINE || y_dst >= SCANLINES ||
            x_dst + w_src <= 0 || y_dst + h_src <= 0) {
            return;
        }

        // 2. Clip rectangle upfront
        int x_src = 0;
        int y_src = 0;
        int w_clip = w_src;
        int h_clip = h_src;

        if (x_dst < 0) {
            x_src = -x_dst;
            w_clip += x_dst;
            x_dst = 0;
        }
        if (y_dst < 0) {
            y_src = -y_dst;
            h_clip += y_dst;
            y_dst = 0;
        }
        if (x_dst + w_clip > PIXELS_PER_SCANLINE) {
            w_clip = PIXELS_PER_SCANLINE - x_dst;
        }
        if (y_dst + h_clip > SCANLINES) {
            h_clip = SCANLINES - y_dst;
        }

        // 3. Ensure base_address + sprite size stays safely inside RAM_SIZE
        base_address &= (RAM_SIZE - 1);
        const std::size_t last_byte = static_cast<std::size_t>(base_address) + ((y_src + h_clip - 1) * w_src) + (x_src + w_clip);
        if (last_byte > RAM_SIZE) {
            return;
        }
        // 4. Fast pointer-based scanline loop
        const uint8_t* __restrict src_row = ram.data() + base_address + (y_src * w_src) + x_src;
        uint8_t* __restrict dst_row = buffer.data() + (y_dst * PIXELS_PER_SCANLINE) + x_dst;
        
        for (int y = 0; y < h_clip; ++y) {
            int x = 0;

            // Skip 8 transparent pixels at once when possible
            for (; x <= w_clip - 8; x += 8) {
                uint64_t chunk;
                std::memcpy(&chunk, src_row + x, sizeof(uint64_t));
                if (chunk == 0) {
                    continue;
                }

                for (int i = 0; i < 8; ++i) {
                    uint8_t val = src_row[x + i];
                    if (val) {
                        dst_row[x + i] = val;
                    }
                }
            }

            // Remaining 0..7 pixels
            for (; x < w_clip; ++x) {
                uint8_t val = src_row[x];
                if (val) {
                    dst_row[x] = val;
                }
            }

            src_row += w_src;
            dst_row += PIXELS_PER_SCANLINE;
        }
    }
};

#endif // VDC_HPP
