#define _POSIX_C_SOURCE 200809L

#include "termgfx.h"

#include "retroprofile.h"
#include "stb_image.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long clamp_layer(long layer) {
    if (layer < 1) {
        return 1;
    }
    if (layer > 16) {
        return 16;
    }
    return layer;
}

static size_t base64_encoded_size(size_t raw_size) {
    size_t blocks = raw_size / 3u;
    size_t encoded = blocks * 4u;

    if (raw_size % 3u != 0u) {
        encoded += 4u;
    }
    return encoded;
}

static char base64_encode_table(int idx) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (idx < 0 || idx >= 64) {
        return '=';
    }
    return table[idx];
}

static int encode_base64(const uint8_t *data, size_t size, char *out, size_t out_size) {
    if (!data || !out) {
        return -1;
    }

    size_t required = base64_encoded_size(size);
    if (out_size < required + 1u) {
        return -1;
    }

    size_t out_idx = 0u;
    size_t i = 0u;
    for (; i + 2u < size; i += 3u) {
        uint32_t block = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1u] << 8) | (uint32_t)data[i + 2u];
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3fu));
        out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3fu));
        out[out_idx++] = base64_encode_table((int)((block >> 6) & 0x3fu));
        out[out_idx++] = base64_encode_table((int)(block & 0x3fu));
    }

    if (i < size) {
        uint32_t block = (uint32_t)data[i] << 16;
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3fu));
        if (i + 1u < size) {
            block |= (uint32_t)data[i + 1u] << 8;
            out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3fu));
            out[out_idx++] = base64_encode_table((int)((block >> 6) & 0x3fu));
            out[out_idx++] = '=';
        } else {
            out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3fu));
            out[out_idx++] = '=';
            out[out_idx++] = '=';
        }
    }

    out[out_idx] = '\0';
    return 0;
}

static int load_file_as_encoded_rgba(const char *path, int *width_out, int *height_out, char **encoded_out) {
    if (!path || !width_out || !height_out || !encoded_out) {
        fprintf(stderr, "termgfx: invalid sprite load argument.\n");
        return -1;
    }

    *width_out = 0;
    *height_out = 0;
    *encoded_out = NULL;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        if (reason && *reason) {
            fprintf(stderr, "termgfx: failed to load '%s': %s\n", path, reason);
        } else {
            fprintf(stderr, "termgfx: failed to load '%s'\n", path);
        }
        return -1;
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        fprintf(stderr, "termgfx: invalid image dimensions in '%s'\n", path);
        return -1;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (width_sz != 0u && height_sz > SIZE_MAX / width_sz) {
        stbi_image_free(pixels);
        fprintf(stderr, "termgfx: image dimensions overflow for '%s'.\n", path);
        return -1;
    }

    size_t pixel_count = width_sz * height_sz;
    if (pixel_count > SIZE_MAX / 4u) {
        stbi_image_free(pixels);
        fprintf(stderr, "termgfx: image too large to encode: '%s'.\n", path);
        return -1;
    }

    size_t raw_size = pixel_count * 4u;
    size_t encoded_size = base64_encoded_size(raw_size);
    if (encoded_size == 0u || encoded_size > SIZE_MAX - 1u) {
        stbi_image_free(pixels);
        fprintf(stderr, "termgfx: failed to compute encoded size for '%s'.\n", path);
        return -1;
    }

    char *encoded = (char *)malloc(encoded_size + 1u);
    if (!encoded) {
        stbi_image_free(pixels);
        perror("termgfx: malloc");
        return -1;
    }

    int encode_status = encode_base64(pixels, raw_size, encoded, encoded_size + 1u);
    stbi_image_free(pixels);
    if (encode_status != 0) {
        free(encoded);
        fprintf(stderr, "termgfx: failed to encode image data from '%s'.\n", path);
        return -1;
    }

    *width_out = width;
    *height_out = height;
    *encoded_out = encoded;
    return 0;
}

static int parse_sprite_literal(const char *literal, int *width_out, int *height_out, char **encoded_out) {
    if (!literal || !width_out || !height_out || !encoded_out) {
        return -1;
    }

    const char *p = literal;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '{') {
        fprintf(stderr, "termgfx: sprite literal must start with '{'.\n");
        return -1;
    }
    p++;

    errno = 0;
    char *endptr = NULL;
    long width = strtol(p, &endptr, 10);
    if (errno != 0 || endptr == p || width <= 0 || width > INT_MAX) {
        fprintf(stderr, "termgfx: invalid sprite width in literal.\n");
        return -1;
    }
    p = endptr;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ',') {
        fprintf(stderr, "termgfx: sprite literal missing comma after width.\n");
        return -1;
    }
    p++;

    errno = 0;
    long height = strtol(p, &endptr, 10);
    if (errno != 0 || endptr == p || height <= 0 || height > INT_MAX) {
        fprintf(stderr, "termgfx: invalid sprite height in literal.\n");
        return -1;
    }
    p = endptr;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ',') {
        fprintf(stderr, "termgfx: sprite literal missing comma after height.\n");
        return -1;
    }
    p++;
    while (isspace((unsigned char)*p)) {
        p++;
    }

    const char *data_start = p;
    const char *data_end = NULL;
    if (*p == '"') {
        p++;
        data_start = p;
        while (*p && *p != '"') {
            p++;
        }
        if (*p != '"') {
            fprintf(stderr, "termgfx: sprite literal is missing the closing quote for data.\n");
            return -1;
        }
        data_end = p;
        p++;
    } else {
        while (*p && *p != '}') {
            p++;
        }
        data_end = p;
        while (data_end > data_start && isspace((unsigned char)data_end[-1])) {
            data_end--;
        }
    }

    if (data_end <= data_start) {
        fprintf(stderr, "termgfx: sprite literal must contain base64 data.\n");
        return -1;
    }

    size_t data_len = (size_t)(data_end - data_start);
    char *encoded = (char *)malloc(data_len + 1u);
    if (!encoded) {
        perror("termgfx: malloc");
        return -1;
    }
    memcpy(encoded, data_start, data_len);
    encoded[data_len] = '\0';

    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '}') {
        free(encoded);
        fprintf(stderr, "termgfx: sprite literal must end with '}'.\n");
        return -1;
    }
    p++;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        free(encoded);
        fprintf(stderr, "termgfx: unexpected characters after sprite literal.\n");
        return -1;
    }

    *width_out = (int)width;
    *height_out = (int)height;
    *encoded_out = encoded;
    return 0;
}

int termgfx_color_from_index(int index, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out) {
    if (!r_out || !g_out || !b_out) {
        return -1;
    }

    if (index < 0) {
        index = 0;
    } else if (index > 18) {
        index = 18;
    }

    const RetroProfile *profile = retroprofile_active();
    if (!profile) {
        return -1;
    }

    RetroColor color;
    if (index >= 0 && index < 16) {
        color = profile->colors[index];
    } else if (index == 16) {
        color = profile->defaults.foreground;
    } else if (index == 17) {
        color = profile->defaults.background;
    } else {
        color = profile->defaults.cursor;
    }

    *r_out = color.r;
    *g_out = color.g;
    *b_out = color.b;
    return 0;
}

int termgfx_pixel(long x, long y, uint8_t r, uint8_t g, uint8_t b, long layer) {
    if (x < 0 || y < 0) {
        return -1;
    }

    if (printf("\x1b]777;pixel=draw;pixel_x=%ld;pixel_y=%ld;pixel_r=%u;pixel_g=%u;pixel_b=%u;pixel_layer=%ld\a",
               x,
               y,
               (unsigned int)r,
               (unsigned int)g,
               (unsigned int)b,
               clamp_layer(layer)) < 0) {
        return -1;
    }

    return 0;
}

int termgfx_rect(long x, long y, long width, long height, uint8_t r, uint8_t g, uint8_t b, long layer) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        return -1;
    }

    if (printf("\x1b]777;pixel=rect;pixel_x=%ld;pixel_y=%ld;pixel_w=%ld;pixel_h=%ld;pixel_r=%u;pixel_g=%u;pixel_b=%u;pixel_layer=%ld\a",
               x,
               y,
               width,
               height,
               (unsigned int)r,
               (unsigned int)g,
               (unsigned int)b,
               clamp_layer(layer)) < 0) {
        return -1;
    }

    return 0;
}

int termgfx_clear(long x, long y, long width, long height, long layer) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        return -1;
    }

    if (printf("\x1b]777;sprite=clear;sprite_x=%ld;sprite_y=%ld;sprite_w=%ld;sprite_h=%ld;sprite_layer=%ld\a",
               x,
               y,
               width,
               height,
               clamp_layer(layer)) < 0) {
        return -1;
    }

    return 0;
}

int termgfx_render(long layer) {
    if (layer <= 0) {
        if (printf("\x1b]777;pixel=render\a") < 0) {
            return -1;
        }
    } else {
        if (printf("\x1b]777;pixel=render;pixel_layer=%ld\a", clamp_layer(layer)) < 0) {
            return -1;
        }
    }

    if (fflush(stdout) != 0) {
        return -1;
    }
    return 0;
}

int termgfx_sprite_data(long x, long y, long width, long height, const char *encoded_rgba, long layer) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || !encoded_rgba || *encoded_rgba == '\0') {
        return -1;
    }

    if (printf("\x1b]777;sprite=draw;sprite_x=%ld;sprite_y=%ld;sprite_w=%ld;sprite_h=%ld;sprite_layer=%ld;sprite_data=%s\a",
               x,
               y,
               width,
               height,
               clamp_layer(layer),
               encoded_rgba) < 0) {
        return -1;
    }

    return fflush(stdout) == 0 ? 0 : -1;
}

int termgfx_sprite_literal(long x, long y, const char *literal, long layer) {
    int width = 0;
    int height = 0;
    char *encoded = NULL;

    if (parse_sprite_literal(literal, &width, &height, &encoded) != 0) {
        return -1;
    }

    int rc = termgfx_sprite_data(x, y, width, height, encoded, layer);
    free(encoded);
    return rc;
}

int termgfx_sprite_file(long x, long y, const char *path, long layer) {
    int width = 0;
    int height = 0;
    char *encoded = NULL;

    if (load_file_as_encoded_rgba(path, &width, &height, &encoded) != 0) {
        return -1;
    }

    int rc = termgfx_sprite_data(x, y, width, height, encoded, layer);
    free(encoded);
    return rc;
}

int termgfx_sprite_load_literal(const char *path, char **literal_out) {
    if (!literal_out) {
        return -1;
    }
    *literal_out = NULL;

    int width = 0;
    int height = 0;
    char *encoded = NULL;
    if (load_file_as_encoded_rgba(path, &width, &height, &encoded) != 0) {
        return -1;
    }

    int needed = snprintf(NULL, 0, "{%d,%d,\"%s\"}", width, height, encoded);
    if (needed < 0) {
        free(encoded);
        return -1;
    }

    char *literal = (char *)malloc((size_t)needed + 1u);
    if (!literal) {
        free(encoded);
        perror("termgfx: malloc");
        return -1;
    }

    int written = snprintf(literal, (size_t)needed + 1u, "{%d,%d,\"%s\"}", width, height, encoded);
    free(encoded);
    if (written != needed) {
        free(literal);
        return -1;
    }

    *literal_out = literal;
    return 0;
}
