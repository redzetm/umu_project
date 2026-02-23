#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "uim_term.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_orig;
static int g_raw_on = 0;

int uim_term_enable_raw(void) {
  if (g_raw_on) return 0;
  if (tcgetattr(STDIN_FILENO, &g_orig) != 0) return 1;

  struct termios raw = g_orig;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return 1;
  g_raw_on = 1;
  return 0;
}

void uim_term_disable_raw(void) {
  if (!g_raw_on) return;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
  g_raw_on = 0;
}

int uim_term_get_winsz(uim_winsz_t *out) {
  if (!out) return 1;
  struct winsize ws;
  memset(&ws, 0, sizeof(ws));
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) return 1;
  out->rows = (int)ws.ws_row;
  out->cols = (int)ws.ws_col;
  if (out->rows <= 0) out->rows = 24;
  if (out->cols <= 0) out->cols = 80;
  return 0;
}

static int read_byte(int fd, unsigned char *out) {
  if (!out) return -1;
  while (1) {
    ssize_t r = read(fd, out, 1);
    if (r == 1) return 0;
    if (r == 0) return 1; // EOF
    if (errno == EAGAIN || errno == EINTR) continue;
    return -1;
  }
}

int uim_term_read_key(int fd) {
  unsigned char c;
  int rr = read_byte(fd, &c);
  if (rr != 0) return UIM_KEY_NONE;

  if (c == 3) return UIM_KEY_CTRL_C;
  if (c == 127 || c == 8) return UIM_KEY_BACKSPACE;
  if (c == '\r' || c == '\n') return UIM_KEY_ENTER;

  if (c != 27) return (int)c;

  // ESC sequence
  unsigned char seq1, seq2;
  int r1 = read_byte(fd, &seq1);
  if (r1 != 0) return UIM_KEY_ESC;

  // SS3 (ESC O ...) is used by some terminals for Home/End.
  if (seq1 == 'O') {
    int r2 = read_byte(fd, &seq2);
    if (r2 != 0) return UIM_KEY_ESC;
    switch (seq2) {
      case 'H': return UIM_KEY_HOME;
      case 'F': return UIM_KEY_END;
      default: return UIM_KEY_ESC;
    }
  }

  // CSI (ESC [ ...)
  if (seq1 != '[') return UIM_KEY_ESC;
  int r2 = read_byte(fd, &seq2);
  if (r2 != 0) return UIM_KEY_ESC;

  switch (seq2) {
    case 'A': return UIM_KEY_ARROW_UP;
    case 'B': return UIM_KEY_ARROW_DOWN;
    case 'C': return UIM_KEY_ARROW_RIGHT;
    case 'D': return UIM_KEY_ARROW_LEFT;
    case 'H': return UIM_KEY_HOME;
    case 'F': return UIM_KEY_END;
    default: break;
  }

  // Some terminals send Home/End as ESC [ 1 ~ / 4 ~ or 7~/8~.
  if (seq2 >= '0' && seq2 <= '9') {
    unsigned char seq3;
    int r3 = read_byte(fd, &seq3);
    if (r3 != 0) return UIM_KEY_ESC;
    if (seq3 == '~') {
      if (seq2 == '1' || seq2 == '7') return UIM_KEY_HOME;
      if (seq2 == '4' || seq2 == '8') return UIM_KEY_END;
    }
  }

  return UIM_KEY_ESC;
}

int uim_term_pending_bytes(int fd) {
  int n = 0;
  if (ioctl(fd, FIONREAD, &n) != 0) return 0;
  if (n < 0) return 0;
  return n;
}

static void write_str(const char *s) {
  if (!s) return;
  write(STDOUT_FILENO, s, strlen(s));
}

void uim_term_clear(void) {
  write_str("\x1b[2J\x1b[H");
}

void uim_term_move_cursor(int r1, int c1) {
  char buf[64];
  if (r1 < 1) r1 = 1;
  if (c1 < 1) c1 = 1;
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", r1, c1);
  write_str(buf);
}

void uim_term_hide_cursor(int hide) {
  write_str(hide ? "\x1b[?25l" : "\x1b[?25h");
}

void uim_term_flush(void) {
  // no-op: writing to a terminal/pty does not require fsync()
}
