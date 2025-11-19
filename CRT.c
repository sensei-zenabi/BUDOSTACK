#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#include <SDL2/SDL_syswm.h>
#endif

#if defined(__has_include)
#if __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#define BUDOSTACK_HAVE_X11 1
#else
#define BUDOSTACK_HAVE_X11 0
#endif
#else
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#define BUDOSTACK_HAVE_X11 1
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if !BUDOSTACK_HAVE_SDL2 || !BUDOSTACK_HAVE_X11
int main(void)
{
    fprintf(stderr, "CRT overlay requires SDL2 and X11 development files.\n");
    return 1;
}
#else

struct crt_shader_program {
    GLuint program;
    GLint uniform_texture;
    GLint uniform_output_size;
    GLint uniform_texture_size;
    GLint uniform_time;
    GLint uniform_frame;
    GLint uniform_mvp;
};

struct crt_state {
    SDL_Window *window;
    SDL_GLContext gl_context;
    Display *display;
    Window root_window;
    int screen_width;
    int screen_height;
    GLuint capture_texture;
    GLuint framebuffer;
    GLuint intermediate_textures[2];
    GLuint quad_vao;
    GLuint quad_vbo;
    GLuint quad_ebo;
    struct crt_shader_program *shaders;
    size_t shader_count;
    uint64_t frame_count;
    uint8_t *capture_buffer;
    size_t capture_capacity;
};

static void crt_log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static char *crt_load_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        crt_log_error("Failed to open %s: %s", path, strerror(errno));
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        crt_log_error("Failed to seek %s: %s", path, strerror(errno));
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        crt_log_error("Failed to measure %s: %s", path, strerror(errno));
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        crt_log_error("Failed to rewind %s: %s", path, strerror(errno));
        fclose(file);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)size + 1u);
    if (!buffer) {
        crt_log_error("Out of memory while reading %s", path);
        fclose(file);
        return NULL;
    }

    size_t read = fread(buffer, 1u, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        crt_log_error("Failed to read %s", path);
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}

static GLuint crt_compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0u) {
        crt_log_error("Unable to allocate shader");
        return 0u;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        char *log = NULL;
        if (log_length > 0) {
            log = (char *)malloc((size_t)log_length + 1u);
        }
        if (log) {
            glGetShaderInfoLog(shader, log_length, NULL, log);
            crt_log_error("Shader compile error: %s", log);
            free(log);
        }
        glDeleteShader(shader);
        return 0u;
    }

    return shader;
}

static GLuint crt_link_program(GLuint vertex, GLuint fragment)
{
    GLuint program = glCreateProgram();
    if (program == 0u) {
        crt_log_error("Unable to allocate program");
        return 0u;
    }

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0u, "VertexCoord");
    glBindAttribLocation(program, 1u, "TexCoord");
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        char *log = NULL;
        if (log_length > 0) {
            log = (char *)malloc((size_t)log_length + 1u);
        }
        if (log) {
            glGetProgramInfoLog(program, log_length, NULL, log);
            crt_log_error("Program link error: %s", log);
            free(log);
        }
        glDeleteProgram(program);
        return 0u;
    }

    return program;
}

static int crt_create_shader_from_file(struct crt_shader_program *out, const char *path)
{
    char *file_source = crt_load_file(path);
    if (!file_source) {
        return -1;
    }

    const char *prefix = "#version 330 core\n";
    size_t prefix_len = strlen(prefix);
    size_t source_len = strlen(file_source);

    size_t vertex_len = prefix_len + strlen("#define VERTEX\n") + source_len + 1u;
    size_t fragment_len = prefix_len + strlen("#define FRAGMENT\n") + source_len + 1u;
    char *vertex_src = (char *)malloc(vertex_len);
    char *fragment_src = (char *)malloc(fragment_len);
    if (!vertex_src || !fragment_src) {
        crt_log_error("Out of memory while preparing shader %s", path);
        free(file_source);
        free(vertex_src);
        free(fragment_src);
        return -1;
    }

    snprintf(vertex_src, vertex_len, "%s#define VERTEX\n%s", prefix, file_source);
    snprintf(fragment_src, fragment_len, "%s#define FRAGMENT\n%s", prefix, file_source);

    GLuint vertex = crt_compile_shader(GL_VERTEX_SHADER, vertex_src);
    GLuint fragment = 0u;
    if (vertex != 0u) {
        fragment = crt_compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    }

    free(file_source);
    free(vertex_src);
    free(fragment_src);

    if (vertex == 0u || fragment == 0u) {
        if (vertex != 0u) {
            glDeleteShader(vertex);
        }
        if (fragment != 0u) {
            glDeleteShader(fragment);
        }
        return -1;
    }

    GLuint program = crt_link_program(vertex, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (program == 0u) {
        return -1;
    }

    out->program = program;
    out->uniform_texture = glGetUniformLocation(program, "Texture");
    out->uniform_output_size = glGetUniformLocation(program, "OutputSize");
    out->uniform_texture_size = glGetUniformLocation(program, "TextureSize");
    out->uniform_time = glGetUniformLocation(program, "Time");
    out->uniform_frame = glGetUniformLocation(program, "FrameCount");
    out->uniform_mvp = glGetUniformLocation(program, "MVPMatrix");

    return 0;
}

static int crt_create_quad(struct crt_state *state)
{
    const GLfloat vertices[] = {
        // pos x, pos y, tex u, tex v
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f
    };
    const GLuint indices[] = {0u, 1u, 2u, 2u, 3u, 0u};

    glGenVertexArrays(1u, &state->quad_vao);
    glBindVertexArray(state->quad_vao);

    glGenBuffers(1u, &state->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenBuffers(1u, &state->quad_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->quad_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0u);
    glVertexAttribPointer(0u, 2, GL_FLOAT, GL_FALSE, 4 * (GLint)sizeof(GLfloat), (void *)0);
    glEnableVertexAttribArray(1u);
    glVertexAttribPointer(1u, 2, GL_FLOAT, GL_FALSE, 4 * (GLint)sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));

    glBindVertexArray(0u);

    return 0;
}

static int crt_create_capture(struct crt_state *state)
{
    glGenTextures(1u, &state->capture_texture);
    glBindTexture(GL_TEXTURE_2D, state->capture_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, state->screen_width, state->screen_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenTextures(2u, state->intermediate_textures);
    for (size_t i = 0; i < 2u; ++i) {
        glBindTexture(GL_TEXTURE_2D, state->intermediate_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, state->screen_width, state->screen_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    glGenFramebuffers(1u, &state->framebuffer);
    return 0;
}

static void crt_destroy_state(struct crt_state *state)
{
    if (!state) {
        return;
    }

    if (state->shaders) {
        for (size_t i = 0; i < state->shader_count; ++i) {
            if (state->shaders[i].program != 0u) {
                glDeleteProgram(state->shaders[i].program);
            }
        }
        free(state->shaders);
    }

    glDeleteFramebuffers(1u, &state->framebuffer);
    glDeleteTextures(1u, &state->capture_texture);
    glDeleteTextures(2u, state->intermediate_textures);
    glDeleteVertexArrays(1u, &state->quad_vao);
    glDeleteBuffers(1u, &state->quad_vbo);
    glDeleteBuffers(1u, &state->quad_ebo);

    free(state->capture_buffer);

    if (state->gl_context) {
        SDL_GL_DeleteContext(state->gl_context);
    }
    if (state->window) {
        SDL_DestroyWindow(state->window);
    }
    if (state->display) {
        XCloseDisplay(state->display);
    }
    SDL_Quit();
}

static unsigned int crt_mask_shift(unsigned long mask)
{
    unsigned int shift = 0u;
    while ((mask & 1ul) == 0ul && mask != 0ul) {
        mask >>= 1u;
        ++shift;
    }
    return shift;
}

static unsigned int crt_mask_bits(unsigned long mask)
{
    unsigned int bits = 0u;
    while (mask != 0ul) {
        bits += (unsigned int)(mask & 1ul);
        mask >>= 1u;
    }
    return bits;
}

static void crt_capture_desktop(struct crt_state *state)
{
    XImage *image = XGetImage(state->display, state->root_window, 0, 0, (unsigned int)state->screen_width, (unsigned int)state->screen_height, AllPlanes, ZPixmap);
    if (!image) {
        return;
    }

    const size_t required = (size_t)state->screen_width * (size_t)state->screen_height * 4u;
    if (required > state->capture_capacity) {
        uint8_t *new_buffer = (uint8_t *)realloc(state->capture_buffer, required);
        if (!new_buffer) {
            XDestroyImage(image);
            return;
        }
        state->capture_buffer = new_buffer;
        state->capture_capacity = required;
    }

    const unsigned int r_shift = crt_mask_shift(image->red_mask);
    const unsigned int g_shift = crt_mask_shift(image->green_mask);
    const unsigned int b_shift = crt_mask_shift(image->blue_mask);
    const unsigned int r_bits = crt_mask_bits(image->red_mask);
    const unsigned int g_bits = crt_mask_bits(image->green_mask);
    const unsigned int b_bits = crt_mask_bits(image->blue_mask);

    uint8_t *out = state->capture_buffer;
    for (int y = 0; y < state->screen_height; ++y) {
        for (int x = 0; x < state->screen_width; ++x) {
            const unsigned long pixel = XGetPixel(image, x, y);
            uint8_t r = (uint8_t)((pixel & image->red_mask) >> r_shift);
            uint8_t g = (uint8_t)((pixel & image->green_mask) >> g_shift);
            uint8_t b = (uint8_t)((pixel & image->blue_mask) >> b_shift);

            if (r_bits < 8u) {
                r = (uint8_t)((r * 255u) / ((1u << r_bits) - 1u));
            }
            if (g_bits < 8u) {
                g = (uint8_t)((g * 255u) / ((1u << g_bits) - 1u));
            }
            if (b_bits < 8u) {
                b = (uint8_t)((b * 255u) / ((1u << b_bits) - 1u));
            }

            *out++ = r;
            *out++ = g;
            *out++ = b;
            *out++ = 255u;
        }
    }

    XDestroyImage(image);

    glBindTexture(GL_TEXTURE_2D, state->capture_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, state->screen_width, state->screen_height, GL_RGBA, GL_UNSIGNED_BYTE, state->capture_buffer);
}

static void crt_apply_passthrough(SDL_Window *window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) == SDL_FALSE) {
        return;
    }

    if (info.subsystem == SDL_SYSWM_X11) {
        Display *display = info.info.x11.display;
        Window xwindow = info.info.x11.window;
        int shape_event = 0;
        int shape_error = 0;
        if (XShapeQueryExtension(display, &shape_event, &shape_error)) {
            XRectangle rect = {0, 0, 0, 0};
            XShapeCombineRectangles(display, xwindow, ShapeInput, 0, 0, &rect, 0, ShapeSet, Unsorted);
            XFlush(display);
        }
    }
}

static void crt_render(struct crt_state *state)
{
    crt_capture_desktop(state);

    GLuint input_texture = state->capture_texture;
    GLuint output_texture = state->intermediate_textures[0];

    const GLfloat identity_mvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    glBindVertexArray(state->quad_vao);

    for (size_t i = 0; i < state->shader_count; ++i) {
        const int is_last = (i + 1u == state->shader_count);
        struct crt_shader_program *shader = &state->shaders[i];

        if (is_last) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, state->framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture, 0);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                continue;
            }
        }

        glViewport(0, 0, state->screen_width, state->screen_height);
        glUseProgram(shader->program);

        if (shader->uniform_texture >= 0) {
            glUniform1i(shader->uniform_texture, 0);
        }
        if (shader->uniform_output_size >= 0) {
            GLfloat size[2] = {(GLfloat)state->screen_width, (GLfloat)state->screen_height};
            glUniform2fv(shader->uniform_output_size, 1, size);
        }
        if (shader->uniform_texture_size >= 0) {
            GLfloat size[2] = {(GLfloat)state->screen_width, (GLfloat)state->screen_height};
            glUniform2fv(shader->uniform_texture_size, 1, size);
        }
        if (shader->uniform_time >= 0) {
            const GLfloat time_seconds = (GLfloat)SDL_GetTicks() / 1000.0f;
            glUniform1f(shader->uniform_time, time_seconds);
        }
        if (shader->uniform_frame >= 0) {
            glUniform1i(shader->uniform_frame, (GLint)(state->frame_count & 0x7fffffff));
        }
        if (shader->uniform_mvp >= 0) {
            glUniformMatrix4fv(shader->uniform_mvp, 1, GL_FALSE, identity_mvp);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input_texture);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        if (!is_last) {
            GLuint next_input = output_texture;
            output_texture = (output_texture == state->intermediate_textures[0]) ? state->intermediate_textures[1] : state->intermediate_textures[0];
            input_texture = next_input;
        }
    }

    glBindVertexArray(0u);
    SDL_GL_SwapWindow(state->window);
    ++state->frame_count;
}

static int crt_init(struct crt_state *state, int shader_count)
{
    memset(state, 0, sizeof(*state));

    state->display = XOpenDisplay(NULL);
    if (!state->display) {
        crt_log_error("Unable to open X11 display");
        return -1;
    }
    state->root_window = DefaultRootWindow(state->display);
    state->screen_width = DisplayWidth(state->display, DefaultScreen(state->display));
    state->screen_height = DisplayHeight(state->display, DefaultScreen(state->display));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        crt_log_error("SDL init failed: %s", SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    state->window = SDL_CreateWindow(
        "BUDOSTACK CRT",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        state->screen_width,
        state->screen_height,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_OPENGL | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!state->window) {
        crt_log_error("SDL window failed: %s", SDL_GetError());
        return -1;
    }

    crt_apply_passthrough(state->window);

    state->gl_context = SDL_GL_CreateContext(state->window);
    if (!state->gl_context) {
        crt_log_error("SDL GL context failed: %s", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(state->window, state->gl_context);
    SDL_GL_SetSwapInterval(1);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    if (crt_create_quad(state) != 0) {
        return -1;
    }
    if (crt_create_capture(state) != 0) {
        return -1;
    }

    state->shaders = (struct crt_shader_program *)calloc((size_t)shader_count > 0 ? (size_t)shader_count : 1u, sizeof(struct crt_shader_program));
    if (!state->shaders) {
        return -1;
    }

    return 0;
}

static int crt_parse_arguments(int argc, char **argv, char shader_paths[][PATH_MAX], size_t *shader_count)
{
    *shader_count = 0u;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shader") == 0) {
            if (i + 1 >= argc) {
                crt_log_error("Missing shader path after %s", argv[i]);
                return -1;
            }
            if (*shader_count >= 8u) {
                crt_log_error("Too many shaders specified (max 8)");
                return -1;
            }
            ++i;
            strncpy(shader_paths[*shader_count], argv[i], PATH_MAX - 1u);
            shader_paths[*shader_count][PATH_MAX - 1u] = '\0';
            ++(*shader_count);
        } else {
            crt_log_error("Unknown argument: %s", argv[i]);
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    char shader_paths[8][PATH_MAX];
    size_t shader_count = 0u;
    if (crt_parse_arguments(argc, argv, shader_paths, &shader_count) != 0) {
        return 1;
    }

    if (shader_count == 0u) {
        crt_log_error("No shaders provided. Use -s <path> to specify shaders.");
        return 1;
    }

    struct crt_state state;
    if (crt_init(&state, (int)shader_count) != 0) {
        crt_destroy_state(&state);
        return 1;
    }

    for (size_t i = 0; i < shader_count; ++i) {
        if (crt_create_shader_from_file(&state.shaders[i], shader_paths[i]) != 0) {
            crt_log_error("Failed to initialize shader: %s", shader_paths[i]);
            crt_destroy_state(&state);
            return 1;
        }
    }
    state.shader_count = shader_count;

    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        crt_render(&state);
    }

    crt_destroy_state(&state);
    return 0;
}

#endif
