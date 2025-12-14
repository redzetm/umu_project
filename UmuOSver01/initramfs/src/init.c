#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>

static void mount_fs(const char *source, const char *target, const char *fstype)
{
	if (mount(source, target, fstype, 0, "") != 0) {
		fprintf(stderr, "mount(%s -> %s, %s) failed: %s\n",
			source, target, fstype, strerror(errno));
	}
}

static int cmdline_has_single(void)
{
	char buf[4096];
	ssize_t n;
	FILE *fp = fopen("/proc/cmdline", "r");

	if (!fp) {
		fprintf(stderr, "open /proc/cmdline failed: %s\n", strerror(errno));
		return 0;
	}

	n = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	if (n <= 0) {
		return 0;
	}
	buf[n] = '\0';

	return strstr(buf, "single") != NULL;
}

static void exec_shell(void)
{
	char *const argv[] = { (char *)"sh", NULL };
	execv("/bin/sh", argv);
	fprintf(stderr, "execv(/bin/sh) failed: %s\n", strerror(errno));
}

int main(void)
{
	/* --- 仮想ファイルシステムのマウント --- */
	mount_fs("proc", "/proc", "proc");
	mount_fs("sysfs", "/sys", "sysfs");
	mount_fs("devtmpfs", "/dev", "devtmpfs");

	/* --- 起動モードの判定 --- */
	if (cmdline_has_single()) {
		puts("UmuOSver01: Single-user rescue mode");
		exec_shell();
	} else {
		char *const argv[] = {
			(char *)"getty",
			(char *)"-L",
			(char *)"ttyS0",
			(char *)"115200",
			(char *)"vt100",
			NULL,
		};

		puts("UmuOSver01: Multi-user mode");
		execv("/bin/getty", argv);
		fprintf(stderr, "execv(/bin/getty) failed: %s\n", strerror(errno));

		/* Fallback */
		exec_shell();
	}

	for (;;) {
		pause();
	}
}
