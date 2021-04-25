/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>

/*** defines ***/

#define VERSION "0.0.1"

/* Get Ctrl key code by setting the upper 3 bits to 0 */
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    //LEFT = 'h',
    //DOWN = 'j',
    //UP = 'k',
    //RIGHT = 'l'
    LEFT = 1000,
    DOWN,
    UP,
    RIGHT
};

/*** data ***/

/*
 * Struct for storing information about the editor
 */
struct editorConfig {
    // Cursor x position
    int cx;
    // Cursor y position
    int cy;
    // terminal number of rows
    int screenrows;
    // terminal number of columns
    int screencols;

    // Original terminal state
    struct termios orig_termios;
};

struct editorConfig E;


/*** terminal ***/

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
 * Error handling: clears screen, prints error and exits
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
 * Continuously attempt to read and return input
 */
int editorReadKey() {
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1 || read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return UP;
                case 'B': return DOWN;
                case 'C': return RIGHT;
                case 'D': return LEFT;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

/*
 * Get the cursor position
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
 * Use ioctl to get the size of the terminal window
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

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/*
 * Append to buffer
 */
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) {
        return;
    }

    memcpy(&new [ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

/*
 * Free buffer
 */
void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

/*
 * Draw column of tiles on left side of screen
 */
void editorDrawRows(struct abuf *ab) {
    for (int y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            // Draw welcome message
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "\x1b[4mLeon's editor -- version %s\x1b[m", VERSION);
            if (welcomelen > E.screencols) {
                welcomelen = E.screencols;
            }

            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }

            while (padding--) {
                abAppend(ab, " ", 1);
            }

            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        // Erase in line escape sequence
        abAppend(ab, "\x1b[K", 3);

        // Only draw newline when we're not at the last line (to prevent scroll)
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

/*
 * Clear the screen
 * (see https://vt100.net/docs/vt100-ug/chapter3.html0 for VT100 escape sequences)
 */
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    // Hide cursor before refeshing the screen
    abAppend(&ab, "\x1b[?25l", 6);
    // write 3 byte escape sequence to position the cursor in the top left
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    // Draw cursor in correct position
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // show cursor after refeshing the screen
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
    switch (key) {
        case LEFT:
            if (E.cx != 0) E.cx--;
            break;
        case DOWN:
            if (E.cy != E.screenrows - 1) E.cy++;
            break;
        case UP:
            if (E.cy != 0) E.cy--;
            break;
        case RIGHT:
            if (E.cx != E.screencols - 1) E.cx++;
            break;
    }
}

/*
 * Read input and decide what to do with it
 */
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('x'):
            clearScreen();
            exit(0);
            break;
        case LEFT:
        case DOWN:
        case UP:
        case RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

/*
 * Initialize editor
 */
void initEditor() {
    // Set cursor position
    E.cx = 0;
    E.cy = 0;

    // Get window size
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
