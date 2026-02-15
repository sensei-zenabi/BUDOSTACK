#include "budo_shader_stack.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct budo_shader_parameter {
    char *name;
    float default_value;
};

struct budo_gl_shader {
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
    GLint uniform_prev_sampler;
    GLint uniform_crt_gamma;
    GLint uniform_monitor_gamma;
    GLint uniform_distance;
    GLint uniform_curvature;
    GLint uniform_radius;
    GLint uniform_corner_size;
    GLint uniform_corner_smooth;
    GLint uniform_x_tilt;
    GLint uniform_y_tilt;
    GLint uniform_overscan_x;
    GLint uniform_overscan_y;
    GLint uniform_dotmask;
    GLint uniform_sharper;
    GLint uniform_scanline_weight;
    GLint uniform_luminance;
    GLint uniform_interlace_detect;
    GLint uniform_saturation;
    GLint uniform_inv_gamma;
    GLuint history_texture;
    GLuint history_texture_flipped;
    GLuint quad_vaos[2];
    int has_cached_mvp;
    GLfloat cached_mvp[16];
    int has_cached_output_size;
    GLfloat cached_output_size[2];
    int has_cached_texture_size;
    GLfloat cached_texture_size[2];
    int has_cached_input_size;
    GLfloat cached_input_size[2];
};

struct budo_shader_stack {
    struct budo_gl_shader *shaders;
    size_t shader_count;
    GLuint quad_vbo;
    GLuint bound_texture;
    GLuint framebuffer;
    GLuint intermediate_textures[2];
    int intermediate_width;
    int intermediate_height;
    int history_width;
    int history_height;
};

struct budo_quad_vertex {
    GLfloat position[4];
    GLfloat texcoord_cpu[2];
    GLfloat texcoord_fbo[2];
};

static const struct budo_quad_vertex budo_quad_vertices[4] = {
    { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } },
    { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f } },
    { {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } }
};

static const GLsizei budo_quad_vertex_count = 4;

static const GLfloat budo_identity_mvp[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

static char *budo_read_text_file(const char *path, size_t *out_size) {
    if (!path) {
        return NULL;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "budo: Failed to open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "budo: Failed to seek %s\n", path);
        fclose(fp);
        return NULL;
    }
    long file_size = ftell(fp);
    if (file_size < 0) {
        fprintf(stderr, "budo: Failed to size %s\n", path);
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "budo: Failed to rewind %s\n", path);
        fclose(fp);
        return NULL;
    }

    size_t size = (size_t)file_size;
    char *buffer = malloc(size + 1u);
    if (!buffer) {
        fprintf(stderr, "budo: Out of memory reading %s\n", path);
        fclose(fp);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, fp);
    fclose(fp);
    if (read_bytes != size) {
        fprintf(stderr, "budo: Short read for %s\n", path);
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_size) {
        *out_size = size;
    }
    return buffer;
}

static const char *budo_skip_utf8_bom(const char *src, size_t *size) {
    if (!src || !size) {
        return src;
    }
    if (*size >= 3u) {
        const unsigned char *bytes = (const unsigned char *)src;
        if (bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) {
            *size -= 3u;
            return src + 3;
        }
    }
    return src;
}

static const char *budo_skip_leading_space_and_comments(const char *src, const char *end) {
    const char *ptr = src;
    while (ptr < end) {
        while (ptr < end && isspace((unsigned char)*ptr)) {
            ptr++;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '/') {
            ptr += 2;
            while (ptr < end && *ptr != '\n') {
                ptr++;
            }
            continue;
        }
        if ((end - ptr) >= 2 && ptr[0] == '/' && ptr[1] == '*') {
            ptr += 2;
            while ((end - ptr) >= 2 && !(ptr[0] == '*' && ptr[1] == '/')) {
                ptr++;
            }
            if ((end - ptr) >= 2) {
                ptr += 2;
            }
            continue;
        }
        break;
    }
    return ptr;
}

static void budo_free_shader_parameters(struct budo_shader_parameter *params, size_t count) {
    if (!params) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(params[i].name);
    }
    free(params);
}

static int budo_parse_shader_parameters(const char *source,
                                        size_t length,
                                        struct budo_shader_parameter **out_params,
                                        size_t *out_count) {
    if (!out_params || !out_count) {
        return -1;
    }
    *out_params = NULL;
    *out_count = 0u;
    if (!source || length == 0u) {
        return 0;
    }

    struct budo_shader_parameter *params = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    const char *ptr = source;
    const char *end = source + length;

    while (ptr < end) {
        const char *line_start = ptr;
        const char *line_end = line_start;
        while (line_end < end && line_end[0] != '\n' && line_end[0] != '\r') {
            line_end++;
        }

        const char *cursor = line_start;
        while (cursor < line_end && (*cursor == ' ' || *cursor == '\t')) {
            cursor++;
        }

        if ((size_t)(line_end - cursor) >= 7u && strncmp(cursor, "#pragma", 7) == 0) {
            cursor += 7;
            while (cursor < line_end && isspace((unsigned char)*cursor)) {
                cursor++;
            }

            const char keyword[] = "parameter";
            size_t keyword_len = sizeof(keyword) - 1u;
            if ((size_t)(line_end - cursor) >= keyword_len && strncmp(cursor, keyword, keyword_len) == 0) {
                const char *after_keyword = cursor + keyword_len;
                if (after_keyword < line_end && !isspace((unsigned char)*after_keyword)) {
                    /* Likely parameteri or another pragma, ignore. */
                } else {
                    cursor = after_keyword;
                    while (cursor < line_end && isspace((unsigned char)*cursor)) {
                        cursor++;
                    }

                    const char *name_start = cursor;
                    while (cursor < line_end && (isalnum((unsigned char)*cursor) || *cursor == '_')) {
                        cursor++;
                    }
                    const char *name_end = cursor;
                    if (name_end > name_start) {
                        size_t name_len = (size_t)(name_end - name_start);
                        while (cursor < line_end && isspace((unsigned char)*cursor)) {
                            cursor++;
                        }
                        if (cursor < line_end && *cursor == '"') {
                            cursor++;
                            while (cursor < line_end && *cursor != '"') {
                                cursor++;
                            }
                            if (cursor < line_end && *cursor == '"') {
                                cursor++;
                                while (cursor < line_end && isspace((unsigned char)*cursor)) {
                                    cursor++;
                                }
                                if (cursor < line_end) {
                                    const char *value_start = cursor;
                                    while (cursor < line_end && !isspace((unsigned char)*cursor)) {
                                        cursor++;
                                    }
                                    size_t value_len = (size_t)(cursor - value_start);
                                    if (value_len > 0u) {
                                        char stack_buffer[64];
                                        char *value_str = stack_buffer;
                                        char *heap_buffer = NULL;
                                        if (value_len >= sizeof(stack_buffer)) {
                                            heap_buffer = malloc(value_len + 1u);
                                            if (!heap_buffer) {
                                                budo_free_shader_parameters(params, count);
                                                return -1;
                                            }
                                            value_str = heap_buffer;
                                        }
                                        memcpy(value_str, value_start, value_len);
                                        value_str[value_len] = '\0';

                                        errno = 0;
                                        char *endptr = NULL;
                                        double parsed = strtod(value_str, &endptr);
                                        if (endptr != value_str && errno != ERANGE) {
                                            char *name_copy = malloc(name_len + 1u);
                                            if (!name_copy) {
                                                free(heap_buffer);
                                                budo_free_shader_parameters(params, count);
                                                return -1;
                                            }
                                            memcpy(name_copy, name_start, name_len);
                                            name_copy[name_len] = '\0';

                                            if (count == capacity) {
                                                size_t new_capacity = capacity == 0u ? 4u : capacity * 2u;
                                                struct budo_shader_parameter *new_params = realloc(params, new_capacity * sizeof(*new_params));
                                                if (!new_params) {
                                                    free(name_copy);
                                                    free(heap_buffer);
                                                    budo_free_shader_parameters(params, count);
                                                    return -1;
                                                }
                                                params = new_params;
                                                capacity = new_capacity;
                                            }

                                            params[count].name = name_copy;
                                            params[count].default_value = (float)parsed;
                                            count++;
                                        }
                                        free(heap_buffer);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ptr = line_end;
        while (ptr < end && (*ptr == '\n' || *ptr == '\r')) {
            ptr++;
        }
    }

    if (count == 0u) {
        free(params);
        params = NULL;
    }

    *out_params = params;
    *out_count = count;
    return 0;
}

static void budo_shader_reset_uniform_cache(struct budo_gl_shader *shader) {
    if (!shader) {
        return;
    }
    shader->has_cached_mvp = 0;
    shader->has_cached_output_size = 0;
    shader->has_cached_texture_size = 0;
    shader->has_cached_input_size = 0;
}

static void budo_shader_set_matrix(GLint location, GLfloat *cache, int *has_cache, const GLfloat *matrix) {
    if (location < 0 || !cache || !has_cache || !matrix) {
        return;
    }
    if (*has_cache && memcmp(cache, matrix, sizeof(GLfloat) * 16u) == 0) {
        return;
    }
    memcpy(cache, matrix, sizeof(GLfloat) * 16u);
    *has_cache = 1;
    glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
}

static void budo_shader_set_vec2(GLint location, GLfloat *cache, int *has_cache, GLfloat x, GLfloat y) {
    if (location < 0 || !cache || !has_cache) {
        return;
    }
    if (*has_cache && cache[0] == x && cache[1] == y) {
        return;
    }
    cache[0] = x;
    cache[1] = y;
    *has_cache = 1;
    glUniform2f(location, x, y);
}

static void budo_shader_clear_vaos(struct budo_gl_shader *shader) {
    if (!shader) {
        return;
    }
    for (size_t i = 0; i < 2; i++) {
        if (shader->quad_vaos[i] != 0) {
            glDeleteVertexArrays(1, &shader->quad_vaos[i]);
            shader->quad_vaos[i] = 0;
        }
    }
    budo_shader_reset_uniform_cache(shader);
}

static int budo_initialize_quad_geometry(struct budo_shader_stack *stack) {
    if (!stack) {
        return -1;
    }
    if (stack->quad_vbo != 0) {
        return 0;
    }
    glGenBuffers(1, &stack->quad_vbo);
    if (stack->quad_vbo == 0) {
        return -1;
    }
    glBindBuffer(GL_ARRAY_BUFFER, stack->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(budo_quad_vertices), budo_quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 0;
}

static void budo_destroy_quad_geometry(struct budo_shader_stack *stack) {
    if (!stack) {
        return;
    }
    if (stack->quad_vbo != 0) {
        glDeleteBuffers(1, &stack->quad_vbo);
        stack->quad_vbo = 0;
    }
}

static int budo_shader_configure_vaos(struct budo_shader_stack *stack, struct budo_gl_shader *shader) {
    if (!stack || !shader) {
        return -1;
    }
    if (stack->quad_vbo == 0) {
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

    const GLsizei stride = (GLsizei)sizeof(struct budo_quad_vertex);
    const void *position_offset = (const void *)offsetof(struct budo_quad_vertex, position);
    const void *cpu_offset = (const void *)offsetof(struct budo_quad_vertex, texcoord_cpu);
    const void *fbo_offset = (const void *)offsetof(struct budo_quad_vertex, texcoord_fbo);

    GLuint *targets[2] = {&vaos[0], &vaos[1]};
    const void *texcoord_offsets[2] = {cpu_offset, fbo_offset};

    for (size_t i = 0; i < 2; i++) {
        glBindVertexArray(*targets[i]);
        glBindBuffer(GL_ARRAY_BUFFER, stack->quad_vbo);
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
    return 0;
}

static void budo_bind_texture(struct budo_shader_stack *stack, GLuint texture) {
    if (!stack) {
        return;
    }
    if (stack->bound_texture != texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        stack->bound_texture = texture;
    }
}

static float budo_get_parameter_default(const struct budo_shader_parameter *params,
                                        size_t count,
                                        const char *name,
                                        float fallback) {
    if (!params || !name) {
        return fallback;
    }
    for (size_t i = 0; i < count; i++) {
        if (params[i].name && strcmp(params[i].name, name) == 0) {
            return params[i].default_value;
        }
    }
    return fallback;
}

static GLuint budo_compile_shader(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetShaderInfoLog(shader, log_length, NULL, log);
                fprintf(stderr, "budo: Failed to compile %s shader: %s\n", label ? label : "GL", log);
                free(log);
            }
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static int budo_initialize_gl_program(struct budo_shader_stack *stack, const char *shader_path) {
    if (!stack || !shader_path) {
        return -1;
    }

    int result = -1;
    size_t shader_size = 0u;
    char *shader_source = NULL;
    char *vertex_source = NULL;
    char *fragment_source = NULL;
    struct budo_shader_parameter *parameters = NULL;
    size_t parameter_count = 0u;
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;
    struct budo_gl_shader shader_info;
    memset(&shader_info, 0, sizeof(shader_info));

    shader_source = budo_read_text_file(shader_path, &shader_size);
    if (!shader_source) {
        fprintf(stderr, "budo: Failed to read shader from %s\n", shader_path);
        goto cleanup;
    }

    const char *version_line = "#version 110\n";
    const char *parameter_define = "#define PARAMETER_UNIFORM 1\n";
    const char *vertex_define = "#define VERTEX 1\n";
    const char *fragment_define = "#define FRAGMENT 1\n";

    size_t parameter_len = strlen(parameter_define);
    size_t vertex_define_len = strlen(vertex_define);
    size_t fragment_define_len = strlen(fragment_define);
    size_t version_line_len = strlen(version_line);

    size_t content_size = shader_size;
    const char *content_start = budo_skip_utf8_bom(shader_source, &content_size);
    const char *content_end = content_start + content_size;

    if (budo_parse_shader_parameters(content_start, content_size, &parameters, &parameter_count) != 0) {
        goto cleanup;
    }

    const char *version_start = NULL;
    const char *version_end = NULL;
    const char *scan = budo_skip_leading_space_and_comments(content_start, content_end);
    if (scan < content_end) {
        size_t remaining = (size_t)(content_end - scan);
        if (remaining >= 8u && strncmp(scan, "#version", 8) == 0) {
            if (remaining == 8u || isspace((unsigned char)scan[8])) {
                version_start = scan;
                version_end = scan;
                while (version_end < content_end && *version_end != '\n') {
                    version_end++;
                }
                if (version_end < content_end) {
                    version_end++;
                }
            }
        }
    }

    const char *version_prefix = version_line;
    size_t version_prefix_len = version_line_len;
    const char *shader_body = content_start;
    size_t shader_body_len = content_size;

    if (version_start && version_end) {
        version_prefix = content_start;
        version_prefix_len = (size_t)(version_end - content_start);
        shader_body = version_end;
        shader_body_len = (size_t)(content_end - version_end);
    }

    size_t newline_len = 0u;
    if (version_prefix_len > 0u) {
        char last_char = version_prefix[version_prefix_len - 1u];
        if (last_char != '\n' && last_char != '\r') {
            newline_len = 1u;
        }
    }

    size_t vertex_length = version_prefix_len + newline_len + parameter_len + vertex_define_len + shader_body_len;
    size_t fragment_length = version_prefix_len + newline_len + parameter_len + fragment_define_len + shader_body_len;

    vertex_source = malloc(vertex_length + 1u);
    fragment_source = malloc(fragment_length + 1u);
    if (!vertex_source || !fragment_source) {
        fprintf(stderr, "budo: Out of memory building shader sources\n");
        goto cleanup;
    }

    size_t offset = 0u;
    memcpy(vertex_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    if (newline_len > 0u) {
        vertex_source[offset++] = '\n';
    }
    memcpy(vertex_source + offset, parameter_define, parameter_len);
    offset += parameter_len;
    memcpy(vertex_source + offset, vertex_define, vertex_define_len);
    offset += vertex_define_len;
    memcpy(vertex_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    vertex_source[offset] = '\0';

    offset = 0u;
    memcpy(fragment_source + offset, version_prefix, version_prefix_len);
    offset += version_prefix_len;
    if (newline_len > 0u) {
        fragment_source[offset++] = '\n';
    }
    memcpy(fragment_source + offset, parameter_define, parameter_len);
    offset += parameter_len;
    memcpy(fragment_source + offset, fragment_define, fragment_define_len);
    offset += fragment_define_len;
    memcpy(fragment_source + offset, shader_body, shader_body_len);
    offset += shader_body_len;
    fragment_source[offset] = '\0';

    vertex_shader = budo_compile_shader(GL_VERTEX_SHADER, vertex_source, "vertex");
    fragment_shader = budo_compile_shader(GL_FRAGMENT_SHADER, fragment_source, "fragment");

    if (vertex_shader == 0 || fragment_shader == 0) {
        goto cleanup;
    }

    program = glCreateProgram();
    if (program == 0) {
        fprintf(stderr, "budo: Failed to create program for %s\n", shader_path);
        goto cleanup;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    vertex_shader = 0;
    fragment_shader = 0;

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        if (log_length > 1) {
            char *log = malloc((size_t)log_length);
            if (log) {
                glGetProgramInfoLog(program, log_length, NULL, log);
                fprintf(stderr, "budo: Failed to link shader program: %s\n", log);
                free(log);
            }
        }
        goto cleanup;
    }

    budo_shader_reset_uniform_cache(&shader_info);
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
    shader_info.uniform_prev_sampler = glGetUniformLocation(program, "Prev0");
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
    if (shader_info.uniform_prev_sampler >= 0) {
        glUniform1i(shader_info.uniform_prev_sampler, 1);
    }
    if (shader_info.uniform_frame_direction >= 0) {
        glUniform1i(shader_info.uniform_frame_direction, 1);
    }
    if (shader_info.uniform_mvp >= 0) {
        budo_shader_set_matrix(shader_info.uniform_mvp,
                               shader_info.cached_mvp,
                               &shader_info.has_cached_mvp,
                               budo_identity_mvp);
    }

    for (size_t i = 0; i < parameter_count; i++) {
        if (!parameters[i].name) {
            continue;
        }
        GLint location = glGetUniformLocation(program, parameters[i].name);
        if (location >= 0) {
            glUniform1f(location, parameters[i].default_value);
        }
    }

    if (shader_info.uniform_crt_gamma >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "CRTgamma", 2.4f);
        glUniform1f(shader_info.uniform_crt_gamma, value);
    }
    if (shader_info.uniform_monitor_gamma >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "monitorgamma", 2.2f);
        glUniform1f(shader_info.uniform_monitor_gamma, value);
    }
    if (shader_info.uniform_distance >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "d", 1.6f);
        glUniform1f(shader_info.uniform_distance, value);
    }
    if (shader_info.uniform_curvature >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "CURVATURE", 1.0f);
        glUniform1f(shader_info.uniform_curvature, value);
    }
    if (shader_info.uniform_radius >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "R", 2.0f);
        glUniform1f(shader_info.uniform_radius, value);
    }
    if (shader_info.uniform_corner_size >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "cornersize", 0.03f);
        glUniform1f(shader_info.uniform_corner_size, value);
    }
    if (shader_info.uniform_corner_smooth >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "cornersmooth", 1000.0f);
        glUniform1f(shader_info.uniform_corner_smooth, value);
    }
    if (shader_info.uniform_x_tilt >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "x_tilt", 0.0f);
        glUniform1f(shader_info.uniform_x_tilt, value);
    }
    if (shader_info.uniform_y_tilt >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "y_tilt", 0.0f);
        glUniform1f(shader_info.uniform_y_tilt, value);
    }
    if (shader_info.uniform_overscan_x >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "overscan_x", 100.0f);
        glUniform1f(shader_info.uniform_overscan_x, value);
    }
    if (shader_info.uniform_overscan_y >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "overscan_y", 100.0f);
        glUniform1f(shader_info.uniform_overscan_y, value);
    }
    if (shader_info.uniform_dotmask >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "DOTMASK", 0.3f);
        glUniform1f(shader_info.uniform_dotmask, value);
    }
    if (shader_info.uniform_sharper >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "SHARPER", 1.0f);
        glUniform1f(shader_info.uniform_sharper, value);
    }
    if (shader_info.uniform_scanline_weight >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "scanline_weight", 0.3f);
        glUniform1f(shader_info.uniform_scanline_weight, value);
    }
    if (shader_info.uniform_luminance >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "lum", 0.0f);
        glUniform1f(shader_info.uniform_luminance, value);
    }
    if (shader_info.uniform_interlace_detect >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "interlace_detect", 1.0f);
        glUniform1f(shader_info.uniform_interlace_detect, value);
    }
    if (shader_info.uniform_saturation >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "SATURATION", 1.0f);
        glUniform1f(shader_info.uniform_saturation, value);
    }
    if (shader_info.uniform_inv_gamma >= 0) {
        float value = budo_get_parameter_default(parameters, parameter_count, "INV", 1.0f);
        glUniform1f(shader_info.uniform_inv_gamma, value);
    }
    glUseProgram(0);

    if (budo_shader_configure_vaos(stack, &shader_info) != 0) {
        fprintf(stderr, "budo: Failed to configure shader VAOs for %s\n", shader_path);
        goto cleanup;
    }

    budo_free_shader_parameters(parameters, parameter_count);
    parameters = NULL;
    parameter_count = 0u;

    struct budo_gl_shader *new_array = realloc(stack->shaders, (stack->shader_count + 1u) * sizeof(*new_array));
    if (!new_array) {
        fprintf(stderr, "budo: Out of memory registering shader %s\n", shader_path);
        goto cleanup;
    }
    stack->shaders = new_array;
    stack->shaders[stack->shader_count] = shader_info;
    stack->shader_count++;
    program = 0;
    result = 0;

cleanup:
    if (result != 0) {
        budo_shader_clear_vaos(&shader_info);
    }
    if (program != 0) {
        glDeleteProgram(program);
    }
    if (fragment_shader != 0) {
        glDeleteShader(fragment_shader);
    }
    if (vertex_shader != 0) {
        glDeleteShader(vertex_shader);
    }
    budo_free_shader_parameters(parameters, parameter_count);
    free(fragment_source);
    free(vertex_source);
    free(shader_source);
    return result;
}

static int budo_prepare_intermediate_targets(struct budo_shader_stack *stack, int width, int height) {
    if (!stack || width <= 0 || height <= 0) {
        return -1;
    }

    if (stack->framebuffer == 0) {
        glGenFramebuffers(1, &stack->framebuffer);
    }
    if (stack->framebuffer == 0) {
        return -1;
    }

    int resized = 0;
    for (size_t i = 0; i < 2; i++) {
        if (stack->intermediate_textures[i] == 0) {
            glGenTextures(1, &stack->intermediate_textures[i]);
            if (stack->intermediate_textures[i] == 0) {
                return -1;
            }
            resized = 1;
        }
    }

    if (width != stack->intermediate_width || height != stack->intermediate_height) {
        resized = 1;
    }

    if (resized) {
        for (size_t i = 0; i < 2; i++) {
            budo_bind_texture(stack, stack->intermediate_textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        budo_bind_texture(stack, 0);
        stack->intermediate_width = width;
        stack->intermediate_height = height;
    }

    return 0;
}

static void budo_clear_history_texture(struct budo_shader_stack *stack, GLuint texture, int width, int height) {
    if (!stack || texture == 0 || width <= 0 || height <= 0) {
        return;
    }
    if (stack->framebuffer == 0) {
        glGenFramebuffers(1, &stack->framebuffer);
        if (stack->framebuffer == 0) {
            return;
        }
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, stack->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

static int budo_prepare_shader_history(struct budo_shader_stack *stack,
                                       struct budo_gl_shader *shader,
                                       int width,
                                       int height,
                                       int resized) {
    if (!stack || !shader || width <= 0 || height <= 0) {
        return -1;
    }

    int created_history = 0;
    int created_flipped = 0;

    if (shader->history_texture == 0) {
        glGenTextures(1, &shader->history_texture);
        if (shader->history_texture != 0) {
            created_history = 1;
        }
    }
    if (shader->history_texture == 0) {
        return -1;
    }

    if (shader->history_texture_flipped == 0) {
        glGenTextures(1, &shader->history_texture_flipped);
        if (shader->history_texture_flipped == 0) {
            fprintf(stderr, "budo: Failed to create flipped history texture.\n");
        } else {
            created_flipped = 1;
        }
    }

    if (created_history || resized || stack->history_width == 0 || stack->history_height == 0) {
        budo_bind_texture(stack, shader->history_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        budo_bind_texture(stack, 0);
        budo_clear_history_texture(stack, shader->history_texture, width, height);

        if (shader->history_texture_flipped != 0 &&
            (created_flipped || resized || stack->history_width == 0 || stack->history_height == 0)) {
            budo_bind_texture(stack, shader->history_texture_flipped);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            budo_bind_texture(stack, 0);
            budo_clear_history_texture(stack, shader->history_texture_flipped, width, height);
        }
    }

    return 0;
}

static void budo_update_flipped_history_texture(struct budo_shader_stack *stack,
                                                GLuint history_texture,
                                                GLuint history_texture_flipped,
                                                int width,
                                                int height) {
    if (!stack || history_texture == 0 || history_texture_flipped == 0 || width <= 0 || height <= 0) {
        return;
    }

    if (stack->framebuffer == 0) {
        glGenFramebuffers(1, &stack->framebuffer);
        if (stack->framebuffer == 0) {
            return;
        }
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, stack->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, history_texture_flipped, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    budo_bind_texture(stack, history_texture);
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
    budo_bind_texture(stack, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

static void budo_update_shader_history(struct budo_shader_stack *stack,
                                       struct budo_gl_shader *shader,
                                       int width,
                                       int height) {
    if (!stack || !shader || shader->history_texture == 0 || width <= 0 || height <= 0) {
        return;
    }

    glActiveTexture(GL_TEXTURE1);
    budo_bind_texture(stack, shader->history_texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    budo_bind_texture(stack, 0);
    glActiveTexture(GL_TEXTURE0);

    budo_update_flipped_history_texture(stack,
                                        shader->history_texture,
                                        shader->history_texture_flipped,
                                        width,
                                        height);
}

static void budo_clear_shader_history(struct budo_gl_shader *shader) {
    if (!shader) {
        return;
    }
    if (shader->history_texture != 0) {
        glDeleteTextures(1, &shader->history_texture);
        shader->history_texture = 0;
    }
    if (shader->history_texture_flipped != 0) {
        glDeleteTextures(1, &shader->history_texture_flipped);
        shader->history_texture_flipped = 0;
    }
}

int budo_shader_stack_init(struct budo_shader_stack **out_stack) {
    if (!out_stack) {
        return -1;
    }

    struct budo_shader_stack *stack = calloc(1, sizeof(*stack));
    if (!stack) {
        fprintf(stderr, "budo: Failed to allocate shader stack\n");
        return -1;
    }

    *out_stack = stack;
    return 0;
}

void budo_shader_stack_destroy(struct budo_shader_stack *stack) {
    if (!stack) {
        return;
    }

    budo_shader_stack_clear(stack);
    budo_destroy_quad_geometry(stack);
    if (stack->intermediate_textures[0] != 0) {
        glDeleteTextures(1, &stack->intermediate_textures[0]);
        stack->intermediate_textures[0] = 0;
    }
    if (stack->intermediate_textures[1] != 0) {
        glDeleteTextures(1, &stack->intermediate_textures[1]);
        stack->intermediate_textures[1] = 0;
    }
    if (stack->framebuffer != 0) {
        glDeleteFramebuffers(1, &stack->framebuffer);
        stack->framebuffer = 0;
    }
    free(stack);
}

int budo_shader_stack_load(struct budo_shader_stack *stack,
                           const char *const *shader_paths,
                           size_t shader_count) {
    if (!stack || (!shader_paths && shader_count > 0u)) {
        return -1;
    }

    budo_shader_stack_clear(stack);
    if (shader_count == 0u) {
        return 0;
    }

    if (budo_initialize_quad_geometry(stack) != 0) {
        fprintf(stderr, "budo: Failed to initialize quad geometry\n");
        return -1;
    }

    for (size_t i = 0; i < shader_count; i++) {
        if (budo_initialize_gl_program(stack, shader_paths[i]) != 0) {
            fprintf(stderr, "budo: Failed to load shader '%s'\n", shader_paths[i]);
            budo_shader_stack_clear(stack);
            return -1;
        }
    }

    return 0;
}

void budo_shader_stack_clear(struct budo_shader_stack *stack) {
    if (!stack) {
        return;
    }

    if (stack->shaders) {
        for (size_t i = 0; i < stack->shader_count; i++) {
            if (stack->shaders[i].program != 0) {
                glDeleteProgram(stack->shaders[i].program);
            }
            budo_clear_shader_history(&stack->shaders[i]);
            budo_shader_clear_vaos(&stack->shaders[i]);
        }
        free(stack->shaders);
        stack->shaders = NULL;
    }
    stack->history_width = 0;
    stack->history_height = 0;
    stack->shader_count = 0u;
}

int budo_shader_stack_render(struct budo_shader_stack *stack,
                             GLuint source_texture,
                             int source_width,
                             int source_height,
                             int output_width,
                             int output_height,
                             int source_tex_is_fbo,
                             int frame_value) {
    if (!stack || stack->shader_count == 0u) {
        return 0;
    }
    if (source_texture == 0 || output_width <= 0 || output_height <= 0) {
        return -1;
    }

    GLuint current_texture = source_texture;
    GLfloat current_texture_width = (GLfloat)source_width;
    GLfloat current_texture_height = (GLfloat)source_height;
    GLfloat current_input_width = (GLfloat)source_width;
    GLfloat current_input_height = (GLfloat)source_height;
    int current_from_fbo = source_tex_is_fbo != 0;

    int history_resized = 0;
    if (stack->history_width != output_width || stack->history_height != output_height) {
        stack->history_width = output_width;
        stack->history_height = output_height;
        history_resized = 1;
    }

    int multipass_failed = 0;

    for (size_t shader_index = 0; shader_index < stack->shader_count; shader_index++) {
        struct budo_gl_shader *shader = &stack->shaders[shader_index];
        if (!shader || shader->program == 0) {
            continue;
        }

        int last_pass = (shader_index + 1u == stack->shader_count);
        GLuint target_texture = 0;
        int using_intermediate = 0;

        if (!last_pass) {
            if (budo_prepare_intermediate_targets(stack, output_width, output_height) != 0) {
                fprintf(stderr, "budo: Failed to prepare intermediate targets; skipping remaining shader passes.\n");
                multipass_failed = 1;
                last_pass = 1;
            } else {
                target_texture = stack->intermediate_textures[shader_index % 2u];
                glBindFramebuffer(GL_FRAMEBUFFER, stack->framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
                GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    fprintf(stderr, "budo: Framebuffer incomplete (0x%04x); skipping remaining shader passes.\n", (unsigned int)status);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    multipass_failed = 1;
                    last_pass = 1;
                } else {
                    using_intermediate = 1;
                    glViewport(0, 0, output_width, output_height);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
            }
        }

        if (last_pass && !using_intermediate) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, output_width, output_height);
        }

        glUseProgram(shader->program);

        budo_shader_set_vec2(shader->uniform_output_size,
                             shader->cached_output_size,
                             &shader->has_cached_output_size,
                             (GLfloat)output_width,
                             (GLfloat)output_height);
        if (shader->uniform_frame_count >= 0) {
            glUniform1i(shader->uniform_frame_count, frame_value);
        }
        budo_shader_set_vec2(shader->uniform_texture_size,
                             shader->cached_texture_size,
                             &shader->has_cached_texture_size,
                             current_texture_width,
                             current_texture_height);
        budo_shader_set_vec2(shader->uniform_input_size,
                             shader->cached_input_size,
                             &shader->has_cached_input_size,
                             current_input_width,
                             current_input_height);

        if (shader->uniform_prev_sampler >= 0) {
            GLuint history_texture = 0;
            if (budo_prepare_shader_history(stack, shader, output_width, output_height, history_resized) == 0) {
                history_texture = shader->history_texture;
                if (!current_from_fbo && shader->history_texture_flipped != 0) {
                    history_texture = shader->history_texture_flipped;
                }
            }
            glActiveTexture(GL_TEXTURE1);
            budo_bind_texture(stack, history_texture);
            glActiveTexture(GL_TEXTURE0);
        }

        glActiveTexture(GL_TEXTURE0);
        budo_bind_texture(stack, current_texture);

        GLuint vao = current_from_fbo ? shader->quad_vaos[1] : shader->quad_vaos[0];
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
                const GLfloat *quad_texcoords = current_from_fbo ? fallback_texcoords_fbo : fallback_texcoords_cpu;
                glEnableVertexAttribArray((GLuint)shader->attrib_texcoord);
                glVertexAttribPointer((GLuint)shader->attrib_texcoord, 2, GL_FLOAT, GL_FALSE, 0, quad_texcoords);
            }
        }
        if (shader->attrib_color >= 0) {
            glDisableVertexAttribArray((GLuint)shader->attrib_color);
            glVertexAttrib4f((GLuint)shader->attrib_color, 1.0f, 1.0f, 1.0f, 1.0f);
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, budo_quad_vertex_count);

        if (shader->uniform_prev_sampler >= 0) {
            budo_update_shader_history(stack, shader, output_width, output_height);
        }

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
            current_texture = target_texture;
            current_texture_width = (GLfloat)output_width;
            current_texture_height = (GLfloat)output_height;
            current_input_width = (GLfloat)output_width;
            current_input_height = (GLfloat)output_height;
            current_from_fbo = 1;
        }

        if (multipass_failed) {
            break;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    return multipass_failed ? -1 : 0;
}
