#ifndef CRT_SHADER_GL_H
#define CRT_SHADER_GL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct crt_shader_vertex {
    GLfloat position[4];
    GLfloat texcoord_cpu[2];
    GLfloat texcoord_fbo[2];
};

struct crt_gl_shader {
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

static inline void crt_shader_reset_uniform_cache(struct crt_gl_shader *shader) {
    if (!shader) {
        return;
    }
    shader->has_cached_mvp = 0;
    shader->has_cached_output_size = 0;
    shader->has_cached_texture_size = 0;
    shader->has_cached_input_size = 0;
}

static inline void crt_shader_set_matrix(GLint location,
                                         GLfloat *cache,
                                         int *has_cache,
                                         const GLfloat *matrix) {
    if (location < 0 || !cache || !has_cache || !matrix) {
        return;
    }
    if (*has_cache && memcmp(cache, matrix, sizeof(GLfloat) * 16) == 0) {
        return;
    }
    glUniformMatrix4fv(location, 1, GL_FALSE, matrix);
    memcpy(cache, matrix, sizeof(GLfloat) * 16);
    *has_cache = 1;
}

static inline void crt_shader_set_vec2(GLint location,
                                       GLfloat *cache,
                                       int *has_cache,
                                       GLfloat x,
                                       GLfloat y) {
    if (location < 0 || !cache || !has_cache) {
        return;
    }
    GLfloat values[2] = {x, y};
    if (*has_cache && memcmp(cache, values, sizeof(values)) == 0) {
        return;
    }
    glUniform2f(location, x, y);
    cache[0] = x;
    cache[1] = y;
    *has_cache = 1;
}

static inline void crt_shader_clear_vaos(struct crt_gl_shader *shader) {
    if (!shader) {
        return;
    }
    for (size_t i = 0; i < 2; i++) {
        if (shader->quad_vaos[i] != 0) {
            glDeleteVertexArrays(1, &shader->quad_vaos[i]);
            shader->quad_vaos[i] = 0;
        }
    }
    crt_shader_reset_uniform_cache(shader);
}

static inline int crt_shader_configure_vaos(struct crt_gl_shader *shader,
                                            GLuint quad_vbo,
                                            size_t vertex_stride,
                                            size_t position_offset,
                                            size_t texcoord_cpu_offset,
                                            size_t texcoord_fbo_offset) {
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

    const GLsizei stride = (GLsizei)vertex_stride;
    const void *position_ptr = (const void *)(uintptr_t)position_offset;
    const void *cpu_ptr = (const void *)(uintptr_t)texcoord_cpu_offset;
    const void *fbo_ptr = (const void *)(uintptr_t)texcoord_fbo_offset;
    const void *texcoord_offsets[2] = {cpu_ptr, fbo_ptr};

    for (size_t i = 0; i < 2; i++) {
        glBindVertexArray(vaos[i]);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        if (shader->attrib_vertex >= 0) {
            glEnableVertexAttribArray((GLuint)shader->attrib_vertex);
            glVertexAttribPointer((GLuint)shader->attrib_vertex, 4, GL_FLOAT, GL_FALSE, stride, position_ptr);
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

#endif
