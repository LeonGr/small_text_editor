#ifndef EDITOR_H
#define EDITOR_H

#include <time.h>
#include <stdbool.h>
#include <termios.h>

#define TAB_SIZE 4

/*
 * A row in the editor
 */
typedef struct erow {
    int index;
    int size;
    char *chars;
    int renderSize;
    char *render;
    unsigned char *highlight;
    bool open_comment;
} erow;

/*
 * Struct for storing information about the editor
 */
typedef struct editorConfig {
    // Cursor x position
    int cx;
    // Cursor y position
    int cy;
    // Rendered x position (since some characters are rendered differently)
    int rx;
    // Scroll position
    int row_offset;
    int col_offset;
    // terminal number of rows
    int screenrows;
    // terminal number of columns
    int screencols;

    // Number of rows
    int numrows;
    // Pointer to rows
    erow *row;

    // Width of line number column
    int line_nr_len;

    // Set to true if text buffer has been modified since opening or saving
    bool dirty;
    bool forceQuit;

    // Set to true if the user is typing in a prompt
    bool prompt;

    // Saved cursor x position for pleasant scrolling (Vim style):
    // Keeps cursor at end of line if we scrolled from end of line before
    // Keeps cursor at old x position when a shorter line was passed in between
    int savedCx;

    char *filename;

    char statusMessage[80];
    time_t statusMessage_time;

    // Store the current highlight information
    struct editorSyntax *syntax;

    // Original terminal state
    struct termios orig_termios;
} editorConfig;

/*
 * Returns `true` if character `c` is considered a separator of words
 */
bool isSeparator(int c);

/*
 * Append `len` characters of chars `s` to editor
 */
void editorInsertRow(int at, char *s, size_t len);

/*
 * Free memory of `row`
 */
void editorFreeRow(erow *row);

/*
 * Delete row at line `at`
 */
void editorDeleteRow(int at);

/*
 * Append string `s` of length `len` to row `row`
 */
void editorRowAppendString(erow *row, char *s, size_t len);

/*
 * Delete char at index `at` in row `row`
 */
void editorRowDeleteChar(erow *row, int at);

/*
 * Delete characters in current row from cursor x to start
 */
void editorClearRowToStart();

/*
 * Insert char `c` at current cursor position
 */
void editorInsertChar(char c);

/*
 * Insert newline at current cursor position
 */
void editorInsertNewline();

/*
 * Perform backspace action
 */
void editorDeleteChar();

void editorDeleteWord();

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/*
 * Append `len` characters `s` to append buffer `ab`
 */
void abAppend(struct abuf *ab, const char *s, int len);

/*
 * Free append buffer `ab`
 */
void abFree(struct abuf *ab);

/*
 * Write message(s) `fmt` to message bar
 */
void editorSetStatusMessage(const char *fmt, ...);

/*
 * Initialize editor
 */
void initEditor();

#endif
