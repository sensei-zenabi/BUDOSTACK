#ifndef BUDO_INPUT_H
#define BUDO_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUDO_KEY_NONE = 0,
    BUDO_KEY_UP,
    BUDO_KEY_DOWN,
    BUDO_KEY_LEFT,
    BUDO_KEY_RIGHT,
    BUDO_KEY_ENTER,
    BUDO_KEY_SPACE,
    BUDO_KEY_QUIT
} budo_key_t;

int budo_input_init(void);
void budo_input_shutdown(void);
int budo_input_poll(budo_key_t *out_key);

#ifdef __cplusplus
}
#endif

#endif
