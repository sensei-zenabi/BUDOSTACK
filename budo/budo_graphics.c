#define _POSIX_C_SOURCE 200809L

#include "budo_graphics.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUDO_MAX_LAYERS 16

static uint8_t *budo_layer_buffers[BUDO_MAX_LAYERS + 1] = {NULL};
static int budo_layer_dirty[BUDO_MAX_LAYERS + 1] = {0};
static size_t budo_layer_capacity = 0u;
static int budo_layer_width = 0;
static int budo_layer_height = 0;
static uint8_t *budo_composite_buffer = NULL;
static size_t budo_composite_capacity = 0u;

static size_t budo_base64_encoded_size(size_t raw_size) {
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

static char budo_base64_encode_table(int idx) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (idx < 0 || idx >= 64) {
        return '=';
    }
    return table[idx];
}

static int budo_base64_encode(const uint8_t *data, size_t size, char *out, size_t out_size) {
    if (!data || !out) {
        return -1;
    }

    size_t required = budo_base64_encoded_size(size);
    if (out_size < required + 1u) {
        return -1;
    }

    size_t out_idx = 0u;
    for (size_t i = 0u; i + 2u < size; i += 3u) {
        uint32_t block = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1u] << 8) | (uint32_t)data[i + 2u];
        out[out_idx++] = budo_base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = budo_base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = budo_base64_encode_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = budo_base64_encode_table((int)(block & 0x3Fu));
    }

    size_t remaining = size % 3u;
    if (remaining == 1u) {
        uint32_t block = ((uint32_t)data[size - 1u]) << 16;
        out[out_idx++] = budo_base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = budo_base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = '=';
        out[out_idx++] = '=';
    } else if (remaining == 2u) {
        uint32_t block = ((uint32_t)data[size - 2u] << 16) | ((uint32_t)data[size - 1u] << 8);
        out[out_idx++] = budo_base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = budo_base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = budo_base64_encode_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = '=';
    }

    out[out_idx] = '\0';
    return 0;
}

static int budo_layer_buffer_ensure(int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (height_sz > SIZE_MAX / width_sz) {
        return -1;
    }
    size_t needed = width_sz * height_sz;
    if (needed > SIZE_MAX / 4u) {
        return -1;
    }
    needed *= 4u;

    if (needed > budo_layer_capacity) {
        for (int layer = 1; layer <= BUDO_MAX_LAYERS; layer++) {
            uint8_t *new_buffer = realloc(budo_layer_buffers[layer], needed);
            if (!new_buffer) {
                return -1;
            }
            budo_layer_buffers[layer] = new_buffer;
        }
        budo_layer_capacity = needed;
    }

    if (needed > budo_composite_capacity) {
        uint8_t *new_composite = realloc(budo_composite_buffer, needed);
        if (!new_composite) {
            return -1;
        }
        budo_composite_buffer = new_composite;
        budo_composite_capacity = needed;
    }

    budo_layer_width = width;
    budo_layer_height = height;

    for (int layer = 1; layer <= BUDO_MAX_LAYERS; layer++) {
        if (budo_layer_buffers[layer]) {
            memset(budo_layer_buffers[layer], 0, needed);
        }
        budo_layer_dirty[layer] = 1;
    }

    if (budo_composite_buffer) {
        memset(budo_composite_buffer, 0, needed);
    }

    return 0;
}

static void budo_layer_buffers_clear_all(void) {
    for (int layer = 1; layer <= BUDO_MAX_LAYERS; layer++) {
        free(budo_layer_buffers[layer]);
        budo_layer_buffers[layer] = NULL;
        budo_layer_dirty[layer] = 0;
    }
    free(budo_composite_buffer);
    budo_composite_buffer = NULL;
    budo_layer_capacity = 0u;
    budo_composite_capacity = 0u;
    budo_layer_width = 0;
    budo_layer_height = 0;
}

static int budo_send_command(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int status = vprintf(format, args);
    va_end(args);
    if (status < 0) {
        return -1;
    }
    if (fflush(stdout) != 0) {
        return -1;
    }
    return 0;
}

int budo_graphics_set_resolution(int width, int height) {
    if (width < 0 || height < 0 || width > INT_MAX || height > INT_MAX) {
        fprintf(stderr, "budo_graphics_set_resolution: invalid dimensions\n");
        return -1;
    }

    if (width > 0 && height > 0) {
        if (budo_layer_buffer_ensure(width, height) != 0) {
            fprintf(stderr, "budo_graphics_set_resolution: failed to allocate buffers\n");
            return -1;
        }
    } else {
        budo_layer_buffers_clear_all();
    }

    if (budo_send_command("\x1b]777;resolution=%dx%d\a", width, height) != 0) {
        perror("budo_graphics_set_resolution: printf");
        return -1;
    }

    return 0;
}

int budo_graphics_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, int layer) {
    if (x < 0 || y < 0) {
        fprintf(stderr, "budo_graphics_draw_pixel: invalid coordinates\n");
        return -1;
    }
    if (layer < 0 || layer > 16) {
        fprintf(stderr, "budo_graphics_draw_pixel: layer must be 0-16\n");
        return -1;
    }

    if (layer >= 1 && layer <= BUDO_MAX_LAYERS &&
        budo_layer_buffers[layer] &&
        x < budo_layer_width &&
        y < budo_layer_height) {
        size_t offset = ((size_t)y * (size_t)budo_layer_width + (size_t)x) * 4u;
        if (offset + 3u < budo_layer_capacity) {
            budo_layer_buffers[layer][offset + 0u] = r;
            budo_layer_buffers[layer][offset + 1u] = g;
            budo_layer_buffers[layer][offset + 2u] = b;
            budo_layer_buffers[layer][offset + 3u] = 255u;
            budo_layer_dirty[layer] = 1;
            return 0;
        }
    }

    if (layer == 0) {
        if (budo_send_command("\x1b]777;pixel=draw;pixel_x=%d;pixel_y=%d;pixel_r=%u;pixel_g=%u;pixel_b=%u\a",
                              x,
                              y,
                              (unsigned int)r,
                              (unsigned int)g,
                              (unsigned int)b) != 0) {
            perror("budo_graphics_draw_pixel: printf");
            return -1;
        }
    } else {
        if (budo_send_command("\x1b]777;pixel=draw;pixel_x=%d;pixel_y=%d;pixel_layer=%d;pixel_r=%u;pixel_g=%u;pixel_b=%u\a",
                              x,
                              y,
                              layer,
                              (unsigned int)r,
                              (unsigned int)g,
                              (unsigned int)b) != 0) {
            perror("budo_graphics_draw_pixel: printf");
            return -1;
        }
    }

    return 0;
}

int budo_graphics_clear_pixels(int layer) {
    if (layer < 0 || layer > 16) {
        fprintf(stderr, "budo_graphics_clear_pixels: layer must be 0-16\n");
        return -1;
    }

    if (layer >= 1 && layer <= BUDO_MAX_LAYERS &&
        budo_layer_buffers[layer] && budo_layer_capacity > 0u) {
        memset(budo_layer_buffers[layer], 0, budo_layer_capacity);
        budo_layer_dirty[layer] = 1;
        return 0;
    }

    if (layer == 0) {
        if (budo_send_command("\x1b]777;pixel=clear\a") != 0) {
            perror("budo_graphics_clear_pixels: printf");
            return -1;
        }
    } else {
        if (budo_send_command("\x1b]777;pixel=clear;pixel_layer=%d\a", layer) != 0) {
            perror("budo_graphics_clear_pixels: printf");
            return -1;
        }
    }

    return 0;
}

int budo_graphics_clear_rect(int x, int y, int width, int height, int layer) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        fprintf(stderr, "budo_graphics_clear_rect: invalid rectangle\n");
        return -1;
    }
    if (layer < 1 || layer > 16) {
        fprintf(stderr, "budo_graphics_clear_rect: layer must be 1-16\n");
        return -1;
    }

    if (layer >= 1 && layer <= BUDO_MAX_LAYERS &&
        budo_layer_buffers[layer] &&
        budo_layer_width > 0 &&
        budo_layer_height > 0) {
        int end_x = x + width;
        int end_y = y + height;
        if (end_x > budo_layer_width) {
            end_x = budo_layer_width;
        }
        if (end_y > budo_layer_height) {
            end_y = budo_layer_height;
        }
        if (x < end_x && y < end_y) {
            size_t row_bytes = (size_t)(end_x - x) * 4u;
            for (int py = y; py < end_y; py++) {
                size_t offset = ((size_t)py * (size_t)budo_layer_width + (size_t)x) * 4u;
                if (offset + row_bytes <= budo_layer_capacity) {
                    memset(budo_layer_buffers[layer] + offset, 0, row_bytes);
                }
            }
            budo_layer_dirty[layer] = 1;
        }
        return 0;
    }

    if (budo_send_command("\x1b]777;sprite=clear;sprite_x=%d;sprite_y=%d;sprite_w=%d;sprite_h=%d;sprite_layer=%d\a",
                          x,
                          y,
                          width,
                          height,
                          layer) != 0) {
        perror("budo_graphics_clear_rect: printf");
        return -1;
    }

    return 0;
}

int budo_graphics_clear_screen(int width, int height, int layer) {
    return budo_graphics_clear_rect(0, 0, width, height, layer);
}

int budo_graphics_draw_sprite_rgba(int x,
                                   int y,
                                   int width,
                                   int height,
                                   const uint8_t *rgba,
                                   int layer) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || !rgba) {
        fprintf(stderr, "budo_graphics_draw_sprite_rgba: invalid sprite parameters\n");
        return -1;
    }
    if (layer < 1 || layer > 16) {
        fprintf(stderr, "budo_graphics_draw_sprite_rgba: layer must be 1-16\n");
        return -1;
    }

    if (layer >= 1 && layer <= BUDO_MAX_LAYERS &&
        budo_layer_buffers[layer] &&
        budo_layer_width > 0 &&
        budo_layer_height > 0) {
        int end_x = x + width;
        int end_y = y + height;
        int copy_start_x = x;
        int copy_start_y = y;
        if (copy_start_x < 0) {
            copy_start_x = 0;
        }
        if (copy_start_y < 0) {
            copy_start_y = 0;
        }
        if (end_x > budo_layer_width) {
            end_x = budo_layer_width;
        }
        if (end_y > budo_layer_height) {
            end_y = budo_layer_height;
        }
        if (copy_start_x < end_x && copy_start_y < end_y) {
            int src_offset_x = copy_start_x - x;
            int src_offset_y = copy_start_y - y;
            for (int py = copy_start_y; py < end_y; py++) {
                size_t dst_offset = ((size_t)py * (size_t)budo_layer_width + (size_t)copy_start_x) * 4u;
                size_t src_offset = ((size_t)(py - copy_start_y + src_offset_y) * (size_t)width +
                                     (size_t)src_offset_x) * 4u;
                size_t row_bytes = (size_t)(end_x - copy_start_x) * 4u;
                if (dst_offset + row_bytes <= budo_layer_capacity) {
                    memcpy(budo_layer_buffers[layer] + dst_offset, rgba + src_offset, row_bytes);
                }
            }
            budo_layer_dirty[layer] = 1;
            return 0;
        }
        return 0;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (height_sz > SIZE_MAX / width_sz) {
        fprintf(stderr, "budo_graphics_draw_sprite_rgba: sprite dimensions overflow\n");
        return -1;
    }

    size_t pixel_count = width_sz * height_sz;
    if (pixel_count > SIZE_MAX / 4u) {
        fprintf(stderr, "budo_graphics_draw_sprite_rgba: sprite too large\n");
        return -1;
    }

    size_t raw_size = pixel_count * 4u;
    size_t encoded_size = budo_base64_encoded_size(raw_size);
    char *encoded = malloc(encoded_size + 1u);
    if (!encoded) {
        fprintf(stderr, "budo_graphics_draw_sprite_rgba: failed to allocate sprite buffer\n");
        return -1;
    }

    if (budo_base64_encode(rgba, raw_size, encoded, encoded_size + 1u) != 0) {
        free(encoded);
        fprintf(stderr, "budo_graphics_draw_sprite_rgba: failed to encode sprite\n");
        return -1;
    }

    int status = budo_send_command("\x1b]777;sprite=draw;sprite_x=%d;sprite_y=%d;sprite_w=%d;sprite_h=%d;sprite_layer=%d;sprite_data=%s\a",
                                   x,
                                   y,
                                   width,
                                   height,
                                   layer,
                                   encoded);
    free(encoded);
    if (status != 0) {
        perror("budo_graphics_draw_sprite_rgba: printf");
        return -1;
    }

    return 0;
}

int budo_graphics_draw_frame_rgba(int width, int height, const uint8_t *rgba) {
    if (width <= 0 || height <= 0 || !rgba) {
        fprintf(stderr, "budo_graphics_draw_frame_rgba: invalid frame parameters\n");
        return -1;
    }

    size_t width_sz = (size_t)width;
    size_t height_sz = (size_t)height;
    if (height_sz > SIZE_MAX / width_sz) {
        fprintf(stderr, "budo_graphics_draw_frame_rgba: frame dimensions overflow\n");
        return -1;
    }

    size_t pixel_count = width_sz * height_sz;
    if (pixel_count > SIZE_MAX / 4u) {
        fprintf(stderr, "budo_graphics_draw_frame_rgba: frame too large\n");
        return -1;
    }

    size_t raw_size = pixel_count * 4u;
    size_t encoded_size = budo_base64_encoded_size(raw_size);
    char *encoded = malloc(encoded_size + 1u);
    if (!encoded) {
        fprintf(stderr, "budo_graphics_draw_frame_rgba: failed to allocate frame buffer\n");
        return -1;
    }

    if (budo_base64_encode(rgba, raw_size, encoded, encoded_size + 1u) != 0) {
        free(encoded);
        fprintf(stderr, "budo_graphics_draw_frame_rgba: failed to encode frame\n");
        return -1;
    }

    int status = budo_send_command("\x1b]777;frame=draw;frame_w=%d;frame_h=%d;frame_data=%s\a",
                                   width,
                                   height,
                                   encoded);
    free(encoded);
    if (status != 0) {
        perror("budo_graphics_draw_frame_rgba: printf");
        return -1;
    }

    return 0;
}

int budo_graphics_draw_text(int x, int y, const char *text, int color, int layer) {
    if (x < 0 || y < 0 || !text || text[0] == '\0') {
        fprintf(stderr, "budo_graphics_draw_text: invalid text parameters\n");
        return -1;
    }
    if (layer < 1 || layer > 16) {
        fprintf(stderr, "budo_graphics_draw_text: layer must be 1-16\n");
        return -1;
    }
    if (color < 1 || color > 18) {
        fprintf(stderr, "budo_graphics_draw_text: color must be 1-18\n");
        return -1;
    }

    size_t text_len = strlen(text);
    size_t encoded_size = budo_base64_encoded_size(text_len);
    char *encoded = malloc(encoded_size + 1u);
    if (!encoded) {
        fprintf(stderr, "budo_graphics_draw_text: failed to allocate text buffer\n");
        return -1;
    }

    if (budo_base64_encode((const uint8_t *)text, text_len, encoded, encoded_size + 1u) != 0) {
        free(encoded);
        fprintf(stderr, "budo_graphics_draw_text: failed to encode text\n");
        return -1;
    }

    int status = budo_send_command("\x1b]777;text=draw;text_x=%d;text_y=%d;text_layer=%d;text_color=%d;text_data=%s\a",
                                   x,
                                   y,
                                   layer,
                                   color,
                                   encoded);
    free(encoded);
    if (status != 0) {
        perror("budo_graphics_draw_text: printf");
        return -1;
    }

    return 0;
}

int budo_graphics_render_layer(int layer) {
    if (layer < 0 || layer > 16) {
        fprintf(stderr, "budo_graphics_render_layer: layer must be 0-16\n");
        return -1;
    }

    if (budo_layer_width > 0 &&
        budo_layer_height > 0 &&
        budo_composite_buffer) {
        int any_dirty = 0;
        if (layer == 0) {
            for (int idx = 1; idx <= BUDO_MAX_LAYERS; idx++) {
                if (budo_layer_dirty[idx]) {
                    any_dirty = 1;
                    break;
                }
            }
        } else if (layer >= 1 && layer <= BUDO_MAX_LAYERS) {
            any_dirty = budo_layer_dirty[layer];
        }

        if (any_dirty) {
            size_t pixel_count = (size_t)budo_layer_width * (size_t)budo_layer_height;
            size_t byte_count = pixel_count * 4u;
            if (byte_count <= budo_composite_capacity) {
                memset(budo_composite_buffer, 0, byte_count);
                for (int idx = 1; idx <= BUDO_MAX_LAYERS; idx++) {
                    uint8_t *layer_buffer = budo_layer_buffers[idx];
                    if (!layer_buffer) {
                        continue;
                    }
                    for (size_t offset = 0u; offset < byte_count; offset += 4u) {
                        uint8_t alpha = layer_buffer[offset + 3u];
                        if (alpha == 0u) {
                            continue;
                        }
                        budo_composite_buffer[offset + 0u] = layer_buffer[offset + 0u];
                        budo_composite_buffer[offset + 1u] = layer_buffer[offset + 1u];
                        budo_composite_buffer[offset + 2u] = layer_buffer[offset + 2u];
                        budo_composite_buffer[offset + 3u] = alpha;
                    }
                }
                for (int idx = 1; idx <= BUDO_MAX_LAYERS; idx++) {
                    budo_layer_dirty[idx] = 0;
                }
                return budo_graphics_draw_frame_rgba(budo_layer_width,
                                                     budo_layer_height,
                                                     budo_composite_buffer);
            }
        }
    }

    if (budo_layer_width > 0 &&
        budo_layer_height > 0 &&
        budo_composite_buffer) {
        return 0;
    }

    if (layer == 0) {
        if (budo_send_command("\x1b]777;pixel=render\a") != 0) {
            perror("budo_graphics_render_layer: printf");
            return -1;
        }
    } else {
        if (budo_send_command("\x1b]777;pixel=render;pixel_layer=%d\a", layer) != 0) {
            perror("budo_graphics_render_layer: printf");
            return -1;
        }
    }

    return 0;
}
