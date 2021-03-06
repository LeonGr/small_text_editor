#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>

struct termios orig_termios;

void die(const char *s) {
    perror(s);
    exit(1);
}

/*
 * Disable raw mode by setting back saved termios struct
 */
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/*
 * Enable raw mode by (un)setting a number of flags
 */
void enableRawMode() {
    // Store current terminal attributes in raw
    tcgetattr(STDIN_FILENO, &orig_termios);

    atexit(disableRawMode);

    struct termios raw = orig_termios;

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
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    while (1) {
        char c = '\0';
        read(STDIN_FILENO, &c, 1);
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }

        if (c == 'q') break;
    }

    return 0;
}
