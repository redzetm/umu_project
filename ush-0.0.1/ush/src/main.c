#include "ush.h"

#include "ush_builtins.h"
#include "ush_exec.h"
#include "ush_limits.h"
#include "ush_parser.h"
#include "ush_utils.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void ush_print_prompt(void)
{
    char cwd[4096];
    const char *cwd_str = cwd;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        cwd_str = "?";
    }

    /* 仕様: UmuOS:ush:<cwd>$ */
    printf("UmuOS:ush:%s$ ", cwd_str);
    fflush(stdout);
}

static void ush_handle_parse_error(ush_state_t *st, parse_result_t r)
{
    /* MVP: パース系エラーは last_status=2 */
    switch (r) {
    case PARSE_TOO_LONG:
        ush_eprintf("ush: input too long\n");
        st->last_status = 2;
        break;
    case PARSE_TOO_MANY_ARGS:
        ush_eprintf("ush: too many arguments\n");
        st->last_status = 2;
        break;
    case PARSE_UNSUPPORTED:
        ush_eprintf("ush: unsupported syntax\n");
        st->last_status = 2;
        break;
    default:
        /* PARSE_EMPTY/OK はここに来ない */
        st->last_status = 2;
        break;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ush_state_t st;
    st.last_status = 0;

    /* 親（ush）は SIGINT 無視 */
    (void)signal(SIGINT, SIG_IGN);

    char *line = NULL;
    size_t cap = 0;

    for (;;) {
        ush_print_prompt();

        int eof = ush_read_line(&line, &cap);
        if (eof) {
            /* EOF で終了。終了コードは最後のステータス。 */
            free(line);
            exit(st.last_status);
        }

        /* 行長チェック */
        size_t len = strlen(line);
        if (len > (size_t)USH_MAX_LINE_LEN) {
            ush_eprintf("ush: input too long\n");
            st.last_status = 2;
            continue;
        }

        /* 空行/コメント行は何もしない */
        if (ush_is_blank_line(line) || ush_is_comment_line(line)) {
            continue;
        }

        char *argv2[USH_MAX_ARGS + 1];
        int argc2 = 0;

        parse_result_t r = ush_tokenize_inplace(line, argv2, &argc2);
        if (r == PARSE_EMPTY) {
            continue;
        }
        if (r != PARSE_OK) {
            ush_handle_parse_error(&st, r);
            continue;
        }

        if (argc2 <= 0 || argv2[0] == NULL) {
            continue;
        }

        if (ush_is_builtin(argv2[0])) {
            st.last_status = ush_run_builtin(&st, argv2);
            continue;
        }

        st.last_status = ush_exec_external(&st, argv2);
    }
}
