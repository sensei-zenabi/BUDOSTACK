#ifndef BUDO_SHADER_STACK_H
#define BUDO_SHADER_STACK_H

#include <stddef.h>
#include <SDL_opengl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct budo_shader_stack;

/* CREATE AN EMPTY SHADER STACK INSTANCE.
*  out_stack RECEIVES THE ALLOCATED HANDLE ON SUCCESS.
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE.
*/
int budo_shader_stack_init(struct budo_shader_stack **out_stack);
/* FREE ALL STACK RESOURCES AND THE STACK ITSELF.
*  SAFE TO CALL WITH NULL.
*/
void budo_shader_stack_destroy(struct budo_shader_stack *stack);

/* LOAD A SHADER CHAIN FROM FILE PATHS.
*  shader_paths IS AN ARRAY OF FRAGMENT SHADER FILES APPLIED IN ORDER.
*  PREVIOUSLY LOADED SHADERS ARE RELEASED BEFORE LOADING NEW ONES.
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE.
*/
int budo_shader_stack_load(struct budo_shader_stack *stack,
                           const char *const *shader_paths,
                           size_t shader_count);
/* UNLOAD ALL CURRENTLY LOADED SHADERS FROM A STACK INSTANCE. */
void budo_shader_stack_clear(struct budo_shader_stack *stack);

/* RENDER source_texture THROUGH THE LOADED SHADER CHAIN.
*  source_width/source_height DESCRIBE THE SOURCE TEXTURE SIZE.
*  output_width/output_height ARE THE FINAL VIEWPORT TARGET SIZE.
*  source_tex_is_fbo SHOULD BE 1 FOR FBO-ORIGIN TEXTURES, ELSE 0.
*  frame_value IS FOR SHADERS USING A FRAME COUNTER UNIFORM.
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE.
*/
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
