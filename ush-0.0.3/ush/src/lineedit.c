#include "ush_lineedit.h"

#include "ush_env.h"
#include "ush_utils.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
  int enabled;
  struct termios orig;
} raw_state_t;

static raw_state_t g_raw;

static void restore_raw(void) {
  if (g_raw.enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_raw.orig);
    g_raw.enabled = 0;
  }
}

static int enable_raw(void) {
  if (!isatty(STDIN_FILENO)) return 0; // 非TTYはそのまま（読み取りのみ）

  struct termios t;
  if (tcgetattr(STDIN_FILENO, &t) != 0) return 1;
  g_raw.orig = t;

  t.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN);
  t.c_iflag &= (tcflag_t)~(IXON | ICRNL);
  t.c_oflag &= (tcflag_t)~(OPOST);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) return 1;
  g_raw.enabled = 1;
  atexit(restore_raw);
  return 0;
}

static void redraw(const char *prompt, const char *buf, size_t len, size_t cursor) {
  // \r: 行頭へ、\x1b[K: 行末まで消去
  fputs("\r", stdout);
  fputs(prompt, stdout);
  fwrite(buf, 1, len, stdout);
  fputs("\x1b[K", stdout);

  // カーソルを左へ戻す
  size_t tail = len - cursor;
  if (tail > 0) {
    fprintf(stdout, "\x1b[%zuD", tail);
  }
  fflush(stdout);
}

static int hist_push(ush_history_t *hist, const char *line) {
  if (hist == NULL || line == NULL || line[0] == '\0') return 0;
  if (hist->count < USH_HISTORY_MAX) {
    snprintf(hist->items[hist->count], sizeof(hist->items[hist->count]), "%s", line);
    hist->count++;
  } else {
    for (int i = 1; i < USH_HISTORY_MAX; i++) {
      memcpy(hist->items[i - 1], hist->items[i], sizeof(hist->items[i - 1]));
    }
    snprintf(hist->items[USH_HISTORY_MAX - 1], sizeof(hist->items[USH_HISTORY_MAX - 1]), "%s", line);
  }
  hist->cursor = hist->count;
  return 0;
}

static int hist_set(ush_history_t *hist, int idx, char *buf, size_t cap, size_t *io_len, size_t *io_cursor) {
  if (hist == NULL || buf == NULL || cap == 0 || io_len == NULL || io_cursor == NULL) return 1;
  if (idx < 0) idx = 0;
  if (idx > hist->count) idx = hist->count;
  hist->cursor = idx;

  if (idx == hist->count) {
    buf[0] = '\0';
    *io_len = 0;
    *io_cursor = 0;
    return 0;
  }

  snprintf(buf, cap, "%s", hist->items[idx]);
  *io_len = strlen(buf);
  *io_cursor = *io_len;
  return 0;
}

static int is_cmd_char(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

static int extract_first_token(const char *buf, size_t len, char *out, size_t cap) {
  if (out == NULL || cap == 0) return 1;
  out[0] = '\0';
  if (buf == NULL) return 1;

  size_t i = 0;
  while (i < len && (buf[i] == ' ' || buf[i] == '\t')) i++;
  if (i >= len) return 1;

  size_t j = 0;
  while (i < len && is_cmd_char((unsigned char)buf[i])) {
    if (j + 1 < cap) out[j++] = buf[i];
    i++;
  }
  out[j] = '\0';
  return (j == 0) ? 1 : 0;
}

static int list_dir_matches(const char *dir, const char *prefix, char out[256][USH_MAX_TOKEN_LEN + 1], int *io_n) {
  if (dir == NULL || prefix == NULL || out == NULL || io_n == NULL) return 1;

  DIR *dp = opendir(dir);
  if (dp == NULL) return 1;

  struct dirent *de;
  while ((de = readdir(dp)) != NULL) {
    const char *name = de->d_name;
    if (name == NULL || name[0] == '\0') continue;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

    // 先頭 '.' は prefix も '.' のときだけ候補にする
    if (name[0] == '.' && prefix[0] != '.') continue;

    if (!ush_starts_with(name, prefix)) continue;

    if (*io_n < 256) {
      snprintf(out[*io_n], USH_MAX_TOKEN_LEN + 1, "%s", name);
      (*io_n)++;
    }
  }

  closedir(dp);
  return 0;
}

static void common_prefix(char *io_prefix, size_t cap, char matches[256][USH_MAX_TOKEN_LEN + 1], int n) {
  if (io_prefix == NULL || cap == 0 || n <= 0) return;

  size_t base = strlen(matches[0]);
  for (int i = 1; i < n; i++) {
    size_t k = 0;
    while (k < base && matches[i][k] != '\0' && matches[i][k] == matches[0][k]) k++;
    base = k;
  }

  if (base + 1 > cap) base = cap - 1;
  memcpy(io_prefix, matches[0], base);
  io_prefix[base] = '\0';
}

static int do_tab_complete(char *buf, size_t cap, size_t *io_len, size_t *io_cursor) {
  if (buf == NULL || io_len == NULL || io_cursor == NULL) return 1;
  // 先頭トークンのみ補完。カーソルが先頭トークン内にあるときだけ。
  char tok[USH_MAX_TOKEN_LEN + 1];
  if (extract_first_token(buf, *io_len, tok, sizeof(tok)) != 0) return 0;

  // カーソル位置が先頭トークン末尾より後なら何もしない
  size_t i = 0;
  while (i < *io_len && (buf[i] == ' ' || buf[i] == '\t')) i++;
  size_t start = i;
  while (i < *io_len && is_cmd_char((unsigned char)buf[i])) i++;
  size_t end = i;
  if (*io_cursor > end) return 0;

  char matches[256][USH_MAX_TOKEN_LEN + 1];
  int n = 0;

  // builtins も候補に含める
  const char *builtins[] = {"cd", "pwd", "export", "exit", "help"};
  for (size_t bi = 0; bi < sizeof(builtins) / sizeof(builtins[0]); bi++) {
    if (ush_starts_with(builtins[bi], tok) && n < 256) {
      snprintf(matches[n], USH_MAX_TOKEN_LEN + 1, "%s", builtins[bi]);
      n++;
    }
  }

  // PATH から候補
  const char *path = ush_get_path_or_default();
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char *save = NULL, *p = strtok_r(tmp, ":", &save); p != NULL; p = strtok_r(NULL, ":", &save)) {
    list_dir_matches(p, tok, matches, &n);
    if (n >= 256) break;
  }

  if (n == 0) return 0;

  if (n == 1) {
    const char *m = matches[0];
    size_t mlen = strlen(m);
    // 置換: [start,end) を m で
    if (start + mlen + (*io_len - end) + 1 > cap) return 0;

    memmove(buf + start + mlen, buf + end, (*io_len - end) + 1);
    memcpy(buf + start, m, mlen);
    *io_len = start + mlen + (*io_len - end);
    *io_cursor = start + mlen;
    return 0;
  }

  char cp[USH_MAX_TOKEN_LEN + 1];
  cp[0] = '\0';
  common_prefix(cp, sizeof(cp), matches, n);
  if (cp[0] != '\0' && strlen(cp) > strlen(tok)) {
    size_t cplen = strlen(cp);
    if (start + cplen + (*io_len - end) + 1 > cap) return 0;
    memmove(buf + start + cplen, buf + end, (*io_len - end) + 1);
    memcpy(buf + start, cp, cplen);
    *io_len = start + cplen + (*io_len - end);
    *io_cursor = start + cplen;
    return 0;
  }

  // 伸長不可: 一覧表示して再描画
  fputc('\n', stdout);
  for (int k = 0; k < n; k++) {
    fputs(matches[k], stdout);
    fputc((k == n - 1) ? '\n' : ' ', stdout);
  }
  return 0;
}

int ush_lineedit_readline(const char *prompt, char *out_line, size_t out_cap, ush_history_t *hist) {
  if (out_line == NULL || out_cap == 0) return 1;
  out_line[0] = '\0';

  // 非TTYは行編集なし
  if (!isatty(STDIN_FILENO)) {
    if (fgets(out_line, (int)out_cap, stdin) == NULL) return 1;
    size_t n = strlen(out_line);
    while (n > 0 && (out_line[n - 1] == '\n' || out_line[n - 1] == '\r')) {
      out_line[--n] = '\0';
    }
    return 0;
  }

  if (enable_raw() != 0) return 1;

  char buf[USH_MAX_LINE_LEN + 1];
  size_t len = 0;
  size_t cursor = 0;
  buf[0] = '\0';

  // 履歴の保存領域（履歴カーソル移動中の作業バッファ）
  char saved[USH_MAX_LINE_LEN + 1];
  int saved_valid = 0;

  if (hist != NULL) hist->cursor = hist->count;

  redraw(prompt, buf, len, cursor);

  for (;;) {
    unsigned char ch;
    ssize_t r = read(STDIN_FILENO, &ch, 1);
    if (r <= 0) return 1;

    if (ch == '\r' || ch == '\n') {
      fputc('\n', stdout);
      buf[len] = '\0';
      snprintf(out_line, out_cap, "%s", buf);
      if (hist != NULL && out_line[0] != '\0') hist_push(hist, out_line);
      return 0;
    }

    if (ch == 0x04) { // Ctrl-D
      if (len == 0) {
        fputc('\n', stdout);
        return 1;
      }
      continue;
    }

    if (ch == 0x08 || ch == 0x7f) { // BS/DEL
      if (cursor > 0) {
        memmove(buf + cursor - 1, buf + cursor, len - cursor);
        len--;
        cursor--;
        buf[len] = '\0';
        redraw(prompt, buf, len, cursor);
      }
      continue;
    }

    if (ch == '\t') { // Tab
      do_tab_complete(buf, sizeof(buf), &len, &cursor);
      redraw(prompt, buf, len, cursor);
      continue;
    }

    if (ch == 0x1b) { // ESC
      unsigned char seq[4] = {0};
      if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
      if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

      if (seq[0] == '[') {
        if (seq[1] == 'C') { // right
          if (cursor < len) {
            cursor++;
            redraw(prompt, buf, len, cursor);
          }
          continue;
        }
        if (seq[1] == 'D') { // left
          if (cursor > 0) {
            cursor--;
            redraw(prompt, buf, len, cursor);
          }
          continue;
        }
        if (seq[1] == 'A') { // up
          if (hist != NULL && hist->count > 0) {
            if (hist->cursor == hist->count && !saved_valid) {
              snprintf(saved, sizeof(saved), "%s", buf);
              saved_valid = 1;
            }
            if (hist->cursor > 0) {
              hist_set(hist, hist->cursor - 1, buf, sizeof(buf), &len, &cursor);
              redraw(prompt, buf, len, cursor);
            }
          }
          continue;
        }
        if (seq[1] == 'B') { // down
          if (hist != NULL && hist->count > 0) {
            if (hist->cursor < hist->count) {
              hist_set(hist, hist->cursor + 1, buf, sizeof(buf), &len, &cursor);
              if (hist->cursor == hist->count && saved_valid) {
                snprintf(buf, sizeof(buf), "%s", saved);
                len = strlen(buf);
                cursor = len;
              }
              redraw(prompt, buf, len, cursor);
            }
          }
          continue;
        }
        if (seq[1] == '3') { // Delete: ESC [ 3 ~
          if (read(STDIN_FILENO, &seq[2], 1) <= 0) continue;
          if (seq[2] == '~') {
            if (cursor < len) {
              memmove(buf + cursor, buf + cursor + 1, len - cursor - 1);
              len--;
              buf[len] = '\0';
              redraw(prompt, buf, len, cursor);
            }
          }
          continue;
        }
      }

      continue;
    }

    // printable
    if (ch >= 0x20 && ch <= 0x7e) {
      if (len + 1 >= sizeof(buf)) continue;
      if (len + 1 >= out_cap) continue;

      memmove(buf + cursor + 1, buf + cursor, len - cursor);
      buf[cursor] = (char)ch;
      len++;
      cursor++;
      buf[len] = '\0';
      redraw(prompt, buf, len, cursor);
      continue;
    }
  }
}
