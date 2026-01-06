#define _POSIX_C_SOURCE 200809L

#include "budo_graphics.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUDO_GRAPHICS_MAX_LAYERS 16
#define BUDO_GRAPHICS_TEXT_COLOR_DEFAULT 17

struct budo_graphics_layer {
    uint8_t *pixels;
    size_t opaque_count;
    int dirty;
};

static struct budo_graphics_layer budo_graphics_layers[BUDO_GRAPHICS_MAX_LAYERS];
static int budo_graphics_width = 0;
static int budo_graphics_height = 0;
static uint8_t budo_graphics_bg_r = 0u;
static uint8_t budo_graphics_bg_g = 0u;
static uint8_t budo_graphics_bg_b = 0u;
static int budo_graphics_initialized = 0;

static char *budo_graphics_base64_buffer = NULL;
static size_t budo_graphics_base64_capacity = 0u;

static void budo_graphics_release_layers(void) {
    for (int i = 0; i < BUDO_GRAPHICS_MAX_LAYERS; i++) {
        free(budo_graphics_layers[i].pixels);
        budo_graphics_layers[i].pixels = NULL;
        budo_graphics_layers[i].opaque_count = 0u;
        budo_graphics_layers[i].dirty = 0;
    }
}

static int budo_graphics_write_all(const char *buffer, size_t length) {
    if (!buffer || length == 0u) {
        return 0;
    }

    size_t offset = 0u;
    while (offset < length) {
        ssize_t written = write(STDOUT_FILENO, buffer + offset, length - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        offset += (size_t)written;
    }

    return 0;
}

static size_t budo_graphics_base64_encoded_size(size_t raw_size) {
    if (raw_size == 0u) {
        return 0u;
    }

    size_t rem = raw_size % 3u;
    size_t blocks = raw_size / 3u;
    size_t encoded = blocks * 4u;
    if (rem > 0u) {
        encoded += 4u;
    }
    return encoded;
}

static char budo_graphics_base64_table(int idx) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (idx < 0 || idx >= 64) {
        return '=';
    }
    return table[idx];
}

static int budo_graphics_base64_encode(const uint8_t *data, size_t size, char *out, size_t out_size) {
    if (!data || !out) {
        return -1;
    }

    size_t required = budo_graphics_base64_encoded_size(size);
    if (out_size < required + 1u) {
        return -1;
    }

    size_t out_idx = 0u;
    for (size_t i = 0u; i + 2u < size; i += 3u) {
        uint32_t block = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1u] << 8) | (uint32_t)data[i + 2u];
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = budo_graphics_base64_table((int)(block & 0x3Fu));
    }

    size_t remaining = size % 3u;
    if (remaining == 1u) {
        uint32_t block = ((uint32_t)data[size - 1u]) << 16;
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = '=';
        out[out_idx++] = '=';
    } else if (remaining == 2u) {
        uint32_t block = ((uint32_t)data[size - 2u] << 16) | ((uint32_t)data[size - 1u] << 8);
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = budo_graphics_base64_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = '=';
    }

    out[out_idx] = '\0';
    return 0;
}

static int budo_graphics_encode_payload(const uint8_t *payload, size_t payload_size, char **out_encoded,
                                        size_t *out_length) {
    if (!payload || !out_encoded || !out_length) {
        return -1;
    }

    size_t encoded_size = budo_graphics_base64_encoded_size(payload_size);
    if (encoded_size == 0u) {
        return -1;
    }
    if (encoded_size > SIZE_MAX - 1u) {
        return -1;
    }

    if (encoded_size + 1u > budo_graphics_base64_capacity) {
        size_t new_capacity = encoded_size + 1u;
        char *new_buffer = realloc(budo_graphics_base64_buffer, new_capacity);
        if (!new_buffer) {
            return -1;
        }
        budo_graphics_base64_buffer = new_buffer;
        budo_graphics_base64_capacity = new_capacity;
    }

    if (budo_graphics_base64_encode(payload, payload_size, budo_graphics_base64_buffer,
                                    budo_graphics_base64_capacity) != 0) {
        return -1;
    }

    *out_encoded = budo_graphics_base64_buffer;
    *out_length = encoded_size;
    return 0;
}

static int budo_graphics_write_sprite_clear(int layer) {
    char header[128];
    int header_len = snprintf(header,
                              sizeof(header),
                              "\x1b]777;sprite=clear;sprite_x=0;sprite_y=0;sprite_w=%d;sprite_h=%d;sprite_layer=%d\a",
                              budo_graphics_width,
                              budo_graphics_height,
                              layer);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        return -1;
    }

    return budo_graphics_write_all(header, (size_t)header_len);
}

static int budo_graphics_write_sprite_draw(int layer, const uint8_t *payload, size_t payload_size) {
    char *encoded = NULL;
    size_t encoded_len = 0u;
    if (budo_graphics_encode_payload(payload, payload_size, &encoded, &encoded_len) != 0) {
        return -1;
    }

    char header[160];
    int header_len = snprintf(header,
                              sizeof(header),
                              "\x1b]777;sprite=draw;sprite_x=0;sprite_y=0;sprite_w=%d;sprite_h=%d;sprite_layer=%d;sprite_data=",
                              budo_graphics_width,
                              budo_graphics_height,
                              layer);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        return -1;
    }

    if (budo_graphics_write_all(header, (size_t)header_len) != 0) {
        return -1;
    }
    if (budo_graphics_write_all(encoded, encoded_len) != 0) {
        return -1;
    }
    if (budo_graphics_write_all("\a", 1u) != 0) {
        return -1;
    }

    return 0;
}

static int budo_graphics_write_render(int layer) {
    char header[64];
    int header_len = 0;
    if (layer == 0) {
        header_len = snprintf(header, sizeof(header), "\x1b]777;pixel=render\a");
    } else {
        header_len = snprintf(header, sizeof(header), "\x1b]777;pixel=render;pixel_layer=%d\a", layer);
    }
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        return -1;
    }
    return budo_graphics_write_all(header, (size_t)header_len);
}

static size_t budo_graphics_pixel_offset(int x, int y) {
    return ((size_t)y * (size_t)budo_graphics_width + (size_t)x) * 4u;
}

static void budo_graphics_fill_layer(struct budo_graphics_layer *layer) {
    if (!layer || !layer->pixels) {
        return;
    }

    size_t pixel_count = (size_t)budo_graphics_width * (size_t)budo_graphics_height;
    for (size_t i = 0u; i < pixel_count; i++) {
        size_t idx = i * 4u;
        layer->pixels[idx] = budo_graphics_bg_r;
        layer->pixels[idx + 1u] = budo_graphics_bg_g;
        layer->pixels[idx + 2u] = budo_graphics_bg_b;
        layer->pixels[idx + 3u] = 255u;
    }
    layer->opaque_count = pixel_count;
    layer->dirty = 1;
}

static int budo_graphics_allocate_layers(size_t buffer_size) {
    for (int i = 0; i < BUDO_GRAPHICS_MAX_LAYERS; i++) {
        budo_graphics_layers[i].pixels = malloc(buffer_size);
        if (!budo_graphics_layers[i].pixels) {
            for (int j = 0; j < i; j++) {
                free(budo_graphics_layers[j].pixels);
                budo_graphics_layers[j].pixels = NULL;
                budo_graphics_layers[j].opaque_count = 0u;
                budo_graphics_layers[j].dirty = 0;
            }
            return -1;
        }
        budo_graphics_fill_layer(&budo_graphics_layers[i]);
    }
    return 0;
}

static int budo_graphics_validate_layer(int layer) {
    return layer >= 1 && layer <= BUDO_GRAPHICS_MAX_LAYERS;
}

void budo_graphics_init(int width, int height, int r, int g, int b) {
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "budo_graphics_init: width and height must be positive.\n");
        return;
    }

    if (width > INT_MAX || height > INT_MAX) {
        fprintf(stderr, "budo_graphics_init: width/height too large.\n");
        return;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (height_sz != 0u && width_sz > SIZE_MAX / height_sz) {
        fprintf(stderr, "budo_graphics_init: resolution is too large.\n");
        return;
    }

    size_t pixel_count = width_sz * height_sz;
    if (pixel_count > SIZE_MAX / 4u) {
        fprintf(stderr, "budo_graphics_init: resolution is too large.\n");
        return;
    }

    budo_graphics_release_layers();
    free(budo_graphics_base64_buffer);
    budo_graphics_base64_buffer = NULL;
    budo_graphics_base64_capacity = 0u;
    budo_graphics_initialized = 0;

    budo_graphics_width = width;
    budo_graphics_height = height;
    budo_graphics_bg_r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
    budo_graphics_bg_g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
    budo_graphics_bg_b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));

    size_t buffer_size = pixel_count * 4u;
    if (budo_graphics_allocate_layers(buffer_size) != 0) {
        fprintf(stderr, "budo_graphics_init: failed to allocate layer buffers.\n");
        budo_graphics_release_layers();
        return;
    }

    budo_graphics_initialized = 1;

    char header[80];
    int header_len = snprintf(header,
                              sizeof(header),
                              "\x1b]777;resolution=%dx%d\a",
                              budo_graphics_width,
                              budo_graphics_height);
    if (header_len > 0 && (size_t)header_len < sizeof(header)) {
        if (budo_graphics_write_all(header, (size_t)header_len) != 0) {
            fprintf(stderr, "budo_graphics_init: failed to write resolution command.\n");
        }
    }
}

void budo_graphics_pixel(int x, int y, int r, int g, int b, int layer) {
    if (!budo_graphics_initialized) {
        return;
    }

    if (!budo_graphics_validate_layer(layer)) {
        fprintf(stderr, "budo_graphics_pixel: layer must be 1-16.\n");
        return;
    }

    if (x < 0 || y < 0 || x >= budo_graphics_width || y >= budo_graphics_height) {
        return;
    }

    struct budo_graphics_layer *target = &budo_graphics_layers[layer - 1];
    if (!target->pixels) {
        return;
    }

    size_t offset = budo_graphics_pixel_offset(x, y);
    uint8_t *pixel = &target->pixels[offset];

    if (pixel[3] == 0u) {
        target->opaque_count++;
    }

    pixel[0] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
    pixel[1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
    pixel[2] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
    pixel[3] = 255u;
    target->dirty = 1;
}

void budo_graphics_clean(int x, int y, int layer) {
    if (!budo_graphics_initialized) {
        return;
    }

    if (!budo_graphics_validate_layer(layer)) {
        fprintf(stderr, "budo_graphics_clean: layer must be 1-16.\n");
        return;
    }

    if (x < 0 || y < 0 || x >= budo_graphics_width || y >= budo_graphics_height) {
        return;
    }

    struct budo_graphics_layer *target = &budo_graphics_layers[layer - 1];
    if (!target->pixels) {
        return;
    }

    size_t offset = budo_graphics_pixel_offset(x, y);
    uint8_t *pixel = &target->pixels[offset];

    if (pixel[3] != 0u) {
        if (target->opaque_count > 0u) {
            target->opaque_count--;
        }
    }

    pixel[3] = 0u;
    target->dirty = 1;
}

void budo_graphics_text(int x, int y, const char *str, int layer) {
    if (!budo_graphics_initialized) {
        return;
    }

    if (!str || str[0] == '\0') {
        return;
    }

    if (!budo_graphics_validate_layer(layer)) {
        fprintf(stderr, "budo_graphics_text: layer must be 1-16.\n");
        return;
    }

    if (x < 0 || y < 0) {
        return;
    }

    size_t text_len = strlen(str);
    char *encoded = NULL;
    size_t encoded_len = 0u;
    if (budo_graphics_encode_payload((const uint8_t *)str, text_len, &encoded, &encoded_len) != 0) {
        fprintf(stderr, "budo_graphics_text: failed to encode text.\n");
        return;
    }

    char header[160];
    int header_len = snprintf(header,
                              sizeof(header),
                              "\x1b]777;text=draw;text_x=%d;text_y=%d;text_layer=%d;text_color=%d;text_data=",
                              x,
                              y,
                              layer,
                              BUDO_GRAPHICS_TEXT_COLOR_DEFAULT);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        return;
    }

    if (budo_graphics_write_all(header, (size_t)header_len) != 0) {
        fprintf(stderr, "budo_graphics_text: failed to write text command.\n");
        return;
    }
    if (budo_graphics_write_all(encoded, encoded_len) != 0) {
        fprintf(stderr, "budo_graphics_text: failed to write text payload.\n");
        return;
    }
    if (budo_graphics_write_all("\a", 1u) != 0) {
        fprintf(stderr, "budo_graphics_text: failed to finish text payload.\n");
        return;
    }
}

void budo_graphics_render(int layer) {
    if (!budo_graphics_initialized) {
        return;
    }

    int start_layer = layer;
    int end_layer = layer;
    if (layer <= 0) {
        start_layer = 1;
        end_layer = BUDO_GRAPHICS_MAX_LAYERS;
    }

    if (start_layer < 1 || end_layer > BUDO_GRAPHICS_MAX_LAYERS) {
        fprintf(stderr, "budo_graphics_render: layer must be 1-16 or 0 for all.\n");
        return;
    }

    size_t pixel_count = (size_t)budo_graphics_width * (size_t)budo_graphics_height;
    size_t payload_size = pixel_count * 4u;

    for (int current = start_layer; current <= end_layer; current++) {
        struct budo_graphics_layer *target = &budo_graphics_layers[current - 1];
        if (!target->pixels || !target->dirty) {
            continue;
        }

        if (budo_graphics_write_sprite_clear(current) != 0) {
            fprintf(stderr, "budo_graphics_render: failed to clear layer %d.\n", current);
            continue;
        }

        if (target->opaque_count > 0u) {
            if (budo_graphics_write_sprite_draw(current, target->pixels, payload_size) != 0) {
                fprintf(stderr, "budo_graphics_render: failed to draw layer %d.\n", current);
                continue;
            }
        }

        if (budo_graphics_write_render(current) != 0) {
            fprintf(stderr, "budo_graphics_render: failed to render layer %d.\n", current);
            continue;
        }

        target->dirty = 0;
    }
}
