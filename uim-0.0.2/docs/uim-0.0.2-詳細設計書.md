# uim-0.0.2-詳細設計書.md
UmuOS Interactive Minimal editor (uim) — 詳細設計書（0.0.2）  
Target OS: UmuOS-0.1.7-base-stable（想定）

本書は uim-0.0.2 の参照実装（`uim-0.0.2/uim`）を規定する詳細設計書であり、実装手順・関数分割・データ構造・貼り付け可能コードを提示する。

このリポジトリ運用では **本書を唯一の正** とし、矛盾がある場合は本書を優先する。
- 仕様の要約: [uim-0.0.2-仕様書.md](uim-0.0.2-仕様書.md)
- 基本設計の要約: [uim-0.0.2-基本設計書.md](uim-0.0.2-基本設計書.md)

---

# 0. この文書の読み方（コピペ区分）
- 本書中のコードブロックは、直前のラベルで用途を区分する。
  - 【実装用（貼り付け可）: <貼り付け先パス>】: そのまま貼り付けてビルドできることを想定する。
  - 【説明】: 背景・意図・補足。
- パス表記は「`uim/` ディレクトリからの相対パス」を貼り付け先として示す。

---

# 1. 前提・設計原則

- 最小 vi ライク（移動 + `0/$` + `gg/G` + `dd/yy/p` + `/` + `n` + `:wq/:q!`）だけを実装する
- raw mode + ANSI エスケープによる単純な全画面再描画
- UTF-8 を壊さない編集（境界で移動/削除/挿入）
- 表示幅は厳密性より実用性（ASCII=1、非ASCII=2、タブ整列）

---

# 2. ソース構成

【説明】

参照実装は `uim-0.0.2/uim` に存在する。本書は「1から実装できる」ように、必要ファイルの貼り付け可能コードを併記する。

```
uim/
  include/
    uim_limits.h
    uim_utf8.h
    uim_term.h
    uim_buf.h
    uim.h
  src/
    utf8.c
    term.c
    buf.c
    render.c
    editor.c
    main.c
  tests/
    smoke_uim.sh
```

---

# 3. ビルド

【説明】

ホスト（開発機）でのビルド例（スモークも同様）。

```
musl-gcc -std=c11 -O2 -g -static \
  -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
  -Iuim/include \
  uim/src/*.c \
  -o uim
```

---

# 4. 主要仕様（実装に直結する決め）

## 4.1 モードとキー
- NORMAL:
  - 移動: `h/j/k/l`（矢印キーも同等）
  - `0`: 行頭へ移動（数値プレフィクス未入力、かつペンディング無しのとき）
  - `$`: 行末へ移動
  - `gg`/`Ngg`: 先頭行 / N 行目へ移動（例: `gg`, `10gg`）
  - `G`/`NG`: 最終行 / N 行目へ移動（例: `G`, `10G`）
  - Home/End: 行頭/行末へ移動
  - `i`: INSERT
  - `a`: カーソルの 1 文字右に移動して INSERT
  - `dd`/`Ndd`: 行削除（削除した行はヤンクにも入る。例: `3dd`）
  - `yy`/`Nyy`: 行ヤンク（例: `3yy`）
  - `p`: ヤンクを現在行の下に貼り付け（ヤンクが複数行の場合は複数行を挿入）
  - `/`: SEARCH（前方検索の入力）
  - `n`: 直前の検索を前方に繰り返す
  - `:`: COLON
  - Ctrl-C: 終了
- INSERT:
  - 通常バイト入力: UTF-8 文字単位に組み立てて挿入
  - 矢印キー: INSERT のままカーソル移動
  - Enter: 行分割
  - Backspace: 直前の UTF-8 1 文字削除
  - ESC: NORMAL
- COLON:
  - `w`, `q`, `q!`, `wq`
  - Enter 実行、ESC キャンセル
- SEARCH:
  - `/pattern` 入力（リテラル一致、大小区別あり）
  - Enter で前方検索を実行（カーソルの次の文字からバッファ終端まで。ラップしない）
  - ESC でキャンセル

## 4.2 表示幅（粗いが実用寄り）
- ASCII: 幅 1
- 非 ASCII: 幅 2
- `\t`: タブストップ（4）で次の桁に揃える

この幅計算は、描画・横スクロール・上下移動時の「列維持」に使用する。

---

# 5. 貼り付け可能コード（参照実装）

## 5.1 include

【実装用（貼り付け可）: include/uim_limits.h】
```c
#ifndef UIM_LIMITS_H
#define UIM_LIMITS_H

enum {
  UIM_MAX_LINE_BYTES = 4096,
  UIM_TABSTOP = 4,
};

#endif
```

【実装用（貼り付け可）: include/uim_utf8.h】
```c
#ifndef UIM_UTF8_H
#define UIM_UTF8_H

#include <stddef.h>

int uim_utf8_char_len(unsigned char b);

// Returns index of previous UTF-8 character start (or 0).
size_t uim_utf8_prev(const char *s, size_t i);

// Returns index of next UTF-8 character start (or strlen(s)).
size_t uim_utf8_next(const char *s, size_t i);

// Returns display width at s[i] (1 or 2) and sets *out_len to byte length.
// Tabs are treated as UIM_TABSTOP spaces (width depends on current column; handled elsewhere).
int uim_utf8_width_at(const char *s, size_t i, size_t *out_len);

// Rough display width: ASCII=1, non-ASCII=2, '\t'=tabstop alignment.
int uim_disp_width(const char *s);

// Display column (0-based) for byte index i (clamped to end).
int uim_disp_col_for_byte_index(const char *s, size_t i);

// Byte index for desired display column (0-based). Returns a valid UTF-8 boundary.
size_t uim_byte_index_for_disp_col(const char *s, int target_col);

#endif
```

【実装用（貼り付け可）: include/uim_term.h】
```c
#ifndef UIM_TERM_H
#define UIM_TERM_H

#include <stddef.h>

typedef struct {
  int rows;
  int cols;
} uim_winsz_t;

int uim_term_enable_raw(void);
void uim_term_disable_raw(void);

int uim_term_get_winsz(uim_winsz_t *out);

// Key codes for editor.
typedef enum {
  UIM_KEY_NONE = 0,
  UIM_KEY_ESC = 27,
  UIM_KEY_ENTER = 1000,
  UIM_KEY_BACKSPACE,
  UIM_KEY_CTRL_C,
  UIM_KEY_ARROW_UP,
  UIM_KEY_ARROW_DOWN,
  UIM_KEY_ARROW_LEFT,
  UIM_KEY_ARROW_RIGHT,
  UIM_KEY_HOME,
  UIM_KEY_END,
} uim_key_t;

// Reads one key from fd (0=stdin). Returns ASCII code (0..255) or one of uim_key_t >= 1000.
int uim_term_read_key(int fd);

// Returns pending bytes available to read (0 if none or unknown).
int uim_term_pending_bytes(int fd);

void uim_term_clear(void);
void uim_term_move_cursor(int r1, int c1);
void uim_term_hide_cursor(int hide);
void uim_term_flush(void);

#endif
```

【実装用（貼り付け可）: include/uim_buf.h】
```c
#ifndef UIM_BUF_H
#define UIM_BUF_H

#include <stddef.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} uim_line_t;

typedef struct {
  uim_line_t *lines;
  size_t n;
  size_t cap;
  int modified;
} uim_buf_t;

void uim_buf_init(uim_buf_t *b);
void uim_buf_free(uim_buf_t *b);

int uim_buf_load(uim_buf_t *b, const char *path);
int uim_buf_save(const uim_buf_t *b, const char *path);

// Ensure there is at least one line.
int uim_buf_ensure_nonempty(uim_buf_t *b);

// Delete/yank/paste whole line.
int uim_buf_delete_line(uim_buf_t *b, size_t row);
int uim_buf_insert_line_after(uim_buf_t *b, size_t row, const char *s, size_t slen);

// Edit within a line at byte index (must be UTF-8 boundary).
int uim_buf_insert_bytes(uim_buf_t *b, size_t row, size_t col_byte, const char *bytes, size_t nbytes);
int uim_buf_delete_prev_char(uim_buf_t *b, size_t row, size_t *io_col_byte);
int uim_buf_split_line(uim_buf_t *b, size_t row, size_t col_byte);

#endif
```

【実装用（貼り付け可）: include/uim.h】
```c
#ifndef UIM_H
#define UIM_H

#include "uim_buf.h"

#include <stddef.h>

typedef enum {
  UIM_MODE_NORMAL = 0,
  UIM_MODE_INSERT,
  UIM_MODE_COLON,
  UIM_MODE_SEARCH,
} uim_mode_t;

typedef struct {
  uim_mode_t mode;

  const char *path;
  uim_buf_t buf;

  // cursor in buffer
  size_t cur_row;
  size_t cur_col_byte; // UTF-8 boundary

  // scroll
  size_t row_off;
  int col_off; // display columns

  // yank buffer (one or more lines)
  char **yank_lines;
  size_t yank_n;

  // colon command input
  char colon[64];
  size_t colon_len;

  // search input (/) and last search pattern
  char search[64];
  size_t search_len;
  char last_search[64];
  size_t last_search_len;
  int has_last_search;

  // status message
  char status[128];
  int status_ttl; // frames

  // pending normal-mode command ('d', 'y', or 'g')
  int pending;

  // numeric prefix for NORMAL (e.g. 3dd, 10G)
  int num_prefix;

  // insert-mode UTF-8 assemble buffer
  unsigned char u8buf[8];
  int u8len;
  int u8need;

  int running;
} uim_t;

int uim_run_interactive(uim_t *u);
int uim_run_batch(uim_t *u, int in_fd);

#endif
```

## 5.2 src

【実装用（貼り付け可）: src/utf8.c】
```c
#include "uim_utf8.h"

#include "uim_limits.h"

#include <string.h>

int uim_utf8_char_len(unsigned char b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 1; // invalid lead; treat as single byte
}

static int is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

size_t uim_utf8_prev(const char *s, size_t i) {
  if (s == NULL || i == 0) return 0;
  size_t n = strlen(s);
  if (i > n) i = n;

  size_t j = i - 1;
  // move to lead byte
  int lim = 0;
  while (j > 0 && is_cont((unsigned char)s[j]) && lim < 4) {
    j--;
    lim++;
  }
  return j;
}

size_t uim_utf8_next(const char *s, size_t i) {
  if (s == NULL) return 0;
  size_t n = strlen(s);
  if (i >= n) return n;

  int l = uim_utf8_char_len((unsigned char)s[i]);
  size_t j = i + (size_t)l;
  if (j > n) j = n;
  // If we landed mid-continuation due to invalid bytes, skip continuations.
  while (j < n && is_cont((unsigned char)s[j])) j++;
  return j;
}

int uim_utf8_width_at(const char *s, size_t i, size_t *out_len) {
  if (out_len) *out_len = 0;
  if (s == NULL) return 1;
  unsigned char b = (unsigned char)s[i];
  if (b == '\0') return 1;

  int l = uim_utf8_char_len(b);
  if (out_len) *out_len = (size_t)l;

  if (b < 0x80) {
    if (b == '\t') return UIM_TABSTOP;
    return 1;
  }

  // Rough: treat any non-ASCII as width 2.
  return 2;
}

int uim_disp_width(const char *s) {
  if (s == NULL) return 0;
  int col = 0;
  for (size_t i = 0; s[i] != '\0';) {
    size_t bl = 0;
    int w = uim_utf8_width_at(s, i, &bl);
    if ((unsigned char)s[i] == '\t') {
      int next = ((col / UIM_TABSTOP) + 1) * UIM_TABSTOP;
      col = next;
    } else {
      col += w;
    }
    if (bl == 0) bl = 1;
    i += bl;
  }
  return col;
}

int uim_disp_col_for_byte_index(const char *s, size_t i) {
  if (s == NULL) return 0;
  size_t n = strlen(s);
  if (i > n) i = n;

  int col = 0;
  for (size_t j = 0; j < i && s[j] != '\0';) {
    size_t bl = 0;
    int w = uim_utf8_width_at(s, j, &bl);
    if ((unsigned char)s[j] == '\t') {
      int next = ((col / UIM_TABSTOP) + 1) * UIM_TABSTOP;
      col = next;
    } else {
      col += w;
    }
    if (bl == 0) bl = 1;
    j += bl;
  }
  return col;
}

size_t uim_byte_index_for_disp_col(const char *s, int target_col) {
  if (s == NULL) return 0;
  if (target_col <= 0) return 0;

  int col = 0;
  size_t i = 0;
  while (s[i] != '\0') {
    size_t bl = 0;
    int w = uim_utf8_width_at(s, i, &bl);

    int next_col;
    if ((unsigned char)s[i] == '\t') {
      next_col = ((col / UIM_TABSTOP) + 1) * UIM_TABSTOP;
    } else {
      next_col = col + w;
    }

    if (next_col > target_col) break;

    col = next_col;
    if (bl == 0) bl = 1;
    i += bl;
  }
  return i;
}
```

【実装用（貼り付け可）: src/term.c】
```c
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
```

【実装用（貼り付け可）: src/buf.c】
```c
#include "uim_buf.h"

#include "uim_limits.h"
#include "uim_utf8.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void line_free(uim_line_t *l) {
  if (!l) return;
  free(l->data);
  l->data = NULL;
  l->len = 0;
  l->cap = 0;
}

static int line_reserve(uim_line_t *l, size_t need) {
  if (!l) return 1;
  if (need + 1 <= l->cap) return 0;
  size_t nc = (l->cap == 0) ? 64 : l->cap;
  while (nc < need + 1) nc *= 2;
  if (nc > (size_t)UIM_MAX_LINE_BYTES + 1) nc = (size_t)UIM_MAX_LINE_BYTES + 1;
  if (need + 1 > nc) return 1;
  char *p = (char *)realloc(l->data, nc);
  if (!p) return 1;
  l->data = p;
  l->cap = nc;
  return 0;
}

static int line_set(uim_line_t *l, const char *s, size_t slen) {
  if (!l) return 1;
  if (slen > (size_t)UIM_MAX_LINE_BYTES) return 1;
  if (line_reserve(l, slen)) return 1;
  memcpy(l->data, s, slen);
  l->data[slen] = '\0';
  l->len = slen;
  return 0;
}

void uim_buf_init(uim_buf_t *b) {
  if (!b) return;
  b->lines = NULL;
  b->n = 0;
  b->cap = 0;
  b->modified = 0;
}

void uim_buf_free(uim_buf_t *b) {
  if (!b) return;
  for (size_t i = 0; i < b->n; i++) line_free(&b->lines[i]);
  free(b->lines);
  b->lines = NULL;
  b->n = 0;
  b->cap = 0;
  b->modified = 0;
}

static int buf_reserve(uim_buf_t *b, size_t need) {
  if (!b) return 1;
  if (need <= b->cap) return 0;
  size_t nc = (b->cap == 0) ? 32 : b->cap;
  while (nc < need) nc *= 2;
  uim_line_t *p = (uim_line_t *)realloc(b->lines, nc * sizeof(uim_line_t));
  if (!p) return 1;
  for (size_t i = b->cap; i < nc; i++) {
    p[i].data = NULL;
    p[i].len = 0;
    p[i].cap = 0;
  }
  b->lines = p;
  b->cap = nc;
  return 0;
}

int uim_buf_ensure_nonempty(uim_buf_t *b) {
  if (!b) return 1;
  if (b->n > 0) return 0;
  if (buf_reserve(b, 1)) return 1;
  if (line_set(&b->lines[0], "", 0)) return 1;
  b->n = 1;
  return 0;
}

int uim_buf_load(uim_buf_t *b, const char *path) {
  if (!b) return 1;
  uim_buf_free(b);
  uim_buf_init(b);

  if (path == NULL) return 1;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    // Treat missing file as empty buffer.
    if (errno == ENOENT) {
      if (uim_buf_ensure_nonempty(b)) return 1;
      b->modified = 0;
      return 0;
    }
    return 1;
  }

  char tmp[UIM_MAX_LINE_BYTES + 2];
  while (fgets(tmp, sizeof(tmp), fp) != NULL) {
    size_t n = strlen(tmp);
    while (n > 0 && (tmp[n - 1] == '\n' || tmp[n - 1] == '\r')) tmp[--n] = '\0';

    if (buf_reserve(b, b->n + 1)) {
      fclose(fp);
      return 1;
    }

    if (line_set(&b->lines[b->n], tmp, n)) {
      fclose(fp);
      return 1;
    }
    b->n++;
  }
  fclose(fp);

  if (uim_buf_ensure_nonempty(b)) return 1;
  b->modified = 0;
  return 0;
}

int uim_buf_save(const uim_buf_t *b, const char *path) {
  if (!b || !path) return 1;
  FILE *fp = fopen(path, "wb");
  if (!fp) return 1;

  for (size_t i = 0; i < b->n; i++) {
    const char *s = b->lines[i].data ? b->lines[i].data : "";
    size_t sl = b->lines[i].len;
    if (fwrite(s, 1, sl, fp) != sl) {
      fclose(fp);
      return 1;
    }
    if (fwrite("\n", 1, 1, fp) != 1) {
      fclose(fp);
      return 1;
    }
  }

  fclose(fp);
  return 0;
}

int uim_buf_delete_line(uim_buf_t *b, size_t row) {
  if (!b) return 1;
  if (b->n == 0) return 1;
  if (row >= b->n) row = b->n - 1;

  line_free(&b->lines[row]);

  for (size_t i = row; i + 1 < b->n; i++) {
    b->lines[i] = b->lines[i + 1];
  }

  // clear last slot
  b->lines[b->n - 1].data = NULL;
  b->lines[b->n - 1].len = 0;
  b->lines[b->n - 1].cap = 0;

  b->n--;
  if (b->n == 0) {
    if (uim_buf_ensure_nonempty(b)) return 1;
  }

  b->modified = 1;
  return 0;
}

int uim_buf_insert_line_after(uim_buf_t *b, size_t row, const char *s, size_t slen) {
  if (!b) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  if (buf_reserve(b, b->n + 1)) return 1;
  for (size_t i = b->n; i > row + 1; i--) {
    b->lines[i] = b->lines[i - 1];
  }
  b->lines[row + 1].data = NULL;
  b->lines[row + 1].len = 0;
  b->lines[row + 1].cap = 0;

  if (line_set(&b->lines[row + 1], s ? s : "", slen)) return 1;
  b->n++;
  b->modified = 1;
  return 0;
}

int uim_buf_insert_bytes(uim_buf_t *b, size_t row, size_t col_byte, const char *bytes, size_t nbytes) {
  if (!b || !bytes) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  uim_line_t *l = &b->lines[row];
  if (!l->data) {
    if (line_set(l, "", 0)) return 1;
  }

  if (col_byte > l->len) col_byte = l->len;

  if (l->len + nbytes > (size_t)UIM_MAX_LINE_BYTES) return 1;
  if (line_reserve(l, l->len + nbytes)) return 1;

  memmove(l->data + col_byte + nbytes, l->data + col_byte, l->len - col_byte + 1);
  memcpy(l->data + col_byte, bytes, nbytes);
  l->len += nbytes;

  b->modified = 1;
  return 0;
}

int uim_buf_delete_prev_char(uim_buf_t *b, size_t row, size_t *io_col_byte) {
  if (!b || !io_col_byte) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  uim_line_t *l = &b->lines[row];
  if (!l->data) return 0;

  size_t col = *io_col_byte;
  if (col == 0 || col > l->len) return 0;

  size_t prev = uim_utf8_prev(l->data, col);
  if (prev >= col) return 0;
  size_t del = col - prev;

  memmove(l->data + prev, l->data + col, l->len - col + 1);
  l->len -= del;
  *io_col_byte = prev;

  b->modified = 1;
  return 0;
}

int uim_buf_split_line(uim_buf_t *b, size_t row, size_t col_byte) {
  if (!b) return 1;
  if (uim_buf_ensure_nonempty(b)) return 1;
  if (row >= b->n) row = b->n - 1;

  uim_line_t *l = &b->lines[row];
  if (!l->data) {
    if (line_set(l, "", 0)) return 1;
  }

  if (col_byte > l->len) col_byte = l->len;

  const char *right = l->data + col_byte;
  size_t rlen = l->len - col_byte;

  // shrink current line
  l->data[col_byte] = '\0';
  l->len = col_byte;

  if (uim_buf_insert_line_after(b, row, right, rlen)) return 1;
  b->modified = 1;
  return 0;
}
```

【実装用（貼り付け可）: src/render.c】
```c
#include "uim.h"

#include "uim_limits.h"
#include "uim_term.h"
#include "uim_utf8.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void write_str(const char *s) {
  if (!s) return;
  write(STDOUT_FILENO, s, strlen(s));
}

static void write_n(const char *s, size_t n) {
  if (!s || n == 0) return;
  write(STDOUT_FILENO, s, n);
}

static void draw_status(const uim_t *u, const uim_winsz_t *ws) {
  char line[256];
  const char *m = (u->mode == UIM_MODE_INSERT)   ? "INSERT"
                  : (u->mode == UIM_MODE_COLON)  ? ":"
                  : (u->mode == UIM_MODE_SEARCH) ? "/"
                                                 : "NORMAL";
  const char *p = (u->path != NULL) ? u->path : "(no file)";
  snprintf(line, sizeof(line), "--%s-- %s %s  row=%zu col=%d ", m, p, (u->buf.modified ? "[+]" : ""), u->cur_row + 1,
           uim_disp_col_for_byte_index(u->buf.lines[u->cur_row].data, u->cur_col_byte) + 1);

  // Inverse video
  write_str("\x1b[7m");
  int n = (int)strlen(line);
  if (n > ws->cols) n = ws->cols;
  write_n(line, (size_t)n);
  for (int i = n; i < ws->cols; i++) write_str(" ");
  write_str("\x1b[m");
}

static void draw_message(const uim_t *u, const uim_winsz_t *ws) {
  uim_term_move_cursor(ws->rows, 1);
  if (u->mode == UIM_MODE_COLON) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), ":%s", u->colon);
    int n = (int)strlen(tmp);
    if (n > ws->cols) n = ws->cols;
    write_n(tmp, (size_t)n);
    // clear rest
    for (int i = n; i < ws->cols; i++) write_str(" ");
    return;
  }

  if (u->mode == UIM_MODE_SEARCH) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "/%s", u->search);
    int n = (int)strlen(tmp);
    if (n > ws->cols) n = ws->cols;
    write_n(tmp, (size_t)n);
    for (int i = n; i < ws->cols; i++) write_str(" ");
    return;
  }

  if (u->status[0] != '\0') {
    int n = (int)strlen(u->status);
    if (n > ws->cols) n = ws->cols;
    write_n(u->status, (size_t)n);
    for (int i = n; i < ws->cols; i++) write_str(" ");
  } else {
    for (int i = 0; i < ws->cols; i++) write_str(" ");
  }
}

static void draw_rows(const uim_t *u, const uim_winsz_t *ws) {
  int text_rows = ws->rows - 2;
  for (int r = 0; r < text_rows; r++) {
    size_t row = u->row_off + (size_t)r;
    uim_term_move_cursor(r + 1, 1);

    if (row >= u->buf.n) {
      write_str("~");
      for (int i = 1; i < ws->cols; i++) write_str(" ");
      continue;
    }

    const char *s = u->buf.lines[row].data;
    if (s == NULL) s = "";

    // crude horizontal scroll by display columns
    size_t start_b = uim_byte_index_for_disp_col(s, u->col_off);

    // render until cols
    int col = 0;
    for (size_t i = start_b; s[i] != '\0' && col < ws->cols; ) {
      size_t bl = 0;
      int w = uim_utf8_width_at(s, i, &bl);
      if ((unsigned char)s[i] == '\t') {
        int next = ((col / UIM_TABSTOP) + 1) * UIM_TABSTOP;
        while (col < next && col < ws->cols) {
          write_str(" ");
          col++;
        }
      } else {
        // Print raw bytes; terminal is assumed UTF-8 capable.
        if (col + w <= ws->cols) {
          write_n(s + i, bl);
          col += w;
        }
      }
      if (bl == 0) bl = 1;
      i += bl;
    }
    while (col < ws->cols) {
      write_str(" ");
      col++;
    }
  }
}

static void ensure_scroll(uim_t *u, const uim_winsz_t *ws) {
  if (u->cur_row >= u->buf.n) u->cur_row = (u->buf.n == 0) ? 0 : (u->buf.n - 1);

  int text_rows = ws->rows - 2;
  if ((int)u->cur_row < (int)u->row_off) u->row_off = u->cur_row;
  if ((int)u->cur_row >= (int)u->row_off + text_rows) u->row_off = u->cur_row - (size_t)text_rows + 1;

  const char *s = u->buf.lines[u->cur_row].data;
  if (s == NULL) s = "";
  int cur_col = uim_disp_col_for_byte_index(s, u->cur_col_byte);

  if (cur_col < u->col_off) u->col_off = cur_col;
  if (cur_col >= u->col_off + (ws->cols - 1)) u->col_off = cur_col - (ws->cols - 1);
  if (u->col_off < 0) u->col_off = 0;
}

void uim_render(uim_t *u) {
  if (u == NULL) return;

  uim_winsz_t ws;
  if (uim_term_get_winsz(&ws) != 0) {
    ws.rows = 24;
    ws.cols = 80;
  }

  ensure_scroll(u, &ws);

  // Full redraw by overwriting the entire visible area.
  // (Avoid clearing every frame to reduce flicker.)
  uim_term_move_cursor(1, 1);
  uim_term_hide_cursor(1);

  draw_rows(u, &ws);

  uim_term_move_cursor(ws.rows - 1, 1);
  draw_status(u, &ws);
  draw_message(u, &ws);

  // place cursor
  int text_r = (int)(u->cur_row - u->row_off) + 1;
  const char *s = u->buf.lines[u->cur_row].data;
  if (s == NULL) s = "";
  int cur_col = uim_disp_col_for_byte_index(s, u->cur_col_byte);
  int text_c = (cur_col - u->col_off) + 1;
  if (text_r < 1) text_r = 1;
  if (text_r > ws.rows - 2) text_r = ws.rows - 2;
  if (text_c < 1) text_c = 1;
  if (text_c > ws.cols) text_c = ws.cols;
  uim_term_move_cursor(text_r, text_c);

  uim_term_hide_cursor(0);
  uim_term_flush();

  if (u->status_ttl > 0) {
    u->status_ttl--;
    if (u->status_ttl == 0) u->status[0] = '\0';
  }
}
```

【実装用（貼り付け可）: src/editor.c】
```c
#include "uim.h"

#include "uim_buf.h"
#include "uim_term.h"
#include "uim_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void uim_render(uim_t *u);

static void set_status(uim_t *u, const char *msg) {
  if (!u) return;
  if (!msg) msg = "";
  snprintf(u->status, sizeof(u->status), "%s", msg);
  u->status_ttl = 30;
}

static void clamp_cursor(uim_t *u) {
  if (!u) return;
  if (u->cur_row >= u->buf.n) u->cur_row = (u->buf.n == 0) ? 0 : (u->buf.n - 1);
  if (uim_buf_ensure_nonempty(&u->buf)) return;
  const char *s = u->buf.lines[u->cur_row].data;
  if (!s) s = "";
  size_t n = strlen(s);
  if (u->cur_col_byte > n) u->cur_col_byte = n;

  // ensure at UTF-8 boundary: move to prev boundary if mid continuation
  while (u->cur_col_byte > 0) {
    unsigned char b = (unsigned char)s[u->cur_col_byte];
    if (b == '\0') break;
    if ((b & 0xC0) != 0x80) break;
    u->cur_col_byte--;
  }
}

static void move_left(uim_t *u) {
  const char *s = u->buf.lines[u->cur_row].data;
  if (!s) s = "";
  u->cur_col_byte = uim_utf8_prev(s, u->cur_col_byte);
}

static void move_right(uim_t *u) {
  const char *s = u->buf.lines[u->cur_row].data;
  if (!s) s = "";
  u->cur_col_byte = uim_utf8_next(s, u->cur_col_byte);
}

static void enter_insert_after(uim_t *u) {
  if (!u) return;
  move_right(u);
  u->mode = UIM_MODE_INSERT;
}

static void move_up(uim_t *u) {
  if (u->cur_row == 0) return;
  u->cur_row--;
  // keep same display column roughly
  const char *s = u->buf.lines[u->cur_row + 1].data;
  if (!s) s = "";
  int col = uim_disp_col_for_byte_index(s, u->cur_col_byte);
  const char *t = u->buf.lines[u->cur_row].data;
  if (!t) t = "";
  u->cur_col_byte = uim_byte_index_for_disp_col(t, col);
}

static void move_down(uim_t *u) {
  if (u->cur_row + 1 >= u->buf.n) return;
  u->cur_row++;
  const char *s = u->buf.lines[u->cur_row - 1].data;
  if (!s) s = "";
  int col = uim_disp_col_for_byte_index(s, u->cur_col_byte);
  const char *t = u->buf.lines[u->cur_row].data;
  if (!t) t = "";
  u->cur_col_byte = uim_byte_index_for_disp_col(t, col);
}

static void yank_clear(uim_t *u) {
  if (!u) return;
  if (u->yank_lines) {
    for (size_t i = 0; i < u->yank_n; i++) free(u->yank_lines[i]);
    free(u->yank_lines);
  }
  u->yank_lines = NULL;
  u->yank_n = 0;
}

static void yank_range(uim_t *u, size_t row, size_t count) {
  if (!u) return;
  if (uim_buf_ensure_nonempty(&u->buf)) return;
  if (row >= u->buf.n) row = (u->buf.n == 0) ? 0 : (u->buf.n - 1);

  size_t max = u->buf.n - row;
  if (count > max) count = max;
  if (count == 0) {
    yank_clear(u);
    return;
  }

  yank_clear(u);
  u->yank_lines = (char **)calloc(count, sizeof(char *));
  if (!u->yank_lines) {
    u->yank_n = 0;
    return;
  }

  for (size_t i = 0; i < count; i++) {
    const char *s = u->buf.lines[row + i].data;
    if (!s) s = "";
    u->yank_lines[i] = strdup(s);
    if (!u->yank_lines[i]) {
      // best effort cleanup
      for (size_t j = 0; j < i; j++) free(u->yank_lines[j]);
      free(u->yank_lines);
      u->yank_lines = NULL;
      u->yank_n = 0;
      return;
    }
  }
  u->yank_n = count;
}

static void yank_lines_cmd(uim_t *u, size_t row, size_t count) {
  yank_range(u, row, count);
  set_status(u, (count <= 1) ? "yanked" : "yanked lines");
}

static void delete_lines_cmd(uim_t *u, size_t row, size_t count) {
  if (!u) return;
  if (uim_buf_ensure_nonempty(&u->buf)) return;
  if (row >= u->buf.n) row = (u->buf.n == 0) ? 0 : (u->buf.n - 1);

  size_t max = u->buf.n - row;
  if (count > max) count = max;
  if (count == 0) return;

  // dd also yanks
  yank_range(u, row, count);

  for (size_t i = 0; i < count; i++) {
    uim_buf_delete_line(&u->buf, row);
    if (u->buf.n == 0) break;
    if (row >= u->buf.n) row = u->buf.n - 1;
  }

  u->cur_row = row;
  u->cur_col_byte = 0;
  set_status(u, (count <= 1) ? "deleted line" : "deleted lines");
}

static void paste_below(uim_t *u) {
  if (!u || u->yank_n == 0 || !u->yank_lines) return;
  size_t insert_row = u->cur_row;
  for (size_t i = 0; i < u->yank_n; i++) {
    const char *s = u->yank_lines[i] ? u->yank_lines[i] : "";
    uim_buf_insert_line_after(&u->buf, insert_row, s, strlen(s));
    insert_row++;
  }

  if (u->cur_row + 1 < u->buf.n) u->cur_row++;
  u->cur_col_byte = 0;
  set_status(u, (u->yank_n <= 1) ? "pasted" : "pasted lines");
}

static void go_line_start(uim_t *u) {
  if (!u) return;
  u->cur_col_byte = 0;
}

static void go_line_end(uim_t *u) {
  if (!u) return;
  if (uim_buf_ensure_nonempty(&u->buf)) return;
  const char *s = u->buf.lines[u->cur_row].data;
  if (!s) s = "";
  u->cur_col_byte = strlen(s);
}

static void go_to_line_1based(uim_t *u, int line1) {
  if (!u) return;
  if (uim_buf_ensure_nonempty(&u->buf)) return;
  if (line1 < 1) line1 = 1;
  size_t row = (size_t)(line1 - 1);
  if (row >= u->buf.n) row = u->buf.n - 1;
  u->cur_row = row;
  u->cur_col_byte = 0;
}

static void go_to_last_line(uim_t *u) {
  if (!u) return;
  if (uim_buf_ensure_nonempty(&u->buf)) return;
  u->cur_row = (u->buf.n == 0) ? 0 : (u->buf.n - 1);
  u->cur_col_byte = 0;
}

static void prefix_reset(uim_t *u) {
  if (!u) return;
  u->num_prefix = 0;
}

static int prefix_take_or1(uim_t *u) {
  if (!u) return 1;
  int c = u->num_prefix;
  if (c <= 0) c = 1;
  u->num_prefix = 0;
  return c;
}

static void prefix_push_digit(uim_t *u, int d) {
  if (!u) return;
  if (d < 0) d = 0;
  if (d > 9) d = 9;
  long v = (long)u->num_prefix * 10L + (long)d;
  if (v > 9999) v = 9999;
  u->num_prefix = (int)v;
}

static void colon_reset(uim_t *u) {
  u->colon[0] = '\0';
  u->colon_len = 0;
}

static void search_reset(uim_t *u) {
  if (!u) return;
  u->search[0] = '\0';
  u->search_len = 0;
}

static void search_set_last(uim_t *u, const char *pat) {
  if (!u) return;
  if (!pat) pat = "";
  snprintf(u->last_search, sizeof(u->last_search), "%s", pat);
  u->last_search_len = strlen(u->last_search);
  u->has_last_search = (u->last_search_len > 0);
}

static int is_utf8_boundary(const char *s, const char *p) {
  if (!s || !p) return 0;
  if (p <= s) return 1;
  // UTF-8 character boundary means: p points to a lead byte (or NUL).
  // Checking p[-1] is wrong because a valid boundary can be preceded by a continuation byte.
  unsigned char cur = (unsigned char)p[0];
  return (cur & 0xC0) != 0x80;
}

static int search_forward(uim_t *u, const char *pat) {
  if (!u) return 1;
  if (!pat || pat[0] == '\0') return 1;
  if (uim_buf_ensure_nonempty(&u->buf)) return 1;

  size_t start_row = u->cur_row;
  size_t start_col = u->cur_col_byte;

  for (size_t row = start_row; row < u->buf.n; row++) {
    const char *s = u->buf.lines[row].data;
    if (!s) s = "";

    size_t start = 0;
    if (row == start_row) start = uim_utf8_next(s, start_col);
    const char *base = s + start;

    const char *p = strstr(base, pat);
    while (p) {
      if (is_utf8_boundary(s, p)) {
        u->cur_row = row;
        u->cur_col_byte = (size_t)(p - s);
        return 0;
      }
      p = strstr(p + 1, pat);
    }
  }

  return 1;
}

static void handle_search_enter(uim_t *u) {
  if (!u) return;
  u->mode = UIM_MODE_NORMAL;

  const char *pat = u->search;
  if (u->search_len == 0) {
    if (!u->has_last_search) {
      set_status(u, "no previous search");
      search_reset(u);
      return;
    }
    pat = u->last_search;
  }

  search_set_last(u, pat);
  if (search_forward(u, u->last_search) != 0) {
    set_status(u, "Pattern not found");
  } else {
    set_status(u, "found");
  }

  search_reset(u);
}

static void handle_colon_enter(uim_t *u) {
  u->mode = UIM_MODE_NORMAL;

  if (strcmp(u->colon, "q") == 0) {
    if (u->buf.modified) {
      set_status(u, "No write since last change (use :q!)");
      colon_reset(u);
      return;
    }
    u->running = 0;
    colon_reset(u);
    return;
  }

  if (strcmp(u->colon, "q!") == 0) {
    u->running = 0;
    colon_reset(u);
    return;
  }

  if (strcmp(u->colon, "w") == 0) {
    if (!u->path) {
      set_status(u, "no file");
      colon_reset(u);
      return;
    }
    if (uim_buf_save(&u->buf, u->path) != 0) {
      set_status(u, "write failed");
      colon_reset(u);
      return;
    }
    u->buf.modified = 0;
    set_status(u, "written");
    colon_reset(u);
    return;
  }

  if (strcmp(u->colon, "wq") == 0) {
    if (!u->path) {
      set_status(u, "no file");
      colon_reset(u);
      return;
    }
    if (uim_buf_save(&u->buf, u->path) != 0) {
      set_status(u, "write failed");
      colon_reset(u);
      return;
    }
    u->buf.modified = 0;
    u->running = 0;
    colon_reset(u);
    return;
  }

  set_status(u, "unknown command");
  colon_reset(u);
}

static void feed_insert_byte(uim_t *u, unsigned char b) {
  if (u->u8need == 0) {
    u->u8len = 0;
    u->u8need = uim_utf8_char_len(b);
  }

  if (u->u8len < (int)sizeof(u->u8buf)) {
    u->u8buf[u->u8len++] = b;
  }

  if (u->u8len >= u->u8need) {
    uim_buf_insert_bytes(&u->buf, u->cur_row, u->cur_col_byte, (const char *)u->u8buf, (size_t)u->u8need);
    u->cur_col_byte += (size_t)u->u8need;
    u->u8need = 0;
    u->u8len = 0;
  }
}

static void handle_key(uim_t *u, int k) {
  if (!u) return;

  if (u->mode == UIM_MODE_COLON) {
    if (k == UIM_KEY_ESC) {
      u->mode = UIM_MODE_NORMAL;
      colon_reset(u);
      return;
    }
    if (k == UIM_KEY_BACKSPACE) {
      if (u->colon_len > 0) {
        u->colon[--u->colon_len] = '\0';
      }
      return;
    }
    if (k == UIM_KEY_ENTER) {
      handle_colon_enter(u);
      return;
    }
    if (k >= 32 && k <= 126) {
      if (u->colon_len + 1 < sizeof(u->colon)) {
        u->colon[u->colon_len++] = (char)k;
        u->colon[u->colon_len] = '\0';
      }
      return;
    }
    return;
  }

  if (u->mode == UIM_MODE_SEARCH) {
    if (k == UIM_KEY_ESC) {
      u->mode = UIM_MODE_NORMAL;
      search_reset(u);
      return;
    }
    if (k == UIM_KEY_BACKSPACE) {
      if (u->search_len > 0) {
        u->search[--u->search_len] = '\0';
      }
      return;
    }
    if (k == UIM_KEY_ENTER) {
      handle_search_enter(u);
      return;
    }
    if (k >= 32 && k <= 255) {
      if (u->search_len + 1 < sizeof(u->search)) {
        u->search[u->search_len++] = (char)k;
        u->search[u->search_len] = '\0';
      }
      return;
    }
    return;
  }

  if (u->mode == UIM_MODE_INSERT) {
    if (k == UIM_KEY_ESC) {
      u->mode = UIM_MODE_NORMAL;
      u->pending = 0;
      u->u8need = 0;
      u->u8len = 0;
      return;
    }

    // Allow cursor movement in INSERT mode via arrow/home/end keys.
    if (k == UIM_KEY_ARROW_LEFT || k == UIM_KEY_ARROW_RIGHT || k == UIM_KEY_ARROW_UP || k == UIM_KEY_ARROW_DOWN ||
        k == UIM_KEY_HOME || k == UIM_KEY_END) {
      // Drop any partially assembled UTF-8 sequence.
      u->u8need = 0;
      u->u8len = 0;

      if (k == UIM_KEY_ARROW_LEFT) move_left(u);
      else if (k == UIM_KEY_ARROW_RIGHT) move_right(u);
      else if (k == UIM_KEY_ARROW_UP) move_up(u);
      else if (k == UIM_KEY_ARROW_DOWN) move_down(u);
      else if (k == UIM_KEY_HOME) go_line_start(u);
      else if (k == UIM_KEY_END) go_line_end(u);

      clamp_cursor(u);
      return;
    }

    if (k == UIM_KEY_BACKSPACE) {
      uim_buf_delete_prev_char(&u->buf, u->cur_row, &u->cur_col_byte);
      return;
    }
    if (k == UIM_KEY_ENTER) {
      uim_buf_split_line(&u->buf, u->cur_row, u->cur_col_byte);
      u->cur_row++;
      u->cur_col_byte = 0;
      return;
    }
    if (k >= 0 && k <= 255) {
      feed_insert_byte(u, (unsigned char)k);
    }
    return;
  }

  // NORMAL
  if (k == UIM_KEY_CTRL_C) {
    u->running = 0;
    return;
  }

  // Map arrows
  if (k == UIM_KEY_ARROW_LEFT) k = 'h';
  if (k == UIM_KEY_ARROW_RIGHT) k = 'l';
  if (k == UIM_KEY_ARROW_UP) k = 'k';
  if (k == UIM_KEY_ARROW_DOWN) k = 'j';

  // Numeric prefix (e.g. 3dd, 10G). In NORMAL, '0' is special when prefix is empty.
  if (k >= '0' && k <= '9') {
    int d = k - '0';
    if (d == 0 && u->num_prefix == 0 && u->pending == 0) {
      go_line_start(u);
      clamp_cursor(u);
      return;
    }
    prefix_push_digit(u, d);
    return;
  }

  if (u->pending == 'g') {
    if (k == 'g') {
      int line1 = prefix_take_or1(u);
      go_to_line_1based(u, line1);
      clamp_cursor(u);
    } else {
      prefix_reset(u);
    }
    u->pending = 0;
    return;
  }

  if (u->pending == 'd') {
    if (k == 'd') {
      int c = prefix_take_or1(u);
      delete_lines_cmd(u, u->cur_row, (size_t)c);
      clamp_cursor(u);
    } else {
      prefix_reset(u);
    }
    u->pending = 0;
    return;
  }
  if (u->pending == 'y') {
    if (k == 'y') {
      int c = prefix_take_or1(u);
      yank_lines_cmd(u, u->cur_row, (size_t)c);
    } else {
      prefix_reset(u);
    }
    u->pending = 0;
    return;
  }

  switch (k) {
    case 'h': move_left(u); break;
    case 'l': move_right(u); break;
    case 'k': move_up(u); break;
    case 'j': move_down(u); break;
    case UIM_KEY_HOME: go_line_start(u); prefix_reset(u); break;
    case UIM_KEY_END: go_line_end(u); prefix_reset(u); break;
    case '$': go_line_end(u); prefix_reset(u); break;
    case 'G':
      if (u->num_prefix > 0) go_to_line_1based(u, prefix_take_or1(u));
      else go_to_last_line(u);
      break;
    case 'g': u->pending = 'g'; break;
    case 'i': prefix_reset(u); u->mode = UIM_MODE_INSERT; break;
    case 'a': prefix_reset(u); enter_insert_after(u); break;
    case 'p': prefix_reset(u); paste_below(u); break;
    case 'd': u->pending = 'd'; break;
    case 'y': u->pending = 'y'; break;
    case 'n':
      prefix_reset(u);
      if (!u->has_last_search) {
        set_status(u, "no previous search");
        break;
      }
      if (search_forward(u, u->last_search) != 0) set_status(u, "Pattern not found");
      else set_status(u, "found");
      break;
    case ':':
      u->mode = UIM_MODE_COLON;
      colon_reset(u);
      prefix_reset(u);
      break;
    case '/':
      u->mode = UIM_MODE_SEARCH;
      search_reset(u);
      prefix_reset(u);
      u->pending = 0;
      break;
    default:
      prefix_reset(u);
      break;
  }

  clamp_cursor(u);
}

int uim_run_interactive(uim_t *u) {
  if (!u) return 1;
  u->running = 1;

  if (uim_term_enable_raw() != 0) return 1;
  atexit(uim_term_disable_raw);

  // Clear once on entry; subsequent renders overwrite the full visible area.
  uim_term_clear();
  uim_render(u);
  while (u->running) {
    int k = uim_term_read_key(STDIN_FILENO);
    if (k == UIM_KEY_NONE) continue;

    int steps = 0;
    while (1) {
      handle_key(u, k);
      steps++;
      if (!u->running) break;
      if (steps >= 4096) break;

      if (uim_term_pending_bytes(STDIN_FILENO) <= 0) break;
      k = uim_term_read_key(STDIN_FILENO);
      if (k == UIM_KEY_NONE) break;
    }

    uim_render(u);
  }

  uim_term_clear();
  uim_term_move_cursor(1, 1);
  uim_term_hide_cursor(0);
  uim_term_disable_raw();
  return 0;
}

int uim_run_batch(uim_t *u, int in_fd) {
  if (!u) return 1;
  u->running = 1;

  while (u->running) {
    unsigned char c;
    ssize_t r = read(in_fd, &c, 1);
    if (r == 0) break;
    if (r < 0) return 1;

    int k;
    if (c == 27) k = UIM_KEY_ESC;
    else if (c == '\n' || c == '\r') k = UIM_KEY_ENTER;
    else if (c == 127 || c == 8) k = UIM_KEY_BACKSPACE;
    else k = (int)c;

    handle_key(u, k);
  }

  return 0;
}
```

【実装用（貼り付け可）: src/main.c】
```c
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "uim.h"

#include "uim_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void uim_init(uim_t *u, const char *path) {
  memset(u, 0, sizeof(*u));
  u->mode = UIM_MODE_NORMAL;
  u->path = path;
  uim_buf_init(&u->buf);
  u->cur_row = 0;
  u->cur_col_byte = 0;
  u->row_off = 0;
  u->col_off = 0;
  u->yank_lines = NULL;
  u->yank_n = 0;
  u->colon[0] = '\0';
  u->colon_len = 0;
  u->search[0] = '\0';
  u->search_len = 0;
  u->last_search[0] = '\0';
  u->last_search_len = 0;
  u->has_last_search = 0;
  u->pending = 0;
  u->num_prefix = 0;
  u->u8len = 0;
  u->u8need = 0;
  u->running = 1;
}

static void uim_destroy(uim_t *u) {
  if (!u) return;
  if (u->yank_lines) {
    for (size_t i = 0; i < u->yank_n; i++) free(u->yank_lines[i]);
    free(u->yank_lines);
  }
  u->yank_lines = NULL;
  u->yank_n = 0;
  uim_buf_free(&u->buf);
}

int main(int argc, char **argv) {
  int batch = 0;
  const char *path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--batch") == 0) {
      batch = 1;
      continue;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      fprintf(stderr, "usage: uim [--batch] <file>\n");
      return 2;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "uim: unknown option\n");
      return 2;
    }
    path = argv[i];
  }

  if (path == NULL) {
    fprintf(stderr, "usage: uim [--batch] <file>\n");
    return 2;
  }

  uim_t u;
  uim_init(&u, path);

  if (uim_buf_load(&u.buf, path) != 0) {
    fprintf(stderr, "uim: open failed\n");
    uim_destroy(&u);
    return 1;
  }

  int r = batch ? uim_run_batch(&u, STDIN_FILENO) : uim_run_interactive(&u);
  uim_destroy(&u);
  return r;
}
```

## 5.3 tests

【実装用（貼り付け可）: tests/smoke_uim.sh】
```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT_DIR/uim}"

build_uim() {
  local out="$1"
  musl-gcc -std=c11 -O2 -g -static \
    -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
    -I"$ROOT_DIR/include" \
    "$ROOT_DIR"/src/*.c \
    -o "$out"
}

needs_build() {
  [[ -x "$BIN" ]] || return 0
  local f
  for f in "$ROOT_DIR"/include/*.h "$ROOT_DIR"/src/*.c "$ROOT_DIR"/tests/smoke_uim.sh; do
    [[ "$f" -nt "$BIN" ]] && return 0
  done
  return 1
}

if needs_build; then
  echo "[INFO] build: $BIN"
  build_uim "$BIN"
fi

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

assert_file_eq() {
  local name="$1"
  local path="$2"
  local exp="$3"
  local tmp
  tmp="/tmp/uim_exp_$$.txt"
  printf '%s' "$exp" >"$tmp"
  if ! cmp -s "$tmp" "$path"; then
    echo "[FAIL] $name" >&2
    echo "  expected (hexdump):" >&2
    hexdump -C "$tmp" | head -n 20 >&2 || true
    echo "  got (hexdump):" >&2
    hexdump -C "$path" | head -n 20 >&2 || true
    rm -f "$tmp"
    exit 1
  fi
  rm -f "$tmp"
  echo "[OK]   $name"
}

# Test 1: insert + Japanese + :wq
F1=/tmp/uim_t1.txt
rm -f "$F1"
# keys: i, text, Enter, '# 日本語コメント', Esc, :wq, Enter
printf 'iHello\n# 日本語コメント\033:wq\n' | "$BIN" --batch "$F1"
assert_file_eq "insert+japanese" "$F1" $'Hello\n# 日本語コメント\n'

# Test 2: dd/yy/p then :wq
F2=/tmp/uim_t2.txt
cat >"$F2" <<'EOF'
A
B
C
EOF
# keys:
#   j (to B)
#   dd (delete B)
#   k (to A)
#   yy (yank A)
#   p (paste below)
#   :wq Enter
printf 'jddkyyp\033:wq\n' | "$BIN" --batch "$F2"
assert_file_eq "dd+yy+p" "$F2" $'A\nA\nC\n'

# Test 3: 'a' (append) then insert then :wq
F3=/tmp/uim_t3.txt
cat >"$F3" <<'EOF'
Hlo
EOF
# keys:
#   a (enter insert after cursor)
#   el (make "Hello")
#   Esc, :wq Enter
printf 'ael\033:wq\n' | "$BIN" --batch "$F3"
assert_file_eq "a+insert" "$F3" $'Hello\n'

# Test 4: numeric prefix 3yy (yank 3 lines) + p (paste multiple lines)
F4=/tmp/uim_t4.txt
cat >"$F4" <<'EOF'
A
B
C
D
E
EOF
# keys:
#   jj     (to C)
#   3yy    (yank C,D,E)
#   kk     (back to A)
#   p      (paste below A)
#   :wq Enter
printf 'jj3yykkp\033:wq\n' | "$BIN" --batch "$F4"
assert_file_eq "3yy+p" "$F4" $'A\nC\nD\nE\nB\nC\nD\nE\n'

# Test 5: numeric prefix 3dd (delete 3 lines, also yanks) then paste them back
F5=/tmp/uim_t5.txt
cat >"$F5" <<'EOF'
A
B
C
D
E
EOF
# keys:
#   j      (to B)
#   3dd    (delete B,C,D)
#   k      (to A)
#   p      (paste B,C,D below A)
#   :wq Enter
printf 'j3ddkp\033:wq\n' | "$BIN" --batch "$F5"
assert_file_eq "3dd+p" "$F5" $'A\nB\nC\nD\nE\n'

# Test 6: line start (0) and line end ($)
F6=/tmp/uim_t6.txt
cat >"$F6" <<'EOF'
abc
EOF
# keys:
#   $ a X      (append X at end)
#   Esc 0 i Y  (insert Y at start)
#   Esc :wq Enter
printf '$aX\0330iY\033:wq\n' | "$BIN" --batch "$F6"
assert_file_eq "0+\$" "$F6" $'YabcX\n'

# Test 7: gg and 1G cursor movement
F7=/tmp/uim_t7.txt
cat >"$F7" <<'EOF'
A
B
C
EOF
# keys:
#   jggdd  (go to B, then gg to A, delete A)
#   :wq Enter
printf 'jggdd\033:wq\n' | "$BIN" --batch "$F7"
assert_file_eq "gg" "$F7" $'B\nC\n'

F8=/tmp/uim_t8.txt
cat >"$F8" <<'EOF'
A
B
C
EOF
# keys:
#   j1Gdd  (go to B, then 1G to line 1, delete A)
#   :wq Enter
printf 'j1Gdd\033:wq\n' | "$BIN" --batch "$F8"
assert_file_eq "1G" "$F8" $'B\nC\n'

# Test 9: forward search (/pattern) and repeat (n)
F9=/tmp/uim_t9.txt
cat >"$F9" <<'EOF'
A
TARGET one
TARGET two
Z
EOF
# keys:
#   /TARGET Enter   (jump to first TARGET)
#   0iX Esc $       (mark the first hit line, then move to end so next search goes forward)
#   n 0iY Esc       (jump to next TARGET, mark it)
#   :wq Enter
printf '/TARGET\n0iX\033$n0iY\033:wq\n' | "$BIN" --batch "$F9"
assert_file_eq "search+/+n" "$F9" $'A\nXTARGET one\nYTARGET two\nZ\n'

# Test 10: forward search with Japanese pattern (previously failed when preceded by Japanese)
F10=/tmp/uim_t10.txt
cat >"$F10" <<'EOF'
A
あ日本語CSS日本語 one
あ日本語CSS日本語 two
Z
EOF
# keys:
#   /日本語CSS日本語 Enter (jump to first hit; hit starts after a Japanese char)
#   0iX Esc $       (mark the first hit line, then move to end so next search goes forward)
#   n 0iY Esc       (jump to next hit, mark it)
#   :wq Enter
printf '/日本語CSS日本語\n0iX\033$n0iY\033:wq\n' | "$BIN" --batch "$F10"
assert_file_eq "search+japanese+/+n" "$F10" $'A\nXあ日本語CSS日本語 one\nYあ日本語CSS日本語 two\nZ\n'

echo "ALL OK"
```
