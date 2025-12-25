#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void log_console(const char *fmt, ...)
{
    char buf[2048];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    size_t len = strnlen(buf, sizeof(buf));
    if (len == 0 || buf[len - 1] != '\n') {
        if (len + 1 < sizeof(buf)) {
            buf[len] = '\n';
            buf[len + 1] = '\0';
            len++;
        }
    }

    int fd = open("/dev/console", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)write(fd, buf, len);
        close(fd);
    }

    (void)write(2, buf, len);
}

static void die(const char *fmt, ...)
{
    char buf[2048];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    log_console("[init] FATAL: %s", buf);
    sleep(2);
    _exit(1);
}

static void ensure_dir(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == 0) return;
    if (errno == EEXIST) return;
    die("mkdir %s failed: %s", path, strerror(errno));
}

static bool is_mounted(const char *target)
{
    int fd = open("/proc/mounts", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    char buf[8192];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;

    buf[n] = '\0';

    char needle[PATH_MAX + 3];
    snprintf(needle, sizeof(needle), " %s ", target);
    return strstr(buf, needle) != NULL;
}

static void mount_or_warn(const char *src, const char *tgt, const char *fstype, unsigned long flags, const char *data)
{
    if (mount(src, tgt, fstype, flags, data) != 0) {
        log_console("[init] WARN: mount %s on %s (%s) failed: %s", src, tgt, fstype, strerror(errno));
    }
}

static void mount_basic_filesystems(void)
{
    ensure_dir("/proc", 0555);
    ensure_dir("/sys", 0555);
    ensure_dir("/dev", 0755);
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

    if (!is_mounted("/dev")) {
        mount_or_warn("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755");
    }

    /* devtmpfs を mount すると /dev/pts が消えるので作り直してから mount */
    ensure_dir("/dev/pts", 0755);
    if (!is_mounted("/dev/pts")) {
        mount_or_warn("devpts", "/dev/pts", "devpts", 0, "");
    }
}

static char *read_cmdline(void)
{
    int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        die("open /proc/cmdline failed: %s", strerror(errno));
    }

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        die("read /proc/cmdline failed: %s", (n < 0) ? strerror(errno) : "empty");
    }

    buf[n] = '\0';
    return strdup(buf);
}

static char *parse_root_spec_from_cmdline(const char *cmdline)
{
    const char *key = "root=";
    const char *p = strstr(cmdline, key);
    if (!p) return NULL;

    p += strlen(key);

    char tmp[256];
    size_t i = 0;
    while (*p && !isspace((unsigned char)*p) && i + 1 < sizeof(tmp)) {
        tmp[i++] = *p++;
    }
    tmp[i] = '\0';

    return strdup(tmp);
}

static int hexval(int c)
{
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return 10 + (c - 'a');
    if ('A' <= c && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int parse_uuid_str(const char *s, uint8_t out[16])
{
    int j = 0;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '-') continue;
        int hi = hexval((unsigned char)s[i]);
        int lo = hexval((unsigned char)s[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        if (j >= 16) return -1;
        out[j++] = (uint8_t)((hi << 4) | lo);
        i++;
    }
    return (j == 16) ? 0 : -1;
}

/* ext4 superblock: offset 1024
 * magic: 0x38 (0xEF53), uuid: 0x68 (16 bytes)
 */
static int read_ext4_uuid(const char *devpath, uint8_t out_uuid[16])
{
    int fd = open(devpath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    uint8_t sb[2048];
    ssize_t n = pread(fd, sb, sizeof(sb), 1024);
    close(fd);

    if (n < 0) return -1;
    if (n < (ssize_t)(0x68 + 16)) return -1;

    uint16_t magic = (uint16_t)sb[0x38] | ((uint16_t)sb[0x39] << 8);
    if (magic != 0xEF53) return -1;

    memcpy(out_uuid, sb + 0x68, 16);
    return 0;
}

static bool is_candidate_block_name(const char *name)
{
    if (strncmp(name, "vd", 2) == 0) return true;
    if (strncmp(name, "sd", 2) == 0) return true;
    if (strncmp(name, "xvd", 3) == 0) return true;
    if (strncmp(name, "nvme", 4) == 0) return true;
    return false;
}

static int read_sysfs_dev_numbers(const char *block_name, unsigned *maj_out, unsigned *min_out)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/class/block/%s/dev", block_name);

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;

    buf[n] = '\0';

    unsigned maj = 0, min = 0;
    if (sscanf(buf, "%u:%u", &maj, &min) != 2) return -1;

    *maj_out = maj;
    *min_out = min;
    return 0;
}

static int ensure_block_node(const char *block_name, char out_devpath[PATH_MAX])
{
    snprintf(out_devpath, PATH_MAX, "/dev/%s", block_name);

    struct stat st;
    if (stat(out_devpath, &st) == 0) {
        if (S_ISBLK(st.st_mode)) return 0;
        errno = ENOTBLK;
        return -1;
    }

    unsigned maj = 0, min = 0;
    if (read_sysfs_dev_numbers(block_name, &maj, &min) != 0) {
        return -1;
    }

    dev_t dev = makedev(maj, min);
    if (mknod(out_devpath, S_IFBLK | 0600, dev) != 0) {
        return -1;
    }

    return 0;
}

static int find_blockdev_by_ext4_uuid(const uint8_t target[16], char out_dev[PATH_MAX])
{
    DIR *d = opendir("/sys/class/block");
    if (!d) return -1;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.') continue;
        if (!is_candidate_block_name(name)) continue;

        char devpath[PATH_MAX];
        if (ensure_block_node(name, devpath) != 0) {
            continue;
        }

        uint8_t u[16];
        if (read_ext4_uuid(devpath, u) == 0 && memcmp(u, target, 16) == 0) {
            strncpy(out_dev, devpath, PATH_MAX - 1);
            out_dev[PATH_MAX - 1] = '\0';
            closedir(d);
            return 0;
        }
    }

    closedir(d);
    errno = ENOENT;
    return -1;
}

static int resolve_uuid_root_to_devpath(const char *root_spec, char out_dev[PATH_MAX])
{
    if (strncmp(root_spec, "UUID=", 5) != 0) {
        errno = EINVAL;
        return -1;
    }

    uint8_t target[16];
    if (parse_uuid_str(root_spec + 5, target) != 0) {
        errno = EINVAL;
        return -1;
    }

    return find_blockdev_by_ext4_uuid(target, out_dev);
}

static void mount_newroot_with_retry(const char *root_spec)
{
    ensure_dir("/newroot", 0755);

    const char *fstype = "ext4";
    const int max_tries = 30;

    char src[PATH_MAX];
    src[0] = '\0';

    for (int i = 1; i <= max_tries; i++) {
        const char *mount_src = root_spec;

        if (strncmp(root_spec, "UUID=", 5) == 0) {
            if (resolve_uuid_root_to_devpath(root_spec, src) == 0) {
                mount_src = src;
            } else {
                log_console("[init] resolve UUID failed (try %d/%d): %s", i, max_tries, strerror(errno));
                usleep(500 * 1000);
                continue;
            }
        }

        if (mount(mount_src, "/newroot", fstype, MS_RELATIME, "rw") == 0) {
            log_console("[init] mounted %s on /newroot (type=%s)", mount_src, fstype);
            return;
        }

        log_console("[init] mount try %d/%d failed: %s", i, max_tries, strerror(errno));
        usleep(500 * 1000);
    }

    die("mount /newroot failed after retries (root=%s)", root_spec);
}

static void append_boot_log_to_newroot(void)
{
    ensure_dir("/newroot/logs", 0755);

    int fd = open("/newroot/logs/boot.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return;

    time_t now = time(NULL);
    char line[256];
    int n = snprintf(line, sizeof(line), "[initramfs] switch_root at %ld\n", (long)now);
    if (n > 0) (void)write(fd, line, (size_t)n);

    close(fd);
}

static void do_switch_root(void)
{
    char *const argv1[] = {"switch_root", "/newroot", "/sbin/init", NULL};
    execv("/bin/switch_root", argv1);

    char *const argv2[] = {"busybox", "switch_root", "/newroot", "/sbin/init", NULL};
    execv("/bin/busybox", argv2);

    die("exec switch_root failed: %s", strerror(errno));
}

int main(void)
{
    if (getpid() != 1) {
        log_console("[init] WARN: pid is %d (expected 1)", getpid());
    }

    log_console("[init] UmuOS initramfs init starting");

    mount_basic_filesystems();

    char *cmdline = read_cmdline();
    log_console("[init] cmdline: %s", cmdline);

    char *root_spec = parse_root_spec_from_cmdline(cmdline);
    if (!root_spec) {
        free(cmdline);
        die("root=... not found in /proc/cmdline");
    }

    log_console("[init] root spec: %s", root_spec);

    mount_newroot_with_retry(root_spec);
    append_boot_log_to_newroot();

    free(root_spec);
    free(cmdline);

    log_console("[init] switching root...");
    do_switch_root();

    die("unexpected: switch_root returned");
    return 0;
}