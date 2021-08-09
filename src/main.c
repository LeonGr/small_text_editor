#include "editor.h"
#include "input.h"
#include "io.h"
#include "render.h"
#include "terminal.h"
#include <stdbool.h>
#include <ncurses.h>

int main(int argc, char *argv[]) {
    // initialize ncurses
    initscr();
    // capture input immediately !(canonical/cooked mode)
    cbreak();
    // Block on getch until capture
    nodelay(stdscr, FALSE);
    noecho();
    // enable capturing of keypresses
    keypad(stdscr, TRUE);
    // Remove delay between mouse events
    mouseinterval(0);
    mmask_t old;
    // make mouse events visible,  store old mask in `old`
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, &old);

    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-s = save, Ctrl-d = quit, Ctrl-f = search");

    while (true) {
        refresh();
        editorRefreshScreen();
        editorProcessKeypress();
    }

    endwin();
    return 0;
}
