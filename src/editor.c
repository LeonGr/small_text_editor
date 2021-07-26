#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <termios.h>
#include "editor.h"
#include "terminal.h"
#include "highlight.h"

extern struct editorConfig E;

/*
 * Determine what characters to render based on the characters of `row`
 */
void editorUpdateRow(erow *row) {
    // Count the tabs in the row
    int tabs = 0;
    int ctrl_chars = 0;
    for (int i = 0; i < row->size; i++) {
        char c = row->chars[i];

        if (c == '\t') {
            tabs++;
        }
        else if (iscntrl(c)) {
            ctrl_chars++;
        }
    }

    // allocate extra space for our row with the tabs replaced by spaces
    free(row->render);
    row->render = malloc(row->size + tabs * (TAB_SIZE - 1) + ctrl_chars + 1);

    // Replace characters in row
    int index = 0;
    for (int i = 0; i < row->size; i++) {
        char c = row->chars[i];

        // Render tabs as TAB_SIZE spaces
        if (c == '\t') {
            do {
                row->render[index++] = ' ';
            } while (index % TAB_SIZE != 0);
        }
        // Render control characters with a preceding '^'
        else if (iscntrl(c)) {
            row->render[index++] = '^';
            row->render[index++] = c;
        }
        else {
            row->render[index++] = c;
        }
    }

    row->render[index] = '\0';
    row->renderSize = index;

    editorUpdateSyntax(row);
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

    editorUpdateRow(&E.row[at]);

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

    editorUpdateRow(row);
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
    editorUpdateRow(row);
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
    editorUpdateRow(row);
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
    editorUpdateRow(row);
    E.dirty = true;
    E.cx = 0;
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
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
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

        // Join lines
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);

        // Delete old line
        editorDeleteRow(E.cy);

        E.cy--;
    }

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
