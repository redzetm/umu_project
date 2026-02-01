#include "ush_utils.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * stderr出力。
 * できるだけ「ush: ...」の形にそろえるため、他の関数から呼ばれる。
 */
void ush_eprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void ush_perrorf(const char *context)
{
    /* perror() は末尾に ": <strerror>\n" を付けてくれる */
    if (context == NULL) {
        context = "ush";
    }

    /*
     * perror は prefix に渡した文字列をそのまま出すので、
     * ここで "ush: ..." の形を組み立てる。
     */
    char buf[256];
    (void)snprintf(buf, sizeof(buf), "ush: %s", context);
    errno = errno; /* 読みやすさのため（意味はない） */
    perror(buf);
}

int ush_is_space_tab(char c)
{
    return (c == ' ' || c == '\t');
}

int ush_is_blank_line(const char *line)
{
    if (line == NULL) {
        return 1;
    }

    for (const char *p = line; *p != '\0'; p++) {
        if (*p == '\n') {
            continue;
        }
        if (!ush_is_space_tab(*p)) {
            return 0;
        }
    }
    return 1;
}

int ush_is_comment_line(const char *line)
{
    if (line == NULL) {
        return 0;
    }

    const char *p = line;
    while (*p != '\0' && (*p == ' ' || *p == '\t')) {
        p++;
    }

    return (*p == '#');
}

int ush_read_line(char **line_buf, size_t *cap)
{
    /*
     * getline は line_buf/cap を更新する。
     * 戻り値: 読み込んだ文字数（改行含む） / EOFで -1。
     */
    ssize_t n = getline(line_buf, cap, stdin);
    if (n < 0) {
        return 1; /* EOF */
    }
    return 0;
}
