/*
 * umu_tail.c - Minimal implementation of a "tail"-like command.
 *
 * Purpose (analysis/learning):
 *   Keep only the essence: print the last N lines of input.
 *
 * Supported:
 *   - (no FILE) read from stdin
 *   - FILE...   process each file in order
 *   - "-"       treated as stdin
 *   - -n N      print last N lines (default: 10)
 *
 * Non-goals:
 *   - No follow mode (-f).
 *   - No byte count (-c) or +N semantics.
 *   - No gnulib/coreutils dependencies.
 */

#define _POSIX_C_SOURCE 200809L

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
is_dash_number (const char *s)
{
  if (!s || s[0] != '-')
    return 0;
  if (s[1] == '\0')
    return 0; /* "-" */
  for (size_t i = 1; s[i] != '\0'; i++)
    {
      if (s[i] < '0' || '9' < s[i])
        return 0;
    }
  return 1;
}

static void
free_ring (char **ring, long n)
{
  if (!ring)
    return;
  for (long i = 0; i < n; i++)
    free (ring[i]);
  free (ring);
}

static int
tail_stream (FILE *in, const char *name, long nlines)
{
  if (nlines == 0)
    return 0;

  char **ring = calloc ((size_t) nlines, sizeof *ring);
  if (!ring)
    {
      fprintf (stderr, "umu_tail: out of memory\n");
      return 1;
    }

  long count = 0;
  long idx = 0;

  char *line = NULL;
  size_t cap = 0;
  ssize_t len;

  while ((len = getline (&line, &cap, in)) != -1)
    {
      (void) len;
      free (ring[idx]);
      ring[idx] = strdup (line);
      if (!ring[idx])
        {
          free (line);
          free_ring (ring, nlines);
          fprintf (stderr, "umu_tail: out of memory\n");
          return 1;
        }

      idx = (idx + 1) % nlines;
      if (count < nlines)
        count++;
    }

  free (line);

  if (ferror (in))
    {
      fprintf (stderr, "umu_tail: %s: read error: %s\n", name, strerror (errno));
      free_ring (ring, nlines);
      return 1;
    }

  long start = (count == nlines) ? idx : 0;
  for (long i = 0; i < count; i++)
    {
      long pos = (start + i) % nlines;
      if (ring[pos])
        {
          if (fputs (ring[pos], stdout) == EOF)
            {
              fprintf (stderr, "umu_tail: write error: %s\n", strerror (errno));
              free_ring (ring, nlines);
              return 1;
            }
        }
    }

  free_ring (ring, nlines);
  return 0;
}

static int
tail_file (const char *path, long nlines)
{
  if (strcmp (path, "-") == 0)
    return tail_stream (stdin, "-", nlines);

  FILE *fp = fopen (path, "r");
  if (!fp)
    {
      fprintf (stderr, "umu_tail: %s: %s\n", path, strerror (errno));
      return 1;
    }

  int rc = tail_stream (fp, path, nlines);
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
          fprintf (stderr, "umu_tail: invalid line count: %s\n", argv[argi + 1]);
          return 1;
        }
      nlines = n;
      argi += 2;
    }
  else if (argi < argc && is_dash_number (argv[argi]))
    {
      /* POSIX-like shorthand: "-NUM" means "-n NUM".  */
      long n = parse_n (argv[argi] + 1);
      if (n < 0)
        {
          fprintf (stderr, "umu_tail: invalid line count: %s\n", argv[argi]);
          return 1;
        }
      nlines = n;
      argi += 1;
    }

  int exit_status = 0;

  if (argi >= argc)
    return tail_stream (stdin, "-", nlines);

  for (; argi < argc; argi++)
    {
      if (tail_file (argv[argi], nlines) != 0)
        exit_status = 1;
    }

  return exit_status;
}
