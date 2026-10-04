#ifndef BUDO_RESOLUTION_H
#define BUDO_RESOLUTION_H

/* Choose one of the two supported 4:3 framebuffer modes at build time. */
#ifndef BUDO_WIDTH
#define BUDO_WIDTH 320
#endif
#if BUDO_WIDTH != 320 && BUDO_WIDTH != 640
#error "BUDO_WIDTH must be 320 or 640"
#endif
#define BUDO_HEIGHT (BUDO_WIDTH * 3 / 4)
#define BUDO_PIXEL_SCALE ((float)BUDO_WIDTH / 640.0f)

#endif
