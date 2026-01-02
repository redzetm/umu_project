/*
 * umu_head.c - Minimal implementation of a "head"-like command.
 *
 * Purpose (analysis/learning):
 *   Keep only the essence: print the first N lines of input.
 *
 * Supported:
 *   - (no FILE) read from stdin
 *   - FILE...   process each file in order
 *   - "-"       treated as stdin
 *   - -n N      print first N lines (default: 10)
 *
 * Non-goals:
 *   - No other options (-c, -q, -v, ...).
 *   - No gnulib/coreutils dependencies.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long
parse_n (const char *s)
{
  char *endp = NULL;
  errno = 0;
  long n = strtol (s, &endp, 10);
  if (errno != 0 || endp == s || *endp != '\0' || n < 0)
    return -1;
  return n;
}

static int
head_stream (FILE *in, const char *name, long nlines)
{
  if (nlines == 0)
    return 0;

  long remaining = nlines;
  int c;
  while (remaining > 0 && (c = fgetc (in)) != EOF)
    {
      if (putchar (c) == EOF)
        {
          fprintf (stderr, "umu_head: write error: %s\n", strerror (errno));
          return 1;
        }
      if (c == '\n')
        remaining--;
    }

  if (ferror (in))
    {
      fprintf (stderr, "umu_head: %s: read error: %s\n", name, strerror (errno));
      return 1;
    }

  return 0;
}

static int
head_file (const char *path, long nlines)
{
  if (strcmp (path, "-") == 0)
    return head_stream (stdin, "-", nlines);

  FILE *fp = fopen (path, "r");
  if (!fp)
    {
      fprintf (stderr, "umu_head: %s: %s\n", path, strerror (errno));
      return 1;
    }

  int rc = head_stream (fp, path, nlines);
  fclose (fp);
  return rc;
}

int
main (int argc, char **argv)
{
  long nlines = 10;
  int argi = 1;

  if (argi + 2 <= argc && strcmp (argv[argi], "-n") == 0)
    {
      long n = parse_n (argv[argi + 1]);
      if (n < 0)
        {
          fprintf (stderr, "umu_head: invalid line count: %s\n", argv[argi + 1]);
          return 1;
        }
      nlines = n;
      argi += 2;
    }

  int exit_status = 0;

  if (argi >= argc)
    return head_stream (stdin, "-", nlines);

  for (; argi < argc; argi++)
    {
      if (head_file (argv[argi], nlines) != 0)
        exit_status = 1;
    }

  return exit_status;
}
