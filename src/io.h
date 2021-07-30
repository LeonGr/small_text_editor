#ifndef FILENAME_H
#define FILENAME_H

/*
 * Converts the editor's rows to a string.
 * Stores the length of the string in `bufferLength`
 */
char *editorRowsToString(int *bufferLength);

/*
 * Read the content of `filename` into the editor
 */
void editorOpen(char *filename);

/*
 * Save the editor content to the opened file.
 * If no filename is set, prompt the user for one.
 */
void editorSave();

#endif
