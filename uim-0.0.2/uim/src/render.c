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
