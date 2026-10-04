#define _POSIX_C_SOURCE 200809L
#include "../lib/budo_gfx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(int argc, char **argv) {
    (void)argc;
    struct budo_gfx *screen = NULL;
    assert(budo_gfx_open(&screen, 320, 240, BUDO_GFX_ARGB8888) == 0);
    uint32_t pixels[320 * 240];
    for (size_t i = 0; i < 320u * 240u; ++i) pixels[i] = 0xffcc3311u;
    assert(budo_gfx_present(screen, pixels, NULL) == 0);
    FILE *marker = fopen(getenv("BUDOWIN_LAUNCH_MARKER"), "a");
    assert(marker); fprintf(marker, "%s\n", argv[0]); assert(fclose(marker) == 0);
    budo_gfx_close(screen);
    return 0;
}
