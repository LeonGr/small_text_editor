#include <stdbool.h>
#include "terminal.h"
#include "editor.h"
#include "render.h"
#include "io.h"
#include "input.h"

// feature test macros
// https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
// #define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

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
