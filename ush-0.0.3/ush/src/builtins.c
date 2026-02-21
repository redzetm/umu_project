#include "ush_builtins.h"

#include "ush_utils.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int builtin_cd(char *argv[]) {
  const char *dir = argv[1];
  if (dir == NULL) {
    dir = getenv("HOME");
    if (dir == NULL || dir[0] == '\0') dir = "/";
  }
  if (argv[2] != NULL) return 2;
  if (chdir(dir) != 0) {
    ush_perrorf("cd");
    return 1;
  }
  return 0;
}

static int builtin_pwd(char *argv[]) {
  if (argv[1] != NULL) return 2;
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    ush_perrorf("pwd");
    return 1;
  }
  puts(cwd);
  return 0;
}

static int builtin_export(char *argv[]) {
  // 0.0.3 仕様: export NAME=VALUE / export NAME
  if (argv[1] == NULL) return 2;
  if (argv[2] != NULL) return 2;

  const char *arg = argv[1];
  const char *eq = strchr(arg, '=');
  if (eq != NULL) {
    size_t n = (size_t)(eq - arg);
    char name[256];
    if (n == 0 || n >= sizeof(name)) return 2;
    memcpy(name, arg, n);
    name[n] = '\0';
    if (!ush_is_valid_name(name)) return 2;
    const char *val = eq + 1;
    if (setenv(name, val, 1) != 0) {
      ush_perrorf("export");
      return 1;
    }
    return 0;
  }

  if (!ush_is_valid_name(arg)) return 2;
  // NAME のみ: 未定義なら空文字で作成、定義済みなら維持
  const char *v = getenv(arg);
  if (v == NULL) {
    if (setenv(arg, "", 1) != 0) {
      ush_perrorf("export");
      return 1;
    }
  }
  return 0;
}

static int builtin_exit(ush_state_t *st, char *argv[]) {
  if (argv[1] == NULL) exit(st->last_status & 255);
  if (argv[2] != NULL) return 2;
  char *end = NULL;
  long v = strtol(argv[1], &end, 10);
  if (end == NULL || *end != '\0') return 2;
  exit((int)(v & 255));
}

static int builtin_help(void) {
  puts("ush 0.0.3 builtins:");
  puts("  cd [DIR]");
  puts("  pwd");
  puts("  export NAME=VALUE | export NAME");
  puts("  exit [N]");
  puts("  help");
  puts("");
  puts("operators: | (1 stage), &&, ||, <, >, >>");
  puts("notes: complex scripts => /bin/sh");
  return 0;
}

int ush_is_builtin(const char *cmd) {
  if (cmd == NULL) return 0;
  return strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 || strcmp(cmd, "export") == 0 ||
         strcmp(cmd, "exit") == 0 || strcmp(cmd, "help") == 0;
}

int ush_run_builtin(ush_state_t *st, char *argv[]) {
  if (argv == NULL || argv[0] == NULL) return 2;

  if (strcmp(argv[0], "cd") == 0) return builtin_cd(argv);
  if (strcmp(argv[0], "pwd") == 0) return builtin_pwd(argv);
  if (strcmp(argv[0], "export") == 0) return builtin_export(argv);
  if (strcmp(argv[0], "exit") == 0) return builtin_exit(st, argv);
  if (strcmp(argv[0], "help") == 0) return builtin_help();

  return 2;
}
