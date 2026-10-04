#ifndef BUDOSTACK_TERMINAL_INPUT_H
#define BUDOSTACK_TERMINAL_INPUT_H

enum budostack_key {
    BUDOSTACK_KEY_EOF = -1,
    BUDOSTACK_KEY_NONE = 0,
    BUDOSTACK_KEY_UP = 256,
    BUDOSTACK_KEY_DOWN,
    BUDOSTACK_KEY_RIGHT,
    BUDOSTACK_KEY_LEFT
};

int budostack_terminal_input_start(void);
void budostack_terminal_input_stop(void);
/* Negative timeout waits for a key; zero polls. Escape sequences are retained. */
int budostack_terminal_read_key(int timeout_ms);
int budostack_terminal_interrupted(void);

#endif
