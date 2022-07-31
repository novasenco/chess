
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ncurses.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>


/** Bitboard
 *
 * CLI interface for quickly creating, using, and manipulating bitboards
 *
 * Requirements:
 *  - ncurses
 *  - unix (fork, ncurses) - only linux tested
 *
 * Optional Dependencies:
 *  - X11 for copy, paste
 *
 * Eg Compilation:
 *  - clang -Ofast -lncursesw -lX11 bitboard.c -o bitboard
 */


int main(void);
void ulltostr(unsigned long long num, char out[65]);
void drawboard(unsigned int cursorPos, int state, unsigned long long bitboard);
void diagonal(char inp, unsigned int pos, unsigned long long *bitboard);
void help(void);
void paste(Display *xdisplay, Window xroot, unsigned long long *bitboard);
pid_t yank(const char bitboardStr[65]);
void send_utf8(Display *xdisplay, XSelectionRequestEvent *sev, Atom utf8, const char msg[19]);


int main(void) {
  int input = 0; // user input from ncurses getch()
  unsigned long long bitboard = 0; // bitboard (64-bits)
  unsigned int cursorPos = 0; // cursor position (0-63)
  int state = 0; // bit field for state: 0b1: diagonal, 0b10: 
  Display *xdisplay; // X11 display
  Window xroot;      // X11 root window
  int xscreen;       // current X11 screen
  pid_t yankpid = 0; // pid of fork()'d process for yank (0 if not waiting)

  // set up ncurses screen
  setlocale(LC_ALL, ""); // support unicode
  initscr();
  cbreak(); // disable buffering
  noecho(); // don't echo input
  keypad(stdscr, 1); // allow fn keys (arrows)

  // set up X11 stuff for clipboard management
  xdisplay = XOpenDisplay(NULL);
  if (!xdisplay) {
    clear();
    printw("Cannot connect to X display. 'p' (paste) and 'y' (yank) will not work.\n");
    refresh();
    getch();
  }
  xscreen = DefaultScreen(xdisplay);
  xroot = RootWindow(xdisplay, xscreen);

  for (;;) {
    drawboard(cursorPos, state, bitboard);
    input = getch();

    if (yankpid) {
      pid_t w;
      // set to yankpid=0 if child finished
      w = waitpid(yankpid, NULL, WNOHANG);
      if (w != 0)
        yankpid = 0;
    }

    if (state & 0x1) {
      // handle [d]iagonal + [r]ising, [f]alling, [d]ouble
      diagonal(input, cursorPos, &bitboard);
      state ^= 0x1;
      continue;
    }

    if (input == 'q') {
      // break from loop on [q]uit
      break;
    }

    switch(input) {

      case KEY_LEFT:
      case 'h':
        // move cursor left if possible
        if (cursorPos % 8u > 0)
          cursorPos--;
        break;

      case KEY_DOWN:
      case 'j':
        // move cursor down if possible
        if (cursorPos < 56)
          cursorPos += 8;
        break;

      case KEY_UP:
      case 'k':
        // move cursor up if possible
        if (cursorPos > 7) cursorPos -= 8;
        break;

      case KEY_RIGHT:
      case 'l':
        // move cursor right if possible
        if (cursorPos % 8u < 7) cursorPos++;
        break;

      case ' ':
      case '\n':
        // flip bit under cursor
        bitboard ^= 1ull << cursorPos;
        break;

      case 'c':
        // clear entire bitboard
        bitboard = 0ull;
        break;

      case 'F':
        // flip each bit on bitboard
        for (unsigned int p = 0; p < 64; p++)
          bitboard ^= 1ull << p;
        break;

      case 'r':
        // flip each bit in rank (row) under cursor
        for (unsigned int p = (cursorPos / 8u) * 8, f = p + 8; p < f; p++)
          bitboard ^= 1ull << p;
        break;

      case 'f':
        // flip each bit in file (column) under cursor
        for (unsigned int p = cursorPos % 8u; p < 64; p += 8)
          bitboard ^= 1ull << p;
        break;

      case 'd':
        // set diagonal state (next key: [r]ising / [f]alling / [d]ouble)
        state = 1;
        break;

      case 'n':
        // flip bits a knight can move to from cursor
        if (cursorPos / 8u > 0 && cursorPos % 8u > 1) bitboard ^= 1ull << (cursorPos - 10);
        if (cursorPos / 8u > 0 && cursorPos % 8u < 6) bitboard ^= 1ull << (cursorPos - 6);
        if (cursorPos / 8u > 1 && cursorPos % 8u > 0) bitboard ^= 1ull << (cursorPos - 17);
        if (cursorPos / 8u > 1 && cursorPos % 8u < 7) bitboard ^= 1ull << (cursorPos - 15);
        if (cursorPos / 8u < 6 && cursorPos % 8u > 0) bitboard ^= 1ull << (cursorPos + 15);
        if (cursorPos / 8u < 6 && cursorPos % 8u < 7) bitboard ^= 1ull << (cursorPos + 17);
        if (cursorPos / 8u < 7 && cursorPos % 8u > 1) bitboard ^= 1ull << (cursorPos + 6);
        if (cursorPos / 8u < 7 && cursorPos % 8u < 6) bitboard ^= 1ull << (cursorPos + 10);
        break;

      case 'p':
        // paste bitboard from X11 clipboad
        // valid forms: /\d+/, /0x\x+/, /0b[01]+/
        input = -1;
        if (xdisplay)
          paste(xdisplay, xroot, &bitboard);
        break;

      case 'y':
        // yank bitboard to X11 clipboard and handle its ownership
        if (xdisplay) {
          char bitboardStr[67];
          if (state & 2) { // & 0b10
            *bitboardStr = '0';
            *(bitboardStr+1) = 'b';
            ulltostr(bitboard, bitboardStr + 2);
          } else if (state & 4) { // 0b100
            snprintf(bitboardStr, 20, "%llu", bitboard);
          } else {
            snprintf(bitboardStr, 19, "0x%016llx", bitboard);
          }
          yankpid = yank(bitboardStr);
        }
        break;

      case 'i':
        {
          int base;
          char bitboardStr[20];
          char *endStr = bitboardStr + 19;
          printw("bitboard: ");
          refresh();
          echo();
          getnstr(bitboardStr, 19);
          noecho();
          base = 10;
          if (*bitboardStr == '0') {
            switch (*(bitboardStr+1)) {
              case 'x':
                base = 16;
                break;
              case 'b':
                base = 2;
                break;
            }
          }
          bitboard = strtoull(base == 2 ? bitboardStr+2 : bitboardStr, &endStr, base);
        }
        break;

      case 'I':
        {
          char bitboardStr[65];
          char *endStr = bitboardStr + 64;
          printw("bitboard: 0b");
          refresh();
          echo();
          getnstr(bitboardStr, 64);
          noecho();
          bitboard = strtoull(bitboardStr, &endStr, 2);
        }
        break;

      case 'o':
        if (state & 2)      // & 0b10
          state ^= 6;       // ^= 0b110
        else if (state & 4) // & 0b100
          state ^= 4;       // ^= 0b100
        else
          state ^= 2;       // ^= 0b10
        break;

      case 'H':
        help();
        break;
    }
  }
  endwin();
  return 0;
}


void ulltostr(unsigned long long num, char out[65]) {
  unsigned long long i;
  for (i = 1ull << 63; i > 0ull; i >>= 1) {
    *out++ = num & i ? '1' : '0';
  }
  *out = 0;
}


void drawboard(unsigned int cursorPos, int state, unsigned long long bitboard) {
  clear();

  // print top file letters and wall
  printw("   A B C D E F G H    \n");
  printw("  ┌────────────────┐  \n");

  // print each row
  for (unsigned int row = 0; row < 8; row++) {

    // left rank number and wall
    printw("%c │", '8'-row);

    // print each bit at (row,col) in the row
    for (unsigned int col = 0; col < 8; col++) {
      unsigned int p = 8*row + col; // position (r,c)

      // reverse background and foreground for cursor
      if (p == cursorPos)
        attron(A_REVERSE);

      // print '1' for on bit, '.' for off bit
      addch(bitboard&(1ull<<p) ? '1' : '.');

      // turn of reverse if enabled for cursor
      if (p == cursorPos)
        attroff(A_REVERSE);

      addch(' ');
    } // endfor: col

    // right wall and rank number
    printw("│ %c\n", '8'-row);

  } // endfor: row

  // print wall and bottom file letters
  printw("  └────────────────┘  \n");
  printw("   A B C D E F G H    \n");

  // print bitboard in hex
  addch('\n');
  if (state & 2) { // & 0b10
    char bin[65];
    ulltostr(bitboard, bin);
    printw("0b%s\n", bin);
  } else if (state & 4) { // 0b100
    printw("%-19llu\n", bitboard);
  } else {
    printw("0x%016llx\n", bitboard);
  }

  // print additional info pertatining to state at bottom
  if (state & 1) {
    printw("\ntoggle diagonal ");
    attron(A_BOLD|A_UNDERLINE); addch('r'); attroff(A_BOLD|A_UNDERLINE);
    printw("ising / ");
    attron(A_BOLD|A_UNDERLINE); addch('f'); attroff(A_BOLD|A_UNDERLINE);
    printw("alling / ");
    attron(A_BOLD|A_UNDERLINE); addch('d'); attroff(A_BOLD|A_UNDERLINE);
    printw("ouble (both)\n");
  } else {
    printw("\ntype  H  for help\n");
  }

  // render
  refresh();
}


void diagonal(char inp, unsigned int pos, unsigned long long *bitboard) {
  unsigned int r = pos / 8u,
               c = pos % 8u;
  if (inp == 'r' || inp == 'd') {
    // rising  diagonal
    int rn = c + r - 7; // magic rising diagonal number
    for (unsigned int p = (rn>0?8:1)*rn+7; p < 64; p += 7) {
      *bitboard ^= 1ull << p;
      if (p % 8 == 0) break;
    }
  }
  if (inp == 'f' || inp == 'd') {
    // falling diagonal
    int fn = c - r; // magic falling diagonal number
    for (unsigned int p = (fn<0?-8:1)*fn; p < 64; p += 9) {
      *bitboard ^= 1ull << p;
      if (p % 8 == 7) break;
    }
  }
}


void help(void) {
  clear();
  printw("Help:\n");
  printw("  H                 -> show this help menu\n");
  printw("  h / <left>        -> move cursor left\n");
  printw("  l / <right>       -> move cursor right\n");
  printw("  j / <down>        -> move cursor down\n");
  printw("  k / <up>          -> move cursor up\n");
  printw("  <space> / <enter> -> flip bit at cursor\n");
  printw("  c                 -> clear bitboard\n");
  printw("  F                 -> flip all bits on board\n");
  printw("  r                 -> flip all bits in the rank under cursor\n");
  printw("  f                 -> flip all bits in the file under cursor\n");
  printw("  dr                -> flip rising diagonal (bottom-left to top-right: /)\n");
  printw("  df                -> flip falling diagonal (top-left to bottom-right: \\)\n");
  printw("  dd                -> flip both diagonals\n");
  printw("  n                 -> flip all bits a knight can move to\n");
  printw("  y                 -> yank bitboard to X11 clipboard as hex string\n");
  printw("  p                 -> paste from X11 clipboard (hex, binary, or decimal)\n");
  printw("  i                 -> input bitboard in hex (0x prefix) or decimal\n");
  printw("  I                 -> input bitboard in binary\n");
  printw("  o                 -> cycle through output style (hex, binary, decimal\n");
  printw("                    -> (this affects 'y' (yank)\n");
  printw("\nPress a key to continue...\n");
  refresh();
  getch();
}


void paste(Display *dpy, Window xroot, unsigned long long *bitboard) {
  XEvent ev;
  XSelectionEvent sev;
  Atom sel, utf8, targProp;
  Window targWin;

  sel = XInternAtom(dpy, "CLIPBOARD", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);
  Window curOwner;
  curOwner = XGetSelectionOwner(dpy, sel);
  if (curOwner == None)
    return;
  targWin = XCreateSimpleWindow(dpy, xroot, -10, -10, 1, 1, 0, 0, 0);
  targProp = XInternAtom(dpy, "CHESSBITBOARD", False);
  XConvertSelection(dpy, sel, utf8, targProp, targWin, CurrentTime);
  for (;;) {
    XNextEvent(dpy, &ev);
    if (ev.type == SelectionNotify) {
      sev = ev.xselection;
      if (sev.property == None) {
        clear();
        printw("'p' (paste) failed: cannot connect to X display.\n");
        refresh();
        getch();
      } else {
        Atom da, incr, type;
        int di;
        unsigned long size, dul;
        unsigned char *propRet = NULL;
        char *endPropRet;
        int base = 10;
        XGetWindowProperty(dpy, targWin, targProp, 0, 0, False,
            AnyPropertyType, &type, &di, &dul, &size, &propRet);
        XFree(propRet);
        incr = XInternAtom(dpy, "INCR", False);
        if (type == incr) {
          clear();
          printw("'p' (paste) failed: clipboard too large; INCR not implemented.\n");
          refresh();
          getch();
        }
        XGetWindowProperty(dpy, targWin, targProp, 0, size, False,
            AnyPropertyType, &da, &di, &dul, &dul, &propRet);
        endPropRet = strchr((char*)propRet, 0);
        if (*propRet == '0') {
          switch (*(propRet+1)) {
            case 'x':
              base = 16;
              break;
            case 'b':
              base = 2;
              break;
          }
        }
        *bitboard = strtoull((char*)propRet, &endPropRet, base);
        XFree(propRet);
        XDeleteProperty(dpy, targWin, targProp);
        return;
      }
    }
  }
}


pid_t yank(const char bitboardStr[67]) {
  Display *xdisplay;
  int xscreen;
  Window xroot, xowner;
  Atom sel, utf8;
  XEvent ev;
  XSelectionRequestEvent *sev;
  pid_t pid;

  // fork, return child pid in parent process
  pid = fork();
  if (pid != 0)
    return pid;

  xdisplay = XOpenDisplay(NULL);
  if (!xdisplay)
    return 0;
  xscreen = DefaultScreen(xdisplay);
  xroot = RootWindow(xdisplay, xscreen);
  xowner = XCreateSimpleWindow(xdisplay, xroot, -10, -10, 1, 1, 0, 0, 0);

  sel = XInternAtom(xdisplay, "CLIPBOARD", False);
  utf8 = XInternAtom(xdisplay, "UTF8_STRING", False);
  XSetSelectionOwner(xdisplay, sel, xowner, CurrentTime);

  for (;;) {
    XNextEvent(xdisplay, &ev);
    switch (ev.type) {

      case SelectionClear:
        exit(0);
        break;

      case SelectionRequest:
        sev = (XSelectionRequestEvent*)&ev.xselectionrequest;
        if (sev->target == utf8 && sev->property != None)
          send_utf8(xdisplay, sev, utf8, bitboardStr);
        break;

    }
  }

}


void send_utf8(Display *dpy, XSelectionRequestEvent *sev, Atom utf8, const char msg[19]) {
  XSelectionEvent ssev;

  XChangeProperty(dpy, sev->requestor, sev->property, utf8, 8, PropModeReplace,
      (const unsigned char*)msg, strlen(msg));

  ssev.type = SelectionNotify;
  ssev.requestor = sev->requestor;
  ssev.selection = sev->selection;
  ssev.target = sev->target;
  ssev.property = sev->property;
  ssev.time = sev->time;

  XSendEvent(dpy, sev->requestor, True, NoEventMask, (XEvent *)&ssev);
}

