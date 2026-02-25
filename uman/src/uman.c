#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UMAN_DEFAULT_DIR
#define UMAN_DEFAULT_DIR "/usr/share/uman/ja"
#endif

static void print_usage(FILE *out) {
    fprintf(out,
            "uman - UmuOS向け 日本語マニュアル(Markdown)ビューア\n"
            "\n"
            "使い方:\n"
            "  uman <name>\n"
            "  uman --list\n"
            "  uman -w <name>\n"
            "  uman -k <word>\n"
            "\n"
            "環境変数:\n"
            "  UMAN_PATH  ページのルートディレクトリ（既定: " UMAN_DEFAULT_DIR "）\n");
}

static const char *get_root_dir(void) {
    const char *env = getenv("UMAN_PATH");
    if (env != NULL && env[0] != '\0') {
        return env;
    }
    return UMAN_DEFAULT_DIR;
}

static int is_safe_name(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strstr(name, "..") != NULL) {
        return 0;
    }
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p == '/' || *p == '\\') {
            return 0;
        }
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) {
            return 0;
        }
    }
    return 1;
}

static int has_suffix(const char *s, const char *suffix) {
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    if (sl < su) {
        return 0;
    }
    return strcmp(s + (sl - su), suffix) == 0;
}

static int build_page_path(char *buf, size_t buflen, const char *root, const char *name) {
    const char *ext = has_suffix(name, ".md") ? "" : ".md";
    int n = snprintf(buf, buflen, "%s/%s%s", root, name, ext);
    if (n < 0 || (size_t)n >= buflen) {
        return -1;
    }
    return 0;
}

static int copy_file_to_stdout(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    char buf[4096];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (fwrite(buf, 1, r, stdout) != r) {
            fclose(fp);
            return -1;
        }
    }
    if (ferror(fp)) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static void list_pages(const char *root) {
    DIR *d = opendir(root);
    if (!d) {
        fprintf(stdout, "uman: cannot open dir: %s: %s\n", root, strerror(errno));
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.') {
            continue;
        }
        if (!has_suffix(name, ".md")) {
            continue;
        }

        size_t len = strlen(name);
        if (len <= 3) {
            continue;
        }

        char base[256];
        if (len - 3 >= sizeof(base)) {
            continue;
        }
        memcpy(base, name, len - 3);
        base[len - 3] = '\0';
        puts(base);
    }

    closedir(d);
}

static int file_contains_word(const char *path, const char *word) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return 0;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, word) != NULL) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

static int search_pages(const char *root, const char *word) {
    DIR *d = opendir(root);
    if (!d) {
        fprintf(stdout, "uman: cannot open dir: %s: %s\n", root, strerror(errno));
        return 1;
    }

    int found_any = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.') {
            continue;
        }
        if (!has_suffix(name, ".md")) {
            continue;
        }

        char path[1024];
        int n = snprintf(path, sizeof(path), "%s/%s", root, name);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            continue;
        }

        if (strstr(name, word) != NULL || file_contains_word(path, word)) {
            found_any = 1;
            size_t len = strlen(name);
            if (len > 3) {
                char base[256];
                if (len - 3 < sizeof(base)) {
                    memcpy(base, name, len - 3);
                    base[len - 3] = '\0';
                    puts(base);
                }
            }
        }
    }

    closedir(d);
    return found_any ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *root = get_root_dir();

    if (argc <= 1) {
        print_usage(stdout);
        return 2;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }

    if (strcmp(argv[1], "--list") == 0) {
        list_pages(root);
        return 0;
    }

    if (strcmp(argv[1], "-w") == 0) {
        if (argc < 3) {
            fprintf(stdout, "uman: -w requires <name>\n");
            return 2;
        }
        const char *name = argv[2];
        if (!is_safe_name(name)) {
            fprintf(stdout, "uman: invalid name: %s\n", name);
            return 2;
        }
        char path[1024];
        if (build_page_path(path, sizeof(path), root, name) != 0) {
            fprintf(stdout, "uman: path too long\n");
            return 1;
        }
        puts(path);
        return 0;
    }

    if (strcmp(argv[1], "-k") == 0) {
        if (argc < 3) {
            fprintf(stdout, "uman: -k requires <word>\n");
            return 2;
        }
        return search_pages(root, argv[2]);
    }

    const char *name = argv[1];
    if (!is_safe_name(name)) {
        fprintf(stdout, "uman: invalid name: %s\n", name);
        return 2;
    }

    char path[1024];
    if (build_page_path(path, sizeof(path), root, name) != 0) {
        fprintf(stdout, "uman: path too long\n");
        return 1;
    }

    if (copy_file_to_stdout(path) != 0) {
        fprintf(stdout, "uman: page not found: %s\n", name);
        fprintf(stdout, "uman: tried: %s\n", path);
        return 1;
    }

    return 0;
}
