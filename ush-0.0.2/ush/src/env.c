#include "ush_env.h"

#include <stdlib.h>

const char *ush_get_path_or_default(void) {
  const char *p = getenv("PATH");
  if (p == NULL || p[0] == '\0') return "/umu_bin:/sbin:/bin";
  return p;
}
