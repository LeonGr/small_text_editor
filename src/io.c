// feature test macros
// https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "editor.h"
#include "highlight.h"
#include "prompt.h"
#include "terminal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern struct editorConfig E;

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


