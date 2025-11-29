#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#else
#define BUDOSTACK_HAVE_SDL2 0
#endif
#else
#include <SDL2/SDL.h>
#define BUDOSTACK_HAVE_SDL2 1
#endif

#if BUDOSTACK_HAVE_SDL2
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CRT_DEFAULT_SHADER "shaders/crtscreen.glsl"
#define CRT_VERTEX_SIZE 4
#define CRT_TEXCOORD_SIZE 2

#if BUDOSTACK_HAVE_SDL2
struct crt_program {
    GLuint program;
    GLint attr_vertex;
    GLint attr_texcoord;
    GLint uniform_mvp;
    GLint uniform_frame_direction;
    GLint uniform_frame_count;
    GLint uniform_output_size;
    GLint uniform_texture_size;
    GLint uniform_input_size;
    GLint uniform_texture_sampler;
};

struct crt_state {
    SDL_Window *window;
    SDL_GLContext gl_context;
    Display *display;
    Window root_window;
    int screen_width;
    int screen_height;
    GLuint quad_vbo;
    GLuint framebuffer_texture;
    struct crt_program shader;
    Uint32 frame_count;
};

static void crt_print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [-s shader_path] [-f fps]\n", progname);
    fprintf(stderr, "  -s, --shader PATH   GLSL shader file using VERTEX/FRAGMENT markers.\n");
    fprintf(stderr, "  -f, --fps FPS       Limit updates to FPS (default: 30).\n");
}

static char *crt_read_text_file(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long length = ftell(fp);
    if (length < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    size_t size = (size_t)length;
    char *buffer = (char *)malloc(size + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1u, size, fp);
    fclose(fp);
    if (read_bytes != size) {
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}

static GLuint crt_compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    if (shader == 0u) {
        fprintf(stderr, "Failed to create shader for %s\n", label);
        return 0u;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = (char *)malloc((size_t)log_length);
            if (log) {
                glGetShaderInfoLog(shader, log_length, NULL, log);
                fprintf(stderr, "%s compile error:\n%s\n", label, log);
                free(log);
            }
        }
        glDeleteShader(shader);
        return 0u;
    }

    return shader;
}

static int crt_build_program(const char *shader_path, struct crt_program *out_program) {
    size_t source_length = 0u;
    char *source = crt_read_text_file(shader_path, &source_length);
    if (!source) {
        return -1;
    }

    const char *vertex_prefix = "#define VERTEX\n";
    const char *fragment_prefix = "#define FRAGMENT\n";
    size_t vertex_length = strlen(vertex_prefix) + source_length + 1u;
    size_t fragment_length = strlen(fragment_prefix) + source_length + 1u;

    char *vertex_source = (char *)malloc(vertex_length);
    char *fragment_source = (char *)malloc(fragment_length);
    if (!vertex_source || !fragment_source) {
        free(source);
        free(vertex_source);
        free(fragment_source);
        return -1;
    }

    snprintf(vertex_source, vertex_length, "%s%s", vertex_prefix, source);
    snprintf(fragment_source, fragment_length, "%s%s", fragment_prefix, source);

    GLuint vertex_shader = crt_compile_shader(GL_VERTEX_SHADER, vertex_source, "vertex shader");
    GLuint fragment_shader = crt_compile_shader(GL_FRAGMENT_SHADER, fragment_source, "fragment shader");

    free(source);
    free(vertex_source);
    free(fragment_source);

    if (vertex_shader == 0u || fragment_shader == 0u) {
        if (vertex_shader != 0u) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0u) {
            glDeleteShader(fragment_shader);
        }
        return -1;
    }

    GLuint program = glCreateProgram();
    if (program == 0u) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return -1;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    if (link_status == GL_FALSE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = (char *)malloc((size_t)log_length);
            if (log) {
                glGetProgramInfoLog(program, log_length, NULL, log);
                fprintf(stderr, "Shader link error:\n%s\n", log);
                free(log);
            }
        }
        glDeleteProgram(program);
        return -1;
    }

    memset(out_program, 0, sizeof(*out_program));
    out_program->program = program;
    out_program->attr_vertex = glGetAttribLocation(program, "VertexCoord");
    out_program->attr_texcoord = glGetAttribLocation(program, "TexCoord");
    out_program->uniform_mvp = glGetUniformLocation(program, "MVPMatrix");
    out_program->uniform_frame_direction = glGetUniformLocation(program, "FrameDirection");
    out_program->uniform_frame_count = glGetUniformLocation(program, "FrameCount");
    out_program->uniform_output_size = glGetUniformLocation(program, "OutputSize");
    out_program->uniform_texture_size = glGetUniformLocation(program, "TextureSize");
    out_program->uniform_input_size = glGetUniformLocation(program, "InputSize");
    out_program->uniform_texture_sampler = glGetUniformLocation(program, "Texture");

    return 0;
}

static void crt_destroy_program(struct crt_program *program) {
    if (!program) {
        return;
    }
    if (program->program != 0u) {
        glDeleteProgram(program->program);
        program->program = 0u;
    }
}

static int crt_initialize_geometry(struct crt_state *state) {
    static const GLfloat quad_vertices[] = {
        // x, y, z, w, u, v
        -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f
    };

    glGenBuffers(1, &state->quad_vbo);
    if (state->quad_vbo == 0u) {
        return -1;
    }

    glBindBuffer(GL_ARRAY_BUFFER, state->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0u);
    return 0;
}

static int crt_initialize_texture(struct crt_state *state) {
    glGenTextures(1, &state->framebuffer_texture);
    if (state->framebuffer_texture == 0u) {
        return -1;
    }

    glBindTexture(GL_TEXTURE_2D, state->framebuffer_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state->screen_width, state->screen_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0u);
    return 0;
}

static int crt_capture_screen(struct crt_state *state, uint8_t *buffer, size_t buffer_size) {
    XImage *image = XGetImage(state->display,
                              state->root_window,
                              0,
                              0,
                              (unsigned int)state->screen_width,
                              (unsigned int)state->screen_height,
                              AllPlanes,
                              ZPixmap);
    if (!image) {
        fprintf(stderr, "Failed to capture screen image.\n");
        return -1;
    }

    if (image->bits_per_pixel != 32) {
        fprintf(stderr, "Unsupported screen format: %d bits per pixel.\n", image->bits_per_pixel);
        XDestroyImage(image);
        return -1;
    }

    size_t expected = (size_t)state->screen_width * (size_t)state->screen_height * 4u;
    if (buffer_size < expected) {
        XDestroyImage(image);
        return -1;
    }

    for (int y = 0; y < state->screen_height; y++) {
        uint8_t *dst_row = buffer + ((size_t)y * (size_t)state->screen_width * 4u);
        uint8_t *src_row = (uint8_t *)image->data + ((size_t)y * (size_t)image->bytes_per_line);
        memcpy(dst_row, src_row, (size_t)state->screen_width * 4u);
    }

    XDestroyImage(image);
    return 0;
}

static void crt_render_frame(struct crt_state *state, const uint8_t *pixels) {
    glViewport(0, 0, state->screen_width, state->screen_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(state->shader.program);
    glBindBuffer(GL_ARRAY_BUFFER, state->quad_vbo);

    if (state->shader.attr_vertex >= 0) {
        glEnableVertexAttribArray((GLuint)state->shader.attr_vertex);
        glVertexAttribPointer((GLuint)state->shader.attr_vertex,
                              CRT_VERTEX_SIZE,
                              GL_FLOAT,
                              GL_FALSE,
                              (CRT_VERTEX_SIZE + CRT_TEXCOORD_SIZE) * (GLsizei)sizeof(GLfloat),
                              (void *)0);
    }

    if (state->shader.attr_texcoord >= 0) {
        glEnableVertexAttribArray((GLuint)state->shader.attr_texcoord);
        glVertexAttribPointer((GLuint)state->shader.attr_texcoord,
                              CRT_TEXCOORD_SIZE,
                              GL_FLOAT,
                              GL_FALSE,
                              (CRT_VERTEX_SIZE + CRT_TEXCOORD_SIZE) * (GLsizei)sizeof(GLfloat),
                              (void *)(CRT_VERTEX_SIZE * (GLsizei)sizeof(GLfloat)));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state->framebuffer_texture);
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    state->screen_width,
                    state->screen_height,
                    GL_BGRA,
                    GL_UNSIGNED_BYTE,
                    pixels);

    static const GLfloat identity_mvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    if (state->shader.uniform_mvp >= 0) {
        glUniformMatrix4fv(state->shader.uniform_mvp, 1, GL_FALSE, identity_mvp);
    }
    if (state->shader.uniform_frame_direction >= 0) {
        glUniform1i(state->shader.uniform_frame_direction, 1);
    }
    if (state->shader.uniform_frame_count >= 0) {
        glUniform1i(state->shader.uniform_frame_count, (GLint)state->frame_count);
    }
    if (state->shader.uniform_output_size >= 0) {
        glUniform2f(state->shader.uniform_output_size, (GLfloat)state->screen_width, (GLfloat)state->screen_height);
    }
    if (state->shader.uniform_texture_size >= 0) {
        glUniform2f(state->shader.uniform_texture_size, (GLfloat)state->screen_width, (GLfloat)state->screen_height);
    }
    if (state->shader.uniform_input_size >= 0) {
        glUniform2f(state->shader.uniform_input_size, (GLfloat)state->screen_width, (GLfloat)state->screen_height);
    }
    if (state->shader.uniform_texture_sampler >= 0) {
        glUniform1i(state->shader.uniform_texture_sampler, 0);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (state->shader.attr_vertex >= 0) {
        glDisableVertexAttribArray((GLuint)state->shader.attr_vertex);
    }
    if (state->shader.attr_texcoord >= 0) {
        glDisableVertexAttribArray((GLuint)state->shader.attr_texcoord);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0u);
    glBindTexture(GL_TEXTURE_2D, 0u);
    glUseProgram(0u);
    SDL_GL_SwapWindow(state->window);
    state->frame_count++;
}

static void crt_shutdown(struct crt_state *state) {
    if (!state) {
        return;
    }
    if (state->quad_vbo != 0u) {
        glDeleteBuffers(1, &state->quad_vbo);
        state->quad_vbo = 0u;
    }
    if (state->framebuffer_texture != 0u) {
        glDeleteTextures(1, &state->framebuffer_texture);
        state->framebuffer_texture = 0u;
    }
    crt_destroy_program(&state->shader);
    if (state->gl_context) {
        SDL_GL_DeleteContext(state->gl_context);
        state->gl_context = NULL;
    }
    if (state->window) {
        SDL_DestroyWindow(state->window);
        state->window = NULL;
    }
    if (state->display) {
        XCloseDisplay(state->display);
        state->display = NULL;
    }
    SDL_Quit();
}
#endif

int main(int argc, char **argv) {
#if !BUDOSTACK_HAVE_SDL2
    (void)argc;
    (void)argv;
    fprintf(stderr, "This application requires SDL2 support.\n");
    return 1;
#else
    const char *shader_path = CRT_DEFAULT_SHADER;
    unsigned int target_fps = 30u;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shader") == 0) && i + 1 < argc) {
            shader_path = argv[++i];
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fps") == 0) && i + 1 < argc) {
            unsigned long value = strtoul(argv[++i], NULL, 10);
            if (value > 0 && value < 1000) {
                target_fps = (unsigned int)value;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            crt_print_usage(argv[0]);
            return 0;
        } else {
            crt_print_usage(argv[0]);
            return 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) != 0) {
        fprintf(stderr, "Failed to get desktop display mode: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    struct crt_state state;
    memset(&state, 0, sizeof(state));
    state.screen_width = mode.w;
    state.screen_height = mode.h;

    state.display = XOpenDisplay(NULL);
    if (!state.display) {
        fprintf(stderr, "Failed to open X11 display.\n");
        SDL_Quit();
        return 1;
    }
    state.root_window = DefaultRootWindow(state.display);

    state.window = SDL_CreateWindow("BUDOSTACK CRT",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   state.screen_width,
                                   state.screen_height,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!state.window) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        crt_shutdown(&state);
        return 1;
    }

    state.gl_context = SDL_GL_CreateContext(state.window);
    if (!state.gl_context) {
        fprintf(stderr, "Failed to create OpenGL context: %s\n", SDL_GetError());
        crt_shutdown(&state);
        return 1;
    }

    if (SDL_GL_SetSwapInterval(1) != 0) {
        fprintf(stderr, "Warning: failed to enable vsync: %s\n", SDL_GetError());
    }

    if (crt_build_program(shader_path, &state.shader) != 0) {
        fprintf(stderr, "Failed to build shader program from %s\n", shader_path);
        crt_shutdown(&state);
        return 1;
    }

    if (crt_initialize_geometry(&state) != 0) {
        fprintf(stderr, "Failed to create quad geometry.\n");
        crt_shutdown(&state);
        return 1;
    }

    if (crt_initialize_texture(&state) != 0) {
        fprintf(stderr, "Failed to create framebuffer texture.\n");
        crt_shutdown(&state);
        return 1;
    }

    size_t pixel_count = (size_t)state.screen_width * (size_t)state.screen_height;
    size_t pixel_buffer_size = pixel_count * 4u;
    uint8_t *pixel_buffer = (uint8_t *)malloc(pixel_buffer_size);
    if (!pixel_buffer) {
        fprintf(stderr, "Failed to allocate pixel buffer.\n");
        crt_shutdown(&state);
        return 1;
    }

    Uint32 frame_delay_ms = target_fps > 0u ? (Uint32)(1000u / target_fps) : 0u;
    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                    running = 0;
                }
            }
        }

        if (crt_capture_screen(&state, pixel_buffer, pixel_buffer_size) == 0) {
            crt_render_frame(&state, pixel_buffer);
        }

        if (frame_delay_ms > 0u) {
            SDL_Delay(frame_delay_ms);
        }
    }

    free(pixel_buffer);
    crt_shutdown(&state);
    return 0;
#endif
}
