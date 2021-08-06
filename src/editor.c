#include "input.h"
#include "editor.h"
#include "highlight.h"
#include "terminal.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <tree_sitter/api.h>

struct editorConfig E;

uint32_t rowColPointToBytePoint(int row, int column) {
    uint32_t byte = column;

    for (int r = 0; r < row; r++) {
        erow *row = &E.row[r];
        // Size of the row + a newline
        byte += row->size + 1;
    }

    return byte;
}

/*
 * Returns `true` is character `c` is considered a separator of words
 */
bool isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:\"", c) != NULL;
}

int getSeparatorIndex(int direction) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    if (row == NULL) {
        return 0;
    }

    switch (direction) {
        case LEFT:
            {
                erow *row = &E.row[E.cy];

                // find first separator character between column 0 and the current column
                int last_separator_index = -1;
                for (int i = E.cx - 1; i >= 0; i--) {
                    char c = row->chars[i];

                    if (isSeparator(c)) {
                        last_separator_index = i;
                        break;
                    }
                }

                if (last_separator_index == -1) {
                    return 0;
                } else if (E.cx - 1 == last_separator_index) {
                    // If we are next to a separator, delete until the next separator instead
                    int start = last_separator_index;
                    // find first separator character between column 0 and the adjacent separator
                    int last_separator_index = -1;
                    for (int i = start - 1; i >= 0; i--) {
                        char c = row->chars[i];

                        if (isSeparator(c)) {
                            last_separator_index = i;
                            break;
                        }
                    }
                    return last_separator_index + 1;
                } else {
                    return last_separator_index + 1;
                }
            }
            break;
        case RIGHT:
            {
                // find first separator character between column row->size and the current column
                int first_separator_index = row->size + 1;
                for (int i = E.cx + 1; i < row->size; i++) {
                    char c = row->chars[i];

                    if (isSeparator(c)) {
                        first_separator_index = i + 1;
                        break;
                    }
                }

                if (first_separator_index == row->size + 1) {
                    return row->size;
                } else if (E.cx + 1 == first_separator_index) {
                    // If we are next to a separator, delete until the next separator instead
                    int start = first_separator_index;
                    // find first separator character between column row->size and the adjacent separator
                    int first_separator_index = row->size + 1;
                    for (int i = start + 1; i < row->size; i++) {
                        char c = row->chars[i];

                        if (isSeparator(c)) {
                            first_separator_index = i + 1;
                            break;
                        }
                    }
                    return first_separator_index;
                } else {
                    return first_separator_index;
                }
            }
            break;
        default:
            return 0;
    }
}

/*
 * Append `len` characters of chars `s` to editor
 */
void editorInsertRow(int at, char *s, size_t len) {
    // Only add within editor range
    if (at < 0 || at > E.numrows) {
        return;
    }

    // Increase memory for rows by 1 spot
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    // Move all following rows to the next spot in memory
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    for (int i = at + 1; i <= E.numrows; i++) {
        E.row[i].index++;
    }

    E.row[at].index = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].renderSize = 0;
    E.row[at].render = NULL;
    E.row[at].highlight = NULL;
    E.row[at].open_comment = false;

    E.numrows++;
    E.dirty = true;
}

/*
 * Free memory of `row`
 */
void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->highlight);
}

/*
 * Delete row at line `at`
 */
void editorDeleteRow(int at) {
    // Don't delete row outside text buffer
    if (at < 0 || at >= E.numrows) {
        return;
    }

    editorFreeRow(&E.row[at]);
    // Move all rows after selected row one spot back in memory
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));

    for (int i = at; i < E.numrows - 1; i++) {
        E.row[i].index--;
    }

    E.numrows--;
    E.dirty = true;
}

/*
 * Add character `c` to `row` at given position `at`
 */
void editorRowInsertChar(erow *row, int at, char c) {
    // allow inserting at end of line
    if (at < 0 || at > row->size) {
        at = row->size;
    }

    // increase space for row.chars
    row->chars = realloc(row->chars,row->size+2);

    // move characters after 'at' one spot to make space for the character
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);

    row->size++;

    row->chars[at] = c;

    int old_end_byte = rowColPointToBytePoint(row->index, at);
    int new_end_byte = old_end_byte + 1;
    editorUpdateSyntaxHighlight(row->index, at, old_end_byte, row->index, at + 1, new_end_byte);

    E.dirty = true;
}

/*
 * Append string `s` of length `len` to row `row`
 */
void editorRowAppendString(erow *row, char *s, size_t len) {
    // Increase size of row by length of string to append
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';

    E.dirty = true;
}

/*
 * Delete char at index `at` in row `row`
 */
void editorRowDeleteChar(erow *row, int at) {
    // Only delete character actually in row
    if (at < 0 || at >= row->size) {
        return;
    }

    // Move chars after cursor one spot back
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;

    int old_end_byte = rowColPointToBytePoint(row->index, at + 1);
    int new_end_byte = old_end_byte - 1;
    editorUpdateSyntaxHighlight(row->index, at + 1, old_end_byte, row->index, at, new_end_byte);

    E.dirty = true;
}

/*
 * Delete characters in current row from cursor x to start
 */
void editorClearRowToStart() {
    if (E.cx == 0) {
        return;
    }

    erow *row = &E.row[E.cy];
    memmove(&row->chars[0], &row->chars[E.cx], row->size - E.cx);
    row->size -= E.cx;

    int old_end_byte = rowColPointToBytePoint(E.cy, E.cx);
    int new_end_byte = rowColPointToBytePoint(E.cy, 0);
    editorUpdateSyntaxHighlight(E.cy, E.cx, old_end_byte, E.cy, 0, new_end_byte);

    E.dirty = true;
    E.cx = 0;

    // Reset saved position
    E.savedCx = E.cx;
}

/*** editor operations ***/

/*
 * Insert char `c` at current cursor position
 */
void editorInsertChar(char c) {
    // If the cursor is at the last (tilde) row, add an extra line first
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }

    // Insert character in current row
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;

    // Reset saved position
    E.savedCx = E.cx;
}

/*
 * Insert newline at current cursor position
 */
void editorInsertNewline() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
    }

    int old_end_byte = rowColPointToBytePoint(E.cy, E.cx);
    int new_end_byte = rowColPointToBytePoint(E.cy + 1, 0);
    editorUpdateSyntaxHighlight(E.cy, E.cx, old_end_byte, E.cy + 1, 0, new_end_byte);

    E.cy++;
    E.cx = 0;

    // Reset saved position
    E.savedCx = E.cx;
}

/*
 * Perform backspace action
 */
void editorDeleteChar() {
    // Don't delete past end or start of file
    if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
        return;
    }

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDeleteChar(row, E.cx - 1);
        E.cx--;
    } else {
        // If backspace is pressed at the start of the line, append the current line to the previous line

        // Put cursor at end of previous line
        E.cx = E.row[E.cy - 1].size;

        int old_end_byte = rowColPointToBytePoint(E.cy, 0);

        // Join lines
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);

        // Delete old line
        editorDeleteRow(E.cy);

        int new_end_byte = rowColPointToBytePoint(E.cy - 1, E.cx);
        editorUpdateSyntaxHighlight(E.cy, 0, old_end_byte, E.cy - 1, E.cx, new_end_byte);

        E.cy--;
    }

    // Reset saved position
    E.savedCx = E.cx;
}


void editorDeleteWord() {
    erow *row = &E.row[E.cy];

    int newPos = getSeparatorIndex(LEFT);

    memmove(&row->chars[newPos], &row->chars[E.cx], row->size - E.cx);
    row->size -= E.cx - newPos;

    int old_end_byte = rowColPointToBytePoint(E.cy, E.cx);
    int new_end_byte = rowColPointToBytePoint(E.cy, newPos);
    editorUpdateSyntaxHighlight(E.cy, E.cx, old_end_byte, E.cy, newPos, new_end_byte);

    E.dirty = true;
    E.cx = newPos;

    // Reset saved position
    E.savedCx = E.cx;
}

/*** append buffer ***/

/*
 * Append `len` characters `s` to append buffer `ab`
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
 * Free append buffer `ab`
 */
void abFree(struct abuf *ab) {
    free(ab->b);
}

/*
 * Write message(s) `fmt` to message bar
 */
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusMessage, sizeof(E.statusMessage), fmt, ap);
    va_end(ap);
    E.statusMessage_time = time(NULL);
}

/*** init ***/

/*
 * Initialize editor
 */
void initEditor() {
    // Set cursor position
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;

    // Scroll position
    E.row_offset = 0;
    E.col_offset = 0;

    // Number of rows
    E.numrows = 0;
    E.row = NULL;

    E.line_nr_len = 0;

    E.dirty = false;
    E.forceQuit = false;

    E.prompt = false;

    // Saved cursor x position for pleasant scrolling, start at cx
    E.savedCx = E.cx;

    E.filename = NULL;

    E.statusMessage[0] = '\0';
    E.statusMessage_time = 0;

    E.syntax = NULL;

    // Get window size
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }

    // Leave 1 row for the status bar
    E.screenrows -= 2;
}
