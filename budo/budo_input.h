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

typedef struct {
    int quit_requested;
    int key_up;
    int key_down;
    int key_left;
    int key_right;
    int key_space;
    int mouse_x;
    int mouse_y;
    int mouse_buttons;
} budo_input_state_t;

int budo_input_init(void);
void budo_input_shutdown(void);
int budo_input_poll(budo_key_t *out_key);
int budo_input_sdl_init(void);
void budo_input_sdl_shutdown(void);
int budo_input_sdl_poll(budo_input_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
