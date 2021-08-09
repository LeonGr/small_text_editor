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
#include <ncurses.h>

extern struct editorConfig E;

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

                E.cx = getSeparatorIndex(LEFT);

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

               E.cx = getSeparatorIndex(RIGHT);

                // Reset saved position
                E.savedCx = E.cx;
            }
            break;
    }
}

void editorHandleMouseEvent(MEVENT event) {
    // Scroll up
    if (event.bstate & BUTTON4_PRESSED) {
        // printw("Button4\\n");
        if (E.row_offset > 0) {
            E.row_offset -= 1;

            if (E.cy - E.row_offset == E.screenrows) {
                // E.cy--;
                editorMoveCursor(UP);
            }
        }
    }
    // Scroll down
    else if (event.bstate & BUTTON5_PRESSED) {
        // printw("Button5\\n");
        if (E.row_offset < E.numrows) {
            E.row_offset += 1;

            if (E.cy - E.row_offset + 1 == 0) {
                // E.cy++;
                editorMoveCursor(DOWN);
            }
        }
    }
    // Click
    else {
        // printw("x: %d, y: %d, z: %d\\\\n", event.x, event.y, event.z);
        E.cy = event.y + E.row_offset;
        E.cx = editorRowRxtoCx(&E.row[E.cy], event.x - E.line_nr_len);

        E.savedCx = E.cx;
    }
}

/*
 * Continuously attempt to read and return input
 */
int editorReadKey() {
    int ch;
    ch = getch();

    if (ch == KEY_MOUSE) {
        MEVENT event;

        if (getmouse(&event) == OK) {
            editorHandleMouseEvent(event);
        }

        return '\\x1b';
    }

    switch (ch) {
        case KEY_HOME: return HOME;
        case KEY_DC: return DELETE;
        case KEY_END: return END;
        case KEY_PPAGE: return PAGE_UP;
        case KEY_NPAGE: return PAGE_DOWN;
        case KEY_UP: return UP;
        case KEY_DOWN: return DOWN;
        case KEY_LEFT: return LEFT;
        case KEY_RIGHT: return RIGHT;
    }

    if (ch == '\\x1b') {
        char seq[5];

        int x = getch();
        int y = getch();
        seq[0] = x;
        seq[1] = y;
        if (x == ERR || y == ERR) {
            return '\\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                int z = getch();
                seq[2] = z;
                if (z == ERR) {
                    return '\\x1b';
                }

                if (seq[2] == ';') {
                    int a = getch();
                    int b = getch();
                    seq[3] = a;
                    seq[4] = b;
                    if (a == ERR || b == ERR) {
                        return '\\x1b';
                    }

                    // Ctrl-Left
                    else if (seq[3] == '5' && seq[4] == 'D') {
                        return C_LEFT;
                    }

                    // Ctrl-Right
                    else if (seq[3] == '5' && seq[4] == 'C') {
                        return C_RIGHT;
                    }
                }
            }
        }
    }

    if (ch != ERR) {
        return ch;
    } else {
        return '\\x1b';
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
