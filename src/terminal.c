#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "editor.h"

extern struct editorConfig E;

/*
 * Clears screen and puts cursor topleft
 */
void clearScreen() {
    // write 4 byte escape sequence to clear the screen
    write(STDERR_FILENO, "\x1b[2J", 4);
    // write 3 byte escape sequence to position the cursor in the top left
    write(STDERR_FILENO, "\x1b[H", 3);
}

/*
 * Error handling: clears screen, prints error `s` and exits
 */
void die(const char *s) {
    clearScreen();
    perror(s);
    exit(1);
}

/*
 * Disable raw mode by setting back saved termios struct
 */
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

/*
 * Enable raw mode by (un)setting a number of flags
 */
void enableRawMode() {
    // Store current terminal attributes in raw
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        die("tcgetattr");
    }

    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    /*
    Turn off flags:
    - ICRNL: turn off translation \r to \n
    - IXON: turn off <C-q>, <C-s> (stop/resume data transmission to terminal)
    - BRKINT: stop break condition sending SIGINT
    - INPCK: disable input parity checking
    - ISTRIP: stop stripping 8th bit of input byte

    - OPOST: turn off special output processing (\n -> \r\n)

    - CS8: set character size to 8 bits per byte

    - ECHO: print characters as they are typed
    - ICANON: canonical mode, only send to program once you press Return or <C-d>
    - IEXTEN: turn off special input processing (<C-v>)
    - ISIG: turn off <C-c>, <C-z>
    */
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Set minimum input bytes before read() can return
    raw.c_cc[VMIN] = 0;
    // Set maximum amount of time before read() returns (in 1/10th of a second, i.e. 100 milliseconds)
    raw.c_cc[VTIME] = 1;

    // Save new terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

/*
 * Get the cursor position, store it in `rows` and `cols`
 */
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    // Use n to get the terminal status information, 6n means cursor position
    // we write it to STDOUT
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    // Read cursor position escape sequence into buffer
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }

        if (buf[i] == 'R') {
            break;
        }

        i++;
    }

    // Terminate string
    buf[i] = '\0';

    // Check if we got an escape sequence, if so, attempt to store it in rows, cols
    if (buf[0] != '\x1b' || buf[1] != '[' || sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
}

/*
 * Use ioctl to get the size of the terminal window, store it in `rows` and `cols`
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // If TIOCGWINSZ fails, manually get size of screen by moving very far to the right (C)
        // and very far to the bottom (B)
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
            return -1;
        }
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
