#include <cstddef>
#include <cstdint>
#include <iostream>
#include <lua.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "common.hpp"
#include "rca.hpp"
#include "vdc.hpp"

const bool scanlines = true;
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
	vdc.reset();

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

	rca_t rca;
	for (size_t y=0; y<SCANLINES; y++) {
		for (size_t x=0; x<PIXELS_PER_SCANLINE; x++) {
			if (((x - 160)*(x - 160) + (y - 90)*(y - 90)) < 64*64) {
				vdc.buffer[(PIXELS_PER_SCANLINE * y) + x] = vdc.palette[(((y % 16) << 4) + x) & 0xff];
			} else {
				vdc.buffer[(PIXELS_PER_SCANLINE * y) + x] = vdc.palette[rca.byte()];
			}
		}
	}

	vdc.clear(0x93);

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

		void *pixels;
		int pitch;
		SDL_LockTexture(texture, nullptr, &pixels, &pitch);
			pitch /= vdc_texture_bytes_per_pixel;
			uint32_t *cur_pix_src = vdc.buffer;
			uint32_t *nxt_pix_src = cur_pix_src + PIXELS_PER_SCANLINE;
			uint32_t *cur_pix_dst = (uint32_t *)pixels;
			uint32_t *nxt_pix_dst = cur_pix_dst + pitch;
			for (int y = 0; y < SCANLINES; y++) {
				if (y == (SCANLINES - 1)) nxt_pix_src = cur_pix_src;
				for (int x = 0; x < PIXELS_PER_SCANLINE; x++) {
					*cur_pix_dst++ = *cur_pix_src;
					*nxt_pix_dst++ = scanlines ? video_blend(*cur_pix_src, *nxt_pix_src) : *cur_pix_src;
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
