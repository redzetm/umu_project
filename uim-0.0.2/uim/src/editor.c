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
