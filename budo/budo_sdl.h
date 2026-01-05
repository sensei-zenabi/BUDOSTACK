#ifndef BUDO_SDL_H
#define BUDO_SDL_H

#if defined(BUDOSTACK_HAVE_SDL2)
#define BUDO_HAVE_SDL2 BUDOSTACK_HAVE_SDL2
#elif defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define BUDO_HAVE_SDL2 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define BUDO_HAVE_SDL2 1
#else
#define BUDO_HAVE_SDL2 0
#endif
#else
#include <SDL2/SDL.h>
#define BUDO_HAVE_SDL2 1
#endif

#if defined(BUDOSTACK_HAVE_SDL2) && BUDOSTACK_HAVE_SDL2
#if !defined(BUDO_HAVE_SDL2)
#define BUDO_HAVE_SDL2 1
#endif
#if !defined(SDL_MAJOR_VERSION)
#include <SDL2/SDL.h>
#endif
#endif

#endif
