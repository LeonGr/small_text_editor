#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "highlight.h"
#include "languages.h"

extern struct editorConfig E;

/*** syntax highlighting ***/

/*
 * Returns `true` is character `c` is considered a separator of words
 */
bool isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:\"", c) != NULL;
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

    char *scs = E.syntax->single_line_comment_start;
    char *mcs = E.syntax->multi_line_comment_start;
    char *mce = E.syntax->multi_line_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    bool previous_is_separator = true;
    char in_string_delimiter = '\0';
    bool in_comment = row->index > 0 && E.row[row->index - 1].open_comment;

    int i = 0;
    while (i < row->renderSize) {
        char c = row->render[i];
        unsigned char previous_highlight = (i > 0) ? row->highlight[i - 1] : HL_NORMAL;

        // Single line comment
        if (scs_len && !in_string_delimiter && !in_comment) {
            // Check if the current character position contains the comment start string
            if (!strncmp(&row->render[i], scs, scs_len)) {
                // Set the highlight of the rest of the line to comment
                memset(&row->highlight[i], HL_COMMENT, row->renderSize - i);
                // Don't apply highlighting to the rest of the row
                break;
            }
        }

        // Multi line comment
        if (mcs_len && mcs_len && !in_string_delimiter) {
            if (in_comment) {
                row->highlight[i] = HL_MLCOMMENT;

                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->highlight[i], HL_COMMENT, mce_len);
                    i += mce_len;
                    in_comment = false;
                    previous_is_separator = true;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->highlight[i], HL_COMMENT, mcs_len);
                i += mcs_len;
                in_comment = true;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            // Check if we're in a string
            if (in_string_delimiter) {
                row->highlight[i] = HL_STRING;

                // An escape character started with '\' means that we should not stop the string highlighting
                // (we make sure there is an extra character after the backslash)
                if (c == '\\' && i + 1 < row->renderSize) {
                    row->highlight[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }

                // Reset in_string_delimiter if we reached the end delimeter
                if (c == in_string_delimiter) {
                    in_string_delimiter = '\0';
                }

                i++;
                previous_is_separator = true;
                continue;
            } else {
                // Check if this is the start of a string
                if (c == '"' || c == '\'') {
                    in_string_delimiter = c;
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

        if (previous_is_separator) {
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

    bool changed = row->open_comment != in_comment;
    row->open_comment = in_comment;
    // If the comment status changed, update syntax color
    if (changed && row->index + 1 < E.numrows) {
        editorUpdateSyntax(&E.row[row->index + 1]);
    }
}

/*
 * Convert `editorHighlight` constant `hl` to ANSI escape code number
 * See: https://ss64.com/nt/syntax-ansi.html
 */
int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT:
            return 36; // Cyan
        case HL_KEYWORD1:
            return 33; // Yellow
        case HL_KEYWORD2:
            return 32; // Green
        case HL_STRING:
            return 35; // Magenta
        case HL_NUMBER:
            return 31; // Red
        default:
            return 37; // White
    }
}

/*
 * Check if the current filetype is supported and highlight it
 */
void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    // strrchr returns a pointerr to the last occurrence of the '.' in the filename, NULL if none
    char *extension = strrchr(E.filename, '.');

    unsigned entries = num_hldb_entries();

    // Loop through supported highlights
    for (unsigned int i = 0; i < entries; i++) {
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
