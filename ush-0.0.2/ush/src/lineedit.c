#include "ush_lineedit.h"

#include "ush_env.h"
#include "ush_utils.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int ush_write_all(int fd, const char *buf, size_t len);
static int ush_redraw(const char *prompt, const char *buf, size_t len, size_t cursor);

static int is_space_ch(char c) {
  return c == ' ' || c == '\t';
}

enum {
  USH_TAB_CAND_MAX = 256,
};

static int cand_add_unique(char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1], int *io_n, const char *name) {
  if (io_n == NULL || name == NULL) return 1;
  if (*io_n >= USH_TAB_CAND_MAX) return 1;
  for (int i = 0; i < *io_n; i++) {
    if (strcmp(cands[i], name) == 0) return 0;
  }
  strncpy(cands[*io_n], name, USH_MAX_TOKEN_LEN);
  cands[*io_n][USH_MAX_TOKEN_LEN] = '\0';
  (*io_n)++;
  return 0;
}

static size_t common_prefix_len(char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1], int n) {
  if (n <= 0) return 0;
  size_t l = strlen(cands[0]);
  for (int i = 1; i < n; i++) {
    size_t li = strlen(cands[i]);
    if (li < l) l = li;
  }
  for (size_t k = 0; k < l; k++) {
    char ch = cands[0][k];
    for (int i = 1; i < n; i++) {
      if (cands[i][k] != ch) return k;
    }
  }
  return l;
}

static int is_executable_in_dir(const char *dir, const char *name) {
  if (dir == NULL || name == NULL) return 0;

  char full[8192];
  if (snprintf(full, sizeof(full), "%s/%s", dir, name) >= (int)sizeof(full)) {
    return 0;
  }

  struct stat st;
  if (stat(full, &st) != 0) return 0;
  if (S_ISDIR(st.st_mode)) return 0;
  return access(full, X_OK) == 0;
}

static int find_cmd_token_region(const char *buf, size_t len, size_t cursor, size_t *out_start, size_t *out_end) {
  if (buf == NULL || out_start == NULL || out_end == NULL) return 1;

  size_t i = 0;
  while (i < len && is_space_ch(buf[i])) i++;
  if (i >= len) return 1;  // empty or only spaces

  size_t start = i;
  size_t end = start;
  while (end < len && !is_space_ch(buf[end])) end++;

  // Only complete the first token (command name), and only when cursor is at end of that token.
  if (cursor != end) return 1;

  *out_start = start;
  *out_end = end;
  return 0;
}

static int buf_replace_range(char *buf, size_t buf_cap, size_t *io_len, size_t *io_cursor,
                             size_t start, size_t end, const char *insert) {
  if (buf == NULL || io_len == NULL || io_cursor == NULL || insert == NULL) return 1;
  if (start > end || end > *io_len) return 1;

  size_t ins_len = strlen(insert);
  size_t old_len = end - start;

  if (*io_len - old_len + ins_len + 1 > buf_cap) return 1;

  if (ins_len != old_len) {
    memmove(buf + start + ins_len, buf + end, *io_len - end);
  }
  memcpy(buf + start, insert, ins_len);
  *io_len = *io_len - old_len + ins_len;
  buf[*io_len] = '\0';

  *io_cursor = start + ins_len;
  return 0;
}

static void gather_cmd_candidates(const char *prefix, char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1], int *out_n) {
  if (out_n == NULL) return;
  *out_n = 0;
  if (prefix == NULL) prefix = "";

  // builtins
  static const char *builtins[] = {"cd", "pwd", "export", "exit", "help"};
  for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
    if (ush_starts_with(builtins[i], prefix)) {
      (void)cand_add_unique(cands, out_n, builtins[i]);
    }
  }

  // PATH executables
  const char *path = ush_get_path_or_default();
  const char *seg = path;
  while (1) {
    const char *colon = strchr(seg, ':');
    size_t seg_len = colon ? (size_t)(colon - seg) : strlen(seg);

    char dir[4096];
    if (seg_len == 0) {
      strcpy(dir, ".");
    } else {
      if (seg_len >= sizeof(dir)) seg_len = sizeof(dir) - 1;
      memcpy(dir, seg, seg_len);
      dir[seg_len] = '\0';
    }

    DIR *d = opendir(dir);
    if (d != NULL) {
      for (;;) {
        struct dirent *ent = readdir(d);
        if (ent == NULL) break;
        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (name[0] == '.' && prefix[0] != '.') continue;

        if (!ush_starts_with(name, prefix)) continue;
        if (!is_executable_in_dir(dir, name)) continue;
        if (*out_n >= USH_TAB_CAND_MAX) break;
        (void)cand_add_unique(cands, out_n, name);
      }
      closedir(d);
    }

    if (!colon) break;
    seg = colon + 1;
  }
}

static void handle_tab_completion(const char *prompt, char *buf, size_t buf_cap, size_t *io_len, size_t *io_cursor) {
  size_t start = 0;
  size_t end = 0;
  if (find_cmd_token_region(buf, *io_len, *io_cursor, &start, &end) != 0) {
    return;
  }

  size_t token_len = end - start;
  if (token_len > (size_t)USH_MAX_TOKEN_LEN) token_len = (size_t)USH_MAX_TOKEN_LEN;

  char prefix[USH_MAX_TOKEN_LEN + 1];
  memcpy(prefix, buf + start, token_len);
  prefix[token_len] = '\0';

  char cands[USH_TAB_CAND_MAX][USH_MAX_TOKEN_LEN + 1];
  int n = 0;
  gather_cmd_candidates(prefix, cands, &n);
  if (n <= 0) return;

  if (n == 1) {
    (void)buf_replace_range(buf, buf_cap, io_len, io_cursor, start, end, cands[0]);
    (void)ush_redraw(prompt, buf, *io_len, *io_cursor);
    return;
  }

  size_t cpl = common_prefix_len(cands, n);
  if (cpl > strlen(prefix)) {
    char tmp[USH_MAX_TOKEN_LEN + 1];
    if (cpl > (size_t)USH_MAX_TOKEN_LEN) cpl = (size_t)USH_MAX_TOKEN_LEN;
    memcpy(tmp, cands[0], cpl);
    tmp[cpl] = '\0';
    (void)buf_replace_range(buf, buf_cap, io_len, io_cursor, start, end, tmp);
    (void)ush_redraw(prompt, buf, *io_len, *io_cursor);
    return;
  }

  // No further extension: list candidates then redraw.
  (void)ush_write_all(STDOUT_FILENO, "\n", 1);
  for (int i = 0; i < n; i++) {
    (void)ush_write_all(STDOUT_FILENO, cands[i], strlen(cands[i]));
    (void)ush_write_all(STDOUT_FILENO, "\n", 1);
  }
  (void)ush_redraw(prompt, buf, *io_len, *io_cursor);
}

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

    if (ch == '\t') {
      handle_tab_completion(prompt, buf, sizeof(buf), &len, &cursor);
      continue;
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
