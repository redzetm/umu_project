#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void mount_fs()
{
    mount("none", "/proc", "proc", 0, "");
    mount("none", "/sys", "sysfs", 0, "");
    mount("none", "/dev", "devtmpfs", 0, "");
}

static int cmdline_contains_single()
{
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) return 0;

    char buf[1024] = {0};
    read(fd, buf, sizeof(buf)-1);
    close(fd);

    return strstr(buf, "single") != NULL;
}

int main(int argc, char *argv[])
{
    mount_fs();

    if (cmdline_contains_single()) {
        write(1, "Umu Project step4: Single-user rescue mode\n", 43);

        /* BusyBox の /bin/sh の代わりに自作シェルを exec */
        execl("/bin/tiny-shell", "tiny-shell", NULL);

        /* exec が失敗した場合 */
        write(1, "Failed to exec tiny-shell\n", 26);
        return 1;
    } else {
        write(1, "Umu Project step4: Multi-user mode\n", 35);

        /* BusyBox の getty の代わりに自作 getty を exec */
        execl("/bin/tiny-getty", "tiny-getty", "ttyS0", "115200", "vt100", NULL);

        write(1, "Failed to exec tiny-getty\n", 26);
        return 1;
    }
}
