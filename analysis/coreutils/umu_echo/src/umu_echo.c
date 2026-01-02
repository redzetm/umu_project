/*
 umu_echo.c ― 「echo」風コマンドの最小実装。

目的（解析・学習用）:
  小さなユーザーランドに必要な本質だけを残す。
  引数を空白で区切って表示し、必要に応じて末尾の改行を抑制する。

対応しているオプション:
  -n   末尾の改行を出力しない。

対象外（やらないこと）:
  - エスケープ解釈（-e / -E）は行わない。
  - --help / --version は実装しない。
  - ロケール / 国際化や外部依存（gnulib / coreutils）は使わない。

 */

#include <stdio.h>
#include <string.h>

int
main (int argc, char **argv)
{
  int argi = 1;
  int print_newline = 1;

  if (argi < argc && strcmp (argv[argi], "-n") == 0)
    {
      print_newline = 0;
      argi++;
    }

  for (; argi < argc; argi++)
    {
      fputs (argv[argi], stdout);
      if (argi + 1 < argc)
        putchar (' ');
    }

  if (print_newline)
    putchar ('\n');

  return 0;
}
