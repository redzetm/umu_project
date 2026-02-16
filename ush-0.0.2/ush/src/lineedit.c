#include "ush_lineedit.h"

#include "ush_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int ush_write_all(int fd, const char *buf, size_t len) {
  while (len > 0) {
    ssize_t w = write(fd, buf, len);
    if (w < 0) {
      if (errno == EINTR) continue;
      return 1;
    }
    buf += (size_t)w;
    len -= (size_t)w;
  }
  return 0;
}

static int ush_redraw(const char *prompt, const char *buf, size_t len, size_t cursor) {
  char seq[64];

  if (ush_write_all(STDOUT_FILENO, "\r", 1)) return 1;
  if (ush_write_all(STDOUT_FILENO, prompt, strlen(prompt))) return 1;
  if (ush_write_all(STDOUT_FILENO, buf, len)) return 1;
  if (ush_write_all(STDOUT_FILENO, "\x1b[K", 3)) return 1;

  size_t tail = len - cursor;
  if (tail > 0) {
    snprintf(seq, sizeof(seq), "\x1b[%zuD", tail);
    if (ush_write_all(STDOUT_FILENO, seq, strlen(seq))) return 1;
  }
  return 0;
}

static int ush_read_byte(unsigned char *out_ch) {
  for (;;) {
    ssize_t r = read(STDIN_FILENO, out_ch, 1);
    if (r == 1) return 0;
    if (r == 0) return 1;
    if (errno == EINTR) continue;
    return 1;
  }
}

static void ush_hist_push(ush_history_t *hist, const char *line) {
  if (hist == NULL) return;
  if (line == NULL || line[0] == '\0') return;

  if (hist->count < USH_HISTORY_MAX) {
    strncpy(hist->items[hist->count], line, USH_MAX_LINE_LEN);
    hist->items[hist->count][USH_MAX_LINE_LEN] = '\0';
    hist->count++;
  } else {
    for (int i = 1; i < USH_HISTORY_MAX; i++) {
      strcpy(hist->items[i - 1], hist->items[i]);
    }
    strncpy(hist->items[USH_HISTORY_MAX - 1], line, USH_MAX_LINE_LEN);
    hist->items[USH_HISTORY_MAX - 1][USH_MAX_LINE_LEN] = '\0';
  }
  hist->cursor = hist->count;
}

int ush_lineedit_readline(
  const char *prompt,
  char *out_line,
  size_t out_cap,
  ush_history_t *hist,
  int last_status
) {
  (void)last_status;

  if (out_line == NULL || out_cap == 0) return 1;
  out_line[0] = '\0';

  struct termios orig;
  if (tcgetattr(STDIN_FILENO, &orig) != 0) {
    // stdin が tty でない場合は、最小の「非対話」フォールバックで読む。
    // これにより `</dev/null` 等でもエラー出力せず EOF として扱える。
    if (errno == ENOTTY) {
      if (prompt != NULL) {
        (void)ush_write_all(STDOUT_FILENO, prompt, strlen(prompt));
      }
      fflush(stdout);

      if (fgets(out_line, (int)out_cap, stdin) == NULL) {
        return 1;
      }
      size_t n = strlen(out_line);
      while (n > 0 && (out_line[n - 1] == '\n' || out_line[n - 1] == '\r')) {
        out_line[--n] = '\0';
      }
      if (hist != NULL && out_line[0] != '\0') {
        ush_hist_push(hist, out_line);
      }
      return 0;
    }

    ush_perrorf("tcgetattr");
    return 1;
  }

  struct termios raw = orig;
  raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
    ush_perrorf("tcsetattr");
    return 1;
  }

  char buf[USH_MAX_LINE_LEN + 1];
  size_t len = 0;
  size_t cursor = 0;
  buf[0] = '\0';

  char saved[USH_MAX_LINE_LEN + 1];
  int saved_valid = 0;

  if (hist != NULL) hist->cursor = hist->count;

  (void)ush_redraw(prompt, buf, len, cursor);

  int eof = 0;
  for (;;) {
    unsigned char ch = 0;
    if (ush_read_byte(&ch)) {
      eof = 1;
      break;
    }

    if (ch == '\r' || ch == '\n') {
      ush_write_all(STDOUT_FILENO, "\n", 1);
      break;
    }

    if (ch == 0x04) {
      if (len == 0) {
        eof = 1;
        break;
      }
      continue;
    }

    if (ch == 0x08 || ch == 0x7f) {
      if (cursor > 0) {
        memmove(buf + cursor - 1, buf + cursor, len - cursor);
        len--;
        cursor--;
        buf[len] = '\0';
        ush_redraw(prompt, buf, len, cursor);
      }
      continue;
    }

    if (ch == 0x1b) {
      unsigned char a = 0;
      if (ush_read_byte(&a)) continue;
      if (a != '[') continue;

      unsigned char b = 0;
      if (ush_read_byte(&b)) continue;

      if (b == 'A') {
        if (hist != NULL && hist->count > 0 && hist->cursor > 0) {
          if (hist->cursor == hist->count && !saved_valid) {
            strncpy(saved, buf, sizeof(saved) - 1);
            saved[sizeof(saved) - 1] = '\0';
            saved_valid = 1;
          }
          hist->cursor--;
          strncpy(buf, hist->items[hist->cursor], sizeof(buf) - 1);
          buf[sizeof(buf) - 1] = '\0';
          len = strlen(buf);
          cursor = len;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == 'B') {
        if (hist != NULL && hist->count > 0 && hist->cursor < hist->count) {
          hist->cursor++;
          if (hist->cursor == hist->count) {
            if (saved_valid) {
              strncpy(buf, saved, sizeof(buf) - 1);
              buf[sizeof(buf) - 1] = '\0';
            } else {
              buf[0] = '\0';
            }
          } else {
            strncpy(buf, hist->items[hist->cursor], sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
          }
          len = strlen(buf);
          cursor = len;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == 'C') {
        if (cursor < len) {
          cursor++;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == 'D') {
        if (cursor > 0) {
          cursor--;
          ush_redraw(prompt, buf, len, cursor);
        }
        continue;
      }

      if (b == '3') {
        unsigned char t = 0;
        if (ush_read_byte(&t)) continue;
        if (t == '~') {
          if (cursor < len) {
            memmove(buf + cursor, buf + cursor + 1, len - cursor - 1);
            len--;
            buf[len] = '\0';
            ush_redraw(prompt, buf, len, cursor);
          }
        }
        continue;
      }

      continue;
    }

    if (ch < 0x20 || ch == 0x7f) {
      continue;
    }

    if (len + 1 >= sizeof(buf) || len + 1 >= out_cap) {
      continue;
    }

    if (cursor == len) {
      buf[len] = (char)ch;
      len++;
      buf[len] = '\0';
      cursor = len;
    } else {
      memmove(buf + cursor + 1, buf + cursor, len - cursor);
      buf[cursor] = (char)ch;
      len++;
      cursor++;
      buf[len] = '\0';
    }
    ush_redraw(prompt, buf, len, cursor);
  }

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

  if (eof) return 1;

  strncpy(out_line, buf, out_cap - 1);
  out_line[out_cap - 1] = '\0';

  if (hist != NULL && out_line[0] != '\0') {
    ush_hist_push(hist, out_line);
  }

  return 0;
}
