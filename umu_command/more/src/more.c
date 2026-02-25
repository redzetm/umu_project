#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int get_rows_fallback(void) {
    const char *lines = getenv("LINES");
    if (lines && *lines) {
        char *end = NULL;
        long v = strtol(lines, &end, 10);
        if (end && *end == '\0' && v > 0 && v < 1000) {
            return (int)v;
        }
    }
    return 24;
}

static int get_rows_from_tty(int tty_fd) {
#ifdef TIOCGWINSZ
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(tty_fd, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 0 && ws.ws_row < 1000) {
            return (int)ws.ws_row;
        }
    }
#endif
    (void)tty_fd;
    return get_rows_fallback();
}

static int enable_raw_mode(int fd, struct termios *old) {
    if (tcgetattr(fd, old) != 0) {
        return -1;
    }

    struct termios raw = *old;

    /* キー入力用: 1文字ずつ読む（canonical/echoを切る） */
    raw.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON);
    /* 入力変換は最小にする（CR/NL差は読み取ったキー側で吸収） */
    raw.c_iflag &= (tcflag_t)~(IXON);

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &raw) != 0) {
        return -1;
    }
    return 0;
}

static void restore_term(int fd, const struct termios *old) {
    if (old) {
        tcsetattr(fd, TCSAFLUSH, old);
    }
}

static void print_prompt(void) {
    fputs("(Enter:next line Space:next page Q:quit R:show the rest)", stdout);
    fflush(stdout);
}

static void clear_prompt_line(void) {
    /* 端末制御は最小。CRで先頭へ戻って空白で上書きする */
    fputc('\r', stdout);
    fputs("                                                            ", stdout);
    fputc('\r', stdout);
    fflush(stdout);
}

static int read_key(int tty_fd) {
    unsigned char c = 0;
    ssize_t r = read(tty_fd, &c, 1);
    if (r <= 0) {
        return -1;
    }
    return (int)c;
}

static int page_stream(FILE *in, int tty_fd) {
    int rows = get_rows_from_tty(tty_fd);
    int page_lines = rows > 2 ? (rows - 1) : 23;

    char buf[4096];
    int lines_printed = 0;
    int paging = 1;
    if (!isatty(STDOUT_FILENO) || !isatty(tty_fd)) {
        paging = 0;
    }

    while (fgets(buf, sizeof(buf), in) != NULL) {
        fputs(buf, stdout);
        lines_printed++;

        if (paging && lines_printed >= page_lines) {
            print_prompt();
            int key = read_key(tty_fd);
            clear_prompt_line();

            if (key < 0) {
                return 0;
            }

            if (key == 'q' || key == 'Q') {
                return 0;
            }
            if (key == 'r' || key == 'R') {
                paging = 0;
                continue;
            }

            /* Enter: CR or NL */
            if (key == '\r' || key == '\n') {
                lines_printed = page_lines - 1;
                continue;
            }

            /* Space: next page */
            if (key == ' ') {
                lines_printed = 0;
                continue;
            }

            /* other keys: treat as next page */
            lines_printed = 0;
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    if (argc >= 2) {
        path = argv[1];
    }

    FILE *in = stdin;
    if (path) {
        in = fopen(path, "r");
        if (!in) {
            fprintf(stdout, "more: cannot open %s: %s\n", path, strerror(errno));
            return 1;
        }
    }

    int tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd < 0) {
        /* /dev/tty が無ければ stdin から読む（最小フォールバック） */
        tty_fd = STDIN_FILENO;
    }

    /* foreground でない場合に tty を読むと停止することがあるため、ページングを避ける */
    int tty_foreground_ok = 1;
    if (isatty(tty_fd)) {
        pid_t fg = tcgetpgrp(tty_fd);
        pid_t pg = getpgrp();
        if (fg >= 0 && fg != pg) {
            tty_foreground_ok = 0;
        }
    }

    struct termios old;
    int have_old = 0;
    if (tty_foreground_ok && isatty(tty_fd) && isatty(STDOUT_FILENO)) {
        if (enable_raw_mode(tty_fd, &old) == 0) {
            have_old = 1;
        }
    }

    int rc = page_stream(in, tty_fd);

    if (have_old) {
        restore_term(tty_fd, &old);
    }

    if (path && in && in != stdin) {
        fclose(in);
    }

    return rc;
}
