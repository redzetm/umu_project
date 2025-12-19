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
    if (mount(source, target, NULL, MS_BIND | MS_REC, NULL) != 0) {
        fprintf(stderr, "bind mount(%s -> %s) failed: %s\n", source, target, strerror(errno));
        return -1;
    }
    return 0;
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
    if (!ifname) {
        fprintf(stderr, "no network interface found under /sys/class/net\n");
        return;
    }

    // loopback up
    {
        char *const argv[] = { (char *)"ip", (char *)"link", (char *)"set", (char *)"lo", (char *)"up", NULL };
        spawn_argv("/bin/ip", argv);
    }

    // selected interface up
    {
        char *const argv[] = { (char *)"ip", (char *)"link", (char *)"set", (char *)"dev", (char *)ifname, (char *)"up", NULL };
        spawn_argv("/bin/ip", argv);
    }

    // DHCP (background)
    if (file_exists("/bin/udhcpc")) {
        char *const argv[] = { (char *)"udhcpc", (char *)"-i", (char *)ifname, (char *)"-b", (char *)"-q", NULL };
        spawn_argv("/bin/udhcpc", argv);
    } else {
        fprintf(stderr, "/bin/udhcpc not found; skip DHCP\n");
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
    const char *candidates[] = { "/dev/vda", "/dev/sda", "/dev/vdb", "/dev/sdb" };
    const char *device = NULL;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (file_exists(candidates[i])) {
            device = candidates[i];
            break;
        }
    }

    if (!device) {
        fprintf(stderr, "no persistent block device found; skip ext4 mount\n");
        return;
    }

    mkdir_if_missing("/persist", 0755);
    mkdir_if_missing("/persist/home", 0755);

    if (mount(device, "/persist", "ext4", MS_RELATIME, NULL) != 0) {
        fprintf(stderr, "mount(ext4 %s -> /persist) failed: %s\n", device, strerror(errno));
        return;
    }

    // Ensure a usable home exists on first boot
    mkdir_if_missing("/persist/home/tama", 0755);

    if (mount_bind("/persist/home", "/home") != 0) {
        return;
    }

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
        (char *)"ttyS0",
        (char *)"115200",
        (char *)"vt100",
        NULL,
    };
    return spawn_argv("/bin/getty", argv);
}

static pid_t start_getty_tty1(void)
{
    char *const argv[] = {
        (char *)"getty",
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
