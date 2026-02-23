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

void uim_term_clear(void);
void uim_term_move_cursor(int r1, int c1);
void uim_term_hide_cursor(int hide);
void uim_term_flush(void);

#endif
