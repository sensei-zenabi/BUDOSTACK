#include "retro_shader_bridge.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
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
#endif

struct retro_shader_parameter {
    char name[64];
    float default_value;
};

struct retro_gl_shader {
    GLuint program;
    GLint attrib_vertex;
    GLint attrib_color;
    GLint attrib_texcoord;
    GLint uniform_mvp;
    GLint uniform_frame_direction;
    GLint uniform_frame_count;
    GLint uniform_output_size;
    GLint uniform_texture_size;
    GLint uniform_input_size;
    GLint uniform_texture_sampler;
    GLfloat cached_mvp[16];
    GLfloat cached_output_size[2];
    GLfloat cached_texture_size[2];
    GLfloat cached_input_size[2];
    int has_cached_mvp;
    int has_cached_output_size;
    int has_cached_texture_size;
    int has_cached_input_size;
    GLuint quad_vaos[2];
};

struct retro_quad_vertex {
    GLfloat position[4];
    GLfloat texcoord_cpu[2];
    GLfloat texcoord_fbo[2];
};

static const struct retro_quad_vertex retro_quad_vertices[4] = {
    { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f } },
    { {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } }
};

static const GLsizei retro_quad_vertex_count = 4;

static const GLfloat retro_identity_mvp[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

struct retro_shader_bridge {
    SDL_Window *window;
    GLuint texture;
    int texture_width;
    int texture_height;
    GLuint framebuffer;
    GLuint intermediate_textures[2];
    int intermediate_width;
    int intermediate_height;
    struct retro_gl_shader *shaders;
    size_t shader_count;
    GLuint quad_vbo;
    int gl_ready;
    uint8_t *frame_pixels;
    size_t frame_capacity;
    unsigned frame_width;
    unsigned frame_height;
    int frame_dirty;
};

static int retro_build_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
    if (!dest || dest_size == 0u || !base || !suffix) {
        return -1;
    }
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    if (base_len + suffix_len + 2u > dest_size) {
        return -1;
    }
    memcpy(dest, base, base_len);
    if (base_len > 0u && base[base_len - 1u] != '/' && suffix[0] != '/') {
        dest[base_len] = '/';
        memcpy(dest + base_len + 1u, suffix, suffix_len + 1u);
    } else if (base_len > 0u && base[base_len - 1u] == '/' && suffix[0] == '/') {
        memcpy(dest + base_len, suffix + 1u, suffix_len);
        dest[base_len + suffix_len - 1u] = '\0';
    } else {
        memcpy(dest + base_len, suffix, suffix_len + 1u);
    }
    return 0;
}

static int retro_resolve_shader_path(const char *root_dir, const char *shader_arg, char *out_path, size_t out_size) {
    if (!shader_arg || !out_path || out_size == 0u) {
        return -1;
    }
    if (shader_arg[0] == '/') {
        size_t len = strlen(shader_arg);
        if (len + 1u > out_size) {
            return -1;
        }
        memcpy(out_path, shader_arg, len + 1u);
        return 0;
    }
    if (root_dir && retro_build_path(out_path, out_size, root_dir, shader_arg) == 0 && access(out_path, R_OK) == 0) {
        return 0;
    }
    size_t len = strlen(shader_arg);
    if (len + 1u > out_size) {
        return -1;
    }
    memcpy(out_path, shader_arg, len + 1u);
    return 0;
}

static char *retro_read_text_file(const char *path, size_t *out_size) {
    if (out_size) {
        *out_size = 0u;
    }
    if (!path) {
        return NULL;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long file_len = ftell(fp);
    if (file_len < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    size_t size = (size_t)file_len;
    char *buffer = malloc(size + 1u);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    size_t read_len = fread(buffer, 1, size, fp);
    fclose(fp);
    if (read_len != size) {
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}

static const char *retro_skip_utf8_bom(const char *src, size_t *size) {
    if (!src || !size || *size < 3u) {
        return src;
    }
    const unsigned char *bytes = (const unsigned char *)src;
    if (bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        *size -= 3u;
        return src + 3u;
    }
    return src;
}

static void retro_free_shader_parameters(struct retro_shader_parameter *params, size_t count) {
    (void)count;
    free(params);
}

static int retro_parse_shader_parameters(const char *source,
                                         size_t length,
                                         struct retro_shader_parameter **out_params,
                                         size_t *out_count,
                                         char **out_stripped,
                                         size_t *out_stripped_len) {
    if (!source || !out_params || !out_count || !out_stripped || !out_stripped_len) {
        return -1;
    }
    *out_params = NULL;
    *out_count = 0u;
    *out_stripped = NULL;
    *out_stripped_len = 0u;

    char *stripped = malloc(length + 1u);
    if (!stripped) {
        return -1;
    }

    size_t stripped_len = 0u;
    const char *cursor = source;
    const char *end = source + length;

    while (cursor < end) {
        const char *line_start = cursor;
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!line_end) {
            line_end = end;
        }
        size_t line_len = (size_t)(line_end - line_start);
        const char *scan = line_start;
        while (scan < line_end && (*scan == ' ' || *scan == '\t')) {
            scan++;
        }
        int is_parameter = 0;
        if (scan < line_end && strncmp(scan, "#pragma parameter", 17) == 0 &&
            (scan + 17 == line_end || isspace((unsigned char)scan[17]) != 0)) {
            is_parameter = 1;
        }

        if (is_parameter) {
            const char *param_cursor = scan + 17;
            while (param_cursor < line_end && isspace((unsigned char)*param_cursor)) {
                param_cursor++;
            }
            const char *name_start = param_cursor;
            while (param_cursor < line_end && !isspace((unsigned char)*param_cursor)) {
                param_cursor++;
            }
            size_t name_len = (size_t)(param_cursor - name_start);
            char name_buf[64];
            if (name_len > 0u && name_len < sizeof(name_buf)) {
                memcpy(name_buf, name_start, name_len);
                name_buf[name_len] = '\0';
                while (param_cursor < line_end && isspace((unsigned char)*param_cursor)) {
                    param_cursor++;
                }
                if (param_cursor < line_end && *param_cursor == '"') {
                    param_cursor++;
                    while (param_cursor < line_end && *param_cursor != '"') {
                        param_cursor++;
                    }
                    if (param_cursor < line_end && *param_cursor == '"') {
                        param_cursor++;
                    }
                }
                while (param_cursor < line_end && isspace((unsigned char)*param_cursor)) {
                    param_cursor++;
                }
                errno = 0;
                char *value_end = NULL;
                float default_value = strtof(param_cursor, &value_end);
                if (param_cursor != value_end && errno == 0) {
                    struct retro_shader_parameter *new_params = realloc(*out_params,
                                                                         (*out_count + 1u) * sizeof(**out_params));
                    if (!new_params) {
                        retro_free_shader_parameters(*out_params, *out_count);
                        free(stripped);
                        return -1;
                    }
                    *out_params = new_params;
                    memset(&(*out_params)[*out_count], 0, sizeof((*out_params)[*out_count]));
                    snprintf((*out_params)[*out_count].name, sizeof((*out_params)[*out_count].name), "%s", name_buf);
                    (*out_params)[*out_count].default_value = default_value;
                    (*out_count)++;
                }
            }
        } else {
            if (line_len > 0u) {
                memcpy(stripped + stripped_len, line_start, line_len);
                stripped_len += line_len;
            }
            if (line_end < end && *line_end == '\n') {
                stripped[stripped_len++] = '\n';
            }
        }

        cursor = (line_end < end && *line_end == '\n') ? line_end + 1u : line_end;
    }

    stripped[stripped_len] = '\0';
    *out_stripped = stripped;
    *out_stripped_len = stripped_len;
    return 0;
}

static float retro_get_parameter_default(const struct retro_shader_parameter *params,
                                         size_t count,
                                         const char *name,
                                         float fallback) {
    if (!params || !name) {
        return fallback;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(params[i].name, name) == 0) {
            return params[i].default_value;
        }
    }
    return fallback;
}

static GLuint retro_compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
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

static int retro_shader_set_matrix(GLint location, GLfloat *cache, int *has_cache, const GLfloat *matrix) {
    if (location < 0 || !cache || !has_cache || !matrix) {
        return -1;
    }
    if (*has_cache && memcmp(cache, matrix, sizeof(GLfloat) * 16u) == 0) {
        return 0;
    }
    memcpy(cache, matrix, sizeof(GLfloat) * 16u);
    *has_cache = 1;
    glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
    return 0;
}

static int retro_shader_set_vec2(GLint location, GLfloat *cache, int *has_cache, GLfloat x, GLfloat y) {
    if (location < 0 || !cache || !has_cache) {
        return -1;
    }
    if (*has_cache && cache[0] == x && cache[1] == y) {
        return 0;
    }
    cache[0] = x;
    cache[1] = y;
    *has_cache = 1;
    glUniform2f(location, x, y);
    return 0;
}

static void retro_shader_reset_uniform_cache(struct retro_gl_shader *shader) {
    if (!shader) {
        return;
    }
    shader->has_cached_mvp = 0;
    shader->has_cached_output_size = 0;
    shader->has_cached_texture_size = 0;
    shader->has_cached_input_size = 0;
}

static int retro_shader_configure_vaos(struct retro_gl_shader *shader, GLuint quad_vbo) {
    if (!shader || quad_vbo == 0) {
        return -1;
    }
    GLuint vaos[2] = {0u, 0u};
    glGenVertexArrays(2, vaos);
    if (vaos[0] == 0u || vaos[1] == 0u) {
        if (vaos[0] != 0u) {
            glDeleteVertexArrays(1, &vaos[0]);
        }
        if (vaos[1] != 0u) {
            glDeleteVertexArrays(1, &vaos[1]);
        }
        return -1;
    }

    const GLint stride = (GLint)sizeof(struct retro_quad_vertex);
    const GLvoid *position_offset = (const GLvoid *)offsetof(struct retro_quad_vertex, position);
    const GLvoid *texcoord_offsets[2] = {
        (const GLvoid *)offsetof(struct retro_quad_vertex, texcoord_cpu),
        (const GLvoid *)offsetof(struct retro_quad_vertex, texcoord_fbo)
    };

    for (size_t i = 0; i < 2u; i++) {
        GLuint *target = &vaos[i];
        glBindVertexArray(*target);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        if (shader->attrib_vertex >= 0) {
            glEnableVertexAttribArray((GLuint)shader->attrib_vertex);
            glVertexAttribPointer((GLuint)shader->attrib_vertex, 4, GL_FLOAT, GL_FALSE, stride, position_offset);
        }
        if (shader->attrib_texcoord >= 0) {
            glEnableVertexAttribArray((GLuint)shader->attrib_texcoord);
            glVertexAttribPointer((GLuint)shader->attrib_texcoord, 2, GL_FLOAT, GL_FALSE, stride, texcoord_offsets[i]);
        }
        if (shader->attrib_color >= 0) {
            glDisableVertexAttribArray((GLuint)shader->attrib_color);
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader->quad_vaos[0] = vaos[0];
    shader->quad_vaos[1] = vaos[1];
    retro_shader_reset_uniform_cache(shader);
    return 0;
}

static void retro_shader_clear_vaos(struct retro_gl_shader *shader) {
    if (!shader) {
        return;
    }
    for (size_t i = 0; i < 2u; i++) {
        if (shader->quad_vaos[i] != 0) {
            glDeleteVertexArrays(1, &shader->quad_vaos[i]);
            shader->quad_vaos[i] = 0;
        }
    }
    retro_shader_reset_uniform_cache(shader);
}

static int retro_initialize_gl_program(struct retro_shader_bridge *bridge, const char *shader_path) {
    if (!bridge || !shader_path) {
        return -1;
    }
    size_t shader_size = 0u;
    char *shader_source = retro_read_text_file(shader_path, &shader_size);
    if (!shader_source) {
        fprintf(stderr, "Failed to read shader from %s\n", shader_path);
        return -1;
    }

    size_t content_size = shader_size;
    const char *content_start = retro_skip_utf8_bom(shader_source, &content_size);

    struct retro_shader_parameter *parameters = NULL;
    size_t parameter_count = 0u;
    char *stripped_source = NULL;
    size_t stripped_len = 0u;
    if (retro_parse_shader_parameters(content_start, content_size, &parameters, &parameter_count,
                                       &stripped_source, &stripped_len) != 0) {
        fprintf(stderr, "Failed to parse shader parameters from %s\n", shader_path);
        free(shader_source);
        return -1;
    }

    const char *version_line = NULL;
    size_t version_len = 0u;
    size_t version_prefix_len = 0u;
    const char *cursor = stripped_source;
    const char *end = stripped_source + stripped_len;
    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!line_end) {
            line_end = end;
        }
        size_t line_len = (size_t)(line_end - cursor);
        const char *scan = cursor;
        while (scan < line_end && (*scan == ' ' || *scan == '\t')) {
            scan++;
        }
        if (scan < line_end && strncmp(scan, "#version", 8) == 0) {
            version_line = cursor;
            version_len = line_len;
            version_prefix_len = (size_t)(line_end - stripped_source);
            if (line_end < end) {
                version_prefix_len++;
            }
            break;
        }
        if (line_end == end) {
            break;
        }
        cursor = line_end + 1u;
    }

    const char *shader_body = stripped_source;
    size_t shader_body_len = stripped_len;
    if (version_line && version_prefix_len > 0u && version_prefix_len <= stripped_len) {
        shader_body = stripped_source + version_prefix_len;
        shader_body_len = stripped_len - version_prefix_len;
    }

    const char *vertex_define = "#define VERTEX\n";
    const char *fragment_define = "#define FRAGMENT\n";
    size_t vertex_define_len = strlen(vertex_define);
    size_t fragment_define_len = strlen(fragment_define);

    size_t vertex_length = version_prefix_len + vertex_define_len + shader_body_len + 1u;
    size_t fragment_length = version_prefix_len + fragment_define_len + shader_body_len + 1u;

    char *vertex_source = malloc(vertex_length);
    char *fragment_source = malloc(fragment_length);
    if (!vertex_source || !fragment_source) {
        free(vertex_source);
        free(fragment_source);
        retro_free_shader_parameters(parameters, parameter_count);
        free(stripped_source);
        free(shader_source);
        return -1;
    }

    size_t offset = 0u;
    if (version_prefix_len > 0u) {
        memcpy(vertex_source + offset, stripped_source, version_prefix_len);
        offset += version_prefix_len;
    }
    memcpy(vertex_source + offset, vertex_define, vertex_define_len);
    offset += vertex_define_len;
    if (shader_body_len > 0u) {
        memcpy(vertex_source + offset, shader_body, shader_body_len);
        offset += shader_body_len;
    }
    vertex_source[offset] = '\0';

    offset = 0u;
    if (version_prefix_len > 0u) {
        memcpy(fragment_source + offset, stripped_source, version_prefix_len);
        offset += version_prefix_len;
    }
    memcpy(fragment_source + offset, fragment_define, fragment_define_len);
    offset += fragment_define_len;
    if (shader_body_len > 0u) {
        memcpy(fragment_source + offset, shader_body, shader_body_len);
        offset += shader_body_len;
    }
    fragment_source[offset] = '\0';

    GLuint vertex_shader = retro_compile_shader(GL_VERTEX_SHADER, vertex_source, "vertex");
    GLuint fragment_shader = retro_compile_shader(GL_FRAGMENT_SHADER, fragment_source, "fragment");

    free(vertex_source);
    free(fragment_source);
    free(stripped_source);
    free(shader_source);

    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        retro_free_shader_parameters(parameters, parameter_count);
        return -1;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        retro_free_shader_parameters(parameters, parameter_count);
        return -1;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status == GL_FALSE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 0) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetProgramInfoLog(program, log_length, NULL, log);
                fprintf(stderr, "Failed to link shader program: %s\n", log);
                free(log);
            }
        }
        glDeleteProgram(program);
        retro_free_shader_parameters(parameters, parameter_count);
        return -1;
    }

    struct retro_gl_shader shader_info;
    memset(&shader_info, 0, sizeof(shader_info));
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

    glUseProgram(program);
    if (shader_info.uniform_texture_sampler >= 0) {
        glUniform1i(shader_info.uniform_texture_sampler, 0);
    }
    if (shader_info.uniform_frame_direction >= 0) {
        glUniform1i(shader_info.uniform_frame_direction, 1);
    }
    if (shader_info.uniform_mvp >= 0) {
        retro_shader_set_matrix(shader_info.uniform_mvp,
                                shader_info.cached_mvp,
                                &shader_info.has_cached_mvp,
                                retro_identity_mvp);
    }

    for (size_t i = 0; i < parameter_count; i++) {
        GLint location = glGetUniformLocation(program, parameters[i].name);
        if (location >= 0) {
            glUniform1f(location, parameters[i].default_value);
        }
    }

    float crt_gamma = retro_get_parameter_default(parameters, parameter_count, "crt_gamma", 2.5f);
    float monitor_gamma = retro_get_parameter_default(parameters, parameter_count, "monitor_gamma", 2.2f);
    GLint crt_location = glGetUniformLocation(program, "CRTgamma");
    if (crt_location >= 0) {
        glUniform1f(crt_location, crt_gamma);
    }
    GLint monitor_location = glGetUniformLocation(program, "monitorgamma");
    if (monitor_location >= 0) {
        glUniform1f(monitor_location, monitor_gamma);
    }

    glUseProgram(0);

    retro_free_shader_parameters(parameters, parameter_count);

    if (bridge->quad_vbo == 0) {
        glGenBuffers(1, &bridge->quad_vbo);
        if (bridge->quad_vbo == 0) {
            glDeleteProgram(program);
            return -1;
        }
        glBindBuffer(GL_ARRAY_BUFFER, bridge->quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(retro_quad_vertices), retro_quad_vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (retro_shader_configure_vaos(&shader_info, bridge->quad_vbo) != 0) {
        glDeleteProgram(program);
        return -1;
    }

    struct retro_gl_shader *new_array = realloc(bridge->shaders, (bridge->shader_count + 1u) * sizeof(*new_array));
    if (!new_array) {
        retro_shader_clear_vaos(&shader_info);
        glDeleteProgram(program);
        return -1;
    }
    bridge->shaders = new_array;
    bridge->shaders[bridge->shader_count] = shader_info;
    bridge->shader_count++;

    return 0;
}

static void retro_clear_gl_shaders(struct retro_shader_bridge *bridge) {
    if (!bridge) {
        return;
    }
    if (bridge->shaders) {
        for (size_t i = 0; i < bridge->shader_count; i++) {
            if (bridge->shaders[i].program != 0) {
                glDeleteProgram(bridge->shaders[i].program);
            }
            retro_shader_clear_vaos(&bridge->shaders[i]);
        }
        free(bridge->shaders);
        bridge->shaders = NULL;
    }
    bridge->shader_count = 0u;
}

static void retro_release_gl_resources(struct retro_shader_bridge *bridge) {
    if (!bridge) {
        return;
    }
    if (bridge->texture != 0) {
        glDeleteTextures(1, &bridge->texture);
        bridge->texture = 0;
    }
    retro_clear_gl_shaders(bridge);
    if (bridge->intermediate_textures[0] != 0) {
        glDeleteTextures(1, &bridge->intermediate_textures[0]);
        bridge->intermediate_textures[0] = 0;
    }
    if (bridge->intermediate_textures[1] != 0) {
        glDeleteTextures(1, &bridge->intermediate_textures[1]);
        bridge->intermediate_textures[1] = 0;
    }
    if (bridge->framebuffer != 0) {
        glDeleteFramebuffers(1, &bridge->framebuffer);
        bridge->framebuffer = 0;
    }
    if (bridge->quad_vbo != 0) {
        glDeleteBuffers(1, &bridge->quad_vbo);
        bridge->quad_vbo = 0;
    }
    bridge->intermediate_width = 0;
    bridge->intermediate_height = 0;
    bridge->texture_width = 0;
    bridge->texture_height = 0;
    bridge->gl_ready = 0;
}

static void retro_bind_texture(GLuint texture) {
    glBindTexture(GL_TEXTURE_2D, texture);
}

static int retro_initialize_texture(struct retro_shader_bridge *bridge, int width, int height) {
    if (!bridge || width <= 0 || height <= 0) {
        return -1;
    }
    if (bridge->texture == 0) {
        glGenTextures(1, &bridge->texture);
    }
    if (bridge->texture == 0) {
        return -1;
    }
    retro_bind_texture(bridge->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bridge->frame_pixels);
    bridge->texture_width = width;
    bridge->texture_height = height;
    return 0;
}

static int retro_upload_framebuffer(struct retro_shader_bridge *bridge) {
    if (!bridge || !bridge->frame_pixels || bridge->frame_width == 0u || bridge->frame_height == 0u) {
        return -1;
    }
    if (bridge->texture == 0 || bridge->texture_width != (int)bridge->frame_width ||
        bridge->texture_height != (int)bridge->frame_height) {
        if (retro_initialize_texture(bridge, (int)bridge->frame_width, (int)bridge->frame_height) != 0) {
            return -1;
        }
    }

    retro_bind_texture(bridge->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    (GLsizei)bridge->frame_width,
                    (GLsizei)bridge->frame_height,
                    GL_RGBA, GL_UNSIGNED_BYTE, bridge->frame_pixels);
    return 0;
}

static int retro_prepare_intermediate_targets(struct retro_shader_bridge *bridge, int width, int height) {
    if (!bridge || width <= 0 || height <= 0) {
        return -1;
    }
    if (bridge->framebuffer == 0) {
        glGenFramebuffers(1, &bridge->framebuffer);
    }
    if (bridge->framebuffer == 0) {
        return -1;
    }
    if (bridge->intermediate_width == width && bridge->intermediate_height == height) {
        return 0;
    }
    for (size_t i = 0; i < 2u; i++) {
        if (bridge->intermediate_textures[i] == 0) {
            glGenTextures(1, &bridge->intermediate_textures[i]);
            if (bridge->intermediate_textures[i] == 0) {
                return -1;
            }
        }
        retro_bind_texture(bridge->intermediate_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    bridge->intermediate_width = width;
    bridge->intermediate_height = height;
    return 0;
}

struct retro_shader_bridge *retro_shader_bridge_create(SDL_Window *window,
                                                       const char *shader_root,
                                                       const char **shader_paths,
                                                       size_t shader_count) {
#if !BUDOSTACK_HAVE_SDL2
    (void)window;
    (void)shader_root;
    (void)shader_paths;
    (void)shader_count;
    return NULL;
#else
    if (!window) {
        return NULL;
    }
    struct retro_shader_bridge *bridge = calloc(1u, sizeof(*bridge));
    if (!bridge) {
        return NULL;
    }
    bridge->window = window;
    bridge->gl_ready = 1;

    for (size_t i = 0; i < shader_count; i++) {
        char resolved[PATH_MAX];
        if (retro_resolve_shader_path(shader_root, shader_paths[i], resolved, sizeof(resolved)) != 0) {
            fprintf(stderr, "Failed to resolve shader path: %s\n", shader_paths[i]);
            retro_shader_bridge_destroy(bridge);
            return NULL;
        }
        if (retro_initialize_gl_program(bridge, resolved) != 0) {
            fprintf(stderr, "Failed to load shader '%s'.\n", resolved);
            retro_shader_bridge_destroy(bridge);
            return NULL;
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return bridge;
#endif
}

void retro_shader_bridge_destroy(struct retro_shader_bridge *bridge) {
    if (!bridge) {
        return;
    }
#if BUDOSTACK_HAVE_SDL2
    retro_release_gl_resources(bridge);
#endif
    free(bridge->frame_pixels);
    free(bridge);
}

int retro_shader_bridge_set_frame(struct retro_shader_bridge *bridge,
                                  const void *data,
                                  unsigned width,
                                  unsigned height,
                                  size_t pitch,
                                  enum retro_pixel_format format) {
#if !BUDOSTACK_HAVE_SDL2
    (void)bridge;
    (void)data;
    (void)width;
    (void)height;
    (void)pitch;
    (void)format;
    return -1;
#else
    if (!bridge || !data || width == 0u || height == 0u || pitch == 0u) {
        return -1;
    }
    size_t width_size = (size_t)width;
    size_t height_size = (size_t)height;
    if (width_size == 0u || height_size == 0u) {
        return -1;
    }
    if (width_size > SIZE_MAX / height_size ||
        width_size * height_size > SIZE_MAX / 4u) {
        return -1;
    }
    size_t required = width_size * height_size * 4u;
    if (required > bridge->frame_capacity) {
        uint8_t *new_pixels = realloc(bridge->frame_pixels, required);
        if (!new_pixels) {
            return -1;
        }
        bridge->frame_pixels = new_pixels;
        bridge->frame_capacity = required;
    }

    const uint8_t *src = (const uint8_t *)data;
    uint8_t *dst = bridge->frame_pixels;

    if (format == RETRO_PIXEL_FORMAT_XRGB8888) {
        for (unsigned y = 0u; y < height; y++) {
            const uint32_t *row = (const uint32_t *)(src + y * pitch);
            for (unsigned x = 0u; x < width; x++) {
                uint32_t pixel = row[x];
                *dst++ = (uint8_t)((pixel >> 16) & 0xFFu);
                *dst++ = (uint8_t)((pixel >> 8) & 0xFFu);
                *dst++ = (uint8_t)(pixel & 0xFFu);
                *dst++ = 0xFFu;
            }
        }
    } else if (format == RETRO_PIXEL_FORMAT_RGB565) {
        for (unsigned y = 0u; y < height; y++) {
            const uint16_t *row = (const uint16_t *)(src + y * pitch);
            for (unsigned x = 0u; x < width; x++) {
                uint16_t pixel = row[x];
                uint8_t r = (uint8_t)(((pixel >> 11) & 0x1Fu) << 3u);
                uint8_t g = (uint8_t)(((pixel >> 5) & 0x3Fu) << 2u);
                uint8_t b = (uint8_t)((pixel & 0x1Fu) << 3u);
                *dst++ = r | (r >> 5u);
                *dst++ = g | (g >> 6u);
                *dst++ = b | (b >> 5u);
                *dst++ = 0xFFu;
            }
        }
    } else {
        return -1;
    }

    bridge->frame_width = width;
    bridge->frame_height = height;
    bridge->frame_dirty = 1;
    return 0;
#endif
}

int retro_shader_bridge_render(struct retro_shader_bridge *bridge, unsigned frame_count) {
#if !BUDOSTACK_HAVE_SDL2
    (void)bridge;
    (void)frame_count;
    return -1;
#else
    if (!bridge || !bridge->window || !bridge->gl_ready) {
        return -1;
    }
    if (bridge->frame_width == 0u || bridge->frame_height == 0u) {
        return 0;
    }

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(bridge->window, &drawable_width, &drawable_height);
    if (drawable_width <= 0 || drawable_height <= 0) {
        return -1;
    }

    if (bridge->frame_dirty) {
        if (retro_upload_framebuffer(bridge) != 0) {
            fprintf(stderr, "Failed to upload framebuffer to GPU.\n");
            return -1;
        }
        bridge->frame_dirty = 0;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    GLuint source_texture = bridge->texture;
    GLfloat source_texture_width = (GLfloat)bridge->texture_width;
    GLfloat source_texture_height = (GLfloat)bridge->texture_height;
    GLfloat source_input_width = (GLfloat)bridge->frame_width;
    GLfloat source_input_height = (GLfloat)bridge->frame_height;

    if (bridge->shader_count > 0u) {
        int multipass_failed = 0;
        for (size_t shader_index = 0; shader_index < bridge->shader_count; shader_index++) {
            struct retro_gl_shader *shader = &bridge->shaders[shader_index];
            if (!shader || shader->program == 0) {
                continue;
            }
            int last_pass = (shader_index + 1u == bridge->shader_count);
            GLuint target_texture = 0;
            int using_intermediate = 0;

            if (!last_pass) {
                if (retro_prepare_intermediate_targets(bridge, drawable_width, drawable_height) != 0) {
                    fprintf(stderr, "Failed to prepare intermediate render targets; skipping remaining shader passes.\n");
                    multipass_failed = 1;
                    last_pass = 1;
                } else {
                    target_texture = bridge->intermediate_textures[shader_index % 2u];
                    glBindFramebuffer(GL_FRAMEBUFFER, bridge->framebuffer);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
                    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    if (status != GL_FRAMEBUFFER_COMPLETE) {
                        fprintf(stderr, "Framebuffer incomplete (0x%04x); skipping remaining shader passes.\n", (unsigned int)status);
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

            retro_shader_set_vec2(shader->uniform_output_size,
                                  shader->cached_output_size,
                                  &shader->has_cached_output_size,
                                  (GLfloat)drawable_width,
                                  (GLfloat)drawable_height);
            if (shader->uniform_frame_count >= 0) {
                glUniform1i(shader->uniform_frame_count, (GLint)frame_count);
            }
            retro_shader_set_vec2(shader->uniform_texture_size,
                                  shader->cached_texture_size,
                                  &shader->has_cached_texture_size,
                                  source_texture_width,
                                  source_texture_height);
            retro_shader_set_vec2(shader->uniform_input_size,
                                  shader->cached_input_size,
                                  &shader->has_cached_input_size,
                                  source_input_width,
                                  source_input_height);

            glActiveTexture(GL_TEXTURE0);
            retro_bind_texture(source_texture);

            GLuint vao = (source_texture == bridge->texture) ? shader->quad_vaos[0] : shader->quad_vaos[1];
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
                const GLfloat *quad_texcoords = (source_texture == bridge->texture)
                                                ? fallback_texcoords_cpu
                                                : fallback_texcoords_fbo;
                if (shader->attrib_vertex >= 0) {
                    glEnableVertexAttribArray((GLuint)shader->attrib_vertex);
                    glVertexAttribPointer((GLuint)shader->attrib_vertex, 4, GL_FLOAT, GL_FALSE, 0, fallback_quad_vertices);
                }
                if (shader->attrib_texcoord >= 0) {
                    glEnableVertexAttribArray((GLuint)shader->attrib_texcoord);
                    glVertexAttribPointer((GLuint)shader->attrib_texcoord, 2, GL_FLOAT, GL_FALSE, 0, quad_texcoords);
                }
                if (shader->attrib_color >= 0) {
                    glDisableVertexAttribArray((GLuint)shader->attrib_color);
                    glVertexAttrib4f((GLuint)shader->attrib_color, 1.0f, 1.0f, 1.0f, 1.0f);
                }
            }

            glDrawArrays(GL_TRIANGLE_STRIP, 0, retro_quad_vertex_count);

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

            if (using_intermediate && !multipass_failed) {
                source_texture = target_texture;
                source_texture_width = (GLfloat)bridge->intermediate_width;
                source_texture_height = (GLfloat)bridge->intermediate_height;
                source_input_width = (GLfloat)bridge->intermediate_width;
                source_input_height = (GLfloat)bridge->intermediate_height;
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        }
    } else {
        glUseProgram(0);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glActiveTexture(GL_TEXTURE0);
        retro_bind_texture(bridge->texture);
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
        retro_bind_texture(0);
    }

    glUseProgram(0);
    SDL_GL_SwapWindow(bridge->window);
    return 0;
#endif
}
