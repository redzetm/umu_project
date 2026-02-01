#include "ush_builtins.h"

#include "ush_utils.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int ush_builtin_cd(ush_state_t *st, char *argv[])
{
    const char *target = NULL;

    /* argv[0] == "cd" */
    if (argv[1] == NULL) {
        target = getenv("HOME");
        if (target == NULL) {
            target = "/";
        }
    } else {
        if (strcmp(argv[1], "-") == 0) {
            ush_eprintf("ush: cd: cd - is not supported\n");
            st->last_status = 2;
            return st->last_status;
        }
        target = argv[1];
    }

    char oldpwd[PATH_MAX];
    int has_oldpwd = 0;

    if (getcwd(oldpwd, sizeof(oldpwd)) != NULL) {
        has_oldpwd = 1;
    } else {
        /* getcwd失敗はエラー扱い（仕様: last_status=1） */
        ush_perrorf("cd: getcwd");
        st->last_status = 1;
        /* ただし chdir 自体は試す（移動できる可能性はある） */
    }

    if (chdir(target) != 0) {
        ush_eprintf("ush: cd: %s: %s\n", target, strerror(errno));
        st->last_status = 1;
        return st->last_status;
    }

    /* 成功したら PWD/OLDPWD 更新 */
    if (has_oldpwd) {
        (void)setenv("OLDPWD", oldpwd, 1);
    }

    char newpwd[PATH_MAX];
    if (getcwd(newpwd, sizeof(newpwd)) != NULL) {
        (void)setenv("PWD", newpwd, 1);
    } else {
        ush_perrorf("cd: getcwd");
        /* cd 自体は成功しているので last_status は 0 のまま */
    }

    st->last_status = 0;
    return st->last_status;
}

static int ush_parse_exit_code(const char *s, int *code_out)
{
    if (s == NULL || code_out == NULL) {
        return 0;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    /*
     * - end==s は「数字が1桁も無い」
     * - *end!='\0' は「数字以外が混ざっている」
     * - ERANGE はオーバーフロー
     */
    if (end == s || (end != NULL && *end != '\0') || errno == ERANGE) {
        return 0;
    }

    /* MVP仕様: n & 255 */
    *code_out = (int)(v & 255L);
    return 1;
}

static int ush_builtin_exit(ush_state_t *st, char *argv[])
{
    if (argv[1] == NULL) {
        exit(st->last_status);
    }

    if (argv[2] != NULL) {
        ush_eprintf("ush: exit: too many arguments\n");
        st->last_status = 2;
        return st->last_status;
    }

    int code = 0;
    if (!ush_parse_exit_code(argv[1], &code)) {
        ush_eprintf("ush: exit: numeric argument required\n");
        st->last_status = 2;
        return st->last_status;
    }

    exit(code);
}

static int ush_builtin_help(ush_state_t *st)
{
    (void)st;

    puts("ush (UmuOS User Shell) - MVP");
    puts("\nBuiltins:");
    puts("  cd [dir]    change directory (cd - is not supported)");
    puts("  exit [n]    exit shell (n is masked with &255)");
    puts("  help        show this help");

    puts("\nUnsupported syntax (error; line is not executed):");
    puts("  quotes: ' \"  backslash: \\");
    puts("  variable expansion: $");
    puts("  glob: * ? [ ]");
    puts("  operators: | < > ; &");

    return 0;
}

int ush_is_builtin(const char *cmd)
{
    if (cmd == NULL) {
        return 0;
    }

    return (strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "help") == 0);
}

int ush_run_builtin(ush_state_t *st, char *argv[])
{
    if (st == NULL || argv == NULL || argv[0] == NULL) {
        return 2;
    }

    if (strcmp(argv[0], "cd") == 0) {
        return ush_builtin_cd(st, argv);
    }
    if (strcmp(argv[0], "exit") == 0) {
        return ush_builtin_exit(st, argv);
    }
    if (strcmp(argv[0], "help") == 0) {
        int rc = ush_builtin_help(st);
        st->last_status = rc;
        return st->last_status;
    }

    /* ここに来るのは呼び出し側のミス */
    ush_eprintf("ush: internal: unknown builtin: %s\n", argv[0]);
    st->last_status = 2;
    return st->last_status;
}
