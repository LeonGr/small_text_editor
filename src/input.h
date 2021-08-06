#ifndef INPUT_H
#define INPUT_H

/* Get Ctrl key code by setting the upper 3 bits to 0 */
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    //LEFT = 'h',
    //DOWN = 'j',
    //UP = 'k',
    //RIGHT = 'l'
    BACKSPACE = 127,
    LEFT = 1000,
    DOWN,
    UP,
    RIGHT,
    DELETE,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN,
    C_LEFT,
    C_RIGHT,
};

/*
 * Read input and decide what to do with it
 */
void editorProcessKeypress();

/*
 * Continuously attempt to read and return input
 */
int editorReadKey();

#endif
