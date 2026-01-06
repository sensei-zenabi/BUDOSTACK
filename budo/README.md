# BUDOSTACK Shader Stack (Extracted)

This folder contains a standalone shader stack extracted from `apps/terminal.c` so you can reuse the CRT/scanline shader passes in other SDL/OpenGL projects.

## Contents

- `budo_shader_stack.h/.c` — reusable shader stack implementation.
- Use the existing `../shaders/` directory as the shader sources.

## Integration steps

1. Copy `budo/` (this folder) and `shaders/` into your SDL project.
2. Add `budo/budo_shader_stack.c` to your build.
3. Make sure your SDL/OpenGL setup provides a GL 2.1 compatible context (the shader stack expects GLSL 1.10 style entry points and the `VertexCoord`, `TexCoord`, `COLOR` attributes used by the existing shaders).
4. Load shaders and render:

```c
#include "budo/budo_shader_stack.h"

struct budo_shader_stack *stack = NULL;
const char *shader_paths[] = {
    "shaders/crt-geom.glsl",
    "shaders/crt-guest.glsl"
};

if (budo_shader_stack_init(&stack) == 0) {
    if (budo_shader_stack_load(stack, shader_paths, 2u) == 0) {
        /* Render your scene into a GL texture named scene_texture. */
        int source_is_fbo = 0;
        int frame_value = 0;
        budo_shader_stack_render(stack,
                                 scene_texture,
                                 scene_width,
                                 scene_height,
                                 window_width,
                                 window_height,
                                 source_is_fbo,
                                 frame_value);
    }
}
```

## Notes

- `source_tex_is_fbo` should be `1` if your source texture comes from a framebuffer (so the shader stack flips texture coordinates correctly).
- The shader stack maintains its own history texture for `Prev0` uniforms.
- If you want to change shader parameters at runtime, update the shader source or set uniforms after `budo_shader_stack_load` returns.

## License

This code is extracted from BUDOSTACK and remains under the same license as the rest of the repository.

## Demo

`example.c` is a small SDL2/OpenGL demo that opens a 320x200 window, draws a rotating wireframe cube into a software pixel buffer, uploads it to a GL texture, and runs the shader stack.

Build example (adjust SDL2 include/lib paths as needed):

```sh
gcc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I. -I../budo \
    budo/example.c budo/budo_shader_stack.c \
    $(sdl2-config --cflags --libs) -lm -o budo_example
```

Run the demo from the repository root so the relative shader paths resolve:

```sh
./budo_example
```
