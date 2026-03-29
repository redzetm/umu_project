#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef S_ISVTX
#define S_ISVTX 01000
#endif

typedef struct {
    bool long_format;
    bool show_all;
    bool one_per_line;
    bool use_color;
} Options;

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} NameList;

static void usage(FILE *fp, const char *argv0) {
    fprintf(fp, "Usage: %s [-a] [-l] [-1] [FILE]...\n", argv0 ? argv0 : "ls");
}

static int push_name(NameList *list, const char *name) {
    if (list->len == list->cap) {
        size_t new_cap = list->cap ? list->cap * 2U : 16U;
        char **new_items = (char **)realloc(list->items, new_cap * sizeof(char *));
        if (!new_items) return -1;
        list->items = new_items;
        list->cap = new_cap;
    }

    list->items[list->len] = strdup(name);
    if (!list->items[list->len]) return -1;
    list->len++;
    return 0;
}

static void free_names(NameList *list) {
    size_t i;
    for (i = 0; i < list->len; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static int cmp_name_ptr(const void *a, const void *b) {
    const char *lhs = *(const char * const *)a;
    const char *rhs = *(const char * const *)b;
    return strcmp(lhs, rhs);
}

static void mode_to_string(mode_t mode, char out[11]) {
    out[0] = S_ISDIR(mode) ? 'd' :
             S_ISLNK(mode) ? 'l' :
             S_ISCHR(mode) ? 'c' :
             S_ISBLK(mode) ? 'b' :
             S_ISFIFO(mode) ? 'p' :
             S_ISSOCK(mode) ? 's' : '-';
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') : ((mode & S_ISUID) ? 'S' : '-');
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') : ((mode & S_ISGID) ? 'S' : '-');
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? ((mode & S_ISVTX) ? 't' : 'x') : ((mode & S_ISVTX) ? 'T' : '-');
    out[10] = '\0';
}

static const char *user_name(uid_t uid, char *buf, size_t buflen) {
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) return pw->pw_name;
    snprintf(buf, buflen, "%lu", (unsigned long)uid);
    return buf;
}

static const char *group_name(gid_t gid, char *buf, size_t buflen) {
    struct group *gr = getgrgid(gid);
    if (gr && gr->gr_name) return gr->gr_name;
    snprintf(buf, buflen, "%lu", (unsigned long)gid);
    return buf;
}

static void format_mtime(time_t value, char *buf, size_t buflen) {
    struct tm tm_value;
    if (!localtime_r(&value, &tm_value)) {
        snprintf(buf, buflen, "1970-01-01 00:00");
        return;
    }
    if (strftime(buf, buflen, "%Y-%m-%d %H:%M", &tm_value) == 0) {
        snprintf(buf, buflen, "1970-01-01 00:00");
    }
}

static bool should_use_color(void) {
    const char *term;
    const char *no_color;

    if (!isatty(STDOUT_FILENO)) return false;

    no_color = getenv("NO_COLOR");
    if (no_color && no_color[0] != '\0') return false;

    term = getenv("TERM");
    if (!term || term[0] == '\0') return false;
    if (strcmp(term, "dumb") == 0) return false;

    return true;
}

static const char *name_color_code(const struct stat *st) {
    if (st == NULL) return NULL;

    if (S_ISDIR(st->st_mode)) return "\033[34m";
    if (S_ISLNK(st->st_mode)) return "\033[36m";
    if (S_ISFIFO(st->st_mode)) return "\033[35m";
    if (S_ISSOCK(st->st_mode)) return "\033[35m";
    if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) return "\033[33m";
    if ((st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) return "\033[33m";
    return NULL;
}

static void print_name(const char *name, const struct stat *st, const Options *opts) {
    const char *color;

    if (!opts || !opts->use_color) {
        fwrite(name, 1, strlen(name), stdout);
        return;
    }

    color = name_color_code(st);
    if (!color) {
        fwrite(name, 1, strlen(name), stdout);
        return;
    }

    fputs(color, stdout);
    fwrite(name, 1, strlen(name), stdout);
    fputs("\033[0m", stdout);
}

static int print_long_line(const char *path, const char *name, const struct stat *st, const Options *opts) {
    char mode[11];
    char user_buf[32];
    char group_buf[32];
    char time_buf[32];

    mode_to_string(st->st_mode, mode);
    format_mtime(st->st_mtime, time_buf, sizeof(time_buf));

    printf("%s %2lu %-8s %-8s %8lu %s ",
           mode,
           (unsigned long)st->st_nlink,
           user_name(st->st_uid, user_buf, sizeof(user_buf)),
           group_name(st->st_gid, group_buf, sizeof(group_buf)),
           (unsigned long)st->st_size,
           time_buf);
    print_name(name, st, opts);

    if (S_ISLNK(st->st_mode)) {
        char target[4096];
        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len >= 0) {
            target[len] = '\0';
            fputs(" -> ", stdout);
            fwrite(target, 1, strlen(target), stdout);
        }
    }

    fputc('\n', stdout);
    return 0;
}

static int print_short_line(const char *name, const struct stat *st, const Options *opts) {
    print_name(name, st, opts);
    fputc('\n', stdout);
    return 0;
}

static int print_entry(const char *path, const char *name, const Options *opts) {
    struct stat st;

    if (lstat(path, &st) != 0) {
        fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (opts->long_format) return print_long_line(path, name, &st, opts);
    return print_short_line(name, &st, opts);
}

static int join_path(const char *dir, const char *name, char **out_path) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    bool add_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    size_t total = dir_len + (add_slash ? 1U : 0U) + name_len + 1U;
    char *buf = (char *)malloc(total);
    if (!buf) return -1;

    memcpy(buf, dir, dir_len);
    if (add_slash) buf[dir_len++] = '/';
    memcpy(buf + dir_len, name, name_len);
    buf[dir_len + name_len] = '\0';
    *out_path = buf;
    return 0;
}

static int list_directory(const char *path, const Options *opts, bool show_header) {
    DIR *dir;
    struct dirent *entry;
    NameList list = {0};
    size_t i;
    int rc = 0;

    dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!opts->show_all && entry->d_name[0] == '.') continue;
        if (push_name(&list, entry->d_name) != 0) {
            fprintf(stderr, "ls: out of memory\n");
            rc = 1;
            goto done;
        }
    }

    qsort(list.items, list.len, sizeof(char *), cmp_name_ptr);

    if (show_header) {
        printf("%s:\n", path);
    }

    for (i = 0; i < list.len; i++) {
        char *entry_path = NULL;
        if (join_path(path, list.items[i], &entry_path) != 0) {
            fprintf(stderr, "ls: out of memory\n");
            rc = 1;
            goto done;
        }
        if (print_entry(entry_path, list.items[i], opts) != 0) {
            rc = 1;
        }
        free(entry_path);
    }

done:
    free_names(&list);
    closedir(dir);
    return rc;
}

static int list_path(const char *path, const Options *opts) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        return list_directory(path, opts, false);
    }

    return print_entry(path, path, opts);
}

static int parse_options(int argc, char **argv, Options *opts, int *first_path) {
    int i;
    memset(opts, 0, sizeof(*opts));
    opts->one_per_line = true;
    opts->use_color = should_use_color();

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        size_t j;

        if (arg[0] != '-' || arg[1] == '\0') break;
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }

        for (j = 1; arg[j] != '\0'; j++) {
            switch (arg[j]) {
                case 'a':
                    opts->show_all = true;
                    break;
                case 'l':
                    opts->long_format = true;
                    break;
                case '1':
                    opts->one_per_line = true;
                    break;
                default:
                    fprintf(stderr, "ls: unsupported option -- '%c'\n", arg[j]);
                    usage(stderr, argv[0]);
                    return -1;
            }
        }
    }

    *first_path = i;
    return 0;
}

int main(int argc, char **argv) {
    Options opts;
    int first_path;
    int rc = 0;
    int i;
    int path_count;

    if (parse_options(argc, argv, &opts, &first_path) != 0) {
        return 1;
    }

    path_count = argc - first_path;
    if (path_count <= 0) {
        return list_path(".", &opts);
    }

    for (i = first_path; i < argc; i++) {
        struct stat st;
        if (path_count > 1) {
            if (i > first_path) fputc('\n', stdout);
            printf("%s:\n", argv[i]);
        }

        if (lstat(argv[i], &st) != 0) {
            fprintf(stderr, "ls: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (list_directory(argv[i], &opts, false) != 0) {
                rc = 1;
            }
        } else {
            if (print_entry(argv[i], argv[i], &opts) != 0) {
                rc = 1;
            }
        }
    }

    return rc;
}