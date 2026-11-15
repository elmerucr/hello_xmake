#include <iostream>
#include <lua.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// Shared state controlled by Lua
struct AppState {
    Uint8 r = 30;
    Uint8 g = 30;
    Uint8 b = 46;
};

// C++ function exposed to Lua: set_bg_color(r, g, b)
static int l_set_bg_color(lua_State* L) {
    AppState* state = static_cast<AppState*>(lua_touserdata(L, lua_upvalueindex(1)));
    state->r = static_cast<Uint8>(luaL_checkinteger(L, 1));
    state->g = static_cast<Uint8>(luaL_checkinteger(L, 2));
    state->b = static_cast<Uint8>(luaL_checkinteger(L, 3));
    return 0;
}

int main(int argc, char* argv[]) {
    // 1. Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("SDL3 + Lua 5.5", 800, 600, 0);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 2. Initialize Lua and register C++ callback
    AppState state;
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

	std::cout << "[Lua] Release: " << LUA_RELEASE << "\n"; // e.g., "Lua 5.5.1"

    lua_pushlightuserdata(L, &state);
    lua_pushcclosure(L, l_set_bg_color, 1);
    lua_setglobal(L, "set_bg_color");

    // 3. Run an inline Lua script to set initial color and define a key handler
    const char* lua_script = R"(
        print("Running " .. _VERSION .. " inside SDL3!")
        set_bg_color(40, 80, 140) -- Initial steel blue

        function on_space_pressed()
            set_bg_color(math.random(30, 220), math.random(30, 220), math.random(30, 220))
        end
    )";

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
        SDL_RenderPresent(renderer);
    }

    // 5. Clean up
    lua_close(L);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
