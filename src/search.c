#include "editor.h"
#include "highlight.h"
#include "input.h"
#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct editorConfig E;

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

    // If there is a saved highlight, set it to the saved highlight line.
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
    } else if (key == '\r') {
        direction = 0;
    } else if (key == DOWN) {
        direction = 1;
    } else if (key == UP) {
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
            memcpy(saved_highlight, row->highlight, row->renderSize);
            // mempcpy(saved_highlight, row->highlight, row->renderSize);
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

    char *query = editorPrompt("Search: %s (ESC = cancel, Arrow up/down = next/prev, Enter = select)", 9, editorFindCallback);

    if (query) {
        free(query);
    } else {
        E.cx = savedCx;
        E.cy = savedCy;
        E.col_offset = savedColumnOffset;
        E.row_offset = savedRowOffset;
    }
}

