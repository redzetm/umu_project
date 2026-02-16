#include "ush_prompt.h"

#include <errno.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *ush_prompt_default(void) {
  return "\\u@UmuOS:ush:\\w\\$ ";
}

static const char *ush_get_prompt_template(void) {
  const char *p = getenv("USH_PS1");
  if (p != NULL && p[0] != '\0') return p;
  p = getenv("PS1");
  if (p != NULL && p[0] != '\0') return p;
  return ush_prompt_default();
}

static const char *ush_get_user(void) {
  const char *u = getenv("USER");
  if (u != NULL && u[0] != '\0') return u;

  struct passwd *pw = getpwuid(getuid());
  if (pw != NULL && pw->pw_name != NULL) return pw->pw_name;
  return "?";
}

static int ush_append(char *out, size_t cap, size_t *io_len, const char *s) {
  if (cap == 0) return 1;
  while (*s) {
    if (*io_len + 1 >= cap) {
      out[cap - 1] = '\0';
      return 1;
    }
    out[*io_len] = *s;
    (*io_len)++;
    s++;
  }
  out[*io_len] = '\0';
  return 0;
}

static int ush_append_ch(char *out, size_t cap, size_t *io_len, char ch) {
  if (cap == 0) return 1;
  if (*io_len + 1 >= cap) {
    out[cap - 1] = '\0';
    return 1;
  }
  out[*io_len] = ch;
  (*io_len)++;
  out[*io_len] = '\0';
  return 0;
}

static int ush_render_w(char *out, size_t cap, size_t *io_len) {
  char cwd[4096];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    return ush_append(out, cap, io_len, "?");
  }

  const char *home = getenv("HOME");
  if (home != NULL && home[0] != '\0') {
    size_t home_len = strlen(home);
    if (strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
      if (cwd[home_len] == '\0') {
        return ush_append(out, cap, io_len, "~");
      }
      int t = ush_append(out, cap, io_len, "~");
      if (t) return 1;
      return ush_append(out, cap, io_len, cwd + home_len);
    }
  }

  return ush_append(out, cap, io_len, cwd);
}

int ush_prompt_render(char *out, size_t out_cap) {
  if (out == NULL || out_cap == 0) return 1;

  const char *tmpl = ush_get_prompt_template();
  size_t len = 0;
  out[0] = '\0';

  int truncated = 0;
  for (size_t i = 0; tmpl[i] != '\0'; i++) {
    if (tmpl[i] != '\\') {
      truncated |= ush_append_ch(out, out_cap, &len, tmpl[i]);
      continue;
    }

    char next = tmpl[i + 1];
    if (next == '\0') {
      truncated |= ush_append_ch(out, out_cap, &len, '\\');
      break;
    }

    i++;
    switch (next) {
      case 'u':
        truncated |= ush_append(out, out_cap, &len, ush_get_user());
        break;
      case 'w':
        truncated |= ush_render_w(out, out_cap, &len);
        break;
      case '$':
        truncated |= ush_append_ch(out, out_cap, &len, (geteuid() == 0) ? '#' : '$');
        break;
      case '\\':
        truncated |= ush_append_ch(out, out_cap, &len, '\\');
        break;
      default:
        truncated |= ush_append_ch(out, out_cap, &len, '\\');
        truncated |= ush_append_ch(out, out_cap, &len, next);
        break;
    }
  }

  return truncated ? 1 : 0;
}
