# ush-0.0.3-詳細設計書.md
UmuOS User Shell (ush) — 詳細設計書（0.0.3 / 拡張）  
Target OS: UmuOS-0.1.6-dev  

本書は [ush-0.0.3/docs/ush-0.0.3-基本設計書.md](ush-0.0.3/docs/ush-0.0.3-基本設計書.md) をコードレベルに落とし込む。

仕様の正は [ush-0.0.3/docs/ush-0.0.3-仕様書.md](ush-0.0.3/docs/ush-0.0.3-仕様書.md) とし、本書は実装手順・関数分割・データ構造・貼り付け可能コードを規定する。

---

# 0. この文書の読み方（コピペ区分）
- 本書中のコードブロックは、直前のラベルで用途を区分する。
  - 【実装用（貼り付け可）: <貼り付け先パス>】: そのまま貼り付けてビルドできることを想定する。
  - 【参考（擬似コード）】: 実装の流れ説明用。
  - 【説明】: 背景・意図・補足。
- パス表記は「`ush/` ディレクトリからの相対パス」を貼り付け先として示す。

---

# 1. 前提・設計原則
- 対話用シェル（POSIX/b*sh 互換は追わない）
- `/bin/sh`（BusyBox ash）は残し、`execve()` が `ENOEXEC` のときのみ `/bin/sh` にフォールバック
- 静的リンク（musl）前提で、単一バイナリ `/umu_bin/ush` を成果物とする
- 未対応構文は「検出してエラー」で誤動作を避ける
- リダイレクト `open()` 失敗時は **行を丸ごと不実行（1つもforkしない）**

---

# 2. ソース構成（ファイル/ディレクトリ）

【説明】

```
ush/
  include/
    ush.h
    ush_limits.h
    ush_err.h
    ush_utils.h
    ush_env.h
    ush_prompt.h
    ush_lineedit.h
    ush_tokenize.h
    ush_expand.h
    ush_parse.h
    ush_exec.h
    ush_builtins.h
  src/
    main.c
    utils.c
    env.c
    prompt.c
    lineedit.c
    tokenize.c
    expand.c
    parse.c
    exec.c
    builtins.c
```

【実装用（貼り付け可）】

```sh
cd /home/tama/umu_project/ush-0.0.3/

mkdir -p ush/include ush/src

touch \
  ush/include/ush.h \
  ush/include/ush_limits.h \
  ush/include/ush_err.h \
  ush/include/ush_utils.h \
  ush/include/ush_env.h \
  ush/include/ush_prompt.h \
  ush/include/ush_lineedit.h \
  ush/include/ush_tokenize.h \
  ush/include/ush_expand.h \
  ush/include/ush_parse.h \
  ush/include/ush_exec.h \
  ush/include/ush_builtins.h \
  ush/src/main.c \
  ush/src/utils.c \
  ush/src/env.c \
  ush/src/prompt.c \
  ush/src/lineedit.c \
  ush/src/tokenize.c \
  ush/src/expand.c \
  ush/src/parse.c \
  ush/src/exec.c \
  ush/src/builtins.c
```

---

# 3. ビルド手順（開発ホスト）

【実装用（貼り付け可）】

```sh
cd /home/tama/umu_project/ush-0.0.3/ush

musl-gcc -static -O2 -Wall -Wextra -Wshadow -Wpointer-arith -Wwrite-strings \
  -Iinclude \
  -o ush \
  src/main.c src/utils.c src/env.c src/prompt.c src/lineedit.c \
  src/tokenize.c src/expand.c src/parse.c src/exec.c src/builtins.c
```

---

# 4. 定数・制限値（ush/include/ush_limits.h）

【実装用（貼り付け可）: ush/include/ush_limits.h】

```c
#pragma once

enum {
  USH_MAX_LINE_LEN  = 8192,
  USH_MAX_ARGS      = 128,
  USH_MAX_TOKEN_LEN = 1024,

  // 0.0.3: パイプは 1 段まで（最大 2 コマンド）
  USH_MAX_CMDS      = 2,

  // list := pipeline ( (&&||) pipeline )*
  // 1行内に許す pipeline 数の上限（実装簡易化）
  USH_MAX_PIPES     = 64,

  // トークン配列上限（経験則）
  USH_MAX_TOKENS    = 256,

  // 簡易履歴
  USH_HISTORY_MAX   = 32,
};
```

注意:
- `argv` 配列サイズは `USH_MAX_ARGS + 1`（末尾NULL）

---

# 5. エラー/戻り値（ush/include/ush_err.h）

## 5.1 tokenize/parse 戻り値

【実装用（貼り付け可）: ush/include/ush_err.h】

```c
#pragma once

typedef enum {
  PARSE_OK = 0,
  PARSE_EMPTY,          // 空行/空白のみ/コメント行
  PARSE_TOO_LONG,       // 行長 or トークン長超過
  PARSE_TOO_MANY_TOKENS,
  PARSE_TOO_MANY_ARGS,  // argvが上限超過

  PARSE_UNSUPPORTED,    // 未対応構文を検出（"unsupported syntax"）
  PARSE_SYNTAX_ERROR,   // 構文エラー（"syntax error"）
} parse_result_t;
```

---

# 6. グローバル状態（ush/include/ush.h）

【実装用（貼り付け可）: ush/include/ush.h】

```c
#pragma once

typedef struct {
  int last_status;  // 初期値0
} ush_state_t;
```

---

# 7. utils（ush/include/ush_utils.h / ush/src/utils.c）

【実装用（貼り付け可）: ush/include/ush_utils.h】

```c
#pragma once
#include <stdarg.h>

void ush_eprintf(const char *fmt, ...);
void ush_perrorf(const char *context);  // ush: <context>: <strerror>

int ush_is_blank_line(const char *line);

int ush_is_space_ch(char c);

int ush_starts_with(const char *s, const char *prefix);

int ush_is_valid_name(const char *name);
int ush_is_assignment_word0(const char *s); // NAME=... の形式か（NAMEは正規）
```

【実装用（貼り付け可）: ush/src/utils.c】

```c
#include "ush_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ush_eprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("ush: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

void ush_perrorf(const char *context) {
  int e = errno;
  if (context == NULL) context = "error";
  fprintf(stderr, "ush: %s: %s\n", context, strerror(e));
}

int ush_is_space_ch(char c) {
  return c == ' ' || c == '\t';
}

int ush_is_blank_line(const char *line) {
  if (line == NULL) return 1;
  for (const char *p = line; *p; p++) {
    if (!ush_is_space_ch(*p)) return 0;
  }
  return 1;
}

int ush_starts_with(const char *s, const char *prefix) {
  if (s == NULL || prefix == NULL) return 0;
  size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

int ush_is_valid_name(const char *name) {
  if (name == NULL || name[0] == '\0') return 0;
  if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return 0;
  for (const char *p = name + 1; *p; p++) {
    if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
  }
  return 1;
}

int ush_is_assignment_word0(const char *s) {
  if (s == NULL) return 0;
  const char *eq = strchr(s, '=');
  if (eq == NULL) return 0;

  // NAME= の NAME 部分が正規
  if (eq == s) return 0;

  char name[256];
  size_t n = (size_t)(eq - s);
  if (n >= sizeof(name)) return 0;
  memcpy(name, s, n);
  name[n] = '\0';

  return ush_is_valid_name(name);
}
```

---

# 8. env（ush/include/ush_env.h / ush/src/env.c）

【実装用（貼り付け可）: ush/include/ush_env.h】

```c
#pragma once

const char *ush_get_path_or_default(void);
```

【実装用（貼り付け可）: ush/src/env.c】

```c
#include "ush_env.h"

#include <stdlib.h>

const char *ush_get_path_or_default(void) {
  const char *p = getenv("PATH");
  if (p == NULL || p[0] == '\0') return "/umu_bin:/sbin:/bin";
  return p;
}
```

---

# 9. prompt（ush/include/ush_prompt.h / ush/src/prompt.c）

【実装用（貼り付け可）: ush/include/ush_prompt.h】

```c
#pragma once
#include <stddef.h>

// out は NUL 終端される
int ush_prompt_render(char *out, size_t out_cap);
```

【実装用（貼り付け可）: ush/src/prompt.c】

```c
#include "ush_prompt.h"

#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static const char *get_ps1_raw(void) {
  const char *p = getenv("USH_PS1");
  if (p != NULL && p[0] != '\0') return p;
  p = getenv("PS1");
  if (p != NULL && p[0] != '\0') return p;
  return "\\u@UmuOS:ush:\\w\\$ ";
}

static const char *get_username(void) {
  const char *u = getenv("USER");
  if (u != NULL && u[0] != '\0') return u;
  struct passwd *pw = getpwuid(getuid());
  if (pw != NULL && pw->pw_name != NULL) return pw->pw_name;
  return "user";
}

static int render_cwd(char *out, size_t cap) {
  if (out == NULL || cap == 0) return 1;
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    snprintf(out, cap, "?");
    return 1;
  }

  const char *home = getenv("HOME");
  if (home != NULL && home[0] != '\0') {
    size_t home_len = strlen(home);
    if (strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
      // HOME配下を ~ 省略
      if (cwd[home_len] == '\0') {
        snprintf(out, cap, "~");
      } else {
        snprintf(out, cap, "~%s", cwd + home_len);
      }
      return 0;
    }
  }

  snprintf(out, cap, "%s", cwd);
  return 0;
}

int ush_prompt_render(char *out, size_t out_cap) {
  if (out == NULL || out_cap == 0) return 1;

  const char *in = get_ps1_raw();
  size_t oi = 0;

  for (size_t i = 0; in[i] != '\0'; i++) {
    if (oi + 1 >= out_cap) break;

    if (in[i] != '\\') {
      out[oi++] = in[i];
      continue;
    }

    char n = in[i + 1];
    if (n == '\0') {
      out[oi++] = '\\';
      break;
    }

    if (n == 'u') {
      const char *u = get_username();
      size_t len = strlen(u);
      if (oi + len >= out_cap) len = out_cap - oi - 1;
      memcpy(out + oi, u, len);
      oi += len;
      i++;
      continue;
    }

    if (n == 'w') {
      char w[1024];
      render_cwd(w, sizeof(w));
      size_t len = strlen(w);
      if (oi + len >= out_cap) len = out_cap - oi - 1;
      memcpy(out + oi, w, len);
      oi += len;
      i++;
      continue;
    }

    if (n == '$') {
      out[oi++] = (geteuid() == 0) ? '#' : '$';
      i++;
      continue;
    }

    if (n == '\\') {
      out[oi++] = '\\';
      i++;
      continue;
    }

    // 未対応はそのまま
    out[oi++] = '\\';
  }

  out[oi] = '\0';
  return 0;
}
```

---

# 10. line editor（ush/include/ush_lineedit.h / ush/src/lineedit.c）

【実装用（貼り付け可）: ush/include/ush_lineedit.h】

```c
#pragma once
#include <stddef.h>
#include "ush_limits.h"

typedef struct {
  char items[USH_HISTORY_MAX][USH_MAX_LINE_LEN + 1];
  int count;
  int cursor;   // 履歴参照位置（0..count）
} ush_history_t;

int ush_lineedit_readline(
  const char *prompt,
  char *out_line,
  size_t out_cap,
  ush_history_t *hist
);
```

【実装用（貼り付け可）: ush/src/lineedit.c】

```c
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
  // 出力の改行を CRLF にする（OPOSTを殺すと '\n' が復帰せず、表示が右にずれる）
  t.c_oflag |= (tcflag_t)(OPOST | ONLCR);
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
```

---

# 11. tokenize（ush/include/ush_tokenize.h / ush/src/tokenize.c）

## 11.1 トークン型

【実装用（貼り付け可）: ush/include/ush_tokenize.h】

```c
#pragma once
#include <stddef.h>
#include "ush_err.h"
#include "ush_limits.h"

typedef enum {
  TOK_WORD = 0,
  TOK_PIPE,
  TOK_AND,
  TOK_OR,
  TOK_REDIR_IN,
  TOK_REDIR_OUT,
  TOK_REDIR_APPEND,
} token_kind_t;

typedef enum {
  QUOTE_NONE = 0,
  QUOTE_SINGLE,
  QUOTE_DOUBLE,
} quote_kind_t;

typedef struct {
  token_kind_t kind;
  quote_kind_t quote; // TOK_WORD のみ
  const char *text;   // TOK_WORD のみ有効（トークン文字列）
} token_t;

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
);
```

## 11.2 方針（仕様準拠）
- 空白区切り
- 演算子は空白の有無に関わらず独立トークン（`|` `&&` `||` `<` `>` `>>`）
- コメント: 未クォートで「トークン先頭の `#`」を見たら以降を無視
- クォート:
  - `'...'` は展開なし
  - `"..."` は「変数展開のみ」（展開は expand で行うため、この段階では quote 種別だけ付与）
  - 未閉鎖は `PARSE_SYNTAX_ERROR`
- 未対応（検出してエラー）:
  - `;` `(` `)` `{` `}`
  - `&` 単体（`&&` は対応）
  - グロブ（`* ? [ ]`）は未クォートで検出したら `PARSE_UNSUPPORTED`

【実装用（貼り付け可）: ush/src/tokenize.c】

```c
#include "ush_tokenize.h"

#include "ush_utils.h"

#include <string.h>

static int is_glob_char(char c) {
  return c == '*' || c == '?' || c == '[' || c == ']';
}

static int push_tok(token_t out[], int *io_n, token_kind_t k, quote_kind_t q, const char *t) {
  if (*io_n >= USH_MAX_TOKENS) return 1;
  out[*io_n].kind = k;
  out[*io_n].quote = q;
  out[*io_n].text = t;
  (*io_n)++;
  return 0;
}

parse_result_t ush_tokenize(
  const char *line,
  token_t out_tokens[USH_MAX_TOKENS],
  int *out_ntok,
  char out_buf[USH_MAX_LINE_LEN + 1]
) {
  if (out_ntok) *out_ntok = 0;
  if (line == NULL) return PARSE_EMPTY;

  size_t len = strlen(line);
  if (len > USH_MAX_LINE_LEN) return PARSE_TOO_LONG;

  int ntok = 0;
  size_t bi = 0;

  size_t i = 0;
  while (i < len) {
    // skip spaces
    while (i < len && ush_is_space_ch(line[i])) i++;
    if (i >= len) break;

    // token-start comment
    if (line[i] == '#') {
      break;
    }

    // operators
    if (line[i] == '&') {
      if (i + 1 < len && line[i + 1] == '&') {
        if (push_tok(out_tokens, &ntok, TOK_AND, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      return PARSE_UNSUPPORTED;
    }

    if (line[i] == '|') {
      if (i + 1 < len && line[i + 1] == '|') {
        if (push_tok(out_tokens, &ntok, TOK_OR, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      if (push_tok(out_tokens, &ntok, TOK_PIPE, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    if (line[i] == '<') {
      // ヒアドキュメント（<< / <<<）は未対応
      if (i + 1 < len && line[i + 1] == '<') {
        return PARSE_UNSUPPORTED;
      }
      if (push_tok(out_tokens, &ntok, TOK_REDIR_IN, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    if (line[i] == '>') {
      if (i + 1 < len && line[i + 1] == '>') {
        if (push_tok(out_tokens, &ntok, TOK_REDIR_APPEND, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
        i += 2;
        continue;
      }
      if (push_tok(out_tokens, &ntok, TOK_REDIR_OUT, QUOTE_NONE, NULL)) return PARSE_TOO_MANY_TOKENS;
      i += 1;
      continue;
    }

    // unsupported single-char tokens (always)
    if (line[i] == ';' || line[i] == '(' || line[i] == ')' || line[i] == '{' || line[i] == '}') {
      return PARSE_UNSUPPORTED;
    }

    // WORD
    quote_kind_t q = QUOTE_NONE;

    if (line[i] == '\'') {
      q = QUOTE_SINGLE;
      i++;
      size_t start = bi;
      while (i < len && line[i] != '\'') {
        if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = line[i++];
      }
      if (i >= len) return PARSE_SYNTAX_ERROR;
      // consume closing '
      i++;

      // token must end here
      if (i < len && !ush_is_space_ch(line[i]) && line[i] != '#' &&
          line[i] != '|' && line[i] != '&' && line[i] != '<' && line[i] != '>') {
        return PARSE_SYNTAX_ERROR;
      }

      out_buf[bi++] = '\0';
      if (push_tok(out_tokens, &ntok, TOK_WORD, q, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;
      continue;
    }

    if (line[i] == '"') {
      q = QUOTE_DOUBLE;
      i++;
      size_t start = bi;
      while (i < len && line[i] != '"') {
        if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
        if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
        out_buf[bi++] = line[i++];
      }
      if (i >= len) return PARSE_SYNTAX_ERROR;
      i++;

      if (i < len && !ush_is_space_ch(line[i]) && line[i] != '#' &&
          line[i] != '|' && line[i] != '&' && line[i] != '<' && line[i] != '>') {
        return PARSE_SYNTAX_ERROR;
      }

      out_buf[bi++] = '\0';
      if (push_tok(out_tokens, &ntok, TOK_WORD, q, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;
      continue;
    }

    // unquoted word: read until space/operator/comment-start
    size_t start = bi;
    while (i < len) {
      char c = line[i];
      if (ush_is_space_ch(c)) break;
      // トークン途中の '#' は文字として扱う（コメント開始は「トークン先頭の #」のみ）

      // stop before operator
      if (c == '|' || c == '&' || c == '<' || c == '>') break;

      if (c == '\'' || c == '"') return PARSE_SYNTAX_ERROR;
      if (c == ';' || c == '(' || c == ')' || c == '{' || c == '}') return PARSE_UNSUPPORTED;
      if (is_glob_char(c)) {
        if (!(c == '?' && i > 0 && line[i - 1] == '$')) return PARSE_UNSUPPORTED;
      }

      if (bi + 1 >= (size_t)(USH_MAX_LINE_LEN + 1)) return PARSE_TOO_LONG;
      if ((bi - start) + 1 > (size_t)USH_MAX_TOKEN_LEN) return PARSE_TOO_LONG;
      out_buf[bi++] = c;
      i++;
    }

    if (bi == start) {
      return PARSE_SYNTAX_ERROR;
    }

    out_buf[bi++] = '\0';
    if (push_tok(out_tokens, &ntok, TOK_WORD, QUOTE_NONE, &out_buf[start])) return PARSE_TOO_MANY_TOKENS;

    // token-start comment: if next is '#', stop
    while (i < len && ush_is_space_ch(line[i])) i++;
    if (i < len && line[i] == '#') break;
  }

  if (out_ntok) *out_ntok = ntok;
  return (ntok == 0) ? PARSE_EMPTY : PARSE_OK;
}
```

---

# 12. expand（ush/include/ush_expand.h / ush/src/expand.c）

【実装用（貼り付け可）: ush/include/ush_expand.h】

```c
#pragma once
#include <stddef.h>

#include "ush_err.h"
#include "ush_tokenize.h"

typedef struct {
  int last_status;
} ush_expand_ctx_t;

// out は NUL 終端される。
// 失敗時: PARSE_UNSUPPORTED または PARSE_TOO_LONG を返す。
parse_result_t ush_expand_word(
  const ush_expand_ctx_t *ctx,
  quote_kind_t quote,
  const char *in,
  char *out,
  size_t out_cap
);
```

【実装用（貼り付け可）: ush/src/expand.c】

```c
#include "ush_expand.h"

#include "ush_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int append_str(char *out, size_t cap, size_t *io_len, const char *s) {
  if (out == NULL || io_len == NULL || s == NULL) return 1;
  size_t sl = strlen(s);
  if (*io_len + sl + 1 > cap) return 1;
  memcpy(out + *io_len, s, sl);
  *io_len += sl;
  out[*io_len] = '\0';
  return 0;
}

static int append_ch(char *out, size_t cap, size_t *io_len, char c) {
  if (out == NULL || io_len == NULL) return 1;
  if (*io_len + 2 > cap) return 1;
  out[*io_len] = c;
  (*io_len)++;
  out[*io_len] = '\0';
  return 0;
}

static parse_result_t expand_var(const ush_expand_ctx_t *ctx, const char *p, size_t *io_i, char *out, size_t cap, size_t *io_len) {
  // p[*io_i] == '$'
  size_t i = *io_i;
  char n = p[i + 1];
  if (n == '\0') {
    if (append_ch(out, cap, io_len, '$')) return PARSE_TOO_LONG;
    *io_i = i + 1;
    return PARSE_OK;
  }

  if (n == '?') {
    char tmp[32];
    int st = (ctx != NULL) ? ctx->last_status : 0;
    snprintf(tmp, sizeof(tmp), "%d", st);
    if (append_str(out, cap, io_len, tmp)) return PARSE_TOO_LONG;
    *io_i = i + 2;
    return PARSE_OK;
  }

  if (n == '{' || n == '(' || (n >= '0' && n <= '9')) {
    return PARSE_UNSUPPORTED;
  }

  if (n == '`') {
    return PARSE_UNSUPPORTED;
  }

  if (isalpha((unsigned char)n) || n == '_') {
    size_t j = i + 1;
    char name[256];
    size_t ni = 0;
    while (p[j] != '\0' && (isalnum((unsigned char)p[j]) || p[j] == '_')) {
      if (ni + 1 < sizeof(name)) name[ni++] = p[j];
      j++;
    }
    name[ni] = '\0';

    const char *v = getenv(name);
    if (v == NULL) v = "";
    if (append_str(out, cap, io_len, v)) return PARSE_TOO_LONG;

    *io_i = j;
    return PARSE_OK;
  }

  // それ以外は '$' を文字として扱う
  if (append_ch(out, cap, io_len, '$')) return PARSE_TOO_LONG;
  *io_i = i + 1;
  return PARSE_OK;
}

parse_result_t ush_expand_word(
  const ush_expand_ctx_t *ctx,
  quote_kind_t quote,
  const char *in,
  char *out,
  size_t out_cap
) {
  if (out == NULL || out_cap == 0) return PARSE_TOO_LONG;
  out[0] = '\0';
  if (in == NULL) return PARSE_OK;

  // tilde expansion（未クォートのみ）
  // 変数展開と併用され得るため、ここでは一旦 in を差し替えて後段の変数展開ループへ流す。
  char tilde_buf[USH_MAX_TOKEN_LEN + 1];
  const char *src = in;

  if (quote == QUOTE_NONE && in[0] == '~') {
    if (in[1] == '\0' || in[1] == '/') {
      const char *home = getenv("HOME");
      if (home == NULL || home[0] == '\0') home = "/";

      size_t ti = 0;
      if (append_str(tilde_buf, sizeof(tilde_buf), &ti, home)) return PARSE_TOO_LONG;

      if (in[1] == '/') {
        // HOME が "/" のときに "//" を作らない
        const char *rest = in + 1;
        if (ti > 0 && tilde_buf[ti - 1] == '/' && rest[0] == '/') rest++;
        if (append_str(tilde_buf, sizeof(tilde_buf), &ti, rest)) return PARSE_TOO_LONG;
      }

      src = tilde_buf;
    } else {
      // ~user は未対応
      return PARSE_UNSUPPORTED;
    }
  }

  size_t olen = 0;

  // 変数展開（未クォート/ダブルクォート）
  if (quote == QUOTE_SINGLE) {
    if (append_str(out, out_cap, &olen, in)) return PARSE_TOO_LONG;
    return PARSE_OK;
  }

  for (size_t i = 0; src[i] != '\0';) {
    if (src[i] == '$') {
      parse_result_t r = expand_var(ctx, src, &i, out, out_cap, &olen);
      if (r != PARSE_OK) return r;
      continue;
    }

    if (src[i] == '`') {
      return PARSE_UNSUPPORTED;
    }

    if (append_ch(out, out_cap, &olen, src[i])) return PARSE_TOO_LONG;
    i++;
  }

  return PARSE_OK;
}
```

---

# 13. parse（ush/include/ush_parse.h / ush/src/parse.c）

## 13.1 AST と内部表現

【実装用（貼り付け可）: ush/include/ush_parse.h】

```c
#pragma once
#include "ush_err.h"
#include "ush_limits.h"
#include "ush_tokenize.h"

typedef struct {
  const char *argv_raw[USH_MAX_ARGS + 1];
  quote_kind_t argv_quote[USH_MAX_ARGS];
  int argc;

  const char *in_path_raw;   // < file
  quote_kind_t in_quote;

  const char *out_path_raw;  // > file, >> file
  quote_kind_t out_quote;
  int out_append;        // 0:>, 1:>>
} ush_cmd_t;

typedef struct {
  ush_cmd_t left;
  int has_right;
  ush_cmd_t right;
} ush_pipeline_t;

typedef enum {
  NODE_PIPELINE = 0,
  NODE_AND,
  NODE_OR,
} node_kind_t;

typedef struct ush_node {
  node_kind_t kind;
  int left;   // index (NODE_AND/OR)
  int right;  // index (NODE_AND/OR)
  ush_pipeline_t pl; // NODE_PIPELINE
} ush_node_t;

typedef struct {
  ush_node_t nodes[USH_MAX_PIPES];
  int n;
} ush_ast_t;

parse_result_t ush_parse_line(
  const token_t *toks,
  int ntok,
  ush_ast_t *out_ast,
  int *out_root
);
```

## 13.2 パース方針（仕様準拠）
- 文法（簡易）
  - `list := pipeline ( (&& || ||) pipeline )*`
  - `pipeline := command ( '|' command )?`
  - `command := words redirects?`
  - `redirects := ("<" WORD)? ((">"|">>") WORD)?`
- リダイレクトは words の後ろにまとめて書く
- builtins の有無は parse では確定しない（exec 側で判定）
- 先頭 `NAME=...` 形式（環境代入）は `PARSE_UNSUPPORTED`

【実装用（貼り付け可）: ush/src/parse.c】

```c
#include "ush_parse.h"
#include "ush_utils.h"

#include <stddef.h>
#include <string.h>

static void init_cmd(ush_cmd_t *c) {
  memset(c, 0, sizeof(*c));
  c->argc = 0;
  c->argv_raw[0] = NULL;
  c->in_path_raw = NULL;
  c->out_path_raw = NULL;
  c->in_quote = QUOTE_NONE;
  c->out_quote = QUOTE_NONE;
  c->out_append = 0;
}

static parse_result_t add_arg(ush_cmd_t *c, const char *s, quote_kind_t q) {
  if (c->argc >= USH_MAX_ARGS) return PARSE_TOO_MANY_ARGS;
  c->argv_raw[c->argc] = s;
  c->argv_quote[c->argc] = q;
  c->argc++;
  c->argv_raw[c->argc] = NULL;
  return PARSE_OK;
}

static int new_node(ush_ast_t *ast, node_kind_t k) {
  if (ast->n >= USH_MAX_PIPES) return -1;
  int idx = ast->n++;
  memset(&ast->nodes[idx], 0, sizeof(ast->nodes[idx]));
  ast->nodes[idx].kind = k;
  ast->nodes[idx].left = -1;
  ast->nodes[idx].right = -1;
  return idx;
}

static parse_result_t parse_command(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_cmd_t *out_cmd,
  int allow_redirects
) {
  init_cmd(out_cmd);

  int i = *io_i;
  if (i >= ntok) return PARSE_SYNTAX_ERROR;

  // words
  while (i < ntok && toks[i].kind == TOK_WORD) {
    parse_result_t ar = add_arg(out_cmd, toks[i].text, toks[i].quote);
    if (ar != PARSE_OK) return ar;
    i++;
  }

  if (out_cmd->argc == 0) return PARSE_SYNTAX_ERROR;

  if (!allow_redirects) {
    *io_i = i;
    return PARSE_OK;
  }

  // redirects? (must be at end)
  int seen_in = 0;
  int seen_out = 0;

  while (i < ntok) {
    token_kind_t k = toks[i].kind;

    if (k != TOK_REDIR_IN && k != TOK_REDIR_OUT && k != TOK_REDIR_APPEND) break;

    if (i + 1 >= ntok || toks[i + 1].kind != TOK_WORD) return PARSE_SYNTAX_ERROR;

    const char *path = toks[i + 1].text;
    quote_kind_t pq = toks[i + 1].quote;

    if (k == TOK_REDIR_IN) {
      if (seen_in) return PARSE_SYNTAX_ERROR;
      if (seen_out) return PARSE_SYNTAX_ERROR; // < は > より前のみ（仕様簡易文法）
      out_cmd->in_path_raw = path;
      out_cmd->in_quote = pq;
      seen_in = 1;
    } else {
      if (seen_out) return PARSE_SYNTAX_ERROR;
      out_cmd->out_path_raw = path;
      out_cmd->out_quote = pq;
      out_cmd->out_append = (k == TOK_REDIR_APPEND);
      seen_out = 1;
    }

    i += 2;

    // redirects must be last elements of command
    if (i < ntok && toks[i].kind == TOK_WORD) return PARSE_SYNTAX_ERROR;
  }

  *io_i = i;
  return PARSE_OK;
}

static parse_result_t parse_pipeline(
  const token_t *toks,
  int ntok,
  int *io_i,
  ush_pipeline_t *out_pl,
  int allow_redirects
) {
  out_pl->has_right = 0;
  init_cmd(&out_pl->left);
  init_cmd(&out_pl->right);

  parse_result_t r = parse_command(toks, ntok, io_i, &out_pl->left, allow_redirects);
  if (r != PARSE_OK) return r;

  if (*io_i < ntok && toks[*io_i].kind == TOK_PIPE) {
    (*io_i)++;
    out_pl->has_right = 1;

    r = parse_command(toks, ntok, io_i, &out_pl->right, allow_redirects);
    if (r != PARSE_OK) return r;

    // 1段パイプのみ
    if (*io_i < ntok && toks[*io_i].kind == TOK_PIPE) return PARSE_SYNTAX_ERROR;

    // 制約: < は左のみ、> は右のみ
    if (out_pl->right.in_path_raw != NULL) return PARSE_SYNTAX_ERROR;
    if (out_pl->left.out_path_raw != NULL) return PARSE_SYNTAX_ERROR;
  }

  return PARSE_OK;
}

parse_result_t ush_parse_line(
  const token_t *toks,
  int ntok,
  ush_ast_t *out_ast,
  int *out_root
) {
  if (out_ast == NULL || out_root == NULL) return PARSE_SYNTAX_ERROR;
  out_ast->n = 0;
  *out_root = -1;

  if (ntok <= 0) return PARSE_EMPTY;

  int i = 0;

  // 1つ目 pipeline
  ush_pipeline_t pl;

  parse_result_t r = parse_pipeline(toks, ntok, &i, &pl, 1);
  if (r != PARSE_OK) return r;

  int left_idx = new_node(out_ast, NODE_PIPELINE);
  if (left_idx < 0) return PARSE_TOO_MANY_TOKENS;
  out_ast->nodes[left_idx].pl = pl;

  while (i < ntok) {
    token_kind_t op = toks[i].kind;
    if (op != TOK_AND && op != TOK_OR) return PARSE_SYNTAX_ERROR;
    i++;

    r = parse_pipeline(toks, ntok, &i, &pl, 1);
    if (r != PARSE_OK) return r;

    int right_idx = new_node(out_ast, NODE_PIPELINE);
    if (right_idx < 0) return PARSE_TOO_MANY_TOKENS;
    out_ast->nodes[right_idx].pl = pl;

    int parent = new_node(out_ast, (op == TOK_AND) ? NODE_AND : NODE_OR);
    if (parent < 0) return PARSE_TOO_MANY_TOKENS;

    out_ast->nodes[parent].left = left_idx;
    out_ast->nodes[parent].right = right_idx;

    left_idx = parent;
  }

  *out_root = left_idx;
  return PARSE_OK;
}
```

---

# 14. builtins（ush/include/ush_builtins.h / ush/src/builtins.c）

【実装用（貼り付け可）: ush/include/ush_builtins.h】

```c
#pragma once
#include "ush.h"

int ush_is_builtin(const char *cmd);
int ush_run_builtin(ush_state_t *st, char *argv[]);
```

【実装用（貼り付け可）: ush/src/builtins.c】

```c
#include "ush_builtins.h"

#include "ush_utils.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int builtin_cd(char *argv[]) {
  const char *dir = argv[1];
  if (dir == NULL) {
    dir = getenv("HOME");
    if (dir == NULL || dir[0] == '\0') dir = "/";
  }
  if (argv[2] != NULL) return 2;
  if (chdir(dir) != 0) {
    ush_perrorf("cd");
    return 1;
  }
  return 0;
}

static int builtin_pwd(char *argv[]) {
  if (argv[1] != NULL) return 2;
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    ush_perrorf("pwd");
    return 1;
  }
  puts(cwd);
  return 0;
}

static int builtin_export(char *argv[]) {
  // 0.0.3 仕様: export NAME=VALUE / export NAME
  if (argv[1] == NULL) return 2;
  if (argv[2] != NULL) return 2;

  const char *arg = argv[1];
  const char *eq = strchr(arg, '=');
  if (eq != NULL) {
    size_t n = (size_t)(eq - arg);
    char name[256];
    if (n == 0 || n >= sizeof(name)) return 2;
    memcpy(name, arg, n);
    name[n] = '\0';
    if (!ush_is_valid_name(name)) return 2;
    const char *val = eq + 1;
    if (setenv(name, val, 1) != 0) {
      ush_perrorf("export");
      return 1;
    }
    return 0;
  }

  if (!ush_is_valid_name(arg)) return 2;
  // NAME のみ: 未定義なら空文字で作成、定義済みなら維持
  const char *v = getenv(arg);
  if (v == NULL) {
    if (setenv(arg, "", 1) != 0) {
      ush_perrorf("export");
      return 1;
    }
  }
  return 0;
}

static int builtin_exit(ush_state_t *st, char *argv[]) {
  if (argv[1] == NULL) exit(st->last_status & 255);
  if (argv[2] != NULL) return 2;
  char *end = NULL;
  long v = strtol(argv[1], &end, 10);
  if (end == NULL || *end != '\0') return 2;
  exit((int)(v & 255));
}

static int builtin_help(void) {
  puts("ush 0.0.3 builtins:");
  puts("  cd [DIR]");
  puts("  pwd");
  puts("  export NAME=VALUE | export NAME");
  puts("  exit [N]");
  puts("  help");
  puts("");
  puts("operators: | (1 stage), &&, ||, <, >, >>");
  puts("notes: complex scripts => /bin/sh");
  return 0;
}

int ush_is_builtin(const char *cmd) {
  if (cmd == NULL) return 0;
  return strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 || strcmp(cmd, "export") == 0 ||
         strcmp(cmd, "exit") == 0 || strcmp(cmd, "help") == 0;
}

int ush_run_builtin(ush_state_t *st, char *argv[]) {
  if (argv == NULL || argv[0] == NULL) return 2;

  if (strcmp(argv[0], "cd") == 0) return builtin_cd(argv);
  if (strcmp(argv[0], "pwd") == 0) return builtin_pwd(argv);
  if (strcmp(argv[0], "export") == 0) return builtin_export(argv);
  if (strcmp(argv[0], "exit") == 0) return builtin_exit(st, argv);
  if (strcmp(argv[0], "help") == 0) return builtin_help();

  return 2;
}
```

---

# 15. exec（ush/include/ush_exec.h / ush/src/exec.c）

【実装用（貼り付け可）: ush/include/ush_exec.h】

```c
#pragma once
#include "ush.h"
#include "ush_parse.h"

int ush_exec_ast(ush_state_t *st, const ush_ast_t *ast, int root);
```

【実装用（貼り付け可）: ush/src/exec.c】

```c
#include "ush_exec.h"

#include "ush_builtins.h"
#include "ush_expand.h"
#include "ush_env.h"
#include "ush_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static void set_child_sigint_default(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
}

static int open_in(const char *path) {
  if (path == NULL) return -1;
  return open(path, O_RDONLY);
}

static int open_out(const char *path, int append) {
  if (path == NULL) return -1;
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  return open(path, flags, 0644);
}

static int resolve_cmd(const char *cmd, char out[1024], int *out_fail_status) {
  if (out_fail_status) *out_fail_status = 127;
  if (cmd == NULL || cmd[0] == '\0') {
    if (out_fail_status) *out_fail_status = 127;
    return 1;
  }

  if (strchr(cmd, '/') != NULL) {
    snprintf(out, 1024, "%s", cmd);
    if (access(out, X_OK) == 0) return 0;
    if (out_fail_status) *out_fail_status = (errno == EACCES) ? 126 : 127;
    return 1;
  }

  const char *path = ush_get_path_or_default();
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s", path);

  int saw_eacces = 0;

  for (char *save = NULL, *p = strtok_r(tmp, ":", &save); p != NULL; p = strtok_r(NULL, ":", &save)) {
    if (p[0] == '\0') continue;
    char cand[1024];
    snprintf(cand, sizeof(cand), "%s/%s", p, cmd);

    if (access(cand, X_OK) == 0) {
      snprintf(out, 1024, "%s", cand);
      return 0;
    }

    if (errno == EACCES) saw_eacces = 1;
  }

  if (out_fail_status) *out_fail_status = saw_eacces ? 126 : 127;
  return 1;
}

static void exec_with_sh_fallback(char *path, char *argv[]) {
  execve(path, argv, environ);
  if (errno == ENOEXEC) {
    // /bin/sh path args...
    // argv[0] を /bin/sh に差し替えた配列を作る
    int argc = 0;
    while (argv[argc] != NULL) argc++;

    char **nargv = (char **)calloc((size_t)argc + 2, sizeof(char *));
    if (nargv == NULL) _exit(126);

    static char sh0[] = "/bin/sh";
    nargv[0] = sh0;
    nargv[1] = path;
    for (int i = 1; i < argc; i++) nargv[i + 1] = argv[i];
    nargv[argc + 1] = NULL;

    execve("/bin/sh", nargv, environ);
  }
  _exit(126);
}

static int exec_external_cmd(char *argv[], int in_fd, int out_fd) {
  char path[1024];
  int fail = 127;
  if (resolve_cmd(argv[0], path, &fail) != 0) {
    return fail;
  }

  pid_t pid = fork();
  if (pid < 0) {
    ush_perrorf("fork");
    return 1;
  }

  if (pid == 0) {
    set_child_sigint_default();

    if (in_fd >= 0) {
      dup2(in_fd, STDIN_FILENO);
    }
    if (out_fd >= 0) {
      dup2(out_fd, STDOUT_FILENO);
    }

    // close inherited
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    exec_with_sh_fallback(path, argv);
    _exit(126);
  }

  int st = 0;
  if (waitpid(pid, &st, 0) < 0) {
    ush_perrorf("waitpid");
    return 1;
  }

  if (WIFEXITED(st)) return WEXITSTATUS(st);
  if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
  return 1;
}

static int expand_argv(
  const ush_state_t *st,
  const ush_cmd_t *cmd,
  char out_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1],
  char *out_argv[USH_MAX_ARGS + 1]
) {
  ush_expand_ctx_t xctx;
  xctx.last_status = (st != NULL) ? st->last_status : 0;

  for (int i = 0; i < cmd->argc; i++) {
    parse_result_t r = ush_expand_word(&xctx, cmd->argv_quote[i], cmd->argv_raw[i], out_words[i], sizeof(out_words[i]));
    if (r == PARSE_UNSUPPORTED) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    if (r != PARSE_OK) {
      ush_eprintf("syntax error");
      return 2;
    }
    out_argv[i] = out_words[i];
  }
  out_argv[cmd->argc] = NULL;

  if (cmd->argc >= 1 && ush_is_assignment_word0(out_argv[0])) {
    ush_eprintf("unsupported syntax");
    return 2;
  }

  return 0;
}

static int expand_redir_path(
  const ush_state_t *st,
  quote_kind_t q,
  const char *raw,
  char out[USH_MAX_TOKEN_LEN + 1]
) {
  if (raw == NULL) {
    out[0] = '\0';
    return 0;
  }

  ush_expand_ctx_t xctx;
  xctx.last_status = (st != NULL) ? st->last_status : 0;

  parse_result_t r = ush_expand_word(&xctx, q, raw, out, USH_MAX_TOKEN_LEN + 1);
  if (r == PARSE_UNSUPPORTED) {
    ush_eprintf("unsupported syntax");
    return 2;
  }
  if (r != PARSE_OK) {
    ush_eprintf("syntax error");
    return 2;
  }
  return 0;
}

static int exec_command(ush_state_t *st, const ush_cmd_t *cmd) {
  char words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *argv[USH_MAX_ARGS + 1];

  int er = expand_argv(st, cmd, words, argv);
  if (er != 0) return er;
  if (argv[0] == NULL) return 2;

  // builtins: パイプなし/リダイレクトなしのみ
  if (ush_is_builtin(argv[0])) {
    if (cmd->in_path_raw != NULL || cmd->out_path_raw != NULL) {
      ush_eprintf("unsupported syntax");
      return 2;
    }
    return ush_run_builtin(st, argv);
  }

  char in_path[USH_MAX_TOKEN_LEN + 1];
  char out_path[USH_MAX_TOKEN_LEN + 1];
  int xr;

  xr = expand_redir_path(st, cmd->in_quote, cmd->in_path_raw, in_path);
  if (xr != 0) return xr;
  xr = expand_redir_path(st, cmd->out_quote, cmd->out_path_raw, out_path);
  if (xr != 0) return xr;

  // redirect pre-open in parent
  int in_fd = -1;
  int out_fd = -1;

  if (cmd->in_path_raw != NULL) {
    in_fd = open_in(in_path);
    if (in_fd < 0) {
      ush_perrorf("open");
      return 1;
    }
  }

  if (cmd->out_path_raw != NULL) {
    out_fd = open_out(out_path, cmd->out_append);
    if (out_fd < 0) {
      if (in_fd >= 0) close(in_fd);
      ush_perrorf("open");
      return 1;
    }
  }

  int r = exec_external_cmd(argv, in_fd, out_fd);

  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  return r;
}

static int exec_pipeline(ush_state_t *st, const ush_pipeline_t *pl) {
  if (!pl->has_right) {
    return exec_command(st, &pl->left);
  }

  // Expand argv first (and detect builtins / env assignment) before any open/fork
  char l_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char r_words[USH_MAX_ARGS][USH_MAX_TOKEN_LEN + 1];
  char *l_argv[USH_MAX_ARGS + 1];
  char *r_argv[USH_MAX_ARGS + 1];

  int er;
  er = expand_argv(st, &pl->left, l_words, l_argv);
  if (er != 0) return er;
  er = expand_argv(st, &pl->right, r_words, r_argv);
  if (er != 0) return er;

  // builtins in pipe are unsupported
  if (ush_is_builtin(l_argv[0]) || ush_is_builtin(r_argv[0])) {
    ush_eprintf("unsupported syntax");
    return 2;
  }

  // redirect pre-open in parent (failure => no fork)
  int in_fd = -1;
  int out_fd = -1;

  char in_path[USH_MAX_TOKEN_LEN + 1];
  char out_path[USH_MAX_TOKEN_LEN + 1];
  int xr;

  xr = expand_redir_path(st, pl->left.in_quote, pl->left.in_path_raw, in_path);
  if (xr != 0) return xr;
  xr = expand_redir_path(st, pl->right.out_quote, pl->right.out_path_raw, out_path);
  if (xr != 0) return xr;

  if (pl->left.in_path_raw != NULL) {
    in_fd = open_in(in_path);
    if (in_fd < 0) {
      ush_perrorf("open");
      return 1;
    }
  }

  if (pl->right.out_path_raw != NULL) {
    out_fd = open_out(out_path, pl->right.out_append);
    if (out_fd < 0) {
      if (in_fd >= 0) close(in_fd);
      ush_perrorf("open");
      return 1;
    }
  }

  int pfd[2];
  if (pipe(pfd) != 0) {
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("pipe");
    return 1;
  }

  pid_t lp = fork();
  if (lp < 0) {
    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("fork");
    return 1;
  }

  if (lp == 0) {
    set_child_sigint_default();

    if (in_fd >= 0) dup2(in_fd, STDIN_FILENO);
    dup2(pfd[1], STDOUT_FILENO);

    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    // exec left
    char path[1024];
    int fail = 127;
    if (resolve_cmd(l_argv[0], path, &fail) != 0) _exit(fail);
    exec_with_sh_fallback(path, l_argv);
    _exit(126);
  }

  pid_t rp = fork();
  if (rp < 0) {
    // kill left? 0.0.3では簡易化して待って返す
    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    ush_perrorf("fork");
    return 1;
  }

  if (rp == 0) {
    set_child_sigint_default();

    dup2(pfd[0], STDIN_FILENO);
    if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);

    close(pfd[0]);
    close(pfd[1]);
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);

    char path[1024];
    int fail = 127;
    if (resolve_cmd(r_argv[0], path, &fail) != 0) _exit(fail);
    exec_with_sh_fallback(path, r_argv);
    _exit(126);
  }

  // parent
  close(pfd[0]);
  close(pfd[1]);
  if (in_fd >= 0) close(in_fd);
  if (out_fd >= 0) close(out_fd);

  int st_l = 0;
  int st_r = 0;
  waitpid(lp, &st_l, 0);
  waitpid(rp, &st_r, 0);

  if (WIFEXITED(st_r)) return WEXITSTATUS(st_r);
  if (WIFSIGNALED(st_r)) return 128 + WTERMSIG(st_r);
  return 1;
}

static int eval_node(ush_state_t *st, const ush_ast_t *ast, int idx) {
  const ush_node_t *n = &ast->nodes[idx];

  switch (n->kind) {
    case NODE_PIPELINE:
      st->last_status = exec_pipeline(st, &n->pl);
      return st->last_status;
    case NODE_AND: {
      int ls = eval_node(st, ast, n->left);
      // 右側の $?: 左側の結果が見える必要がある
      st->last_status = ls;
      if (ls == 0) return eval_node(st, ast, n->right);
      return ls;
    }
    case NODE_OR: {
      int ls = eval_node(st, ast, n->left);
      st->last_status = ls;
      if (ls != 0) return eval_node(st, ast, n->right);
      return ls;
    }
  }

  return 1;
}

int ush_exec_ast(ush_state_t *st, const ush_ast_t *ast, int root) {
  if (st == NULL || ast == NULL || root < 0) return 1;
  int r = eval_node(st, ast, root);
  st->last_status = r;
  return r;
}
```

---

# 16. main（ush/src/main.c）

## 16.1 仕様準拠ポイント
- EOF（Ctrl-D）: `exit(last_status)`
- tokenize/parse 失敗:
  - 未対応検出 → `unsupported syntax` / `last_status=2`
  - 構文エラー → `syntax error` / `last_status=2`
- script mode:
  - `#!` 行を 1 行目だけスキップ
  - エラーでも次行へ

【実装用（貼り付け可）: ush/src/main.c】

```c
#include "ush.h"

#include "ush_err.h"
#include "ush_exec.h"
#include "ush_lineedit.h"
#include "ush_prompt.h"
#include "ush_tokenize.h"
#include "ush_parse.h"
#include "ush_utils.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_parent_sigint_ignore(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
}

static void handle_parse_error(ush_state_t *st, parse_result_t r) {
  if (st == NULL) return;

  switch (r) {
    case PARSE_EMPTY:
      return;
    case PARSE_UNSUPPORTED:
      ush_eprintf("unsupported syntax");
      st->last_status = 2;
      return;
    case PARSE_SYNTAX_ERROR:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_TOO_LONG:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_TOO_MANY_TOKENS:
    case PARSE_TOO_MANY_ARGS:
      ush_eprintf("syntax error");
      st->last_status = 2;
      return;
    case PARSE_OK:
      return;
  }
}

static void eval_line(ush_state_t *st, const char *line) {
  token_t toks[USH_MAX_TOKENS];
  int ntok = 0;
  char tokbuf[USH_MAX_LINE_LEN + 1];

  parse_result_t tr = ush_tokenize(line, toks, &ntok, tokbuf);
  if (tr != PARSE_OK) {
    handle_parse_error(st, tr);
    return;
  }

  ush_ast_t ast;
  int root = -1;
  parse_result_t pr = ush_parse_line(toks, ntok, &ast, &root);
  if (pr != PARSE_OK) {
    handle_parse_error(st, pr);
    return;
  }

  ush_exec_ast(st, &ast, root);
}

static int run_interactive(ush_state_t *st) {
  ush_history_t hist;
  memset(&hist, 0, sizeof(hist));

  for (;;) {
    char prompt[256];
    ush_prompt_render(prompt, sizeof(prompt));

    char line[USH_MAX_LINE_LEN + 1];
    int r = ush_lineedit_readline(prompt, line, sizeof(line), &hist);
    if (r == 1) {
      exit(st->last_status & 255);
    }

    if (ush_is_blank_line(line)) continue;

    eval_line(st, line);
  }
}

static int run_script(ush_state_t *st, const char *path) {
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    ush_perrorf("open");
    st->last_status = 1;
    return 1;
  }

  char line[USH_MAX_LINE_LEN + 2];
  int lineno = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    lineno++;

    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

    if (lineno == 1 && ush_starts_with(line, "#!")) {
      continue;
    }

    if (ush_is_blank_line(line)) continue;

    // トークン先頭 # コメント行は tokenize で PARSE_EMPTY になるが、ここでも軽くスキップしてよい
    // （仕様上、空行/コメント行はスキップ）

    eval_line(st, line);
  }

  fclose(fp);
  return st->last_status;
}

int main(int argc, char **argv) {
  ush_state_t st;
  st.last_status = 0;

  set_parent_sigint_ignore();

  if (argc == 1) {
    return run_interactive(&st);
  }

  if (argc == 2) {
    return run_script(&st, argv[1]);
  }

  ush_eprintf("syntax error");
  return 2;
}
```

---

# 17. UmuOS への組み込み
- ビルドした `ush` バイナリを `/umu_bin/ush` に配置
- shebang は `#!/umu_bin/ush`
- `/bin/sh` は BusyBox のまま維持
