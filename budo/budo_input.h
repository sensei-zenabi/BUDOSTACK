#ifndef BUDO_INPUT_H
#define BUDO_INPUT_H

enum budo_key {
    BUDO_KEY_NONE = 0,
    BUDO_KEY_UP,
    BUDO_KEY_DOWN,
    BUDO_KEY_LEFT,
    BUDO_KEY_RIGHT,
    BUDO_KEY_ESCAPE,
    BUDO_KEY_ENTER,
    BUDO_KEY_SPACE,
    BUDO_KEY_CHAR
};

struct budo_input_event {
    enum budo_key key;
    char ch;
};

struct budo_mouse_state {
    int x;
    int y;
    unsigned int left_clicks;
    unsigned int right_clicks;
};

int budo_input_init(void);
void budo_input_shutdown(void);
int budo_input_poll(struct budo_input_event *event);
int budo_input_query_mouse(struct budo_mouse_state *state, int timeout_ms);

#endif
