#include <cstddef>
#include <cstdint>
#include <iostream>
#include <lua.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "common.hpp"
#include "vdc.hpp"

// 16x16 Heroine with a Sword (0x00 = transparent, 0x01..0x0F = dark to bright greyscale)
constexpr uint8_t sprite1[16 * 16] = {
    0x00, 0x00, 0x00, 0x04, 0x05, 0x05, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 0: Top of hair
    0x00, 0x00, 0x04, 0x07, 0x08, 0x08, 0x07, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, // Row 1: Hair & sword tip
    0x00, 0x04, 0x0C, 0x0D, 0x0D, 0x0D, 0x0C, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x0F, 0x00, // Row 2: Headband & blade
    0x00, 0x04, 0x06, 0x0B, 0x0C, 0x0B, 0x0C, 0x06, 0x04, 0x00, 0x00, 0x00, 0x0D, 0x0F, 0x0E, 0x00, // Row 3: Forehead & blade
    0x00, 0x04, 0x05, 0x0B, 0x01, 0x0B, 0x01, 0x05, 0x04, 0x00, 0x00, 0x0D, 0x0F, 0x0E, 0x00, 0x00, // Row 4: Eyes & blade
    0x00, 0x03, 0x05, 0x0A, 0x0B, 0x0B, 0x0A, 0x05, 0x03, 0x00, 0x0D, 0x0F, 0x0E, 0x00, 0x00, 0x00, // Row 5: Cheeks & blade
    0x00, 0x00, 0x03, 0x04, 0x09, 0x09, 0x04, 0x03, 0x00, 0x0D, 0x0F, 0x0E, 0x00, 0x00, 0x00, 0x00, // Row 6: Chin & blade
    0x00, 0x00, 0x03, 0x07, 0x08, 0x08, 0x07, 0x03, 0x08, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 7: Shoulders & crossguard
    0x00, 0x02, 0x0A, 0x07, 0x09, 0x09, 0x07, 0x0A, 0x0B, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 8: Arms, tunic & hilt
    0x00, 0x02, 0x0A, 0x06, 0x08, 0x08, 0x06, 0x0A, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 9: Torso & hands
    0x00, 0x00, 0x02, 0x03, 0x04, 0x04, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 10: Belt
    0x00, 0x00, 0x05, 0x07, 0x08, 0x08, 0x07, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 11: Tunic skirt
    0x00, 0x02, 0x05, 0x06, 0x07, 0x07, 0x06, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 12: Lower tunic
    0x00, 0x00, 0x02, 0x09, 0x00, 0x00, 0x09, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 13: Legs
    0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Row 14: Boots
    0x00, 0x01, 0x02, 0x02, 0x00, 0x00, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // Row 15: Boot soles
};

const uint8_t scanlines_alpha = 0xaa;

// Shared state controlled by Lua
struct AppState {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// C++ function exposed to Lua: set_bg_color(r, g, b)
static int l_set_bg_color(lua_State* L) {
    AppState *state = static_cast<AppState*>(lua_touserdata(L, lua_upvalueindex(1)));
    state->r = static_cast<uint8_t>(luaL_checkinteger(L, 1));
    state->g = static_cast<uint8_t>(luaL_checkinteger(L, 2));
    state->b = static_cast<uint8_t>(luaL_checkinteger(L, 3));
    return 0;
}

// static int l_pset(lua_State *L) {
// 	uint8_t x = static_cast<uint8_t>(luaL_checkinteger(L, 1));
// }

inline uint32_t video_blend(uint32_t c0, uint32_t c1) {
	return
		((((c0 & 0x00ff00ff) + (c1 & 0x00ff00ff)) >> 1) & 0x00ff00ff) |
		((((c0 & 0x0000ff00) + (c1 & 0x0000ff00)) >> 1) & 0x0000ff00) |
		(scanlines_alpha << 24); // alpha
}

int main(int argc, char* argv[]) {
	vdc_t vdc;

    // 1. Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "[SDL] SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("SDL3 + Lua 5.5", 960, 540, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
	SDL_SetWindowAspectRatio(window, (float)PIXELS_PER_SCANLINE/SCANLINES, (float)PIXELS_PER_SCANLINE/SCANLINES);
	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
	SDL_SetRenderVSync(renderer, 1);
	int i;
    SDL_GetRenderVSync(renderer, &i);
	if (i) {
        printf("[SDL] vsync every %i frame(s)\n", i);
    } else {
		printf("[SDL] vsync is off\n");
    }

	SDL_SetRenderLogicalPresentation(renderer, PIXELS_PER_SCANLINE, SCANLINES, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_HideCursor();				// make sure cursor isn't visible

	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PIXELS_PER_SCANLINE, 2*SCANLINES);
	uint8_t vdc_texture_bytes_per_pixel = (SDL_BITSPERPIXEL(texture->format) >> 3);
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);


    std::memcpy(vdc.ram.data() + 0x30000, sprite1, sizeof(sprite1));

    // 2. Initialize Lua and register C++ callback
    AppState state;
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

	std::cout << "[Lua] Release: " << LUA_RELEASE << "\n"; // e.g., "Lua 5.5.1"

    lua_pushlightuserdata(L, &state);
    lua_pushcclosure(L, l_set_bg_color, 1);
    lua_setglobal(L, "set_bg_color");

    // 3. Run an inline Lua script to set initial color and define a key handler
    const char* lua_script = R"lua(
        print("Running " .. _VERSION .. " inside SDL3!")
		r = 0
		g = 0
		b = 0
        set_bg_color(r, g, b) -- Initial black

        function on_space_pressed()
		  r = math.random(0, 255)
		  g = math.random(0, 255)
		  b = math.random(0, 255)
    	  set_bg_color(r, g, b)
        end
    )lua";

    if (luaL_dostring(L, lua_script) != LUA_OK) {
        std::cerr << "Lua error: " << lua_tostring(L, -1) << "\n";
        lua_pop(L, 1);
    }

    // 4. Main SDL3 event and render loop
    bool running = true;
    SDL_Event event;

    int16_t x = 0;

    typedef struct {
        int x;
        int y;
    } pos;

    #include "rca.hpp"
    rca_t rca;
    pos boel[256];
    for (int i=0;i<256;i++) {
        boel[i].x = rca.byte();
        boel[i].y = rca.byte();
    }


    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_SPACE) {
                // Call Lua function when Space is pressed
                lua_getglobal(L, "on_space_pressed");
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    std::cerr << "Lua callback error: " << lua_tostring(L, -1) << "\n";
                    lua_pop(L, 1);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, state.r, state.g, state.b, 255);
        SDL_RenderClear(renderer);

        vdc.clear(0x93);
        for (int i=0; i<256;i++) {
            vdc.blit(0x30000, boel[i].x, boel[i].y, 16, 16);
        }
        vdc.blit(0x30000, x, 12, 16, 16);
        x++; if (x == 350) x = -20;

		void *pixels;
		int pitch;
		SDL_LockTexture(texture, nullptr, &pixels, &pitch);
			pitch /= vdc_texture_bytes_per_pixel;
			uint8_t *cur_pix_src = vdc.buffer.data();
			uint8_t *nxt_pix_src = cur_pix_src + PIXELS_PER_SCANLINE;
			uint32_t *cur_pix_dst = (uint32_t *)pixels;
			uint32_t *nxt_pix_dst = cur_pix_dst + pitch;
			for (int y = 0; y < SCANLINES; y++) {
				if (y == (SCANLINES - 1)) nxt_pix_src = cur_pix_src;
				for (int x = 0; x < PIXELS_PER_SCANLINE; x++) {
					*cur_pix_dst++ = palette[*cur_pix_src];
					*nxt_pix_dst++ = video_blend(palette[*cur_pix_src], palette[*nxt_pix_src]);
					cur_pix_src++;
					nxt_pix_src++;
				}
				cur_pix_dst += pitch;
				nxt_pix_dst += pitch;
			}
		SDL_UnlockTexture(texture);

		SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    // 5. Clean up
    lua_close(L);
	SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
