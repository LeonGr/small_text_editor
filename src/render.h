#include "editor.h"

#ifndef RENDER_H
#define RENDER_H

/*
 * Convert cursor x (`cx`) to rendered x position based on the characters in `row`
 */
int editorRowCxtoRx(erow *row, int cx);

/*
 * Convert rendered x (`rx`) to cursor x position based on the characters in `row`
 */
int editorRowRxtoCx(erow *row, int rx);

/*
 * Scroll the screen if the cursor reaches an edge
 */
void editorScroll();

/*
 * Determine what characters to render based on the characters in each row
 */
void editorCalculateRenderedRows();

/*
 * Add editor rows to append buffer `ab`.
 * empty lines are shown as "~".
 */
void editorDrawRows(struct abuf *ab);

/*
 * Add statusbar (with inverted colors) to append buffer `ab`
 */
void editorDrawStatusBar(struct abuf *ab);

/*
 * Add message bar to append buffer `ab`
 */
void editorDrawMessageBar(struct abuf *ab);

/*
 * Clear the screen and draw updated content.

 * (see https://vt100.net/docs/vt100-ug/chapter3.html0 for VT100 escape sequences)
 */
void editorRefreshScreen();

#endif
