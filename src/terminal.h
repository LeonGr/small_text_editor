#ifndef TERMINAL_H
#define TERMINAL_H

/*
 * Clears screen and puts cursor topleft
 */
void clearScreen();

/*
 * Error handling: clears screen, prints error `s` and exits
 */
void die(const char *s);

/*
 * Disable raw mode by setting back saved termios struct
 */
void disableRawMode();

/*
 * Enable raw mode by (un)setting a number of flags
 */
void enableRawMode();

/*
 * Get the cursor position, store it in `rows` and `cols`
 */
int getCursorPosition(int *rows, int *cols);

/*
 * Use ioctl to get the size of the terminal window, store it in `rows` and `cols`
 */
int getWindowSize(int *rows, int *cols);

#endif
