#ifndef LIBIMAGE_H
#define LIBIMAGE_H

typedef enum {
    LIBIMAGE_SUCCESS = 0,
    LIBIMAGE_UNSUPPORTED_FORMAT,
    LIBIMAGE_IO_ERROR,
    LIBIMAGE_INVALID_ARGUMENT,
    LIBIMAGE_DATA_ERROR,
    LIBIMAGE_OUT_OF_MEMORY
} LibImageResult;

LibImageResult libimage_render_file_at(const char *path, int origin_x, int origin_y);
const char *libimage_last_error(void);

#endif /* LIBIMAGE_H */
