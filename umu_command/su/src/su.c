#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*
 * crypt(3) は環境によって <crypt.h> が無いことがあるため、ヘッダに依存せず
 * 必要最小のプロトタイプを宣言する（リンクは -lcrypt / -lxcrypt 側で解決）。
 */
extern char *crypt(const char *key, const char *salt);

static void secure_bzero(void *p, size_t n) {
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n--) {
        *vp++ = 0;
    }
}

static int copy_password(char *out, size_t out_len, const char *src) {
    size_t len;

    if (!out || !src || out_len == 0) return -1;

    len = strlen(src);
    if (len + 1 > out_len) return -1;

    memcpy(out, src, len + 1);
    return 0;
}

static int read_shadow_hash_root(char *out, size_t out_len) {
    FILE *fp = fopen("/etc/shadow", "r");
    if (!fp) {
        perror("fopen(/etc/shadow)");
        return -1;
    }

    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "root:", 5) != 0) continue;

        char *p = strchr(line, ':');
        if (!p) break;
        p++;

        char *q = strchr(p, ':');
        if (!q) break;
        *q = '\0';

        if (strlen(p) == 0 || strcmp(p, "!") == 0 || strcmp(p, "*") == 0) {
            fprintf(stderr, "su: root password is locked/empty in /etc/shadow\n");
            fclose(fp);
            return -1;
        }

        if (strlen(p) + 1 > out_len) {
            fprintf(stderr, "su: shadow hash too long\n");
            fclose(fp);
            return -1;
        }

        strncpy(out, p, out_len);
        out[out_len - 1] = '\0';
        fclose(fp);
        return 0;
    }

    fclose(fp);
    fprintf(stderr, "su: root entry not found in /etc/shadow\n");
    return -1;
}

static int read_password_with_getpass(char *out, size_t out_len) {
    char *pw = getpass("Password: ");
    if (!pw) return -1;

    if (copy_password(out, out_len, pw) != 0) {
        secure_bzero(pw, strlen(pw));
        return -1;
    }

    secure_bzero(pw, strlen(pw));
    return 0;
}

static int read_password_from_tty(char *out, size_t out_len) {
    if (!out || out_len == 0) return -1;

    int fd = open("/dev/tty", O_RDWR);
    int opened_tty = 0;
    if (fd < 0) {
        fd = STDIN_FILENO;
    } else {
        opened_tty = 1;
    }

    struct termios oldt;
    int have_old = 0;
    if (isatty(fd)) {
        if (tcgetattr(fd, &oldt) == 0) {
            struct termios t = oldt;
            t.c_lflag &= (tcflag_t)~(ECHO);
            if (tcsetattr(fd, TCSAFLUSH, &t) == 0) {
                have_old = 1;
            }
        }
    }

    FILE *tty = fdopen(fd, opened_tty ? "r+" : "r");
    if (!tty) {
        if (have_old) {
            tcsetattr(fd, TCSAFLUSH, &oldt);
        }
        if (fd != STDIN_FILENO) close(fd);
        return -1;
    }

    fputs("Password: ", stderr);
    fflush(stderr);

    char buf[512];
    if (!fgets(buf, sizeof(buf), tty)) {
        fputc('\n', stderr);
        fflush(stderr);
        if (have_old) {
            tcsetattr(fd, TCSAFLUSH, &oldt);
        }
        if (opened_tty) {
            fclose(tty);
        }
        return -1;
    }

    size_t len = strcspn(buf, "\r\n");
    buf[len] = '\0';

    fputc('\n', stderr);
    fflush(stderr);

    if (have_old) {
        tcsetattr(fd, TCSAFLUSH, &oldt);
    }

    if (strlen(buf) + 1 > out_len) {
        if (opened_tty) fclose(tty);
        return -1;
    }

    strcpy(out, buf);
    secure_bzero(buf, sizeof(buf));

    if (opened_tty) {
        fclose(tty);
    }

    return 0;
}

static int read_password(char *out, size_t out_len) {
    if (read_password_with_getpass(out, out_len) == 0) {
        return 0;
    }
    return read_password_from_tty(out, out_len);
}

int main(void) {
    if (geteuid() != 0) {
        fprintf(stderr, "su: euid!=0 (setuid bit/owner/nosuid を確認)\n");
        return 1;
    }

    char shadow_hash[512];
    if (read_shadow_hash_root(shadow_hash, sizeof(shadow_hash)) != 0) return 1;

    char pw[512];
    if (read_password(pw, sizeof(pw)) != 0) {
        fprintf(stderr, "su: failed to read password\n");
        return 1;
    }

    errno = 0;
    char *calc = crypt(pw, shadow_hash);
    secure_bzero(pw, sizeof(pw));

    if (!calc) {
        perror("crypt");
        return 1;
    }

    if (strcmp(calc, shadow_hash) != 0) {
        fprintf(stderr, "su: Authentication failure\n");
        return 1;
    }

    if (setgid(0) != 0) {
        perror("setgid");
        return 1;
    }
    if (setuid(0) != 0) {
        perror("setuid");
        return 1;
    }

    execl("/bin/sh", "sh", (char *)NULL);
    perror("execl");
    return 1;
}
