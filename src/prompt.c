#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "editor.h"
#include "render.h"
#include "highlight.h"
#include "input.h"

extern struct editorConfig E;

/*
 * Display message `prompt` in the status bar, then prompt the user for input.
 * Draws the cursor at the given `inputPos` in the statusbar.
 * Returns a pointer to the user's input
 */
char *editorPrompt(char *prompt, int inputPos, void (*callback)(char *, int)) {
    int savedCy = E.cy;
    int savedRx = E.rx;
    E.prompt = true;

    int bufferSize = 128;
    char *buf = malloc(bufferSize);

    int bufferLength = 0;
    buf[0] = '\0';

    int promptIndex = 0;

    while (true) {
        // Draw cursor in prompt
        E.cy = E.screenrows + 2;
        E.rx = promptIndex + inputPos + 1;

        // Continuously show prompt with current user input
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();

        // Return NULL if the user presses escape
        if (c == '\x1b') {
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

            memmove(&buf[promptIndex + 1], &buf[promptIndex], bufferLength - promptIndex + 1);

            // Store character in output buffer
            buf[promptIndex] = c;
            promptIndex++;
            // Add NUL after input
            bufferLength++;
            buf[bufferLength] = '\0';
        }
        // Handle control shortcuts
        else {
            switch (c) {
                // Move to start of line
                case CTRL_KEY('a'):
                    promptIndex = 0;
                    break;
                // Move to end of line
                case CTRL_KEY('e'):
                    promptIndex = bufferLength;
                    break;
                case BACKSPACE:
                case CTRL_KEY('h'):
                    if (bufferLength != 0) {
                        memmove(&buf[promptIndex], &buf[promptIndex + 1], bufferLength - promptIndex);
                        buf[--bufferLength] = '\0';
                        promptIndex--;
                    }
                    break;
                case DELETE:
                    if (promptIndex < bufferLength && bufferLength != 0) {
                        memmove(&buf[promptIndex], &buf[promptIndex + 1], bufferLength - promptIndex);
                        buf[--bufferLength] = '\0';
                    }
                    break;
                // Delete to start of prompt
                case CTRL_KEY('u'):
                    bufferLength = 0;
                    buf[bufferLength] = '\0';
                    break;
                // Delete word
                case CTRL_KEY('w'):
                    {
                        // Copy string before the cursor
                        char beforePrompt[bufferLength];
                        memcpy(beforePrompt, buf, promptIndex);
                        beforePrompt[promptIndex] = '\0';

                        int last_separator_index = -1;
                        for (int i = promptIndex - 1; i >= 0; i--) {
                            char c = beforePrompt[i];

                            if (isSeparator(c)) {
                                last_separator_index = i;
                                break;
                            }
                        }

                        if (last_separator_index == -1) {
                            buf[0] = '\0';
                            promptIndex = 0;
                            bufferLength = 0;
                        } else if (promptIndex - 1 == last_separator_index) {
                            buf[last_separator_index] = '\0';
                            promptIndex = last_separator_index;
                            bufferLength = last_separator_index;
                        } else {
                            buf[last_separator_index + 1] = '\0';
                            promptIndex = last_separator_index + 1;
                            bufferLength = last_separator_index + 1;
                        }
                    }
                    break;
                case LEFT:
                    if (promptIndex > 0) {
                        promptIndex--;
                    }
                    break;
                case RIGHT:
                    if (promptIndex < bufferLength) {
                        promptIndex++;
                    }
                    break;
            }
        }

        if (callback) {
            callback(buf, c);
        }
    }
}
