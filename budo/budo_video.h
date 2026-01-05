#ifndef BUDO_VIDEO_H
#define BUDO_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUDO_VIDEO_LOW,
    BUDO_VIDEO_HIGH
} budo_video_mode_t;

int budo_video_init(budo_video_mode_t mode, const char *title, int scale);
void budo_video_shutdown(void);
void budo_video_clear(uint32_t color);
void budo_video_put_pixel(int x, int y, uint32_t color);
void budo_video_draw_pixels(int x, int y, const uint32_t *pixels, int width, int height, int pitch);
void budo_video_present(void);
int budo_video_get_size(int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif
