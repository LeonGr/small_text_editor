/*** includes ***/

// feature test macros
// https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/*** defines ***/

#define VERSION "0.0.1"
#define TAB_SIZE 4

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
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH,
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** data ***/

/*
 * Struct for storing information for highlighting a particular filetype
 */
struct editorSyntax {
    char *filetype;
    // Keywords in the filename to detect filetype
    char **filematch;
    // Keywords of the current filetype (two types: add | after a keyword to set type2)
    char **keywords;
    // String that starts a single line comment
    char *single_line_comment_start;
    // Flags that determine what to highlight
    int flags;
};

typedef struct erow {
    int size;
    char *chars;
    int renderSize;
    char *render;
    unsigned char *highlight;
} erow;

/*
 * Struct for storing information about the editor
 */
struct editorConfig {
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

    // Store the currernt highlight information
    struct editorSyntax *syntax;

    // Original terminal state
    struct termios orig_termios;
};

struct editorConfig E;

/*** filetypes ***/

/* C/C++ highlighting */

// C/C++ file extensions
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
    "break", "case", "class", "continue", "else", "enum", "for", "if", "return", "static", "struct", "switch", "typedef", "union", "while",
    "char|", "double|", "float|", "int|", "long|", "signed|", "unsigned|", "void|", NULL
};

/*
 * highlight database
 */
struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define NUM_HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, int inputPos, void (*callback)(char *, int));

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
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1 || read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
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

/*** syntax highlighting ***/

bool isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];\"", c) != NULL;
}

/*
 * Calculate syntax highlighting for the given `row`
 */
void editorUpdateSyntax(erow *row) {
    // Set correct highlighting array size
    row->highlight = realloc(row->highlight, row->renderSize);
    // Fill array with default highlight
    memset(row->highlight, HL_NORMAL, row->renderSize);

    if (E.syntax == NULL) return;

    char **keywords = E.syntax->keywords;

    char *single_line_comment_start = E.syntax->single_line_comment_start;
    int comment_start_len = single_line_comment_start ? strlen(single_line_comment_start) : 0;

    bool previous_is_separator = true;
    char string_delimiter = '\0';

    int i = 0;
    while (i < row->size) {
        char c = row->render[i];
        unsigned char previous_highlight = (i > 0) ? row->highlight[i - 1] : HL_NORMAL;

        if (comment_start_len && !string_delimiter) {
            // Check if the current character position contains the comment start string
            if (!strncmp(&row->render[i], single_line_comment_start, comment_start_len)) {
                // Set the highlight of the rest of the line to comment
                memset(&row->highlight[i], HL_COMMENT, row->renderSize - i);
                // Don't apply highlighting to the rest of the row
                break;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            // Check if we're in a string
            if (string_delimiter) {
                row->highlight[i] = HL_STRING;

                // An escape character started with '\' means that we should not stop the string highlighting
                // (we make sure there is an extra character after the backslash)
                if (c == '\\' && i + 1 < row->renderSize) {
                    row->highlight[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }

                // Reset string_delimiter if we reached the end delimeter
                if (c == string_delimiter) {
                    string_delimiter = '\0';
                }

                i++;
                previous_is_separator = true;
                continue;
            } else {
                // Check if this is the start of a string
                if (c == '"' || c == '\'') {
                    string_delimiter = c;
                    row->highlight[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (previous_is_separator || previous_highlight == HL_NUMBER)) ||
                (c == '.' && previous_highlight == HL_NUMBER)) {
                row->highlight[i] = HL_NUMBER;
                i++;
                previous_is_separator = false;
                continue;
            }
        }

        if(previous_is_separator) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int keyword_len = strlen(keywords[j]);
                int is_type2 = keywords[j][keyword_len - 1] == '|';

                if (is_type2) {
                    keyword_len--;
                }

                // Check if the current character is the start of a keyword...
                if (!strncmp(&row->render[i], keywords[j], keyword_len) &&
                // ...and is followed by a separator (so it's not the start of a different word)
                    isSeparator(row->render[i + keyword_len])) {
                    // Set the highlight for the keyword
                    memset(&row->highlight[i], is_type2 ? HL_KEYWORD2 : HL_KEYWORD1, keyword_len);
                    i += keyword_len;
                    break;
                }
            }

            if (keywords[j] != NULL) {
                previous_is_separator = false;
                continue;
            }
        }

        previous_is_separator = isSeparator(c);
        i++;
    }
}

/*
 * Convert `editorHighlight` constant `hl` to ANSI escape code number
 * See: https://ss64.com/nt/syntax-ansi.html
 */
int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
            return 36; // Cyan
        case HL_KEYWORD1:
            return 33; // Yellow
        case HL_KEYWORD2:
            return 32; // Green
        case HL_STRING:
            return 35; // Magenta
        case HL_NUMBER:
            return 31; // Red
        case HL_MATCH:
            return 7; // Inverted
        default:
            return 37; // White
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    // strrchr returns a pointerr to the last occurrence of the '.' in the filename, NULL if none
    char *extension = strrchr(E.filename, '.');

    // Loop through supported highlights
    for (unsigned int i = 0; i < NUM_HLDB_ENTRIES; i++) {
        struct editorSyntax *s = &HLDB[i];

        unsigned int j = 0;
        // Loop through filematch keywords
        while (s->filematch[j]) {
            // Check if filematch keyword is an extension
            int is_extension = (s->filematch[j][0] == '.');

            // Check if file extension matches or the filename matches
            if ((is_extension && extension && !strcmp(extension, s->filematch[j])) ||
                (!is_extension && strstr(E.filename, s->filematch[j]))) {
                E.syntax = s;

                // Rehighlight file
                for (int filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }

                return;
            }

            j++;
        }
    }
}

/*** row operations ***/

/*
 * Convert cursor x (`cx`) to rendered x position based on the characters in `row`
 */
int editorRowCxtoRx(erow *row, int cx) {
    int rx = 0;
    for (int i = 0; i < cx; i++) {
        if (row->chars[i] == '\t') {
            rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
        } else if (iscntrl(row->chars[i])) {
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
        if (row->chars[cx] == '\t') {
            cur_rx += (TAB_SIZE - 1) - (cur_rx % TAB_SIZE);
        } else if (iscntrl(row->chars[cx])) {
            cur_rx++;
        }

        cur_rx++;

        if (cur_rx > rx) {
            return cx;
        }
    }

    return cx;
}

/*
 * Determine what characters to render based on the characters of `row`
 */
void editorUpdateRow(erow *row) {
    // Count the tabs in the row
    int tabs = 0;
    int ctrl_chars = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            tabs++;
        }
        else if (iscntrl(row->chars[i])) {
            ctrl_chars++;
        }
    }

    // allocate extra space for our row with the tabs replaced by spaces
    free(row->render);
    row->render = malloc(row->size + tabs * (TAB_SIZE - 1) + ctrl_chars + 1);

    // Replace characters in row
    int index = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            do {
                row->render[index++] = ' ';
            } while (index % TAB_SIZE != 0);
        }
        else if (iscntrl(row->chars[i])) {
            row->render[index++] = '^';
            row->render[index++] = row->chars[i];
        }
        else {
            row->render[index++] = row->chars[i];
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

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].renderSize = 0;
    E.row[at].render = NULL;
    E.row[at].highlight = NULL;
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

    E.numrows--;
    E.dirty = true;
}

// void editorRowInsertString(erow *row, int at, char *s, size_t len) {
    // // allow inserting at end of line
    // if (at < 0 || at > row->size) {
        // at = row->size;
    // }

    // // Increase size of row by length of string to insert
    // row->chars = realloc(row->chars, row->size + len + 1);

    // // move characters after 'at' 'len' spots to make space for the character
    // memmove(&row->chars[at + len], &row->chars[at], row->size - at + len);

    // // // Insert string 's'
    // memcpy(&row->chars[at], s, len);
    // row->size += len;

    // editorUpdateRow(row);
// }

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

/*** file i/o ***/

/*
 * Converts the editor's rows to a string.
 * Stores the length of the string in `bufferLength`
 */
char *editorRowsToString(int *bufferLength) {
    int totalLength = 0;
    for (int i = 0; i < E.numrows; i++) {
        totalLength += E.row[i].size + 1;
    }

    // Create buffer to hold string
    char *buf = malloc(totalLength);

    // Copy each row to the string
    char *p = buf;
    for (int i = 0; i < E.numrows; i++) {
        memcpy(p, E.row[i].chars, E.row[i].size);
        p += E.row[i].size;
        // Add a newline after each row
        *p = '\n';
        p++;
    }

    *bufferLength = totalLength;
    return buf;
}

/*
 * Read the content of `filename` into the editor
 */
void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(filename , "r");
    if (!fp) {
        die("fopen failed");
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        if (linelen != -1) {
            // Do not include newline characters in line length
            while (linelen > 0 && (line[linelen - 1] == '\n' ||
                        line[linelen - 1] == '\r')) {
                linelen--;
            }

            // Append row of size linelen
            editorInsertRow(E.numrows, line, linelen);
        }
    }

    free(line);
    fclose(fp);
    E.dirty = false;
}

/*
 * Save the editor content to the opened file.
 * If no filename is set, prompt the user for one.
 */
void editorSave() {
    // Prompt user for filename if there is none yet
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (press ESC to cancel)", 10, NULL);

        if (E.filename == NULL) {
            editorSetStatusMessage("Save cancelled");
            return;
        }

        editorSelectSyntaxHighlight();
    }

    // Get editor text and its length
    int len;
    char *buf = editorRowsToString(&len);

    // Open (or create) the file
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        // Set file length to len
        if (ftruncate(fd, len) != -1) {
            // Write buf to file
            if (write(fd, buf, len) != -1) {
                close(fd);
                free(buf);
                E.dirty = false;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    editorSetStatusMessage("Couldn't save! I/O error: %s", strerror(errno));
}

/*** find/search ***/

/*
 * Method called after user types in the find prompt.
 * Takes the current `query` and pressed `key` as parameters.
 */
void editorFindCallback(char *query, int key) {
    // Save the search status between calls
    static int last_match = -1;
    static int direction = 1;

    static int saved_highlight_line;
    static char *saved_highlight = NULL;

    // If there is a saved highlight, set if to the saved highlight line.
    // This is done to remove the search result highlight from previous matches.
    if (saved_highlight) {
        memcpy(E.row[saved_highlight_line].highlight, saved_highlight, E.row[saved_highlight_line].renderSize);
        free(saved_highlight);
        saved_highlight = NULL;
    }

    // Return on escape
    if (key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == RIGHT || key == DOWN) {
        direction = 1;
    } else if (key == LEFT || key == UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) {
        direction = 1;
    }
    int current = last_match;

    for (int i = 0; i < E.numrows; i++) {
        current += direction;

        // Wrap around
        if (current == -1) {
            current = E.numrows - 1;
        } else if (current == E.numrows) {
            current = 0;
        }

        erow *row = &E.row[current];
        char *match = strstr(row->render, query);

        if (match) {
            last_match = current;
            int pos = match - row->render;

            // Scroll to the match, the match will appear at the top of the screen
            E.row_offset = current;

            // Place cursor at match on carriage return
            if (key == '\r') {
                E.cy = current;
                E.cx = pos;

                // reset saved match and direction
                last_match = -1;
                direction = 1;
            }

            saved_highlight_line = current;
            saved_highlight = malloc(row->size);
            mempcpy(saved_highlight, row->highlight, row->renderSize);
            memset(&row->highlight[pos], HL_MATCH, strlen(query));
            break;
        }
    }
}

/*
 * Search for query in opened file, search executed after each keypress.
 * Pressing return will keep put the cursor at the match.
 * Pressing escape will return the cursor to the position before the search.
 */
void editorFind() {
    int savedCx = E.cx;
    int savedCy = E.cy;
    int savedColumnOffset = E.col_offset;
    int savedRowOffset = E.row_offset;

    char *query = editorPrompt("Search: %s (ESC = cancel, Arrows = next/prev, Enter = select)", 9, editorFindCallback);

    if (query) {
        free(query);
    } else {
        E.cx = savedCx;
        E.cy = savedCy;
        E.col_offset = savedColumnOffset;
        E.row_offset = savedRowOffset;
    }
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

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
                // Set color of control characters and preceding '^'
                if (iscntrl(c[i]) || (i + 1 < len && iscntrl(c[i+1]))) {
                    char symbol = (c[i] == '^') ?  '^' : (c[i] <= 26) ? '@' + c[i] : '?';
                    // Set color to bright grey
                    abAppend(ab, "\x1b[90m", 5);
                    // Invert color
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &symbol, 1);
                    // Reset color
                    abAppend(ab, "\x1b[m", 4);
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
                // Set special text color
                else {
                    int color = editorSyntaxToColor(highlight[i]);

                    // Only insert color escape code when current color is the current color
                    if (color != current_color) {
                        current_color = color;
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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.row_offset) + 1,
                                              (E.rx - E.col_offset) + 1);
    abAppend(&ab, buf, strlen(buf));

    // show cursor after refeshing the screen
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
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

/*** input ***/

/*
 * Display message `prompt` in the status bar, then prompt the user for input.
 * Draws the cursor at the given `inputPos` in the statusbar.
 * Returns a pointer to the user's input
 */
char *editorPrompt(char *prompt, int inputPos, void (*callback)(char *, int)) {
    int savedCy = E.cy;
    int savedRx = E.rx;
    E.prompt = true;

    size_t bufferSize = 128;
    char *buf = malloc(bufferSize);

    size_t bufferLength = 0;
    buf[0] = '\0';

    while (true) {
        // Draw cursor in prompt
        E.cy = E.screencols - 1;
        E.rx = bufferLength + inputPos;

        // Continuously show prompt with current user input
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();

        // Allow user to delete characters in prompt
        if (c == DELETE || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (bufferLength != 0) {
                buf[--bufferLength] = '\0';
            }
        }
        // Return NULL if the user presses escape
        else if (c == '\x1b') {
            // Reset cursor position
            E.cy = savedCy;
            E.rx = savedRx;
            E.prompt = false;

            editorSetStatusMessage("");

            if (callback) {
                callback(buf, c);
            }

            free(buf);

            return NULL;
        }
        // Return input on carriage return
        else if (c == '\r') {
            if (bufferLength != 0) {
                // Reset cursor position
                E.cy = savedCy;
                E.rx = savedRx;
                E.prompt = false;

                editorSetStatusMessage("");

                if (callback) {
                    callback(buf, c);
                }

                return buf;
            }
        }
        // Add character to buffer if it's an ASCII char
        else if (!iscntrl(c) && c < 128) {
            // Double buffer size when we reach the limit
            if (bufferLength == bufferSize - 1) {
                bufferSize *= 2;
                buf = realloc(buf, bufferSize);
            }

            // Store character in output buffer
            buf[bufferLength] = c;
            // Add NUL after input
            bufferLength++;
            buf[bufferLength] = '\0';
        }

        if (callback) {
            callback(buf, c);
        }
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

/*
 * Read input and decide what to do with it
 */
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case '\r':
            editorInsertNewline();
            break;

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

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-s = save, Ctrl-d = quit, Ctrl-f = search");

    while (true) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
