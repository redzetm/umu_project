/*
 * umu_cat.c - 「cat」風コマンドの最小実装
 *
 * 目的（解析・学習用）:
 *   本質だけを残すこと。
 *   ファイル（または標準入力）から読み込んだバイト列を
 *   そのまま標準出力へコピーする。
 *
 * 対応している機能:
 *   - FILE...   指定されたファイルを連結して出力する
 *   - FILE を指定しない場合は標準入力から読み込む
 *   - "-"       標準入力として扱う
 *
 * 非目標:
 *   - オプションは実装しない（-n, -b, -v など）
 *   - gnulib や coreutils への依存は持たない
 */


#include <errno.h>
#include <stdio.h>
#include <string.h>

static int
copy_stream (FILE *in, const char *name)
{
  unsigned char buf[8192];

  while (1)
    {
      size_t n = fread (buf, 1, sizeof buf, in);
      if (n > 0)
        {
          if (fwrite (buf, 1, n, stdout) != n)
            {
              fprintf (stderr, "umu_cat: write error: %s\n", strerror (errno));
              return 1;
            }
        }

      if (n < sizeof buf)
        {
          if (ferror (in))
            {
              fprintf (stderr, "umu_cat: %s: read error: %s\n",
                       name, strerror (errno));
              return 1;
            }
          return 0; /* EOF */
        }
    }
}

static int
cat_file (const char *path)
{
  if (strcmp (path, "-") == 0)
    return copy_stream (stdin, "-");

  FILE *fp = fopen (path, "rb");
  if (!fp)
    {
      fprintf (stderr, "umu_cat: %s: %s\n", path, strerror (errno));
      return 1;
    }

  int rc = copy_stream (fp, path);
  fclose (fp);
  return rc;
}

int
main (int argc, char **argv)
{
  int exit_status = 0;

  if (argc == 1)
    return copy_stream (stdin, "-");

  for (int i = 1; i < argc; i++)
    {
      if (cat_file (argv[i]) != 0)
        exit_status = 1;
    }

  return exit_status;
}
