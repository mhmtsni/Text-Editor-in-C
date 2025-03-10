#include <asm-generic/ioctls.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
struct editor_config {
  int cx, cy;
  int screen_rows;
  int screen_cols;
  struct termios orig_termios;
};

struct editor_config E;

struct abuf {
  char *b;
  int len;
};
#define CTRL_KEY(k) ((k) & 0x1f)

#define ABUF_INIT {NULL, 0}
#define EDITOR_VERSION "0.0.1"

enum editor_key {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN
};

void enable_raw_mode();
void disable_raw_mode();
void die(const char *s);
int editor_read_key();
void editor_process_keybreaks();
void editor_refresh_screen();
void init_editor();
void ab_append(struct abuf *ab, const char *s, int len);
void ab_free(struct abuf *ab);

int main(void) {
  enable_raw_mode();
  init_editor();
  while (1) {
    editor_refresh_screen();
    editor_process_keybreaks();
  }
  return 0;
}

void editor_draw_rows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screen_rows; y++) {
    if (y == E.screen_rows / 3) {
      char welcome[80];
      int welcome_len = snprintf(welcome, sizeof(welcome),
                                 "Text Editor -- version %s", EDITOR_VERSION);
      if (welcome_len > E.screen_cols) {
        welcome_len = E.screen_cols;
      }
      int padding = (E.screen_cols - welcome_len) / 2;
      if (padding) {
        ab_append(ab, "~", 1);
        padding--;
      }
      while (padding--)
        ab_append(ab, " ", 1);
    } else {
      ab_append(ab, "~", 1);
    }
    ab_append(ab, "\x1b[K", 3);
    if (y < E.screen_rows - 1)
      ab_append(ab, "\r\n", 2);
  }
}

int editor_read_key() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[0] == '[') {
      switch (seq[1]) {
      case 'A':
        return ARROW_UP;
      case 'B':
        return ARROW_DOWN;
      case 'C':
        return ARROW_RIGHT;
      case 'D':
        return ARROW_LEFT;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

void editor_move_cursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
    }
    break;
  case ARROW_RIGHT:
    if (E.cx != E.screen_cols - 1) {
      E.cx++;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy != E.screen_rows - 1) {
      E.cy++;
    }
    break;
  }
}

void editor_process_keybreaks() {
  int c = editor_read_key();
  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editor_move_cursor(c);
    break;
  }
}

void editor_refresh_screen() {
  struct abuf ab = ABUF_INIT;
  ab_append(&ab, "\x1b[?25l", 6);
  ab_append(&ab, "\x1b[H", 3);
  editor_draw_rows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  ab_append(&ab, buf, strlen(buf));
  ab_append(&ab, "\x1b[?25h", 6);
  write(STDOUT_FILENO, ab.b, ab.len);
  ab_free(&ab);
}

void enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disable_raw_mode);
  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int get_cursor_position(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;

    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  editor_read_key();
  return -1;
}

void ab_append(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void ab_free(struct abuf *ab) { free(ab->b); }

int get_window_size(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return get_cursor_position(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

void init_editor() {
  E.cx = 0;
  E.cy = 0;
  if (get_window_size(&E.screen_rows, &E.screen_cols) == -1)
    die("get_window_size");
}

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disable_raw_mode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}