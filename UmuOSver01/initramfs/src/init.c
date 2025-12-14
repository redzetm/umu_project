#include <errno.h>      // エラー番号(errno)を使うため
#include <stdio.h>      // printf / fprintf / puts など
#include <string.h>     // strerror()（エラー文字列化）
#include <unistd.h>     // execv(), pause() など
#include <sys/mount.h>  // mount() システムコール

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

        char *const argv[] = {
            (char *)"getty",   // プログラム名
            (char *)"-L",      // ローカルライン
            (char *)"ttyS0",   // シリアルポート
            (char *)"115200",  // ボーレート
            (char *)"vt100",   // 端末タイプ
            NULL,
        };

        puts("UmuOSver01: Multi-user mode");

        // getty を起動
        execv("/bin/getty", argv);

        // 失敗した場合はエラー表示
        fprintf(stderr,
            "execv(/bin/getty) failed: %s\n",
            strerror(errno));

        // 最後の保険：シェルを起動
        exec_shell();
    }

    /*
     * ここまで来ることは基本的にないが、
     * 万が一のため無限待機
     */
    for (;;) {
        pause(); // シグナルが来るまで何もしない
    }
}
