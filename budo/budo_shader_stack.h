#ifndef BUDO_SHADER_STACK_H
#define BUDO_SHADER_STACK_H

#include <stddef.h>
#include <SDL_opengl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct budo_shader_stack;

int budo_shader_stack_init(struct budo_shader_stack **out_stack);
void budo_shader_stack_destroy(struct budo_shader_stack *stack);

int budo_shader_stack_load(struct budo_shader_stack *stack,
                           const char *const *shader_paths,
                           size_t shader_count);
void budo_shader_stack_clear(struct budo_shader_stack *stack);

int budo_shader_stack_render(struct budo_shader_stack *stack,
                             GLuint source_texture,
                             int source_width,
                             int source_height,
                             int output_width,
                             int output_height,
                             int source_tex_is_fbo,
                             int frame_value);

#ifdef __cplusplus
}
#endif

#endif
