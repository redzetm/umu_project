#include "ush_exec.h"

#include "ush_env.h"
#include "ush_limits.h"
#include "ush_utils.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* execve に渡すため */
extern char **environ;

static int ush_status_from_wait(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static void ush_child_set_signals_for_exec(void)
{
    /* 親は SIGINT 無視、子はデフォルトへ戻す */
    (void)signal(SIGINT, SIG_DFL);
}

static void ush_exec_fallback_sh(char *path, char *argv[])
{
    /*
     * ENOEXEC のときだけ /bin/sh へフォールバックする。
     * argv_sh = ["/bin/sh", path, argv[1], argv[2], ..., NULL]
     */
    static char *argv_sh[USH_MAX_ARGS + 2];

    argv_sh[0] = "/bin/sh";
    argv_sh[1] = path;

    int i = 2;
    for (int j = 1; argv[j] != NULL && i < (USH_MAX_ARGS + 1); j++, i++) {
        argv_sh[i] = argv[j];
    }
    argv_sh[i] = NULL;

    execve("/bin/sh", argv_sh, environ);

    /* ここに来たら /bin/sh の実行に失敗 */
    ush_eprintf("ush: /bin/sh: %s\n", strerror(errno));
    _exit(126);
}

static void ush_try_exec_or_fallback(char *path, char *argv[])
{
    execve(path, argv, environ);

    if (errno == ENOEXEC) {
        ush_exec_fallback_sh(path, argv);
    }
}

static void ush_child_exec_direct(char *argv[])
{
    ush_child_set_signals_for_exec();

    ush_try_exec_or_fallback(argv[0], argv);

    /* exec に失敗した */
    int e = errno;
    if (e == ENOENT) {
        ush_eprintf("ush: %s: not found\n", argv[0]);
        _exit(127);
    }

    if (e == EACCES || e == EPERM) {
        ush_eprintf("ush: %s: permission denied\n", argv[0]);
        _exit(126);
    }

    if (e == EISDIR) {
        ush_eprintf("ush: %s: is a directory\n", argv[0]);
        _exit(126);
    }

    ush_eprintf("ush: %s: %s\n", argv[0], strerror(e));
    _exit(126);
}

static void ush_child_exec_with_path(char *argv[])
{
    ush_child_set_signals_for_exec();

    const char *path_env = ush_get_path_or_default();

    /* strtok_r で壊してよいようにコピー */
    char *path_copy = strdup(path_env);
    if (path_copy == NULL) {
        ush_eprintf("ush: internal: out of memory\n");
        _exit(126);
    }

    int saw_eacces = 0;

    char *save = NULL;
    for (char *dir = strtok_r(path_copy, ":", &save); dir != NULL;
         dir = strtok_r(NULL, ":", &save)) {

        /* PATH 要素が空なら "./" と同義として扱う */
        const char *d = (*dir == '\0') ? "." : dir;

        char full[4096];
        int n = snprintf(full, sizeof(full), "%s/%s", d, argv[0]);
        if (n <= 0 || (size_t)n >= sizeof(full)) {
            /* 長すぎる候補は捨てる */
            continue;
        }

        ush_try_exec_or_fallback(full, argv);

        /* 失敗したので次の候補へ */
        if (errno == EACCES || errno == EPERM) {
            saw_eacces = 1;
        }

        /* ENOENT/ENOTDIR などは無視して次へ */
    }

    free(path_copy);

    if (saw_eacces) {
        ush_eprintf("ush: %s: permission denied\n", argv[0]);
        _exit(126);
    }

    ush_eprintf("ush: %s: not found\n", argv[0]);
    _exit(127);
}

int ush_exec_external(ush_state_t *st, char *argv[])
{
    if (st == NULL || argv == NULL || argv[0] == NULL) {
        return 2;
    }

    pid_t pid = fork();
    if (pid < 0) {
        ush_perrorf("fork");
        st->last_status = 1;
        return st->last_status;
    }

    if (pid == 0) {
        /* child */
        if (strchr(argv[0], '/') != NULL) {
            ush_child_exec_direct(argv);
        } else {
            ush_child_exec_with_path(argv);
        }
        _exit(126);
    }

    /* parent */
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        ush_perrorf("waitpid");
        st->last_status = 1;
        return st->last_status;
    }

    st->last_status = ush_status_from_wait(status);
    return st->last_status;
}
