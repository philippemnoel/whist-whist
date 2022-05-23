#ifndef WHIST_CLIENT_FRONTEND_SDL_COMMON_H
#define WHIST_CLIENT_FRONTEND_SDL_COMMON_H

#define SDL_MAIN_HANDLED
// So that SDL sees symbols such as memcpy
#ifdef _WIN32
#include <windows.h>
#else
#include <string.h>
#endif
#include <SDL2/SDL.h>
#include <whist/core/whist.h>
#include "../api.h"
#include "../frontend.h"

#define SDL_COMMON_HEADER_ENTRY(return_type, name, ...) return_type sdl_##name(__VA_ARGS__);
FRONTEND_API(SDL_COMMON_HEADER_ENTRY)
#undef SDL_COMMON_HEADER_ENTRY

#endif  // WHIST_CLIENT_FRONTEND_SDL_COMMON_H
