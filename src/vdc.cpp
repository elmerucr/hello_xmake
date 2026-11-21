// ---------------------------------------------------------------------
// vdc.cpp
// hydra
//
// ---------------------------------------------------------------------

#include "vdc.hpp"

void vdc_t::clear(uint8_t color)
{
    std::memset(buffer.data(), color, buffer.size());
}

void vdc_t::blit_sprite(
    uint32_t gfx_addr,
    int16_t  dst_x,
    int16_t  dst_y,
    uint8_t  sprite_w,
    uint8_t  sprite_h,
    uint8_t  sprite_id,
    bool     flip_x,
    bool     flip_y
)
{
    // 1. Early rejection if completely outside the 320x180 screen
    if (sprite_w <= 0 || sprite_h <= 0 ||
        dst_x >= PIXELS_PER_SCANLINE || dst_y >= SCANLINES ||
        dst_x + sprite_w <= 0 || dst_y + sprite_h <= 0) {
        return;
    }

    // 2. Clip rectangle upfront
    int src_x = 0;
    int src_y = 0;
    int w_clip = sprite_w;
    int h_clip = sprite_h;

    if (dst_x < 0) {
        src_x = -dst_x;
        w_clip += dst_x;
        dst_x = 0;
    }
    if (dst_y < 0) {
        src_y = -dst_y;
        h_clip += dst_y;
        dst_y = 0;
    }
    if (dst_x + w_clip > PIXELS_PER_SCANLINE) {
        w_clip = PIXELS_PER_SCANLINE - dst_x;
    }
    if (dst_y + h_clip > SCANLINES) {
        h_clip = SCANLINES - dst_y;
    }

    // 3. Ensure gfx_addr + sprite size stays safely inside RAM_SIZE
    uint32_t offset = static_cast<uint32_t>(sprite_id) * sprite_w * sprite_h;
    gfx_addr = (gfx_addr + offset) & (RAM_SIZE - 1);

    const std::size_t last_byte = static_cast<std::size_t>(gfx_addr) + ((src_y + h_clip - 1) * sprite_w) + (src_x + w_clip);
    if (last_byte > RAM_SIZE) {
        return;
    }

    // 4. Fast pointer-based scanline loop
    const int src_start_y = flip_y ? (src_y + h_clip - 1) : src_y;
    const int src_y_step  = flip_y ? -static_cast<int>(sprite_w) : static_cast<int>(sprite_w);

    const uint8_t* __restrict src_row = ram.data() + gfx_addr + (src_start_y * sprite_w);
    uint8_t* __restrict dst_row = buffer.data() + (dst_y * PIXELS_PER_SCANLINE) + dst_x;

    if (!flip_x) {
        src_row += src_x;

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

            src_row += src_y_step;
            dst_row += PIXELS_PER_SCANLINE;
        }
    } else {
        const int flip_start = src_x + w_clip - 1;

        for (int y = 0; y < h_clip; ++y) {
            for (int x = 0; x < w_clip; ++x) {
                uint8_t val = src_row[flip_start - x];
                if (val) {
                    dst_row[x] = val;
                }
            }

            src_row += src_y_step;
            dst_row += PIXELS_PER_SCANLINE;
        }
    }
}

void vdc_t::blit_tilemap(
    uint32_t map_addr,
    int16_t  dst_x,
    int16_t  dst_y,
    uint8_t  map_w,
    uint8_t  map_h,
    uint32_t tileset_addr,
    uint8_t  tile_w,
    uint8_t  tile_h,
    bool     repeat
)
{
    if (map_w == 0 || map_h == 0 || tile_w == 0 || tile_h == 0) {
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
    int x_start = floor_div(-dst_x, tile_w);
    int y_start = floor_div(-dst_y, tile_h);
    int x_end   = floor_div(PIXELS_PER_SCANLINE - 1 - dst_x, tile_w) + 1;
    int y_end   = floor_div(SCANLINES - 1 - dst_y, tile_h) + 1;

    if (!repeat) {
        if (x_start < 0) x_start = 0;
        if (y_start < 0) y_start = 0;
        if (x_end > map_w)   x_end = map_w;
        if (y_end > map_h)   y_end = map_h;
        if (x_start >= x_end || y_start >= y_end) {
            return;
        }
    }

    const uint32_t tile_bytes = static_cast<uint32_t>(tile_w) * tile_h;

    // 2. Row-major loop over visible screen cells
    for (int ty = y_start; ty < y_end; ++ty) {
        const int16_t py = static_cast<int16_t>(dst_y + (ty * tile_h));
        const bool y_unclipped = (py >= 0) && (py + tile_h <= SCANLINES);

        const int map_y = repeat ? (((ty % map_h) + map_h) % map_h) : ty;
        const uint32_t row_map_offset = map_addr + (map_y * map_w);

        for (int tx = x_start; tx < x_end; ++tx) {
            const int map_x = repeat ? (((tx % map_w) + map_w) % map_w) : tx;
            const uint8_t tile_id = ram[(row_map_offset + map_x) & (RAM_SIZE - 1)];

            const int16_t px = static_cast<int16_t>(dst_x + (tx * tile_w));

            // 3. Fast unclipped path for interior tiles
            if (y_unclipped && px >= 0 && (px + tile_w <= PIXELS_PER_SCANLINE)) {
                const uint32_t src_addr = (tileset_addr + static_cast<uint32_t>(tile_id) * tile_bytes) & (RAM_SIZE - 1);
                if (src_addr + tile_bytes > RAM_SIZE) {
                    continue;
                }

                const uint8_t* __restrict src_row = ram.data() + src_addr;
                uint8_t* __restrict dst_row = buffer.data() + (py * PIXELS_PER_SCANLINE) + px;

                for (int sy = 0; sy < tile_h; ++sy) {
                    int sx = 0;
                    for (; sx <= tile_w - 8; sx += 8) {
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
                    for (; sx < tile_w; ++sx) {
                        uint8_t val = src_row[sx];
                        if (val) dst_row[sx] = val;
                    }
                    src_row += tile_w;
                    dst_row += PIXELS_PER_SCANLINE;
                }
            } else {
                // Edge tiles fall back to standard clipped blit
                blit_sprite(tileset_addr, px, py, tile_w, tile_h, tile_id, false, false);
            }
        }
    }
}
