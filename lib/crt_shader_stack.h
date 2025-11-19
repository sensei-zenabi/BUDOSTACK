#ifndef CRT_SHADER_STACK_H
#define CRT_SHADER_STACK_H

#include <stddef.h>

struct crt_shader_parameter {
    char *name;
    float default_value;
};

char *crt_shader_read_text_file(const char *path, size_t *out_size);
const char *crt_shader_skip_utf8_bom(const char *src, size_t *size);
const char *crt_shader_skip_leading_space_and_comments(const char *src, const char *end);
void crt_shader_free_parameters(struct crt_shader_parameter *params, size_t count);
int crt_shader_parse_parameters(const char *source, size_t length,
                                struct crt_shader_parameter **out_params,
                                size_t *out_count);
float crt_shader_get_parameter_default(const struct crt_shader_parameter *params,
                                       size_t count,
                                       const char *name,
                                       float fallback);

#endif
