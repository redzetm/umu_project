//今回の肝となる init プログラム
//init プログラムは initramfs の中で最初に実行される

#include <errno.h>      // エラー番号(errno)を使うため
#include <stdio.h>      // printf / fprintf / puts など
#include <string.h>     // strerror()（エラー文字列化）
#include <unistd.h>     // execv(), pause() など
#include <sys/mount.h>  // mount() システムコール
#include <sys/stat.h>   // mkdir()
#include <sys/types.h>  // pid_t, uid_t, gid_t
#include <sys/wait.h>   // waitpid()
#include <dirent.h>     // opendir()/readdir()

static int read_cmdline(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return -1;
    }

    FILE *fp = fopen("/proc/cmdline", "r");
    if (!fp) {
        return -1;
    }

    size_t n = fread(buf, 1, buf_len - 1, fp);
    fclose(fp);
    if (n == 0) {
        return -1;
    }

    buf[n] = '\0';
    return 0;
}

static int cmdline_get_value(const char *key, char *out, size_t out_len)
{
    if (!key || !out || out_len == 0) {
        return -1;
    }

    char cmdline[4096];
    if (read_cmdline(cmdline, sizeof(cmdline)) != 0) {
        return -1;
    }

    // Look for " key=value" or beginning-of-string "key=value"
    size_t key_len = strlen(key);
    const char *p = cmdline;

    for (;;) {
        const char *hit = strstr(p, key);
        if (!hit) {
            return -1;
        }

        if ((hit == cmdline || hit[-1] == ' ') && hit[key_len] == '=') {
            const char *value = hit + key_len + 1;
            size_t i = 0;
            while (value[i] != '\0' && value[i] != ' ' && i + 1 < out_len) {
                out[i] = value[i];
                i++;
            }
            out[i] = '\0';
            return (i > 0) ? 0 : -1;
        }

        p = hit + 1;
    }
}

/*
 * ファイルシステムをマウントするための関数
 *
 * source : 何をマウントするか（proc, sysfs など）
 * target : どこにマウントするか（/proc, /sys など）
 * fstype : ファイルシステムの種類
 */
static void mount_fs(const char *source, const char *target, const char *fstype)
{
    // mount() は Linux に「このファイルシステムを使えるようにして」
    // とお願いするシステムコール
    if (mount(source, target, fstype, 0, "") != 0) {

        // 失敗したらエラー内容を表示
        fprintf(stderr,
            "mount(%s -> %s, %s) failed: %s\n",
            source, target, fstype, strerror(errno));
    }
}

static void mkdir_if_missing(const char *path, mode_t mode)
{
    if (mkdir(path, mode) != 0) {
        if (errno != EEXIST) {
            fprintf(stderr, "mkdir(%s) failed: %s\n", path, strerror(errno));
        }
    }
}

static int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void sleep_ms(unsigned int ms)
{
    usleep(ms * 1000u);
}

static void log_sys_block_devices(void)
{
    DIR *dir = opendir("/sys/block");
    if (!dir) {
        fprintf(stderr, "opendir(/sys/block) failed: %s\n", strerror(errno));
        return;
    }

    fprintf(stderr, "available /sys/block devices:\n");
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        fprintf(stderr, "  %s\n", name);
    }

    closedir(dir);
}

static pid_t spawn_argv(const char *path, char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork() failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execv(path, argv);
        fprintf(stderr, "execv(%s) failed: %s\n", path, strerror(errno));
        _exit(127);
    }
    return pid;
}

static void wait_for_child(pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    for (;;) {
        int status;
        pid_t r = waitpid(pid, &status, 0);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

static int mount_bind(const char *source, const char *target)
{
    // Some environments reject MS_REC with bind-mount; try plain MS_BIND first.
    if (mount(source, target, NULL, MS_BIND, NULL) == 0) {
        return 0;
    }

    int first_errno = errno;

    if (mount(source, target, NULL, MS_BIND | MS_REC, NULL) == 0) {
        fprintf(stderr, "bind mount(%s -> %s) succeeded with MS_REC after retry\n", source, target);
        return 0;
    }

    fprintf(stderr,
        "bind mount(%s -> %s) failed: %s (first try: %s)\n",
        source,
        target,
        strerror(errno),
        strerror(first_errno));
    return -1;
}

static const char *pick_net_ifname(void)
{
    if (file_exists("/sys/class/net/eth0")) {
        return "eth0";
    }

    DIR *dir = opendir("/sys/class/net");
    if (!dir) {
        return NULL;
    }

    static char ifname[64];
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (strcmp(ent->d_name, "lo") == 0) {
            continue;
        }
        strncpy(ifname, ent->d_name, sizeof(ifname) - 1);
        ifname[sizeof(ifname) - 1] = '\0';
        closedir(dir);
        return ifname;
    }

    closedir(dir);
    return NULL;
}

static void bring_up_network_and_telnet(void)
{
    const char *ifname = pick_net_ifname();

    // Allow override by kernel cmdline: umuifname=ens3 etc.
    static char ifname_override[64];
    if (cmdline_get_value("umuifname", ifname_override, sizeof(ifname_override)) == 0) {
        ifname = ifname_override;
    }

    if (!ifname) {
        fprintf(stderr, "no network interface found under /sys/class/net\n");
        return;
    }

    char ip_cidr[64];
    char gw[64];
    char dns[64];
    int has_static_ip = (cmdline_get_value("umuip", ip_cidr, sizeof(ip_cidr)) == 0);
    int has_gw = (cmdline_get_value("umugw", gw, sizeof(gw)) == 0);
    int has_dns = (cmdline_get_value("umudns", dns, sizeof(dns)) == 0);

    // loopback up
    {
        char *const argv[] = { (char *)"ip", (char *)"link", (char *)"set", (char *)"lo", (char *)"up", NULL };
        wait_for_child(spawn_argv("/bin/ip", argv));
    }

    // selected interface up
    {
        char *const argv[] = { (char *)"ip", (char *)"link", (char *)"set", (char *)"dev", (char *)ifname, (char *)"up", NULL };
        wait_for_child(spawn_argv("/bin/ip", argv));
    }

    if (has_static_ip) {
        fprintf(stderr, "static IP requested: %s dev %s\n", ip_cidr, ifname);

        // ip addr add <IP/CIDR> dev <if>
        {
            char *const argv[] = { (char *)"ip", (char *)"addr", (char *)"add", ip_cidr, (char *)"dev", (char *)ifname, NULL };
            wait_for_child(spawn_argv("/bin/ip", argv));
        }

        // default route
        if (has_gw) {
            char *const argv[] = { (char *)"ip", (char *)"route", (char *)"add", (char *)"default", (char *)"via", gw, (char *)"dev", (char *)ifname, NULL };
            wait_for_child(spawn_argv("/bin/ip", argv));
        } else {
            fprintf(stderr, "static IP set but umugw is missing; skip default route\n");
        }

        // resolv.conf
        if (has_dns) {
            mkdir_if_missing("/etc", 0755);
            FILE *rf = fopen("/etc/resolv.conf", "w");
            if (rf) {
                fprintf(rf, "nameserver %s\n", dns);
                fclose(rf);
            } else {
                fprintf(stderr, "write /etc/resolv.conf failed: %s\n", strerror(errno));
            }
        }
    } else {
        // DHCP (background)
        if (file_exists("/bin/udhcpc")) {
            char *const argv[] = { (char *)"udhcpc", (char *)"-i", (char *)ifname, (char *)"-b", (char *)"-q", NULL };
            spawn_argv("/bin/udhcpc", argv);
        } else {
            fprintf(stderr, "/bin/udhcpc not found; skip DHCP\n");
        }
    }

    // telnetd
    if (file_exists("/bin/telnetd") && file_exists("/bin/login")) {
        char *const argv[] = { (char *)"telnetd", (char *)"-l", (char *)"/bin/login", NULL };
        spawn_argv("/bin/telnetd", argv);
    } else {
        fprintf(stderr, "telnetd/login not found; skip telnet\n");
    }
}

static void setup_persistent_home(void)
{
    mkdir_if_missing("/persist", 0755);
    mkdir_if_missing("/persist/home", 0755);
    mkdir_if_missing("/home", 0755);

    // デバイスは起動直後すぐに /dev に現れないことがある（virtio-blk 等）
    // 少し待ってから探索する。
    const unsigned int total_wait_ms = 10000;
    const unsigned int step_ms = 100;

    // まずはよくある固定候補を優先
    const char *static_candidates[] = { "/dev/vda", "/dev/vdb", "/dev/sda", "/dev/sdb" };

    // 次に /sys/block を見て動的に候補を集める（sr0/cdrom 等は除外）
    for (unsigned int waited = 0; waited <= total_wait_ms; waited += step_ms) {
        const char *device = NULL;
        char device_buf[256];
        char part_buf[256];

        for (size_t i = 0; i < sizeof(static_candidates) / sizeof(static_candidates[0]); i++) {
            if (file_exists(static_candidates[i])) {
                device = static_candidates[i];
                break;
            }
        }

        if (!device) {
            DIR *dir = opendir("/sys/block");
            if (dir) {
                struct dirent *ent;
                while ((ent = readdir(dir)) != NULL) {
                    const char *name = ent->d_name;
                    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                        continue;
                    }
                    // CD-ROM / loop / ram は対象外
                    if (strncmp(name, "sr", 2) == 0 || strncmp(name, "loop", 4) == 0 || strncmp(name, "ram", 3) == 0) {
                        continue;
                    }

                    // virtio-blk: vda/vdb..., SCSI/SATA: sda/sdb...
                    if (!(strncmp(name, "vd", 2) == 0 || strncmp(name, "sd", 2) == 0 || strncmp(name, "xvd", 3) == 0)) {
                        continue;
                    }

                    int n = snprintf(device_buf, sizeof(device_buf), "/dev/%s", name);
                    if (n > 0 && (size_t)n < sizeof(device_buf) && file_exists(device_buf)) {
                        device = device_buf;
                        break;
                    }
                }
                closedir(dir);
            }
        }

        if (!device) {
            sleep_ms(step_ms);
            continue;
        }

        fprintf(stderr, "persistent candidate: %s\n", device);

        // まずディスク直（mkfs.ext4を /dev/vda に実施している想定）を試す
        if (mount(device, "/persist", "ext4", MS_RELATIME, NULL) == 0) {
            goto mounted;
        }

        int first_errno = errno;

        // 次にパーティション構成（/dev/vda1 等）の可能性も試す
        int pn = snprintf(part_buf, sizeof(part_buf), "%s1", device);
        if (pn > 0 && (size_t)pn < sizeof(part_buf) && file_exists(part_buf)) {
            if (mount(part_buf, "/persist", "ext4", MS_RELATIME, NULL) == 0) {
                fprintf(stderr, "mounted persistent disk from %s\n", part_buf);
                goto mounted;
            }
        }

        fprintf(stderr, "mount(ext4 %s -> /persist) failed: %s\n", device, strerror(first_errno));
        if (first_errno == ENODEV) {
            fprintf(stderr,
                "hint: ext4 or block driver may be missing (need CONFIG_EXT4_FS=y and virtio-blk built-in for /dev/vd*)\n");
        }
        return;
    }

    fprintf(stderr, "no persistent block device appeared under /dev; skip ext4 mount\n");
    log_sys_block_devices();
    return;

mounted:

    // mount(/persist) 後は ext4 のルートに切り替わるため、/persist/home は再作成が必要
    mkdir_if_missing("/persist/home", 0755);

    // Ensure a usable home exists on first boot
    mkdir_if_missing("/persist/home/tama", 0755);

    // Target must exist for bind mount
    mkdir_if_missing("/home", 0755);
    mkdir_if_missing("/home/tama", 0755);

    if (mount_bind("/persist/home", "/home") != 0) {
        return;
    }

    fprintf(stderr, "bind-mounted /persist/home -> /home\n");

    // Ensure ownership for the normal user
    if (chown("/home/tama", (uid_t)1000, (gid_t)1000) != 0) {
        fprintf(stderr, "chown(/home/tama) failed: %s\n", strerror(errno));
    }
}

/*
 * カーネル起動時のコマンドラインに
 * 「single」という文字が含まれているか調べる関数
 *
 * single があれば「シングルユーザーモード」
 */
static int cmdline_has_single(void)
{
    char buf[4096];     // /proc/cmdline を読むためのバッファ
    ssize_t n;
    FILE *fp = fopen("/proc/cmdline", "r"); // 起動時パラメータを読む

    // ファイルが開けなかった場合
    if (!fp) {
        fprintf(stderr,
            "open /proc/cmdline failed: %s\n",
            strerror(errno));
        return 0;
    }

    // ファイルの中身を読み込む
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    // 読み込み失敗
    if (n <= 0) {
        return 0;
    }

    // 文字列として扱えるように終端を付ける
    buf[n] = '\0';

    // "single" という文字列が含まれているか？
    // 見つかれば NULL 以外が返る
    return strstr(buf, "single") != NULL;
}

/*
 * /bin/sh を起動する関数
 * つまり「シェルを起動する」
 */
static void exec_shell(void)
{
    // execv に渡す引数配列
    char *const argv[] = { (char *)"sh", NULL };

    // 現在のプロセスを /bin/sh に置き換える
    execv("/bin/sh", argv);

    // execv が戻ってくるのは失敗したときだけ
    fprintf(stderr,
        "execv(/bin/sh) failed: %s\n",
        strerror(errno));
}

static pid_t start_getty_serial(void)
{
    char *const argv[] = {
        (char *)"getty",
        (char *)"-L",
        (char *)"115200",
        (char *)"ttyS0",
        (char *)"vt100",
        NULL,
    };
    return spawn_argv("/bin/getty", argv);
}

static pid_t start_getty_tty1(void)
{
    char *const argv[] = {
        (char *)"getty",
        (char *)"0",
        (char *)"tty1",
        (char *)"linux",
        NULL,
    };
    return spawn_argv("/bin/getty", argv);
}

int main(void)
{
    /*
     * --- 仮想ファイルシステムのマウント ---
     *
     * これをしないと Linux はほとんど何もできない
     */

    // プロセス情報を見るための /proc
    mount_fs("proc", "/proc", "proc");

    // デバイスやカーネル情報を見るための /sys
    mount_fs("sysfs", "/sys", "sysfs");

    // /dev 以下にデバイスファイルを作るため
    mount_fs("devtmpfs", "/dev", "devtmpfs");

    // telnet/login で pseudo-tty が必要
    mkdir_if_missing("/dev/pts", 0755);
    mount_fs("devpts", "/dev/pts", "devpts");

    /*
     * --- 起動モードの判定 ---
     */
    if (cmdline_has_single()) {

        // single が指定されていた場合
        puts("UmuOSver01: Single-user rescue mode");

        // 直接シェルを起動（レスキューモード）
        exec_shell();

    } else {
        /*
         * 通常起動（マルチユーザーモード）
         * getty を起動してログイン待ちにする
         */

        puts("UmuOSver01: Multi-user mode");

        // ext4永続化（/home を bind mount）
        setup_persistent_home();

        // DHCP + telnetd
        bring_up_network_and_telnet();

        // getty を複数起動（シリアルTTYは必須 + 画面側 tty1 も提供）
        pid_t getty_serial = start_getty_serial();
        pid_t getty_tty1 = start_getty_tty1();

        // PID 1 として子プロセスを回収し、落ちたgettyを再起動する
        for (;;) {
            int status;
            pid_t died = waitpid(-1, &status, 0);
            if (died < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "waitpid() failed: %s\n", strerror(errno));
                pause();
                continue;
            }

            if (died == getty_serial) {
                getty_serial = start_getty_serial();
            } else if (died == getty_tty1) {
                getty_tty1 = start_getty_tty1();
            }
        }
    }

    /*
     * ここまで来ることは基本的にないが、
     * 万が一のため無限待機
     */
    for (;;) {
        pause(); // シグナルが来るまで何もしない
    }
}
