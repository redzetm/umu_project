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
