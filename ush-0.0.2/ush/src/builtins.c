#include "ush_builtins.h"

#include "ush_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static int argv_count(char *argv[]) {
  int c = 0;
  while (argv != NULL && argv[c] != NULL) c++;
  return c;
}

static int is_valid_name(const char *s) {
  if (s == NULL || s[0] == '\0') return 0;
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
  for (size_t i = 1; s[i] != '\0'; i++) {
    if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
  }
  return 1;
}

static int bi_cd(char *argv[]) {
  int argc = argv_count(argv);
  if (argc == 1) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') home = "/";

    char oldpwd[4096];
    if (getcwd(oldpwd, sizeof(oldpwd)) == NULL) oldpwd[0] = '\0';

    if (chdir(home) != 0) {
      ush_perrorf("cd");
      return 1;
    }

    char newpwd[4096];
    if (getcwd(newpwd, sizeof(newpwd)) != NULL) {
      if (oldpwd[0] != '\0') setenv("OLDPWD", oldpwd, 1);
      setenv("PWD", newpwd, 1);
    }

    return 0;
  }

  if (argc == 2) {
    if (strcmp(argv[1], "-") == 0) {
      ush_eprintf("ush: unsupported syntax\n");
      return 2;
    }

    char oldpwd[4096];
    if (getcwd(oldpwd, sizeof(oldpwd)) == NULL) oldpwd[0] = '\0';

    if (chdir(argv[1]) != 0) {
      ush_perrorf("cd");
      return 1;
    }

    char newpwd[4096];
    if (getcwd(newpwd, sizeof(newpwd)) != NULL) {
      if (oldpwd[0] != '\0') setenv("OLDPWD", oldpwd, 1);
      setenv("PWD", newpwd, 1);
    }

    return 0;
  }

  ush_eprintf("ush: cd: invalid args\n");
  return 2;
}

static int bi_pwd(char *argv[]) {
  int argc = argv_count(argv);
  if (argc != 1) {
    ush_eprintf("ush: pwd: invalid args\n");
    return 2;
  }

  char cwd[4096];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    ush_perrorf("pwd");
    return 1;
  }

  printf("%s\n", cwd);
  return 0;
}

static int bi_export(char *argv[]) {
  int argc = argv_count(argv);
  if (argc == 1) {
    for (char **p = environ; p != NULL && *p != NULL; p++) {
      puts(*p);
    }
    return 0;
  }

  if (argc == 2) {
    char *arg = argv[1];
    char *eq = strchr(arg, '=');

    if (eq != NULL) {
      *eq = '\0';
      const char *name = arg;
      const char *value = eq + 1;
      if (!is_valid_name(name)) {
        *eq = '=';
        ush_eprintf("ush: export: invalid name\n");
        return 2;
      }
      int r = setenv(name, value, 1);
      *eq = '=';
      if (r != 0) {
        ush_perrorf("export");
        return 1;
      }
      return 0;
    }

    const char *name = arg;
    if (!is_valid_name(name)) {
      ush_eprintf("ush: export: invalid name\n");
      return 2;
    }

    if (getenv(name) == NULL) {
      if (setenv(name, "", 1) != 0) {
        ush_perrorf("export");
        return 1;
      }
    }

    return 0;
  }

  ush_eprintf("ush: export: invalid args\n");
  return 2;
}

static int bi_exit(ush_state_t *st, char *argv[]) {
  int argc = argv_count(argv);
  if (argc == 1) {
    exit(st->last_status);
  }

  if (argc == 2) {
    char *end = NULL;
    long v = strtol(argv[1], &end, 10);
    if (end == NULL || *end != '\0') {
      ush_eprintf("ush: exit: invalid number\n");
      return 2;
    }
    exit((int)(v & 255));
  }

  ush_eprintf("ush: exit: invalid args\n");
  return 2;
}

static int bi_help(char *argv[]) {
  int argc = argv_count(argv);
  if (argc != 1) {
    ush_eprintf("ush: help: invalid args\n");
    return 2;
  }

  puts("ush 0.0.2 (MVP)\n");
  puts("builtins: cd pwd export exit help");
  puts("features: | < > >>, minimal line editor, PATH search");
  puts("notes:");
  puts("  - unsupported syntax is detected and rejected");
  puts("  - < is only allowed on the first command");
  puts("  - > and >> are only allowed on the last command");
  puts("  - builtins work only as a single command (no pipe/redir)");

  return 0;
}

int ush_is_builtin(const char *cmd) {
  if (cmd == NULL) return 0;
  return strcmp(cmd, "cd") == 0 ||
         strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "export") == 0 ||
         strcmp(cmd, "exit") == 0 ||
         strcmp(cmd, "help") == 0;
}

int ush_run_builtin(ush_state_t *st, char *argv[]) {
  if (argv == NULL || argv[0] == NULL) return 2;

  if (strcmp(argv[0], "cd") == 0) return bi_cd(argv);
  if (strcmp(argv[0], "pwd") == 0) return bi_pwd(argv);
  if (strcmp(argv[0], "export") == 0) return bi_export(argv);
  if (strcmp(argv[0], "exit") == 0) return bi_exit(st, argv);
  if (strcmp(argv[0], "help") == 0) return bi_help(argv);

  return 2;
}
