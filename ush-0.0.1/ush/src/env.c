#include "ush_env.h"

#include <stdlib.h>

const char *ush_get_path_or_default(void)
{
    const char *path = getenv("PATH");
    if (path == NULL) {
        return "/bin:/sbin";
    }
    return path;
}
