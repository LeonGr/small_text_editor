#include "editor.h"
#include "highlight.h"
#include "languages.h"
#include "main.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern struct editorConfig E;

/*
 * Convert cursor x (`cx`) to rendered x position based on the characters in `row`
 */
int editorRowCxtoRx(erow *row, int cx) {
    int rx = 0;
    for (int i = 0; i < cx; i++) {
        char c = row->chars[i];

        if (c == '\t') {
            rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
        } else if (iscntrl(c)) {
            rx++;
        }
        rx++;
    }

    return rx;
}

/*
 * Convert rendered x (`rx`) to cursor x position based on the characters in `row`
 */
int editorRowRxtoCx(erow *row, int rx) {
    int cur_rx = 0;

    int cx;
    for (cx = 0; cx < row->size; cx++) {
        char c = row->chars[cx];

        if (c == '\t') {
            cur_rx += (TAB_SIZE - 1) - (cur_rx % TAB_SIZE);
        } else if (iscntrl(c)) {
            cur_rx++;
        }

        cur_rx++;

        if (cur_rx > rx) {
            return cx;
        }
    }

    return cx;
}

/*** output ***/

/*
 * Scroll the screen if the cursor reaches an edge
 */
void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxtoRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.row_offset) {
        E.row_offset = E.cy;
    }

    if (E.cy >= E.row_offset + E.screenrows) {
        E.row_offset = E.cy - E.screenrows + 1;
    }

    if (E.rx < E.col_offset) {
        E.col_offset = E.rx;
    }

    if (E.rx >= E.col_offset + E.screencols) {
        E.col_offset = E.rx - E.screencols + 1;
    }
}

/*
 * Add editor rows to append buffer `ab`.
 * empty lines are shown as "~".
 */
void editorDrawRows(struct abuf *ab) {
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.row_offset;

        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
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
        } else {
            // Draw line numbers
            char max_line_nr[16];
            snprintf(max_line_nr, sizeof(max_line_nr), "%d", E.numrows);
            int max_line_nr_len = strlen(max_line_nr);

            char line_nr_col_width_format[16];
            snprintf(line_nr_col_width_format, sizeof(line_nr_col_width_format), "\x1b[7m%%%dd \x1b[m ", max_line_nr_len);

            char line_nr[16];
            int line_number_len = snprintf(line_nr, sizeof(line_nr), line_nr_col_width_format, y + E.row_offset);
            // subtract size of escape characters
            E.line_nr_len = line_number_len - 7;
            abAppend(ab, line_nr, line_number_len);

            // E.line_nr_len = 0;

            int len = E.row[filerow].renderSize - E.col_offset;
            if (len < 0) {
                len = 0;
            }
            if (len > E.screencols) {
                len = E.screencols;
            }

            char *c = &E.row[filerow].render[E.col_offset];
            unsigned char *highlight = &E.row[filerow].highlight[E.col_offset];

            int current_color = -1;
            for (int i = 0; i < len; i++) {
                // Set color of control characters and preceding '^' as well as non-ASCII characters
                if (iscntrl(c[i]) || (i + 1 < len && iscntrl(c[i+1])) || c[i] < 0) {
                    char symbol;
                    if (c[i] == '^') {
                        symbol = '^';
                    } else if (c[i] < 0) {
                        // draw non-ASCII characters as ?
                        symbol = '?';
                    } else {
                        symbol = (c[i] <= 26) ? '@' + c[i] : '?';
                    }
                    // Set color to bright grey
                    abAppend(ab, "\x1b[90m", 5);
                    // Invert color
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &symbol, 1);
                    // Reset color
                    abAppend(ab, "\x1b[m", 3);
                    if (current_color != -1) {
                        char buf[16];
                        int color_len = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        abAppend(ab, buf, color_len);
                    }
                }
                // Set default text color
                else if (highlight[i] == HL_NORMAL) {
                    // Only insert 'reset' escape code when current color is not default
                    if (current_color != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        abAppend(ab, "\x1b[m", 3);
                        current_color = -1;
                    }

                    abAppend(ab, &c[i], 1);
                }
                // Set search result match color
                else if (highlight[i] == HL_MATCH) {
                    // Only insert invert escape code when current color is not inverted
                    if (current_color != HL_MATCH) {
                        current_color = HL_MATCH;
                        abAppend(ab, "\x1b[34m", 5);
                        abAppend(ab, "\x1b[7m", 4);
                    }

                    abAppend(ab, &c[i], 1);
                }
                // Set special text color
                else {
                    int color = editorSyntaxToColor(highlight[i]);

                    // Only insert color escape code when current color is the current color
                    if (color != current_color) {
                        current_color = color;
                        abAppend(ab, "\x1b[m", 3);
                        char buf[16];
                        int colorLength = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, colorLength);
                    }

                    abAppend(ab, &c[i], 1);
                }
            }

            // reset color at end of line
            abAppend(ab, "\x1b[39m", 5);
            abAppend(ab, "\x1b[m", 3);
        }

        // Erase in line escape sequence
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

/*
 * Add statusbar (with inverted colors) to append buffer `ab`
 */
void editorDrawStatusBar(struct abuf *ab) {
    // Invert colors (graphic rendition mode 7)
    abAppend(ab, "\x1b[7m", 4);

    char status[80], statusRight[80];

    int len = snprintf(status, sizeof(status), " %.20s - %d lines %s",
            E.filename ? E.filename : "[No filename]", E.numrows, E.dirty ? "(modified)" : "");

    char *filetype = E.syntax ? E.syntax->filetype : "no ft";
    int currentLine = E.cy + 1;
    int totalLines = E.numrows;
    int lenRight = snprintf(statusRight, sizeof(statusRight), "%s | %d/%d ", filetype, currentLine, totalLines);

    if (len > E.screencols) {
        len = E.screencols;
    }

    abAppend(ab, status, len);

    while (len < E.screencols) {
        if (E.screencols - len == lenRight) {
            abAppend(ab, statusRight, lenRight);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    // Reset graphic rendition
    abAppend(ab, "\x1b[m", 3);
}

/*
 * Add message bar to append buffer `ab`
 */
void editorDrawMessageBar(struct abuf *ab) {
    // Clear line (space is needed for some reason, otherwise line not cleared)
    abAppend(ab, " \x1b[K", 5);

    int messageLen = strlen(E.statusMessage);
    if (messageLen > E.screencols) {
        messageLen = E.screencols;
    }

    if (messageLen && time(NULL) - E.statusMessage_time < 5) {
        abAppend(ab, E.statusMessage, messageLen);
    }
}

/*
 * Clear the screen and draw updated content.

 * (see https://vt100.net/docs/vt100-ug/chapter3.html0 for VT100 escape sequences)
 */
void editorRefreshScreen() {
    // Do not scroll the editor when the user is using a prompt
    if (!E.prompt) {
        editorScroll();
    }

    struct abuf ab = ABUF_INIT;

    // Hide cursor before refeshing the screen
    abAppend(&ab, "\x1b[?25l", 6);
    // write 3 byte escape sequence to position the cursor in the top left
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // Draw cursor in correct position
    char buf[32];
    if (!E.prompt) {
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.row_offset) + 1,
                                                  (E.rx - E.col_offset) + 1 + E.line_nr_len);
    } else {
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy,
                                                  E.rx);
    }
    abAppend(&ab, buf, strlen(buf));

    // show cursor after refeshing the screen
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}
