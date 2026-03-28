#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int pid;
    char user[16];
    long pr;
    long ni;
    unsigned long long virt_bytes;
    unsigned long long res_bytes;
    unsigned long long shr_bytes;
    char state;
    double cpu_pct;
    double mem_pct;
    unsigned long long time_ticks;
    char comm[32];
} ProcRow;

typedef struct {
    int pid;
    unsigned long long proc_ticks;
} PrevProc;

typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
} CpuTimes;

static bool g_use_color = false;

typedef struct {
    bool enabled;
    uid_t uid;
    char label[32];
} UserFilter;

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    exit(1);
}

static void usage(FILE *fp, const char *argv0) {
    fprintf(fp, "Usage: %s [-u USER]\n", argv0 ? argv0 : "top");
}

static int read_file_line(const char *path, char *buf, size_t buflen) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(buf, (int)buflen, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static unsigned long long parse_ull_kb(const char *s) {
    while (*s && !isdigit((unsigned char)*s)) s++;
    unsigned long long v = 0;
    while (*s && isdigit((unsigned char)*s)) {
        v = v * 10ULL + (unsigned long long)(*s - '0');
        s++;
    }
    return v; /* kB */
}

static int read_meminfo(unsigned long long *mem_total_kb,
                       unsigned long long *mem_free_kb,
                       unsigned long long *mem_avail_kb,
                       unsigned long long *buffers_kb,
                       unsigned long long *cached_kb,
                       unsigned long long *sreclaim_kb,
                       unsigned long long *shmem_kb,
                       unsigned long long *swap_total_kb,
                       unsigned long long *swap_free_kb) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) *mem_total_kb = parse_ull_kb(line);
        else if (strncmp(line, "MemFree:", 8) == 0) *mem_free_kb = parse_ull_kb(line);
        else if (strncmp(line, "MemAvailable:", 13) == 0) *mem_avail_kb = parse_ull_kb(line);
        else if (strncmp(line, "Buffers:", 8) == 0) *buffers_kb = parse_ull_kb(line);
        else if (strncmp(line, "Cached:", 7) == 0) *cached_kb = parse_ull_kb(line);
        else if (strncmp(line, "SReclaimable:", 13) == 0) *sreclaim_kb = parse_ull_kb(line);
        else if (strncmp(line, "Shmem:", 6) == 0) *shmem_kb = parse_ull_kb(line);
        else if (strncmp(line, "SwapTotal:", 10) == 0) *swap_total_kb = parse_ull_kb(line);
        else if (strncmp(line, "SwapFree:", 9) == 0) *swap_free_kb = parse_ull_kb(line);
    }

    fclose(fp);
    return 0;
}

static int read_loadavg(double *a1, double *a5, double *a15) {
    char buf[128];
    if (read_file_line("/proc/loadavg", buf, sizeof(buf)) != 0) return -1;
    if (sscanf(buf, "%lf %lf %lf", a1, a5, a15) != 3) return -1;
    return 0;
}

static int read_uptime(double *uptime_sec) {
    char buf[128];
    if (read_file_line("/proc/uptime", buf, sizeof(buf)) != 0) return -1;
    if (sscanf(buf, "%lf", uptime_sec) != 1) return -1;
    return 0;
}

static int read_cpu_times(CpuTimes *out) {
    memset(out, 0, sizeof(*out));
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;

    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* cpu  user nice system idle iowait irq softirq steal ... */
    char cpu_label[8];
    int n = sscanf(line, "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
                   cpu_label,
                   &out->user, &out->nice, &out->system, &out->idle,
                   &out->iowait, &out->irq, &out->softirq, &out->steal);
    if (n < 5) return -1;
    return 0;
}

static unsigned long long cpu_total(const CpuTimes *t) {
    return t->user + t->nice + t->system + t->idle + t->iowait + t->irq + t->softirq + t->steal;
}

static void format_uptime(char *buf, size_t buflen, double up) {
    long sec = (long)up;
    long days = sec / 86400;
    sec %= 86400;
    long hours = sec / 3600;
    sec %= 3600;
    long mins = sec / 60;

    if (days > 0) {
        snprintf(buf, buflen, "%ld days, %02ld:%02ld", days, hours, mins);
    } else {
        snprintf(buf, buflen, "%02ld:%02ld", hours, mins);
    }
}

static int count_users_from_utmp(void) {
    /* /var/run/utmp を簡易に読む。無い場合は 0。厳密なUTMP構造に依存しないため保守的に0扱い。 */
    struct stat st;
    if (stat("/var/run/utmp", &st) != 0) return 0;
    if (st.st_size == 0) return 0;
    return 1; /* 最小: 存在すれば1ユーザーとみなす */
}

static int tty_open(void) {
    int fd = open("/dev/tty", O_RDONLY);
    if (fd >= 0) return fd;
    return STDIN_FILENO;
}

static int tty_enable_raw(int fd, struct termios *old) {
    if (!isatty(fd)) return -1;
    if (tcgetattr(fd, old) != 0) return -1;

    struct termios t = *old;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSAFLUSH, &t) != 0) return -1;
    return 0;
}

static void tty_restore(int fd, const struct termios *old) {
    if (isatty(fd)) {
        tcsetattr(fd, TCSAFLUSH, old);
    }
}

static int tty_read_key(int fd) {
    unsigned char c;
    ssize_t r = read(fd, &c, 1);
    if (r == 1) return (int)c;
    return -1;
}

static void ansi_clear(void) {
    fputs("\033[H\033[J", stdout);
}

static void ansi_hide_cursor(void) {
    fputs("\033[?25l", stdout);
}

static void ansi_show_cursor(void) {
    fputs("\033[?25h", stdout);
}

static bool ansi_color_ok(void) {
    if (!isatty(STDOUT_FILENO)) return false;
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) return false;
    const char *term = getenv("TERM");
    if (!term || !*term) return false;
    if (strcmp(term, "dumb") == 0) return false;
    return true;
}

static const char *c_yellow(void) { return g_use_color ? "\033[33m" : ""; }
static const char *c_reset(void) { return g_use_color ? "\033[0m" : ""; }

static int parse_proc_status_uid(const char *path, uid_t *uid) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            unsigned long u = 0;
            if (sscanf(line, "Uid:\t%lu", &u) == 1) {
                *uid = (uid_t)u;
                fclose(fp);
                return 0;
            }
        }
    }
    fclose(fp);
    return -1;
}

static void uid_to_name(uid_t uid, char *out, size_t outlen) {
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        snprintf(out, outlen, "%s", pw->pw_name);
    } else {
        snprintf(out, outlen, "%u", (unsigned)uid);
    }
}

static int parse_user_filter(const char *arg, UserFilter *filter) {
    if (!arg || !*arg || !filter) return -1;

    struct passwd *pw = getpwnam(arg);
    if (pw) {
        filter->enabled = true;
        filter->uid = pw->pw_uid;
        snprintf(filter->label, sizeof(filter->label), "%s", pw->pw_name);
        return 0;
    }

    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(arg, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }

    filter->enabled = true;
    filter->uid = (uid_t)parsed;
    snprintf(filter->label, sizeof(filter->label), "%lu", parsed);
    return 0;
}

static int read_proc_stat(int pid,
                          char *comm, size_t commlen,
                          char *state,
                          long *prio,
                          long *nice,
                          unsigned long long *utime,
                          unsigned long long *stime,
                          unsigned long long *vsize,
                          long long *rss_pages) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char buf[4096];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* format: pid (comm) state ... with spaces inside comm */
    char *lp = strchr(buf, '(');
    char *rp = strrchr(buf, ')');
    if (!lp || !rp || rp < lp) return -1;

    size_t clen = (size_t)(rp - lp - 1);
    if (clen >= commlen) clen = commlen - 1;
    memcpy(comm, lp + 1, clen);
    comm[clen] = '\0';

    /* parse after ") " */
    char *p = rp + 2;

    /* Tokenize by spaces. token[0] is state (field #3), token[11]=utime (field #14), ... */
    char *tokens[64];
    int nt = 0;
    while (*p && nt < (int)(sizeof(tokens) / sizeof(tokens[0]))) {
        while (*p == ' ') p++;
        if (!*p || *p == '\n') break;
        tokens[nt++] = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        if (*p == ' ') {
            *p = '\0';
            p++;
        } else if (*p == '\n') {
            *p = '\0';
            break;
        }
    }

    if (nt < 22) {
        return -1;
    }

    *state = tokens[0][0];
    *utime = strtoull(tokens[11], NULL, 10);
    *stime = strtoull(tokens[12], NULL, 10);
    *prio = strtol(tokens[15], NULL, 10);
    *nice = strtol(tokens[16], NULL, 10);
    *vsize = strtoull(tokens[20], NULL, 10);
    *rss_pages = strtoll(tokens[21], NULL, 10);
    return 0;
}

static int read_proc_statm_shr(int pid, unsigned long long *shr_pages) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    unsigned long long size = 0, resident = 0, shared = 0;
    int n = fscanf(fp, "%llu %llu %llu", &size, &resident, &shared);
    fclose(fp);
    if (n < 3) return -1;
    (void)size;
    (void)resident;
    *shr_pages = shared;
    return 0;
}

static int prev_find(const PrevProc *prev, int prev_n, int pid, unsigned long long *out_ticks) {
    for (int i = 0; i < prev_n; i++) {
        if (prev[i].pid == pid) {
            *out_ticks = prev[i].proc_ticks;
            return 0;
        }
    }
    return -1;
}

static int cmp_cpu_desc(const void *a, const void *b) {
    const ProcRow *pa = (const ProcRow *)a;
    const ProcRow *pb = (const ProcRow *)b;
    if (pa->cpu_pct < pb->cpu_pct) return 1;
    if (pa->cpu_pct > pb->cpu_pct) return -1;
    /* tie-breaker: PID ascending (closer to procps top default) */
    if (pa->pid < pb->pid) return -1;
    if (pa->pid > pb->pid) return 1;
    return 0;
}

static void print_header(const CpuTimes *cpu_prev, const CpuTimes *cpu_cur,
                         double load1, double load5, double load15,
                         double uptime,
                         int users,
                         int tasks_total, int tasks_running, int tasks_sleeping, int tasks_stopped,
                         int tasks_zombie,
                         unsigned long long mem_total_kb, unsigned long long mem_free_kb,
                         unsigned long long mem_avail_kb, unsigned long long buffcache_kb,
                         unsigned long long swap_total_kb, unsigned long long swap_free_kb) {
    const char *Y = c_yellow();
    const char *R = c_reset();

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm);

    char upbuf[64];
    format_uptime(upbuf, sizeof(upbuf), uptime);

    unsigned long long total_prev = cpu_total(cpu_prev);
    unsigned long long total_cur = cpu_total(cpu_cur);
    unsigned long long total_delta = (total_cur > total_prev) ? (total_cur - total_prev) : 0;

    unsigned long long u_delta = (cpu_cur->user > cpu_prev->user) ? (cpu_cur->user - cpu_prev->user) : 0;
    unsigned long long n_delta = (cpu_cur->nice > cpu_prev->nice) ? (cpu_cur->nice - cpu_prev->nice) : 0;
    unsigned long long s_delta = (cpu_cur->system > cpu_prev->system) ? (cpu_cur->system - cpu_prev->system) : 0;
    unsigned long long i_delta = (cpu_cur->idle > cpu_prev->idle) ? (cpu_cur->idle - cpu_prev->idle) : 0;

    double us = 0.0, sy = 0.0, ni = 0.0, id = 0.0;
    if (total_delta > 0) {
        us = 100.0 * (double)u_delta / (double)total_delta;
        sy = 100.0 * (double)s_delta / (double)total_delta;
        ni = 100.0 * (double)n_delta / (double)total_delta;
        id = 100.0 * (double)i_delta / (double)total_delta;
    }

    unsigned long long mem_total_mib = mem_total_kb / 1024ULL;
    unsigned long long mem_free_mib = mem_free_kb / 1024ULL;
    unsigned long long mem_avail_mib = mem_avail_kb / 1024ULL;
    unsigned long long buffcache_mib = buffcache_kb / 1024ULL;

    unsigned long long mem_used_kb = 0;
    if (mem_total_kb > mem_free_kb + buffcache_kb) {
        mem_used_kb = mem_total_kb - mem_free_kb - buffcache_kb;
    }
    unsigned long long mem_used_mib = mem_used_kb / 1024ULL;

    unsigned long long swap_total_mib = swap_total_kb / 1024ULL;
    unsigned long long swap_free_mib = swap_free_kb / 1024ULL;
    unsigned long long swap_used_mib = (swap_total_kb > swap_free_kb) ? ((swap_total_kb - swap_free_kb) / 1024ULL)
                                                                     : 0;

        printf("top - %s up %s,  %s%d%s users,  load average: %s%.2f%s, %s%.2f%s, %s%.2f%s\n",
            tbuf, upbuf, Y, users, R, Y, load1, R, Y, load5, R, Y, load15, R);
        printf("Tasks: %s%d%s total, %s%d%s running, %s%d%s sleeping, %s%d%s stopped, %s%d%s zombie\n",
            Y, tasks_total, R,
            Y, tasks_running, R,
            Y, tasks_sleeping, R,
            Y, tasks_stopped, R,
            Y, tasks_zombie, R);
        printf("%%Cpu(s): %s%4.1f%s us, %s%4.1f%s sy, %s%4.1f%s ni, %s%4.1f%s id, %s%4.1f%s wa, %s%4.1f%s hi, %s%4.1f%s si, %s%4.1f%s st\n",
            Y, us, R,
            Y, sy, R,
            Y, ni, R,
            Y, id, R,
            Y, 0.0, R,
            Y, 0.0, R,
            Y, 0.0, R,
            Y, 0.0, R);
        printf("MiB Mem : %s%8llu%s total, %s%8llu%s free, %s%8llu%s used, %s%8llu%s buff/cache\n",
            Y, mem_total_mib, R,
            Y, mem_free_mib, R,
            Y, mem_used_mib, R,
            Y, buffcache_mib, R);
        printf("MiB Swap: %s%8llu%s total, %s%8llu%s free, %s%8llu%s used. %s%8llu%s avail Mem\n\n",
            Y, swap_total_mib, R,
            Y, swap_free_mib, R,
            Y, swap_used_mib, R,
            Y, mem_avail_mib, R);
}

static int collect_procs(ProcRow *rows, int max_rows,
                         PrevProc *prev_out, int max_prev,
                         const PrevProc *prev_in, int prev_in_n,
                         const UserFilter *user_filter,
                         unsigned long long total_delta_ticks,
                         unsigned long long mem_total_bytes,
                         int *tasks_total, int *tasks_running, int *tasks_sleeping,
                         int *tasks_stopped, int *tasks_zombie,
                         unsigned long long *out_proc_total_ticks) {
    DIR *d = opendir("/proc");
    if (!d) return -1;

    long page_size = sysconf(_SC_PAGESIZE);
    long clk_tck = sysconf(_SC_CLK_TCK);
    if (page_size <= 0) page_size = 4096;
    if (clk_tck <= 0) clk_tck = 100;

    int nrows = 0;
    int np = 0;

    *tasks_total = 0;
    *tasks_running = 0;
    *tasks_sleeping = 0;
    *tasks_stopped = 0;
    *tasks_zombie = 0;

    unsigned long long proc_total_ticks = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;
        int pid = atoi(ent->d_name);
        if (pid <= 0) continue;

        char comm[32];
        char st = '\0';
        long pr = 0, ni = 0;
        unsigned long long ut = 0, stime = 0, vs = 0;
        long long rss_pages = 0;

        if (read_proc_stat(pid, comm, sizeof(comm), &st, &pr, &ni, &ut, &stime, &vs, &rss_pages) != 0) {
            continue;
        }

        (*tasks_total)++;
        switch (st) {
            case 'R': (*tasks_running)++; break;
            case 'S':
            case 'D': (*tasks_sleeping)++; break;
            case 'T':
            case 't': (*tasks_stopped)++; break;
            case 'Z': (*tasks_zombie)++; break;
            default: break;
        }

        unsigned long long proc_ticks = ut + stime;
        proc_total_ticks += proc_ticks;

        unsigned long long prev_ticks = 0;
        double cpu_pct = 0.0;
        if (total_delta_ticks > 0 && prev_find(prev_in, prev_in_n, pid, &prev_ticks) == 0) {
            unsigned long long dlt = (proc_ticks > prev_ticks) ? (proc_ticks - prev_ticks) : 0;
            cpu_pct = 100.0 * (double)dlt / (double)total_delta_ticks;
        }

        unsigned long long res_bytes = 0;
        if (rss_pages > 0) {
            res_bytes = (unsigned long long)rss_pages * (unsigned long long)page_size;
        }

        unsigned long long shr_pages = 0;
        unsigned long long shr_bytes = 0;
        if (read_proc_statm_shr(pid, &shr_pages) == 0) {
            shr_bytes = shr_pages * (unsigned long long)page_size;
        }

        uid_t uid = 0;
        char status_path[64];
        snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
        if (parse_proc_status_uid(status_path, &uid) != 0) {
            uid = 0;
        }

        if (user_filter && user_filter->enabled && uid != user_filter->uid) {
            continue;
        }

        char user[16];
        uid_to_name(uid, user, sizeof(user));

        double mem_pct = 0.0;
        if (mem_total_bytes > 0) {
            mem_pct = 100.0 * (double)res_bytes / (double)mem_total_bytes;
        }

        if (nrows < max_rows) {
            ProcRow *r = &rows[nrows++];
            memset(r, 0, sizeof(*r));
            r->pid = pid;
            snprintf(r->user, sizeof(r->user), "%s", user);
            r->pr = pr;
            r->ni = ni;
            r->virt_bytes = vs;
            r->res_bytes = res_bytes;
            r->shr_bytes = shr_bytes;
            r->state = st;
            r->cpu_pct = cpu_pct;
            r->mem_pct = mem_pct;
            r->time_ticks = proc_ticks;
            snprintf(r->comm, sizeof(r->comm), "%s", comm);
        }

        if (np < max_prev) {
            prev_out[np].pid = pid;
            prev_out[np].proc_ticks = proc_ticks;
            np++;
        }
    }

    closedir(d);

    *out_proc_total_ticks = proc_total_ticks;
    return nrows;
}

static void print_procs(const ProcRow *rows, int nrows, int max_lines) {
    const char *Y = c_yellow();
    const char *R = c_reset();

    printf("  PID USER      PR  NI    VIRT    RES    SHR S  %%CPU  %%MEM   TIME+ COMMAND\n");

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    int shown = 0;
    for (int i = 0; i < nrows && shown < max_lines; i++) {
        const ProcRow *r = &rows[i];

        unsigned long long virt_k = r->virt_bytes / 1024ULL;
        unsigned long long res_k = r->res_bytes / 1024ULL;
        unsigned long long shr_k = r->shr_bytes / 1024ULL;

        unsigned long long sec = (unsigned long long)(r->time_ticks / (unsigned long long)clk_tck);
        unsigned long long mm = sec / 60ULL;
        unsigned long long ss = sec % 60ULL;

        char comm_disp[64];
        const bool is_kthread = (r->virt_bytes == 0 && r->res_bytes == 0);
        if (is_kthread && r->comm[0] != '[') {
            snprintf(comm_disp, sizeof(comm_disp), "[%s]", r->comm);
        } else {
            snprintf(comm_disp, sizeof(comm_disp), "%s", r->comm);
        }

         if (r->state == 'R') fputs(Y, stdout);
         printf("%5d %-8.8s %3ld %3ld %7lluk %6lluk %6lluk %c %5.1f %5.1f %3llu:%02llu %-s\n",
             r->pid,
             r->user,
             r->pr,
             r->ni,
             virt_k,
             res_k,
             shr_k,
             r->state,
             r->cpu_pct,
             r->mem_pct,
             mm,
             ss,
             comm_disp);
         if (r->state == 'R') fputs(R, stdout);
        shown++;
    }
}

int main(int argc, char **argv) {
    g_use_color = ansi_color_ok();

    UserFilter user_filter;
    memset(&user_filter, 0, sizeof(user_filter));

    int opt;
    while ((opt = getopt(argc, argv, "u:h")) != -1) {
        switch (opt) {
            case 'u':
                if (parse_user_filter(optarg, &user_filter) != 0) {
                    die("top: unknown user: %s", optarg);
                }
                break;
            case 'h':
                usage(stdout, argv[0]);
                return 0;
            default:
                usage(stderr, argv[0]);
                return 1;
        }
    }

    if (optind < argc) {
        usage(stderr, argv[0]);
        return 1;
    }

    int tty_fd = tty_open();
    struct termios old;
    int have_old = 0;
    if (tty_enable_raw(tty_fd, &old) == 0) {
        have_old = 1;
    }

    ansi_hide_cursor();

    CpuTimes cpu_prev, cpu_cur;
    if (read_cpu_times(&cpu_prev) != 0) {
        die("top: cannot read /proc/stat");
    }

    PrevProc *prev = calloc(8192, sizeof(PrevProc));
    PrevProc *prev2 = calloc(8192, sizeof(PrevProc));
    ProcRow *rows = calloc(8192, sizeof(ProcRow));
    if (!prev || !prev2 || !rows) {
        die("top: out of memory");
    }

    int prev_n = 0;

    const int interval_ms = 2000;

    for (;;) {
        if (read_cpu_times(&cpu_cur) != 0) {
            break;
        }
        unsigned long long total_prev = cpu_total(&cpu_prev);
        unsigned long long total_cur = cpu_total(&cpu_cur);
        unsigned long long total_delta = (total_cur > total_prev) ? (total_cur - total_prev) : 1;

        double load1 = 0, load5 = 0, load15 = 0;
        (void)read_loadavg(&load1, &load5, &load15);

        double up = 0;
        (void)read_uptime(&up);

        int users = count_users_from_utmp();

        unsigned long long mem_total_kb = 0, mem_free_kb = 0, mem_avail_kb = 0;
        unsigned long long buffers_kb = 0, cached_kb = 0, sreclaim_kb = 0, shmem_kb = 0;
        unsigned long long swap_total_kb = 0, swap_free_kb = 0;
        (void)read_meminfo(&mem_total_kb, &mem_free_kb, &mem_avail_kb,
                           &buffers_kb, &cached_kb, &sreclaim_kb, &shmem_kb,
                           &swap_total_kb, &swap_free_kb);

        unsigned long long buffcache_kb = 0;
        if (buffers_kb + cached_kb + sreclaim_kb >= shmem_kb) {
            buffcache_kb = buffers_kb + cached_kb + sreclaim_kb - shmem_kb;
        } else {
            buffcache_kb = buffers_kb + cached_kb;
        }

        unsigned long long mem_total_bytes = mem_total_kb * 1024ULL;

        int tasks_total = 0, tasks_running = 0, tasks_sleeping = 0, tasks_stopped = 0, tasks_zombie = 0;
        unsigned long long proc_total_ticks = 0;

        int nrows = collect_procs(rows, 8192, prev2, 8192, prev, prev_n,
                      &user_filter,
                                  total_delta, mem_total_bytes,
                                  &tasks_total, &tasks_running, &tasks_sleeping,
                                  &tasks_stopped, &tasks_zombie,
                                  &proc_total_ticks);
        if (nrows < 0) {
            break;
        }

        qsort(rows, (size_t)nrows, sizeof(ProcRow), cmp_cpu_desc);

        /* determine max lines based on terminal rows */
        int term_rows = 24;
#ifdef TIOCGWINSZ
        if (isatty(STDOUT_FILENO)) {
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                if (ws.ws_row > 0) term_rows = (int)ws.ws_row;
            }
        }
#endif
        int proc_lines = term_rows - 8;
        if (proc_lines < 5) proc_lines = 5;

        ansi_clear();
        print_header(&cpu_prev, &cpu_cur, load1, load5, load15, up, users,
                     tasks_total, tasks_running, tasks_sleeping, tasks_stopped, tasks_zombie,
                     mem_total_kb, mem_free_kb, mem_avail_kb, buffcache_kb,
                     swap_total_kb, swap_free_kb);
        print_procs(rows, nrows, proc_lines);
        fflush(stdout);

        /* wait for key or timeout */
        int elapsed = 0;
        int quit = 0;
        while (elapsed < interval_ms) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(tty_fd, &rfds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            int r = select(tty_fd + 1, &rfds, NULL, NULL, &tv);
            if (r > 0 && FD_ISSET(tty_fd, &rfds)) {
                int key = tty_read_key(tty_fd);
                if (key == 'q' || key == 'Q') {
                    quit = 1;
                    break;
                }
            }
            elapsed += 100;
        }
        if (quit) break;

        /* swap prev */
        PrevProc *tmp = prev;
        prev = prev2;
        prev2 = tmp;
        prev_n = tasks_total;
        if (prev_n > 8192) prev_n = 8192;

        cpu_prev = cpu_cur;
    }

    ansi_show_cursor();
    if (have_old) {
        tty_restore(tty_fd, &old);
    }

    free(prev);
    free(prev2);
    free(rows);

    return 0;
}
