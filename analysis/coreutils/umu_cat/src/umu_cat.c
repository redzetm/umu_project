/*
 * umu_cat.c - Minimal implementation of a "cat"-like command.
 *
 * Purpose (analysis/learning):
 *   Keep only the essence: copy bytes from files (or stdin) to stdout.
 *
 * Supported:
 *   - FILE...   concatenate files
 *   - (no FILE) read from stdin
 *   - "-"       treated as stdin
 *
 * Non-goals:
 *   - No options (-n, -b, -v, ...).
 *   - No gnulib/coreutils dependencies.
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
