#ifndef FILENAME_H
#define FILENAME_H

/*
 * Display message `prompt` in the status bar, then prompt the user for input.
 * Draws the cursor at the given `inputPos` in the statusbar.
 * Returns a pointer to the user's input
 */
char *editorPrompt(char *prompt, int inputPos, void (*callback)(char *, int));

#endif
