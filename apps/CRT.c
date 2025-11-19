#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
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
#if defined(__has_include)
#if __has_include(<SDL2/SDL_opengl.h>)
#include <SDL2/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif
#else
#include <SDL2/SDL_opengl.h>
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifndef BUDOSTACK_HAVE_XTEST
#if defined(__has_include)
#if __has_include(<X11/extensions/XTest.h>)
#define BUDOSTACK_HAVE_XTEST 1
#else
#define BUDOSTACK_HAVE_XTEST 0
#endif
#else
#define BUDOSTACK_HAVE_XTEST 1
#endif
#endif

#if BUDOSTACK_HAVE_XTEST
#include <X11/extensions/XTest.h>
#endif

#include "../lib/crt_shader_stack.h"
#include "../lib/crt_shader_gl.h"
#include "../lib/budostack_paths.h"
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CRT_DEFAULT_SHADER "shaders/fakelottes-geom.glsl"
#define CRT_TARGET_FPS 60u

#if BUDOSTACK_HAVE_SDL2

#ifndef SDL_MOUSEWHEEL_FLIPPED
#define SDL_MOUSEWHEEL_FLIPPED 1
#endif

static SDL_Window *crt_window_handle = NULL;
static SDL_GLContext crt_gl_context_handle = NULL;
static Display *crt_display = NULL;
static Window crt_root_window = 0;
static int crt_display_screen = 0;
static int crt_screen_width = 0;
static int crt_screen_height = 0;
static int crt_xtest_available = 0;
static GLuint crt_screen_texture = 0;
static GLuint crt_gl_framebuffer = 0;
static GLuint crt_gl_intermediate_textures[2] = {0u, 0u};
static int crt_intermediate_width = 0;
static int crt_intermediate_height = 0;
static GLuint crt_quad_vbo = 0;
static GLuint crt_bound_texture = 0;
static struct crt_gl_shader *crt_gl_shaders = NULL;
static size_t crt_gl_shader_count = 0u;
static uint8_t *crt_capture_pixels = NULL;
static size_t crt_capture_capacity = 0u;
static int crt_texture_width = 0;
static int crt_texture_height = 0;
static Uint32 crt_frame_interval_ms = 1000u / CRT_TARGET_FPS;
static Uint32 crt_last_frame_tick = 0u;
static int crt_frame_counter = 0;

static const struct crt_shader_vertex crt_quad_vertices[4] = {
    { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f } },
    { {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } }
};

static const GLfloat crt_identity_mvp[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

static GLuint crt_compile_shader(GLenum type, const char *source, const char *label);
static int crt_initialize_gl_program(const char *shader_path);
static int crt_initialize_quad_geometry(void);
static void crt_destroy_quad_geometry(void);
static void crt_bind_texture(GLuint texture);
static int crt_prepare_screen_texture(int width, int height);
static int crt_upload_screen_pixels(const uint8_t *pixels, int width, int height);
static int crt_prepare_intermediate_targets(int width, int height);
static void crt_release_gl_resources(void);
static int crt_ensure_capture_capacity(size_t bytes);
static int crt_capture_screen(uint8_t **out_pixels, int *out_width, int *out_height);
static int crt_forward_mouse_motion(int x, int y);
static void crt_forward_mouse_button(Uint8 button, int pressed);
static void crt_forward_mouse_wheel(int amount, int horizontal);
static void crt_forward_key(SDL_Keycode keycode, int pressed);
static KeySym crt_map_keycode(SDL_Keycode keycode);
static int crt_window_to_screen_coords(int win_x, int win_y, int *out_x, int *out_y);
static void crt_render_frame(int drawable_width, int drawable_height, int input_width, int input_height);

static void crt_print_usage(const char *progname) {
    const char *name = (progname && progname[0] != '\0') ? progname : "CRT";
    fprintf(stderr, "Usage: %s [-s shader_path]...\n", name);
}

static int crt_initialize_quad_geometry(void) {
    if (crt_quad_vbo != 0) {
        return 0;
    }
    glGenBuffers(1, &crt_quad_vbo);
    if (crt_quad_vbo == 0) {
        return -1;
    }
    glBindBuffer(GL_ARRAY_BUFFER, crt_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(crt_quad_vertices), crt_quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 0;
}

static void crt_destroy_quad_geometry(void) {
    if (crt_quad_vbo != 0) {
        glDeleteBuffers(1, &crt_quad_vbo);
        crt_quad_vbo = 0;
    }
}

static void crt_bind_texture(GLuint texture) {
    if (crt_bound_texture == texture) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    crt_bound_texture = texture;
}

static int crt_prepare_screen_texture(int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1;
    }
    if (crt_screen_texture == 0) {
        glGenTextures(1, &crt_screen_texture);
        if (crt_screen_texture == 0) {
            return -1;
        }
    }
    if (width == crt_texture_width && height == crt_texture_height) {
        return 0;
    }
    crt_bind_texture(crt_screen_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    crt_bind_texture(0);
    crt_texture_width = width;
    crt_texture_height = height;
    return 0;
}

static int crt_upload_screen_pixels(const uint8_t *pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0 || crt_screen_texture == 0) {
        return -1;
    }
    crt_bind_texture(crt_screen_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum error = glGetError();
    crt_bind_texture(0);
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "glTexSubImage2D failed with error 0x%x\n", error);
        return -1;
    }
    return 0;
}

static int crt_prepare_intermediate_targets(int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1;
    }
    if (crt_gl_framebuffer == 0) {
        glGenFramebuffers(1, &crt_gl_framebuffer);
        if (crt_gl_framebuffer == 0) {
            return -1;
        }
    }
    int resized = 0;
    for (size_t i = 0; i < 2; i++) {
        if (crt_gl_intermediate_textures[i] == 0) {
            glGenTextures(1, &crt_gl_intermediate_textures[i]);
            if (crt_gl_intermediate_textures[i] == 0) {
                return -1;
            }
            resized = 1;
        }
    }
    if (width != crt_intermediate_width || height != crt_intermediate_height) {
        resized = 1;
    }
    if (resized) {
        for (size_t i = 0; i < 2; i++) {
            crt_bind_texture(crt_gl_intermediate_textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        crt_bind_texture(0);
        crt_intermediate_width = width;
        crt_intermediate_height = height;
    }
    return 0;
}

static void crt_release_gl_resources(void) {
    if (crt_screen_texture != 0) {
        glDeleteTextures(1, &crt_screen_texture);
        crt_screen_texture = 0;
    }
    if (crt_gl_intermediate_textures[0] != 0) {
        glDeleteTextures(1, &crt_gl_intermediate_textures[0]);
        crt_gl_intermediate_textures[0] = 0;
    }
    if (crt_gl_intermediate_textures[1] != 0) {
        glDeleteTextures(1, &crt_gl_intermediate_textures[1]);
        crt_gl_intermediate_textures[1] = 0;
    }
    if (crt_gl_framebuffer != 0) {
        glDeleteFramebuffers(1, &crt_gl_framebuffer);
        crt_gl_framebuffer = 0;
    }
    if (crt_gl_shaders) {
        for (size_t i = 0; i < crt_gl_shader_count; i++) {
            if (crt_gl_shaders[i].program != 0) {
                glDeleteProgram(crt_gl_shaders[i].program);
            }
            crt_shader_clear_vaos(&crt_gl_shaders[i]);
        }
        free(crt_gl_shaders);
        crt_gl_shaders = NULL;
        crt_gl_shader_count = 0u;
    }
    crt_destroy_quad_geometry();
    crt_bound_texture = 0;
    crt_capture_capacity = 0u;
    free(crt_capture_pixels);
    crt_capture_pixels = NULL;
    crt_texture_width = 0;
    crt_texture_height = 0;
    crt_intermediate_width = 0;
    crt_intermediate_height = 0;
}

static GLuint crt_compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetShaderInfoLog(shader, log_length, NULL, log);
                fprintf(stderr, "Failed to compile %s shader: %s\n", label ? label : "GL", log);
                free(log);
            }
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int crt_initialize_gl_program(const char *shader_path) {
    if (!shader_path) {
        return -1;
    }

    size_t shader_size = 0u;
    char *shader_source = crt_shader_read_text_file(shader_path, &shader_size);
    if (!shader_source) {
        fprintf(stderr, "Failed to read shader from %s\n", shader_path);
        return -1;
    }

    size_t content_size = shader_size;
    const char *content_start = crt_shader_skip_utf8_bom(shader_source, &content_size);
    const char *content_end = content_start + content_size;

    struct crt_shader_parameter *parameters = NULL;
    size_t parameter_count = 0u;
    if (crt_shader_parse_parameters(content_start, content_size, &parameters, &parameter_count) != 0) {
        fprintf(stderr, "Failed to parse shader parameters from %s\n", shader_path);
        free(shader_source);
        return -1;
    }

    const char *version_start = NULL;
    const char *version_end = NULL;
    const char *scan = content_start;
    while (scan < content_end) {
        const char *line_start = scan;
        while (scan < content_end && *scan != '\n' && *scan != '\r') {
            scan++;
        }
        if (scan < content_end) {
            const char *line_end = scan;
            if (line_end > line_start && line_start[0] == '#' && strncmp(line_start, "#version", 8) == 0) {
                version_start = line_start;
                version_end = line_end;
                break;
            }
            while (scan < content_end && (*scan == '\n' || *scan == '\r')) {
                scan++;
            }
        } else {
            break;
        }
    }
    if (version_end && version_end < content_end && *version_end == '\0') {
        version_end = NULL;
    }
    if (version_end && version_end < content_end) {
        version_end++;
    }
    const char *shader_body = version_end ? version_end : content_start;
    size_t shader_body_len = (size_t)(content_end - shader_body);
    const char *version_prefix = version_start ? version_start : "#version 120\n";
    size_t version_prefix_len = version_start ? (size_t)(version_end - version_start) : strlen("#version 120\n");
    const char *parameter_block = "#define PARAMETER_UNIFORM 1\n";
    size_t parameter_block_len = strlen(parameter_block);
    const char *vertex_define = "#define VERTEX 1\n";
    const char *fragment_define = "#define FRAGMENT 1\n";
    size_t vertex_define_len = strlen(vertex_define);
    size_t fragment_define_len = strlen(fragment_define);
    size_t vertex_length = version_prefix_len + parameter_block_len + vertex_define_len + shader_body_len + 1u;
    size_t fragment_length = version_prefix_len + parameter_block_len + fragment_define_len + shader_body_len + 1u;

    char *vertex_source = malloc(vertex_length);
    char *fragment_source = malloc(fragment_length);
    if (!vertex_source || !fragment_source) {
        fprintf(stderr, "Failed to allocate shader source buffers.\n");
        free(vertex_source);
        free(fragment_source);
        crt_shader_free_parameters(parameters, parameter_count);
        free(shader_source);
        return -1;
    }

    size_t offset = 0u;
    memcpy(vertex_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    memcpy(vertex_source + offset, parameter_block, parameter_block_len);
    offset += parameter_block_len;
    memcpy(vertex_source + offset, vertex_define, vertex_define_len);
    offset += vertex_define_len;
    memcpy(vertex_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    vertex_source[offset] = '\0';

    offset = 0u;
    memcpy(fragment_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    memcpy(fragment_source + offset, parameter_block, parameter_block_len);
    offset += parameter_block_len;
    memcpy(fragment_source + offset, fragment_define, fragment_define_len);
    offset += fragment_define_len;
    memcpy(fragment_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    fragment_source[offset] = '\0';

    GLuint vertex_shader = crt_compile_shader(GL_VERTEX_SHADER, vertex_source, "vertex");
    GLuint fragment_shader = crt_compile_shader(GL_FRAGMENT_SHADER, fragment_source, "fragment");
    free(vertex_source);
    free(fragment_source);
    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        crt_shader_free_parameters(parameters, parameter_count);
        free(shader_source);
        return -1;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        crt_shader_free_parameters(parameters, parameter_count);
        free(shader_source);
        return -1;
    }
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint link_status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetProgramInfoLog(program, log_length, NULL, log);
                fprintf(stderr, "Failed to link shader program: %s\n", log);
                free(log);
            }
        }
        glDeleteProgram(program);
        crt_shader_free_parameters(parameters, parameter_count);
        free(shader_source);
        return -1;
    }

    struct crt_gl_shader shader_info;
    memset(&shader_info, 0, sizeof(shader_info));
    crt_shader_reset_uniform_cache(&shader_info);
    shader_info.program = program;
    shader_info.attrib_vertex = glGetAttribLocation(program, "VertexCoord");
    shader_info.attrib_color = glGetAttribLocation(program, "COLOR");
    shader_info.attrib_texcoord = glGetAttribLocation(program, "TexCoord");
    shader_info.uniform_mvp = glGetUniformLocation(program, "MVPMatrix");
    shader_info.uniform_frame_direction = glGetUniformLocation(program, "FrameDirection");
    shader_info.uniform_frame_count = glGetUniformLocation(program, "FrameCount");
    shader_info.uniform_output_size = glGetUniformLocation(program, "OutputSize");
    shader_info.uniform_texture_size = glGetUniformLocation(program, "TextureSize");
    shader_info.uniform_input_size = glGetUniformLocation(program, "InputSize");
    shader_info.uniform_texture_sampler = glGetUniformLocation(program, "Texture");
    shader_info.uniform_crt_gamma = glGetUniformLocation(program, "CRTgamma");
    shader_info.uniform_monitor_gamma = glGetUniformLocation(program, "monitorgamma");
    shader_info.uniform_distance = glGetUniformLocation(program, "d");
    shader_info.uniform_curvature = glGetUniformLocation(program, "CURVATURE");
    shader_info.uniform_radius = glGetUniformLocation(program, "R");
    shader_info.uniform_corner_size = glGetUniformLocation(program, "cornersize");
    shader_info.uniform_corner_smooth = glGetUniformLocation(program, "cornersmooth");
    shader_info.uniform_x_tilt = glGetUniformLocation(program, "x_tilt");
    shader_info.uniform_y_tilt = glGetUniformLocation(program, "y_tilt");
    shader_info.uniform_overscan_x = glGetUniformLocation(program, "overscan_x");
    shader_info.uniform_overscan_y = glGetUniformLocation(program, "overscan_y");
    shader_info.uniform_dotmask = glGetUniformLocation(program, "DOTMASK");
    shader_info.uniform_sharper = glGetUniformLocation(program, "SHARPER");
    shader_info.uniform_scanline_weight = glGetUniformLocation(program, "scanline_weight");
    shader_info.uniform_luminance = glGetUniformLocation(program, "lum");
    shader_info.uniform_interlace_detect = glGetUniformLocation(program, "interlace_detect");
    shader_info.uniform_saturation = glGetUniformLocation(program, "SATURATION");
    shader_info.uniform_inv_gamma = glGetUniformLocation(program, "INV");

    glUseProgram(program);
    if (shader_info.uniform_texture_sampler >= 0) {
        glUniform1i(shader_info.uniform_texture_sampler, 0);
    }
    if (shader_info.uniform_frame_direction >= 0) {
        glUniform1i(shader_info.uniform_frame_direction, 1);
    }
    if (shader_info.uniform_mvp >= 0) {
        crt_shader_set_matrix(shader_info.uniform_mvp,
                              shader_info.cached_mvp,
                              &shader_info.has_cached_mvp,
                              crt_identity_mvp);
    }
    if (shader_info.uniform_crt_gamma >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "CRTgamma", 2.4f);
        glUniform1f(shader_info.uniform_crt_gamma, value);
    }
    if (shader_info.uniform_monitor_gamma >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "monitorgamma", 2.2f);
        glUniform1f(shader_info.uniform_monitor_gamma, value);
    }
    if (shader_info.uniform_distance >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "d", 1.6f);
        glUniform1f(shader_info.uniform_distance, value);
    }
    if (shader_info.uniform_curvature >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "CURVATURE", 1.0f);
        glUniform1f(shader_info.uniform_curvature, value);
    }
    if (shader_info.uniform_radius >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "R", 2.0f);
        glUniform1f(shader_info.uniform_radius, value);
    }
    if (shader_info.uniform_corner_size >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "cornersize", 0.03f);
        glUniform1f(shader_info.uniform_corner_size, value);
    }
    if (shader_info.uniform_corner_smooth >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "cornersmooth", 1000.0f);
        glUniform1f(shader_info.uniform_corner_smooth, value);
    }
    if (shader_info.uniform_x_tilt >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "x_tilt", 0.0f);
        glUniform1f(shader_info.uniform_x_tilt, value);
    }
    if (shader_info.uniform_y_tilt >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "y_tilt", 0.0f);
        glUniform1f(shader_info.uniform_y_tilt, value);
    }
    if (shader_info.uniform_overscan_x >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "overscan_x", 100.0f);
        glUniform1f(shader_info.uniform_overscan_x, value);
    }
    if (shader_info.uniform_overscan_y >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "overscan_y", 100.0f);
        glUniform1f(shader_info.uniform_overscan_y, value);
    }
    if (shader_info.uniform_dotmask >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "DOTMASK", 0.3f);
        glUniform1f(shader_info.uniform_dotmask, value);
    }
    if (shader_info.uniform_sharper >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "SHARPER", 1.0f);
        glUniform1f(shader_info.uniform_sharper, value);
    }
    if (shader_info.uniform_scanline_weight >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "scanline_weight", 0.3f);
        glUniform1f(shader_info.uniform_scanline_weight, value);
    }
    if (shader_info.uniform_luminance >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "lum", 0.0f);
        glUniform1f(shader_info.uniform_luminance, value);
    }
    if (shader_info.uniform_interlace_detect >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "interlace_detect", 1.0f);
        glUniform1f(shader_info.uniform_interlace_detect, value);
    }
    if (shader_info.uniform_saturation >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "SATURATION", 1.0f);
        glUniform1f(shader_info.uniform_saturation, value);
    }
    if (shader_info.uniform_inv_gamma >= 0) {
        float value = crt_shader_get_parameter_default(parameters, parameter_count, "INV", 1.0f);
        glUniform1f(shader_info.uniform_inv_gamma, value);
    }

    size_t new_count = crt_gl_shader_count + 1u;
    struct crt_gl_shader *new_shaders = realloc(crt_gl_shaders, new_count * sizeof(*new_shaders));
    if (!new_shaders) {
        glDeleteProgram(program);
        crt_shader_free_parameters(parameters, parameter_count);
        free(shader_source);
        return -1;
    }

    crt_gl_shaders = new_shaders;
    crt_gl_shaders[crt_gl_shader_count] = shader_info;
    if (crt_initialize_quad_geometry() != 0 ||
        crt_shader_configure_vaos(&crt_gl_shaders[crt_gl_shader_count],
                                  crt_quad_vbo,
                                  sizeof(struct crt_shader_vertex),
                                  offsetof(struct crt_shader_vertex, position),
                                  offsetof(struct crt_shader_vertex, texcoord_cpu),
                                  offsetof(struct crt_shader_vertex, texcoord_fbo)) != 0) {
        glDeleteProgram(program);
        crt_shader_clear_vaos(&crt_gl_shaders[crt_gl_shader_count]);
        crt_gl_shaders[crt_gl_shader_count].program = 0;
        crt_shader_free_parameters(parameters, parameter_count);
        free(shader_source);
        return -1;
    }

    crt_gl_shader_count = new_count;
    crt_shader_free_parameters(parameters, parameter_count);
    free(shader_source);
    return 0;
}
static int crt_ensure_capture_capacity(size_t bytes) {
    if (bytes == 0u) {
        return -1;
    }
    if (bytes <= crt_capture_capacity) {
        return 0;
    }
    uint8_t *new_pixels = realloc(crt_capture_pixels, bytes);
    if (!new_pixels) {
        return -1;
    }
    crt_capture_pixels = new_pixels;
    crt_capture_capacity = bytes;
    return 0;
}

static unsigned long crt_mask_shift(unsigned long mask) {
    unsigned long shift = 0;
    while ((mask & 1u) == 0u && mask != 0u) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

static unsigned int crt_mask_bits(unsigned long mask) {
    unsigned int bits = 0;
    while (mask != 0u) {
        if (mask & 1u) {
            bits++;
        }
        mask >>= 1;
    }
    return bits;
}

static uint8_t crt_extract_component(unsigned long pixel, unsigned long mask) {
    if (mask == 0u) {
        return 0u;
    }
    unsigned long shift = crt_mask_shift(mask);
    unsigned long value = (pixel & mask) >> shift;
    unsigned int bits = crt_mask_bits(mask);
    if (bits == 0u) {
        return 0u;
    }
    if (bits >= 8u) {
        unsigned int drop = bits - 8u;
        return (uint8_t)(value >> drop);
    }
    if (bits >= sizeof(unsigned long) * CHAR_BIT) {
        return 255u;
    }
    unsigned long max_value = (1ul << bits) - 1ul;
    if (max_value == 0u) {
        return 0u;
    }
    double scaled = (double)value * 255.0 / (double)max_value;
    if (scaled < 0.0) {
        scaled = 0.0;
    }
    if (scaled > 255.0) {
        scaled = 255.0;
    }
    return (uint8_t)lrint(scaled);
}

static int crt_capture_screen(uint8_t **out_pixels, int *out_width, int *out_height) {
    if (!crt_display || crt_root_window == 0 || !out_pixels || !out_width || !out_height) {
        return -1;
    }
    XImage *image = XGetImage(crt_display,
                              crt_root_window,
                              0,
                              0,
                              (unsigned int)crt_screen_width,
                              (unsigned int)crt_screen_height,
                              AllPlanes,
                              ZPixmap);
    if (!image) {
        return -1;
    }
    if (image->width <= 0 || image->height <= 0) {
        XDestroyImage(image);
        return -1;
    }
    size_t width = (size_t)image->width;
    size_t height = (size_t)image->height;
    if (width > 0u && height > SIZE_MAX / width) {
        XDestroyImage(image);
        return -1;
    }
    size_t total_pixels = width * height;
    if (total_pixels == 0u || total_pixels > SIZE_MAX / 4u) {
        XDestroyImage(image);
        return -1;
    }
    size_t total_bytes = total_pixels * 4u;
    if (crt_ensure_capture_capacity(total_bytes) != 0) {
        XDestroyImage(image);
        return -1;
    }
    size_t offset = 0u;
    for (int y = 0; y < image->height; y++) {
        for (int x = 0; x < image->width; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            crt_capture_pixels[offset++] = crt_extract_component(pixel, image->red_mask);
            crt_capture_pixels[offset++] = crt_extract_component(pixel, image->green_mask);
            crt_capture_pixels[offset++] = crt_extract_component(pixel, image->blue_mask);
            crt_capture_pixels[offset++] = 0xFFu;
        }
    }
    int image_width = image->width;
    int image_height = image->height;
    XDestroyImage(image);
    *out_pixels = crt_capture_pixels;
    *out_width = image_width;
    *out_height = image_height;
    return 0;
}
static int crt_window_to_screen_coords(int win_x, int win_y, int *out_x, int *out_y) {
    if (!out_x || !out_y || crt_screen_width <= 0 || crt_screen_height <= 0 || !crt_window_handle) {
        return -1;
    }
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(crt_window_handle, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        *out_x = win_x;
        *out_y = win_y;
        return 0;
    }
    double scale_x = (double)crt_screen_width / (double)window_width;
    double scale_y = (double)crt_screen_height / (double)window_height;
    int screen_x = (int)lrint((double)win_x * scale_x);
    int screen_y = (int)lrint((double)win_y * scale_y);
    if (screen_x < 0) {
        screen_x = 0;
    }
    if (screen_y < 0) {
        screen_y = 0;
    }
    if (screen_x >= crt_screen_width) {
        screen_x = crt_screen_width - 1;
    }
    if (screen_y >= crt_screen_height) {
        screen_y = crt_screen_height - 1;
    }
    *out_x = screen_x;
    *out_y = screen_y;
    return 0;
}

static int crt_forward_mouse_motion(int x, int y) {
#if BUDOSTACK_HAVE_XTEST
    if (!crt_xtest_available || !crt_display) {
        return -1;
    }
    int screen_x = x;
    int screen_y = y;
    if (crt_window_to_screen_coords(x, y, &screen_x, &screen_y) != 0) {
        return -1;
    }
    XTestFakeMotionEvent(crt_display, crt_display_screen, screen_x, screen_y, CurrentTime);
    XFlush(crt_display);
    return 0;
#else
    (void)x;
    (void)y;
    return -1;
#endif
}

static unsigned int crt_map_mouse_button(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return 1u;
    case SDL_BUTTON_MIDDLE:
        return 2u;
    case SDL_BUTTON_RIGHT:
        return 3u;
    case SDL_BUTTON_X1:
        return 8u;
    case SDL_BUTTON_X2:
        return 9u;
    default:
        return 0u;
    }
}

static void crt_forward_mouse_button(Uint8 button, int pressed) {
#if BUDOSTACK_HAVE_XTEST
    if (!crt_xtest_available || !crt_display) {
        return;
    }
    unsigned int mapped = crt_map_mouse_button(button);
    if (mapped == 0u) {
        return;
    }
    XTestFakeButtonEvent(crt_display, mapped, pressed, CurrentTime);
    XFlush(crt_display);
#else
    (void)button;
    (void)pressed;
#endif
}

static void crt_forward_mouse_wheel(int amount, int horizontal) {
#if BUDOSTACK_HAVE_XTEST
    if (!crt_xtest_available || !crt_display) {
        return;
    }
    unsigned int up_button = horizontal ? 6u : 4u;
    unsigned int down_button = horizontal ? 7u : 5u;
    int direction = (amount > 0) ? 1 : -1;
    for (int i = 0; i < abs(amount); i++) {
        unsigned int button = (direction > 0) ? up_button : down_button;
        XTestFakeButtonEvent(crt_display, button, True, CurrentTime);
        XTestFakeButtonEvent(crt_display, button, False, CurrentTime);
    }
    XFlush(crt_display);
#else
    (void)amount;
    (void)horizontal;
#endif
}
static KeySym crt_map_keycode(SDL_Keycode keycode) {
    if (keycode >= SDLK_a && keycode <= SDLK_z) {
        return XK_a + (keycode - SDLK_a);
    }
    if (keycode >= SDLK_0 && keycode <= SDLK_9) {
        return XK_0 + (keycode - SDLK_0);
    }
    if (keycode >= SDLK_F1 && keycode <= SDLK_F12) {
        return XK_F1 + (keycode - SDLK_F1);
    }
    switch (keycode) {
    case SDLK_SPACE: return XK_space;
    case SDLK_RETURN: return XK_Return;
    case SDLK_RETURN2: return XK_Return;
    case SDLK_KP_ENTER: return XK_KP_Enter;
    case SDLK_ESCAPE: return XK_Escape;
    case SDLK_BACKSPACE: return XK_BackSpace;
    case SDLK_TAB: return XK_Tab;
    case SDLK_DELETE: return XK_Delete;
    case SDLK_INSERT: return XK_Insert;
    case SDLK_HOME: return XK_Home;
    case SDLK_END: return XK_End;
    case SDLK_PAGEUP: return XK_Page_Up;
    case SDLK_PAGEDOWN: return XK_Page_Down;
    case SDLK_LEFT: return XK_Left;
    case SDLK_RIGHT: return XK_Right;
    case SDLK_UP: return XK_Up;
    case SDLK_DOWN: return XK_Down;
    case SDLK_LCTRL: return XK_Control_L;
    case SDLK_RCTRL: return XK_Control_R;
    case SDLK_LSHIFT: return XK_Shift_L;
    case SDLK_RSHIFT: return XK_Shift_R;
    case SDLK_LALT: return XK_Alt_L;
    case SDLK_RALT: return XK_Alt_R;
    case SDLK_LGUI: return XK_Super_L;
    case SDLK_RGUI: return XK_Super_R;
    case SDLK_CAPSLOCK: return XK_Caps_Lock;
    case SDLK_PRINTSCREEN: return XK_Print;
    case SDLK_SCROLLLOCK: return XK_Scroll_Lock;
    case SDLK_PAUSE: return XK_Pause;
    case SDLK_MENU: return XK_Menu;
    case SDLK_SEMICOLON: return XK_semicolon;
    case SDLK_EQUALS: return XK_equal;
    case SDLK_COMMA: return XK_comma;
    case SDLK_MINUS: return XK_minus;
    case SDLK_PERIOD: return XK_period;
    case SDLK_SLASH: return XK_slash;
    case SDLK_BACKQUOTE: return XK_grave;
    case SDLK_LEFTBRACKET: return XK_bracketleft;
    case SDLK_RIGHTBRACKET: return XK_bracketright;
    case SDLK_BACKSLASH: return XK_backslash;
    case SDLK_QUOTE: return XK_quoteright;
    case SDLK_KP_MULTIPLY: return XK_KP_Multiply;
    case SDLK_KP_PLUS: return XK_KP_Add;
    case SDLK_KP_MINUS: return XK_KP_Subtract;
    case SDLK_KP_DIVIDE: return XK_KP_Divide;
    case SDLK_KP_PERIOD: return XK_KP_Decimal;
    case SDLK_KP_0: return XK_KP_0;
    case SDLK_KP_1: return XK_KP_1;
    case SDLK_KP_2: return XK_KP_2;
    case SDLK_KP_3: return XK_KP_3;
    case SDLK_KP_4: return XK_KP_4;
    case SDLK_KP_5: return XK_KP_5;
    case SDLK_KP_6: return XK_KP_6;
    case SDLK_KP_7: return XK_KP_7;
    case SDLK_KP_8: return XK_KP_8;
    case SDLK_KP_9: return XK_KP_9;
    default:
        return NoSymbol;
    }
}

static void crt_forward_key(SDL_Keycode keycode, int pressed) {
#if BUDOSTACK_HAVE_XTEST
    if (!crt_xtest_available || !crt_display) {
        return;
    }
    KeySym keysym = crt_map_keycode(keycode);
    if (keysym == NoSymbol) {
        return;
    }
    KeyCode keycode_x11 = XKeysymToKeycode(crt_display, keysym);
    if (keycode_x11 == 0) {
        return;
    }
    XTestFakeKeyEvent(crt_display, keycode_x11, pressed, CurrentTime);
    XFlush(crt_display);
#else
    (void)keycode;
    (void)pressed;
#endif
}
static void crt_render_frame(int drawable_width, int drawable_height, int input_width, int input_height) {
    if (drawable_width <= 0 || drawable_height <= 0 || crt_screen_texture == 0) {
        return;
    }
    glClear(GL_COLOR_BUFFER_BIT);
    if (crt_gl_shader_count > 0u) {
        int frame_value = crt_frame_counter++;
        GLuint source_texture = crt_screen_texture;
        GLfloat source_texture_width = (GLfloat)crt_texture_width;
        GLfloat source_texture_height = (GLfloat)crt_texture_height;
        GLfloat source_input_width = (GLfloat)input_width;
        GLfloat source_input_height = (GLfloat)input_height;
        int multipass_failed = 0;
        for (size_t shader_index = 0; shader_index < crt_gl_shader_count; shader_index++) {
            struct crt_gl_shader *shader = &crt_gl_shaders[shader_index];
            if (!shader || shader->program == 0) {
                continue;
            }
            int last_pass = (shader_index + 1u == crt_gl_shader_count);
            GLuint target_texture = 0;
            int using_intermediate = 0;
            if (!last_pass) {
                if (crt_prepare_intermediate_targets(drawable_width, drawable_height) != 0) {
                    fprintf(stderr, "Failed to prepare intermediate render targets; stopping shader chain.\n");
                    multipass_failed = 1;
                    last_pass = 1;
                } else {
                    target_texture = crt_gl_intermediate_textures[shader_index % 2u];
                    glBindFramebuffer(GL_FRAMEBUFFER, crt_gl_framebuffer);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
                    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    if (status != GL_FRAMEBUFFER_COMPLETE) {
                        fprintf(stderr, "Framebuffer incomplete (0x%04x); stopping shader chain.\n", (unsigned int)status);
                        glBindFramebuffer(GL_FRAMEBUFFER, 0);
                        multipass_failed = 1;
                        last_pass = 1;
                    } else {
                        using_intermediate = 1;
                        glViewport(0, 0, drawable_width, drawable_height);
                        glClear(GL_COLOR_BUFFER_BIT);
                    }
                }
            }
            if (last_pass && !using_intermediate) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, drawable_width, drawable_height);
            }
            glUseProgram(shader->program);
            crt_shader_set_vec2(shader->uniform_output_size,
                                shader->cached_output_size,
                                &shader->has_cached_output_size,
                                (GLfloat)drawable_width,
                                (GLfloat)drawable_height);
            if (shader->uniform_frame_count >= 0) {
                glUniform1i(shader->uniform_frame_count, frame_value);
            }
            crt_shader_set_vec2(shader->uniform_texture_size,
                                shader->cached_texture_size,
                                &shader->has_cached_texture_size,
                                source_texture_width,
                                source_texture_height);
            crt_shader_set_vec2(shader->uniform_input_size,
                                shader->cached_input_size,
                                &shader->has_cached_input_size,
                                source_input_width,
                                source_input_height);
            glActiveTexture(GL_TEXTURE0);
            crt_bind_texture(source_texture);
            GLuint vao = (source_texture == crt_screen_texture) ? shader->quad_vaos[0] : shader->quad_vaos[1];
            int using_vao = 0;
            if (vao != 0) {
                glBindVertexArray(vao);
                using_vao = 1;
            } else {
                static const GLfloat fallback_quad_vertices[16] = {
                    -1.0f, -1.0f, 0.0f, 1.0f,
                     1.0f, -1.0f, 0.0f, 1.0f,
                    -1.0f,  1.0f, 0.0f, 1.0f,
                     1.0f,  1.0f, 0.0f, 1.0f
                };
                static const GLfloat fallback_texcoords_cpu[8] = {
                    0.0f, 1.0f,
                    1.0f, 1.0f,
                    0.0f, 0.0f,
                    1.0f, 0.0f
                };
                static const GLfloat fallback_texcoords_fbo[8] = {
                    0.0f, 0.0f,
                    1.0f, 0.0f,
                    0.0f, 1.0f,
                    1.0f, 1.0f
                };
                if (shader->attrib_vertex >= 0) {
                    glEnableVertexAttribArray((GLuint)shader->attrib_vertex);
                    glVertexAttribPointer((GLuint)shader->attrib_vertex, 4, GL_FLOAT, GL_FALSE, 0, fallback_quad_vertices);
                }
                if (shader->attrib_texcoord >= 0) {
                    const GLfloat *texcoords = (source_texture == crt_screen_texture) ? fallback_texcoords_cpu : fallback_texcoords_fbo;
                    glEnableVertexAttribArray((GLuint)shader->attrib_texcoord);
                    glVertexAttribPointer((GLuint)shader->attrib_texcoord, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
                }
            }
            if (shader->attrib_color >= 0) {
                glDisableVertexAttribArray((GLuint)shader->attrib_color);
                glVertexAttrib4f((GLuint)shader->attrib_color, 1.0f, 1.0f, 1.0f, 1.0f);
            }
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            if (using_vao) {
                glBindVertexArray(0);
            } else {
                if (shader->attrib_vertex >= 0) {
                    glDisableVertexAttribArray((GLuint)shader->attrib_vertex);
                }
                if (shader->attrib_texcoord >= 0) {
                    glDisableVertexAttribArray((GLuint)shader->attrib_texcoord);
                }
            }
            if (using_intermediate) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                source_texture = target_texture;
                source_texture_width = (GLfloat)drawable_width;
                source_texture_height = (GLfloat)drawable_height;
                source_input_width = (GLfloat)drawable_width;
                source_input_height = (GLfloat)drawable_height;
            }
            if (multipass_failed) {
                break;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glActiveTexture(GL_TEXTURE0);
        crt_bind_texture(crt_screen_texture);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(1.0f, -1.0f);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(-1.0f, 1.0f);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2f(1.0f, 1.0f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        crt_bind_texture(0);
    }
}

int main(int argc, char **argv) {
    const char *progname = (argc > 0 && argv && argv[0]) ? argv[0] : "CRT";
    const char **shader_args = NULL;
    size_t shader_arg_count = 0u;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--shader") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing shader path after %s.\n", arg);
                crt_print_usage(progname);
                free(shader_args);
                return EXIT_FAILURE;
            }
            const char *value = argv[++i];
            const char **new_args = realloc(shader_args, (shader_arg_count + 1u) * sizeof(*new_args));
            if (!new_args) {
                fprintf(stderr, "Failed to allocate memory for shader arguments.\n");
                free(shader_args);
                return EXIT_FAILURE;
            }
            shader_args = new_args;
            shader_args[shader_arg_count++] = value;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            crt_print_usage(progname);
            free(shader_args);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Unrecognized argument: %s\n", arg);
            crt_print_usage(progname);
            free(shader_args);
            return EXIT_FAILURE;
        }
    }
    if (shader_arg_count == 0u) {
        const char **new_args = realloc(shader_args, sizeof(*shader_args));
        if (!new_args) {
            fprintf(stderr, "Failed to allocate memory for default shader argument.\n");
            free(shader_args);
            return EXIT_FAILURE;
        }
        shader_args = new_args;
        shader_args[0] = CRT_DEFAULT_SHADER;
        shader_arg_count = 1u;
    }
    char root_dir[PATH_MAX];
    if (budostack_compute_root_directory(argv[0], root_dir, sizeof(root_dir)) != 0) {
        fprintf(stderr, "Failed to resolve BUDOSTACK root directory.\n");
        free(shader_args);
        return EXIT_FAILURE;
    }
    struct shader_path_entry {
        char path[PATH_MAX];
    };
    struct shader_path_entry *shader_paths = NULL;
    size_t shader_path_count = 0u;
    for (size_t i = 0; i < shader_arg_count; i++) {
        struct shader_path_entry *new_paths = realloc(shader_paths, (shader_path_count + 1u) * sizeof(*new_paths));
        if (!new_paths) {
            fprintf(stderr, "Failed to allocate memory for shader paths.\n");
            free(shader_paths);
            free(shader_args);
            return EXIT_FAILURE;
        }
        shader_paths = new_paths;
        if (budostack_resolve_resource_path(root_dir,
                                            shader_args[i],
                                            shader_paths[shader_path_count].path,
                                            sizeof(shader_paths[shader_path_count].path)) != 0) {
            fprintf(stderr, "Shader path is too long.\n");
            free(shader_paths);
            free(shader_args);
            return EXIT_FAILURE;
        }
        shader_path_count++;
    }
    free(shader_args);
    shader_args = NULL;

    crt_display = XOpenDisplay(NULL);
    if (!crt_display) {
        fprintf(stderr, "Failed to open X11 display.\n");
        free(shader_paths);
        return EXIT_FAILURE;
    }
    crt_display_screen = DefaultScreen(crt_display);
    crt_root_window = RootWindow(crt_display, crt_display_screen);
    crt_screen_width = DisplayWidth(crt_display, crt_display_screen);
    crt_screen_height = DisplayHeight(crt_display, crt_display_screen);
    if (crt_screen_width <= 0 || crt_screen_height <= 0) {
        fprintf(stderr, "Invalid screen size reported by X11.\n");
        XCloseDisplay(crt_display);
        crt_display = NULL;
        free(shader_paths);
        return EXIT_FAILURE;
    }
    crt_xtest_available = 0;
#if BUDOSTACK_HAVE_XTEST
    int xtest_event = 0;
    int xtest_error = 0;
    int xtest_major = 0;
    int xtest_minor = 0;
    if (XTestQueryExtension(crt_display, &xtest_event, &xtest_error, &xtest_major, &xtest_minor) == True) {
        crt_xtest_available = 1;
    } else {
        fprintf(stderr, "Warning: XTest extension unavailable; input pass-through disabled.\n");
    }
#else
    fprintf(stderr, "Warning: XTest headers unavailable; input pass-through disabled.\n");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        XCloseDisplay(crt_display);
        crt_display = NULL;
        free(shader_paths);
        return EXIT_FAILURE;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#ifdef SDL_GL_CONTEXT_PROFILE_MASK
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP;
#ifdef SDL_WINDOW_ALLOW_HIGHDPI
    window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    SDL_Window *window = SDL_CreateWindow("BUDOSTACK CRT",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          crt_screen_width,
                                          crt_screen_height,
                                          window_flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        XCloseDisplay(crt_display);
        crt_display = NULL;
        free(shader_paths);
        return EXIT_FAILURE;
    }
    crt_window_handle = window;
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        XCloseDisplay(crt_display);
        crt_display = NULL;
        free(shader_paths);
        return EXIT_FAILURE;
    }
    crt_gl_context_handle = gl_context;
    if (SDL_GL_MakeCurrent(window, gl_context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        XCloseDisplay(crt_display);
        crt_display = NULL;
        free(shader_paths);
        return EXIT_FAILURE;
    }
    if (SDL_GL_SetSwapInterval(1) != 0) {
        fprintf(stderr, "Warning: Unable to enable VSync: %s\n", SDL_GetError());
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    for (size_t i = 0; i < shader_path_count; i++) {
        if (crt_initialize_gl_program(shader_paths[i].path) != 0) {
            fprintf(stderr, "Failed to load shader: %s\n", shader_paths[i].path);
            crt_release_gl_resources();
            SDL_GL_DeleteContext(gl_context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            XCloseDisplay(crt_display);
            crt_display = NULL;
            free(shader_paths);
            return EXIT_FAILURE;
        }
    }
    free(shader_paths);
    shader_paths = NULL;

    crt_last_frame_tick = SDL_GetTicks();
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
        fprintf(stderr, "Invalid drawable size reported by SDL.\n");
        crt_release_gl_resources();
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        XCloseDisplay(crt_display);
        crt_display = NULL;
        return EXIT_FAILURE;
    }

    int running = 1;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (!event.key.repeat) {
                    if (event.key.keysym.sym == SDLK_F12) {
                        running = 0;
                    } else {
                        crt_forward_key(event.key.keysym.sym, 1);
                    }
                }
                break;
            case SDL_KEYUP:
                if (!event.key.repeat) {
                    crt_forward_key(event.key.keysym.sym, 0);
                }
                break;
            case SDL_MOUSEMOTION:
                crt_forward_mouse_motion(event.motion.x, event.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
                crt_forward_mouse_button(event.button.button, 1);
                break;
            case SDL_MOUSEBUTTONUP:
                crt_forward_mouse_button(event.button.button, 0);
                break;
            case SDL_MOUSEWHEEL: {
                int amount_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    amount_y = -amount_y;
                }
                if (event.wheel.x != 0) {
                    int amount_x = event.wheel.x;
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                        amount_x = -amount_x;
                    }
                    crt_forward_mouse_wheel(amount_x, 1);
                }
                if (amount_y != 0) {
                    crt_forward_mouse_wheel(amount_y, 0);
                }
                break;
            }
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    running = 0;
                } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                           event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
                }
                break;
            default:
                break;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (crt_frame_interval_ms > 0u) {
            Uint32 elapsed = now - crt_last_frame_tick;
            if (elapsed < crt_frame_interval_ms) {
                SDL_Delay(1);
                continue;
            }
        }
        crt_last_frame_tick = now;

        uint8_t *pixels = NULL;
        int frame_width = 0;
        int frame_height = 0;
        if (crt_capture_screen(&pixels, &frame_width, &frame_height) != 0) {
            SDL_Delay(10);
            continue;
        }
        if (crt_prepare_screen_texture(frame_width, frame_height) != 0) {
            fprintf(stderr, "Failed to prepare screen texture.\n");
            break;
        }
        if (crt_upload_screen_pixels(pixels, frame_width, frame_height) != 0) {
            fprintf(stderr, "Failed to upload screen texture.\n");
            break;
        }
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        if (drawable_width <= 0 || drawable_height <= 0) {
            drawable_width = crt_screen_width;
            drawable_height = crt_screen_height;
        }
        glViewport(0, 0, drawable_width, drawable_height);
        crt_render_frame(drawable_width, drawable_height, frame_width, frame_height);
        SDL_GL_SwapWindow(window);
    }

    crt_release_gl_resources();
    if (crt_gl_context_handle) {
        SDL_GL_DeleteContext(crt_gl_context_handle);
        crt_gl_context_handle = NULL;
    }
    if (crt_window_handle) {
        SDL_DestroyWindow(crt_window_handle);
        crt_window_handle = NULL;
    }
    SDL_Quit();
    if (crt_display) {
        XCloseDisplay(crt_display);
        crt_display = NULL;
    }
    return EXIT_SUCCESS;
}

#else

int main(void) {
    fprintf(stderr, "CRT app requires SDL2 support.\n");
    return EXIT_FAILURE;
}

#endif
