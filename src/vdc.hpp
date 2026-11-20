// ---------------------------------------------------------------------
// vdc.hpp
// hydra
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

    void blit_tiles(
        uint32_t tiles_address,
        int16_t  x_dst,
        int16_t  y_dst,
        uint8_t  w,
        uint8_t  h,
        uint32_t base_address,
        uint8_t  w_src,
        uint8_t  h_src,
        bool     rept = false
    )
    {
        if (w == 0 || h == 0 || w_src == 0 || h_src == 0) {
            return;
        }

        auto floor_div = [](int a, int b) -> int {
            int q = a / b;
            int r = a % b;
            if ((r != 0) && ((r < 0) != (b < 0))) {
                --q;
            }
            return q;
        };

        // 1. Compute exact grid cells visible on the 320x180 screen
        int x_start = floor_div(-x_dst, w_src);
        int y_start = floor_div(-y_dst, h_src);
        int x_end   = floor_div(PIXELS_PER_SCANLINE - 1 - x_dst, w_src) + 1;
        int y_end   = floor_div(SCANLINES - 1 - y_dst, h_src) + 1;

        if (!rept) {
            if (x_start < 0) x_start = 0;
            if (y_start < 0) y_start = 0;
            if (x_end > w)   x_end = w;
            if (y_end > h)   y_end = h;
            if (x_start >= x_end || y_start >= y_end) {
                return;
            }
        }

        const uint32_t tile_bytes = static_cast<uint32_t>(w_src) * h_src;

        // 2. Row-major loop over visible screen cells
        for (int ty = y_start; ty < y_end; ++ty) {
            const int16_t py = static_cast<int16_t>(y_dst + (ty * h_src));
            const bool y_unclipped = (py >= 0) && (py + h_src <= SCANLINES);

            const int map_y = rept ? (((ty % h) + h) % h) : ty;
            const uint32_t row_map_offset = tiles_address + (map_y * w);

            for (int tx = x_start; tx < x_end; ++tx) {
                const int map_x = rept ? (((tx % w) + w) % w) : tx;
                const uint8_t index = ram[(row_map_offset + map_x) & (RAM_SIZE - 1)];

                const int16_t px = static_cast<int16_t>(x_dst + (tx * w_src));

                // 3. Fast unclipped path for interior tiles
                if (y_unclipped && px >= 0 && (px + w_src <= PIXELS_PER_SCANLINE)) {
                    const uint32_t src_addr = (base_address + static_cast<uint32_t>(index) * tile_bytes) & (RAM_SIZE - 1);
                    if (src_addr + tile_bytes > RAM_SIZE) {
                        continue;
                    }

                    const uint8_t* __restrict src_row = ram.data() + src_addr;
                    uint8_t* __restrict dst_row = buffer.data() + (py * PIXELS_PER_SCANLINE) + px;

                    for (int sy = 0; sy < h_src; ++sy) {
                        int sx = 0;
                        for (; sx <= w_src - 8; sx += 8) {
                            uint64_t chunk;
                            std::memcpy(&chunk, src_row + sx, sizeof(uint64_t));
                            if (chunk == 0) {
                                continue;
                            }
                            for (int i = 0; i < 8; ++i) {
                                uint8_t val = src_row[sx + i];
                                if (val) dst_row[sx + i] = val;
                            }
                        }
                        for (; sx < w_src; ++sx) {
                            uint8_t val = src_row[sx];
                            if (val) dst_row[sx] = val;
                        }
                        src_row += w_src;
                        dst_row += PIXELS_PER_SCANLINE;
                    }
                } else {
                    // Edge tiles fall back to standard clipped blit
                    blit_sprite(base_address, px, py, w_src, h_src, index);
                }
            }
        }
    }

    void blit_sprite(
        uint32_t base_address,
        int16_t  x_dst,
        int16_t  y_dst,
        uint8_t  w_src,
        uint8_t  h_src,
        uint8_t  index
    )
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
        uint32_t offset = static_cast<uint32_t>(index) * w_src * h_src;
        base_address = (base_address + offset) & (RAM_SIZE - 1);

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
