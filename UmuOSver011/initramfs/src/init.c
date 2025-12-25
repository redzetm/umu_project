#define _GNU_SOURCE

/*
 * UmuOS ver0.1.1 initramfs 用 /init
 *
 * 目的:
 *  - initramfs で最小限の初期化を行う
 *  - /proc/cmdline から root=UUID=... を取得する
 *  - 永続ディスク(ext4)を /newroot にマウントする
 *  - /newroot/logs/boot.log に起動ログを残す
 *  - BusyBox の switch_root を実行し、ext4 側の /sbin/init へ移行する
 *
 * ポリシー:
 *  - 「まず動く」を優先（0.1.1は安定化版）
 *  - エラー時は /dev/console に理由を出して停止（原因切り分けしやすくする）
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/*
 * できるだけ早期にログを出すため、/dev/console に直接書く。
 * （まだsyslog等は無いので「見えること」を優先する）
 */
static void log_console(const char *fmt, ...)
{
    char buf[1024];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    int fd = open("/dev/console", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)write(fd, buf, strnlen(buf, sizeof(buf)));
        (void)write(fd, "\n", 1);
        close(fd);
    }

    /* ついでに stderr にも出す（serial/stdio で見えることがある） */
    fprintf(stderr, "%s\n", buf);
}

static void die(const char *fmt, ...)
{
    char buf[1024];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    log_console("[init] FATAL: %s", buf);

    /* 何が起きているか見えるように無限待機 */
    for (;;) {
        pause();
    }
}

static void ensure_dir(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == 0) {
        return;
    }
    if (errno == EEXIST) {
        return;
    }
    die("mkdir(%s) failed: %s", path, strerror(errno));
}

/*
 * すでにマウントされているかを /proc/mounts で確認する。
 * 例: is_mounted("/proc")
 */
static bool is_mounted(const char *mountpoint)
{
    FILE *fp = fopen("/proc/mounts", "re");
    if (!fp) {
        /* /proc がまだ無い可能性もあるので、ここでは false 扱い */
        return false;
    }

    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), fp) != NULL) {
        /* 2番目のフィールドがマウントポイント */
        char *saveptr = NULL;
        char *dev = strtok_r(line, " ", &saveptr);
        char *mnt = strtok_r(NULL, " ", &saveptr);
        (void)dev;
        if (mnt && strcmp(mnt, mountpoint) == 0) {
            found = true;
            break;
        }
    }
    fclose(fp);
    return found;
}

/*
 * よく使う仮想FSをマウントする。
 * 失敗しても、原因を出しつつ可能な範囲で続行できるように設計する。
 */
static void mount_basic_filesystems(void)
{
    ensure_dir("/proc", 0555);
    ensure_dir("/sys", 0555);
    ensure_dir("/dev", 0755);
    ensure_dir("/dev/pts", 0755);
    ensure_dir("/run", 0755);

    if (!is_mounted("/proc")) {
        if (mount("proc", "/proc", "proc", 0, "") != 0) {
            die("mount /proc failed: %s", strerror(errno));
        }
    }

    if (!is_mounted("/sys")) {
        if (mount("sysfs", "/sys", "sysfs", 0, "") != 0) {
            die("mount /sys failed: %s", strerror(errno));
        }
    }

    /* devtmpfs はカーネル機能に依存。失敗したらログだけ出して続行。 */
    if (!is_mounted("/dev")) {
        if (mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755") != 0) {
            log_console("[init] WARN: mount /dev (devtmpfs) failed: %s", strerror(errno));
        }
    }

    if (!is_mounted("/dev/pts")) {
        if (mount("devpts", "/dev/pts", "devpts", 0, "") != 0) {
            log_console("[init] WARN: mount /dev/pts failed: %s", strerror(errno));
        }
    }
}

/*
 * /proc/cmdline を読み込む。
 * 例: "... root=UUID=xxxx rootfstype=ext4 ..."
 */
static char *read_cmdline(void)
{
    int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        die("open(/proc/cmdline) failed: %s", strerror(errno));
    }

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        die("read(/proc/cmdline) failed: %s", (n < 0) ? strerror(errno) : "empty");
    }
    buf[n] = '\0';

    /* 改行が入っていたら除去 */
    char *newline = strchr(buf, '\n');
    if (newline) {
        *newline = '\0';
    }

    return strdup(buf);
}

/*
 * コマンドラインから root=... の値を抜き出す。
 * サポート:
 *   root=UUID=xxxxxxxx-....
 * 返り値:
 *   "UUID=..." の文字列を新規確保して返す（free必要）
 */
static char *parse_root_spec_from_cmdline(const char *cmdline)
{
    const char *key = "root=";
    const char *p = strstr(cmdline, key);
    if (!p) {
        return NULL;
    }

    p += strlen(key);

    /* root= の値はスペースまで */
    const char *end = p;
    while (*end != '\0' && *end != ' ') {
        end++;
    }

    size_t len = (size_t)(end - p);
    if (len == 0) {
        return NULL;
    }

    char *root_spec = (char *)calloc(len + 1, 1);
    if (!root_spec) {
        die("calloc failed");
    }
    memcpy(root_spec, p, len);
    root_spec[len] = '\0';

    /* 今回は UUID 指定を前提にしたいので、UUID= 以外はエラーにする */
    if (strncmp(root_spec, "UUID=", 5) != 0) {
        log_console("[init] ERROR: root= is not UUID based: %s", root_spec);
        free(root_spec);
        return NULL;
    }

    return root_spec;
}

/*
 * /newroot に ext4 をマウントする。
 * - 初期起動時はデバイスが見えるまで少し時間がかかることがあるのでリトライする
 */
static void mount_newroot_with_retry(const char *root_spec)
{
    ensure_dir("/newroot", 0755);

    /* 例: root_spec = "UUID=de27..." */
    const char *fstype = "ext4";

    const int max_tries = 30;      /* 30秒程度 */
    const int sleep_seconds = 1;

    for (int i = 1; i <= max_tries; i++) {
        if (mount(root_spec, "/newroot", fstype, MS_RELATIME, "") == 0) {
            log_console("[init] mounted %s on /newroot (type=%s)", root_spec, fstype);
            return;
        }

        int err = errno;
        log_console("[init] mount try %d/%d failed: %s", i, max_tries, strerror(err));
        sleep(sleep_seconds);
    }

    die("mount /newroot failed after retries (root=%s)", root_spec);
}

/*
 * /newroot/logs/boot.log にログを追記する。
 * （永続化確認のため、ext4側にログを残す）
 */
static void append_boot_log_to_newroot(void)
{
    /* /newroot/logs を作成 */
    char logs_dir[256];
    snprintf(logs_dir, sizeof(logs_dir), "%s", "/newroot/logs");

    if (mkdir(logs_dir, 0755) != 0 && errno != EEXIST) {
        log_console("[init] WARN: mkdir(%s) failed: %s", logs_dir, strerror(errno));
        return;
    }

    char log_path[256];
    snprintf(log_path, sizeof(log_path), "%s", "/newroot/logs/boot.log");

    int fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        log_console("[init] WARN: open(%s) failed: %s", log_path, strerror(errno));
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    dprintf(fd, "[initramfs:init] %s switch_root start\n", ts);
    close(fd);
}

/*
 * BusyBox の switch_root を起動する。
 * 例: switch_root /newroot /sbin/init
 */
static void do_switch_root(void)
{
    /*
     * BusyBox の symlink が /bin/switch_root として存在する想定。
     * もし無ければ "busybox switch_root ..." を試す。
     */

    const char *newroot = "/newroot";
    const char *new_init = "/sbin/init";

    char *argv1[] = {"switch_root", (char *)newroot, (char *)new_init, NULL};
    char *argv2[] = {"busybox", "switch_root", (char *)newroot, (char *)new_init, NULL};

    if (access("/bin/switch_root", X_OK) == 0) {
        execv("/bin/switch_root", argv1);
        die("execv(/bin/switch_root) failed: %s", strerror(errno));
    }

    if (access("/bin/busybox", X_OK) == 0) {
        execv("/bin/busybox", argv2);
        die("execv(/bin/busybox switch_root) failed: %s", strerror(errno));
    }

    die("switch_root not found (need /bin/switch_root or /bin/busybox)");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* initramfsの /init は pid=1 で動くのが前提 */
    if (getpid() != 1) {
        log_console("[init] WARN: pid is %d (expected 1)", getpid());
    }

    log_console("[init] UmuOS initramfs init starting");

    /* まずは /proc /sys /dev を用意してログ・デバイスを確保 */
    mount_basic_filesystems();

    char *cmdline = read_cmdline();
    log_console("[init] cmdline: %s", cmdline);

    char *root_spec = parse_root_spec_from_cmdline(cmdline);
    if (!root_spec) {
        free(cmdline);
        die("root=UUID=... not found in /proc/cmdline");
    }

    log_console("[init] root spec: %s", root_spec);

    /* ext4 を /newroot にマウント */
    mount_newroot_with_retry(root_spec);

    /* ext4側へログを残す（できる範囲で） */
    append_boot_log_to_newroot();

    /* いよいよ移行 */
    log_console("[init] switching root...");

    free(root_spec);
    free(cmdline);

    do_switch_root();

    /* ここには戻ってこない */
    die("unexpected: switch_root returned");
    return 0;
}
