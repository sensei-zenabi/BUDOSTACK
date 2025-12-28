#ifndef BUDOSTACK_RETRO_SHADER_BRIDGE_H
#define BUDOSTACK_RETRO_SHADER_BRIDGE_H

#include <stddef.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window;

struct retro_shader_bridge;

struct retro_shader_bridge *retro_shader_bridge_create(struct SDL_Window *window,
                                                       const char *shader_root,
                                                       const char **shader_paths,
                                                       size_t shader_count);
void retro_shader_bridge_destroy(struct retro_shader_bridge *bridge);

int retro_shader_bridge_set_frame(struct retro_shader_bridge *bridge,
                                  const void *data,
                                  unsigned width,
                                  unsigned height,
                                  size_t pitch,
                                  enum retro_pixel_format format);

int retro_shader_bridge_render(struct retro_shader_bridge *bridge,
                               unsigned frame_count);

#ifdef __cplusplus
}
#endif

#endif
