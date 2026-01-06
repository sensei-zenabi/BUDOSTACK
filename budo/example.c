#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t base64_encoded_size(size_t raw_size) {
    return ((raw_size + 2u) / 3u) * 4u;
}

static char base64_encode_table(int idx) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (idx < 0 || idx >= (int)(sizeof(table) - 1u)) {
        return '=';
    }
    return table[idx];
}

static int encode_base64(const uint8_t *data, size_t size, char *out, size_t out_size) {
    if (!out) {
        return -1;
    }
    size_t required = base64_encoded_size(size);
    if (out_size < required + 1u) {
        return -1;
    }

    size_t out_idx = 0u;
    size_t in_idx = 0u;
    while (in_idx + 3u <= size) {
        uint32_t block = ((uint32_t)data[in_idx] << 16u) |
            ((uint32_t)data[in_idx + 1u] << 8u) |
            (uint32_t)data[in_idx + 2u];
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)(block & 0x3Fu));
        in_idx += 3u;
    }

    size_t remaining = size - in_idx;
    if (remaining == 1u) {
        uint32_t block = (uint32_t)data[in_idx] << 16u;
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = '=';
        out[out_idx++] = '=';
    } else if (remaining == 2u) {
        uint32_t block = ((uint32_t)data[in_idx] << 16u) |
            ((uint32_t)data[in_idx + 1u] << 8u);
        out[out_idx++] = base64_encode_table((int)((block >> 18) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 12) & 0x3Fu));
        out[out_idx++] = base64_encode_table((int)((block >> 6) & 0x3Fu));
        out[out_idx++] = '=';
    }

    out[out_idx] = '\0';
    return 0;
}

static void fill_rect_rgba(uint8_t *pixels,
                           int frame_w,
                           int frame_h,
                           int rect_x,
                           int rect_y,
                           int rect_w,
                           int rect_h,
                           uint8_t r,
                           uint8_t g,
                           uint8_t b) {
    if (!pixels || frame_w <= 0 || frame_h <= 0) {
        return;
    }
    if (rect_x < 0 || rect_y < 0 || rect_w <= 0 || rect_h <= 0) {
        return;
    }
    if (rect_x >= frame_w || rect_y >= frame_h) {
        return;
    }

    int max_x = rect_x + rect_w;
    int max_y = rect_y + rect_h;
    if (max_x > frame_w) {
        max_x = frame_w;
    }
    if (max_y > frame_h) {
        max_y = frame_h;
    }

    for (int y = rect_y; y < max_y; y++) {
        for (int x = rect_x; x < max_x; x++) {
            size_t offset = ((size_t)y * (size_t)frame_w + (size_t)x) * 4u;
            pixels[offset] = r;
            pixels[offset + 1u] = g;
            pixels[offset + 2u] = b;
            pixels[offset + 3u] = 255u;
        }
    }
}

int main(int argc, char **argv) {
    int frame_w = 800;
    int frame_h = 600;
    if (argc == 3) {
        char *endptr = NULL;
        long parsed_w = strtol(argv[1], &endptr, 10);
        if (endptr && *endptr == '\0' && parsed_w > 0 && parsed_w <= INT_MAX) {
            frame_w = (int)parsed_w;
        }
        endptr = NULL;
        long parsed_h = strtol(argv[2], &endptr, 10);
        if (endptr && *endptr == '\0' && parsed_h > 0 && parsed_h <= INT_MAX) {
            frame_h = (int)parsed_h;
        }
    }

    size_t pixel_count = (size_t)frame_w * (size_t)frame_h;
    if (pixel_count == 0u || pixel_count > SIZE_MAX / 4u) {
        fprintf(stderr, "Invalid frame dimensions.\n");
        return 1;
    }

    size_t raw_size = pixel_count * 4u;
    uint8_t *pixels = calloc(raw_size, 1u);
    if (!pixels) {
        perror("calloc");
        return 1;
    }

    fill_rect_rgba(pixels, frame_w, frame_h, 200, 150, 400, 100, 255, 255, 255);

    size_t encoded_size = base64_encoded_size(raw_size);
    char *encoded = calloc(encoded_size + 1u, 1u);
    if (!encoded) {
        perror("calloc");
        free(pixels);
        return 1;
    }

    if (encode_base64(pixels, raw_size, encoded, encoded_size + 1u) != 0) {
        fprintf(stderr, "Failed to encode frame data.\n");
        free(encoded);
        free(pixels);
        return 1;
    }

    printf("\x1b]777;frame=draw;frame_x=0;frame_y=0;frame_w=%d;frame_h=%d;frame_data=%s\x07",
           frame_w, frame_h, encoded);
    fflush(stdout);

    free(encoded);
    free(pixels);
    return 0;
}
