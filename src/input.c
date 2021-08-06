#include "editor.h"
#include "input.h"
#include "io.h"
#include "render.h"
#include "search.h"
#include "terminal.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern struct editorConfig E;

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

    // Read escape sequence
    if (c == '\x1b') {
        char seq[5];

        int x = read(STDIN_FILENO, &seq[0], 1);
        int y = read(STDIN_FILENO, &seq[1], 1);
        if (x != 1 || y != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                int z = read(STDIN_FILENO, &seq[2], 1);
                if (z != 1) {
                    return '\x1b';
                }

                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME;
                        case '3': return DELETE;
                        case '4': return END;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME;
                        case '8': return END;
                    }
                } else if (seq[2] == ';') {
                    int a = read(STDIN_FILENO, &seq[3], 1);
                    int b = read(STDIN_FILENO, &seq[4], 1);
                    if (a != 1 || b != 1) {
                        return '\x1b';
                    }

                    // Ctrl-Left
                    else if (seq[3] == '5' && seq[4] == 'D') {
                        return C_LEFT;
                    }

                    else if (seq[3] == '5' && seq[4] == 'C') {
                        return C_RIGHT;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return UP;
                    case 'B': return DOWN;
                    case 'C': return RIGHT;
                    case 'D': return LEFT;
                    case 'H': return HOME;
                    case 'F': return END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME;
                case 'F': return END;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}
/*
 * Move cursor based on pressed `key`.
 * When moving up and down the cursor will attempt to stay at the same column.
 * The cursor will also stick to the end of line when moving up and down.
 */
void editorMoveCursor(int key) {
    // Get current row using cursor position, NULL if on extra row at the end
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowLen = row ? row->size : 0;

    switch (key) {
        case LEFT:
            // Only scroll left if we have not reached column 0
            if (E.cx != 0) {
                E.cx--;
                E.savedCx = E.cx;
            }
            // If we scroll left at position 0, move to the end of the previous line
            else if (E.cy > 0) {
                E.cy--;
                E.cx = rowLen;
                E.savedCx = -1;
            }
            break;
        case DOWN:
            // Only scroll down if we have not reached the number of rows
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
        case UP:
            // Only scroll up if we have not reached line 0
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case RIGHT:
            // Only scroll to the right if we have not reached the end of the current row
            if (row && E.cx < rowLen) {
                E.cx++;
                E.savedCx = E.cx;
            }
            // If we scroll right at the end of line, move to the next line
            // (but not further than the number of rows)
            else if (E.cx == rowLen && E.cy < E.numrows) {
                E.cy++;
                E.cx = 0;
                E.savedCx = E.cx;
            }
            break;
    }

    // Get new row
    erow *newRow = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    // Get new row length
    int newRowLen = newRow ? newRow->size : 0;

    // If we're scrolling up/down at the end of the line, keep the cursor at the end of the line
    if (E.savedCx == -1) {
        E.cx = newRowLen;
    }
    // If we scrolled up/down from a longer line, but not the end, and then went to a shorter line
    // we keep cursor at the position we reached before in the longer line
    else {
        E.cx = E.savedCx;
    }

    // If we scrolled up/down from a longer line, snap cursor to end of current line
    if (E.cx > newRowLen) {
        if (E.cx == rowLen) {
            E.savedCx = -1;
        } else {
            E.savedCx = E.cx;
        }
        E.cx = newRowLen;
    }
}

void editorJumpWord(int direction) {
    // Get current row using cursor position, NULL if on extra row at the end
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowLen = row ? row->size : 0;

    switch (direction) {
        // Jump to next separator on C-Left
        case C_LEFT:
            {
                if (E.cx == 0 && E.cy > 0) {
                    editorMoveCursor(LEFT);
                    return;
                }

                char beforeCursor[E.cx];
                memcpy(beforeCursor, row->chars, E.cx);
                beforeCursor[E.cx] = '\0';

                // find first separator character between column 0 and the current column
                int last_separator_index = -1;
                for (int i = E.cx - 1; i >= 0; i--) {
                    char c = beforeCursor[i];

                    if (isSeparator(c)) {
                        last_separator_index = i;
                        break;
                    }
                }

                int newPos;
                if (last_separator_index == -1) {
                    newPos = 0;
                } else if (E.cx - 1 == last_separator_index) {
                    // If we are next to a separator, delete until the next separator instead
                    newPos = last_separator_index;
                    // find first separator character between column 0 and the adjacent separator
                    int last_separator_index = -1;
                    for (int i = newPos - 1; i >= 0; i--) {
                        char c = beforeCursor[i];

                        if (isSeparator(c)) {
                            last_separator_index = i;
                            break;
                        }
                    }
                    newPos = last_separator_index + 1;
                } else {
                    newPos = last_separator_index + 1;
                }

                E.cx = newPos;

                // Reset saved position
                E.savedCx = E.cx;
            }
            break;
        case C_RIGHT:
            {
                if (E.cx == rowLen && E.cy < E.numrows) {
                    editorMoveCursor(RIGHT);
                    return;
                }

                // find first separator character between column rowLen and the current column
                int first_separator_index = rowLen + 1;
                for (int i = E.cx + 1; i < rowLen; i++) {
                    char c = row->chars[i];

                    if (isSeparator(c)) {
                        first_separator_index = i + 1;
                        break;
                    }
                }

                int newPos;
                if (first_separator_index == rowLen + 1) {
                    newPos = rowLen;
                } else if (E.cx + 1 == first_separator_index) {
                    // If we are next to a separator, delete until the next separator instead
                    newPos = first_separator_index;
                    // find first separator character between column rowLen and the adjacent separator
                    int first_separator_index = rowLen + 1;
                    for (int i = newPos + 1; i < rowLen; i++) {
                        char c = row->chars[i];

                        if (isSeparator(c)) {
                            first_separator_index = i + 1;
                            break;
                        }
                    }
                    newPos = first_separator_index;
                } else {
                    newPos = first_separator_index;
                }

                E.cx = newPos;

                // Reset saved position
                E.savedCx = E.cx;
            }
            break;
    }
}

/*
 * Read input and decide what to do with it
 */
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case '\r':
            editorInsertNewline();
            break;

        case CTRL_KEY('r'):
            editorRefreshScreen();
            break;

#ifdef TABSPACE
        // Tabs are spaces >:)
        case '\t':
            for (int i = 0; i < TAB_SIZE; i++) {
                editorInsertChar(' ');
            }
            break;
#endif

        // Quit on C-d
        case CTRL_KEY('d'):
            if (E.dirty && !E.forceQuit) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                        "Press Ctrl-d again to quit.");
                E.forceQuit = true;
                return;
            }
            clearScreen();
            exit(0);
            break;

        // Save on C-s
        case CTRL_KEY('s'):
            editorSave();
            break;

        // Find/search on C-f
        case CTRL_KEY('f'):
            editorFind();
            break;

        // Move to start of line
        case CTRL_KEY('a'):
        case HOME:
            E.cx = 0;
            E.savedCx = E.cx;
            break;
        // Move to end of line
        case CTRL_KEY('e'):
        case END:
            {
                //E.cx = E.screencols - 1;
                erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
                int rowLen = row ? row->size : 0;
                E.cx = rowLen;
                E.savedCx = E.cx;
            }
            break;

        // Removing characters
        case BACKSPACE:
        case CTRL_KEY('h'):
            editorDeleteChar();
            break;
        case DELETE:
            // Simulate delete by first moving cursor to the right
            editorMoveCursor(RIGHT);
            editorDeleteChar();
            break;

        case CTRL_KEY('u'):
            editorClearRowToStart();
            break;

        case CTRL_KEY('w'):
            editorDeleteWord();
            break;

        // Move to top or bottom of screen with PAGE_UP, PAGE_DOWN
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? UP : DOWN);
                }
            }
            break;

        // Move 1 position with arrows
        case LEFT:
        case DOWN:
        case UP:
        case RIGHT:
            editorMoveCursor(c);
            break;

        case C_LEFT:
        case C_RIGHT:
            editorJumpWord(c);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            editorInsertChar(c);
            break;
    }

    // Reset forceQuit when any other key is pressed, also reset message bar
    if (E.forceQuit) {
        E.forceQuit = false;
        editorSetStatusMessage("");
    }
}
