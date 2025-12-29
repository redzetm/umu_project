# init.c 疑似コード版（UmuOS-0.1.1 /init）

目的：
- initramfs 上で動く `/init`（C実装）の処理を、日本語の疑似コードとして固定する。
- 将来、実装を作り直す場合でも、この文書を元に同等の挙動（観測点・切り分け容易性・混線防止）を再現できる状態にする。

重要方針（0.1.1）：
- `root=UUID=...` を **自前で解決**して ext4 を mount し、`switch_root` まで到達する。
- **観測性**：失敗理由と走査内容を必ずログに出す。
- **混線防止**：initramfs 段階では永続領域（`/newroot` 配下）へ書かない。

---

## 0. インクルード（利用する標準ライブラリ/機能）

このファイルでは、以下の標準ライブラリ機能を利用する：

- 文字判定（ctype）
- ディレクトリ走査（dirent）
- エラー番号（errno）
- ファイル操作（open, read, write など）
- 可変引数処理（stdarg）
- 真偽値（stdbool）
- 固定幅整数（stdint）
- 標準入出力（stdio）
- メモリ管理・文字列処理（stdlib, string）
- ファイルシステムのマウント（sys/mount）
- ファイル属性取得（sys/stat）
- 型定義（sys/types）
- システムコール（unistd）

（実装上は概ね以下を include する）

```c
#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
```

---

## 1. 定数（設計上固定するサイズ）

- `CMDLINE_MAX = 4096`
  - `/proc/cmdline` を読む最大長
- `UUID_BIN_LEN = 16`
  - UUID をバイナリ16バイトとして扱う
- `UUID_STR_MAX = 64`
  - UUID 文字列（ハイフン込み等）を出力するバッファ

---

## 2. ログ出力

### 2.1 static void log_printf(フォーマット文字列, 可変引数)

目的：
- initramfs の観測点を **確実に出す**。
- 可能なら `/dev/console` と `stderr` の両方へ出して取りこぼしを減らす。

疑似コード：

```text
static void log_printf(フォーマット文字列 fmt, 可変引数 ...)
{
    1. 可変引数を使ってメッセージを文字列バッファに整形する
       → printf と同じ形式で buf に書き込む（vsnprintf）

    2. メッセージの末尾が改行で終わっているか確認する
       → 改行が無ければ buf の末尾に '\n' を追加する

    3. /dev/console を書き込み用で開く
       → 開けた場合は buf を write() して close()
       → これは ttyS0 のログ取りこぼしを防ぐため

    4. 標準エラー（fd=2）にも buf を write() する
       → QEMU のシリアル出力（ttyS0）に確実に流れる

    5. stderr を flush する
       → バッファリングによる遅延を防ぐ
}
```

---

## 3. 失敗時停止

### 3.1 static void emergency_loop()

目的：
- “落ちる”より“止まる”を優先し、シリアルログを残したまま停止する。

疑似コード：

```text
static void emergency_loop()
{
    1. "緊急停止に入る" 旨をログに出す
    2. 無限ループ:
       - sleep(1) を繰り返す
}
```

---

## 4. ディレクトリ作成・マウント

### 4.1 static int ensure_dir(path, mode)

目的：
- マウント先や必要ディレクトリの存在を保証する。

疑似コード：

```text
static int ensure_dir(path, mode)
{
    1. mkdir(path, mode) を試す
    2. 成功なら 0 を返す
    3. 失敗でも errno が EEXIST なら 0 を返す（既に存在するため）
    4. それ以外の失敗はログを出して -1 を返す
}
```

### 4.2 static int mount_if_needed(source, target, fstype, flags, data)

目的：
- `/proc` `/sys` `/dev` `/dev/pts` 等を安全に mount する。
- 既に mount 済みでも「失敗として扱わない」ことで混線を減らす。

疑似コード：

```text
static int mount_if_needed(source, target, fstype, flags, data)
{
    1. ensure_dir(target) を実行して target を作る
       → 失敗したら -1

    2. mount(source, target, fstype, flags, data) を実行する

    3. 成功したら
       - "mount ok" をログに出す
       - 0 を返す

    4. 失敗したが errno == EBUSY の場合
       - "mount skip (busy)" をログに出す
       - 0 を返す（既に mount 済みとみなす）

    5. その他の失敗
       - "mount failed" を errno 付きでログに出す
       - -1 を返す
}
```

---

## 5. ファイル読み取り

### 5.1 static int read_file_to_buf(path, buf, buf_size)

目的：
- `/proc/cmdline` 等のテキストを読み取る。

疑似コード：

```text
static int read_file_to_buf(path, buf, buf_size)
{
    1. open(path, O_RDONLY) する
       → 失敗したら errno をログに出して -1

    2. read(fd, buf, buf_size - 1) で読み込む
       → 失敗したら errno をログに出して -1

    3. buf[n] = '\0' で必ず終端する

    4. close(fd)

    5. 0 を返す
}
```

---

## 6. UUID処理

### 6.1 static int hex_value(c)

目的：
- 16進数字（0-9, a-f, A-F）を 0〜15 に変換する。

疑似コード：

```text
static int hex_value(c)
{
    1. '0'..'9' なら c - '0'
    2. 'a'..'f' なら 10 + (c - 'a')
    3. 'A'..'F' なら 10 + (c - 'A')
    4. それ以外は -1
}
```

### 6.2 static int parse_uuid_string(s, out16)

目的：
- `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` 形式の UUID 文字列を、バイナリ16バイトに変換する。
- ハイフンは無視し、空白/改行/タブで終了とみなす。

疑似コード：

```text
static int parse_uuid_string(s, out16)
{
    1. out_pos = 0, high = 未設定

    2. 文字列を先頭から走査する:
       - 空白/改行/タブに当たったら終了
       - '-' は無視して次へ
       - hex_value() が -1 なら失敗

       - high が未設定なら high = v として次へ
       - high が設定済みなら (high<<4 | v) を1バイトとして out16[out_pos] に格納
         → out_pos++
         → high を未設定に戻す

    3. high が残っていたら失敗（桁が奇数）
    4. out_pos が 16 でなければ失敗
    5. 成功なら 0
}
```

### 6.3 static void uuid_to_string(in16, outstr)

目的：
- UUID（16バイト）をログ出力向けの文字列へ整形する。

疑似コード：

```text
static void uuid_to_string(in16, outstr)
{
    1. 16バイトを 8-4-4-4-12 の形式で snprintf する
}
```

### 6.4 static int parse_root_uuid_from_cmdline(out16)

目的：
- `/proc/cmdline` を読み、`root=UUID=...` の UUID を取り出して 16バイト化する。

疑似コード：

```text
static int parse_root_uuid_from_cmdline(out16)
{
    1. read_file_to_buf("/proc/cmdline") で cmdline を読む
       → 失敗なら -1

    2. cmdline をログに出す（観測点）

    3. "root=UUID=" を検索する
       → 見つからなければログを出して -1

    4. 見つかった位置の直後から UUID 文字列として parse_uuid_string() する
       → 失敗ならログを出して -1

    5. uuid_to_string() で整形して
       - "cmdline parsed: root=UUID=..." をログ
       - "want root UUID: ..." をログ

    6. 0 を返す
}
```

---

## 7. ext4 UUID 読み取り（superblock 直読み）

### 7.1 static int read_ext4_uuid_from_device(dev_path, out16)

目的：
- `blkid` や `/dev/disk/by-uuid` に依存せず、ext4 superblock から UUID を読む。

前提（ext4）：
- superblock はデバイス先頭 + 1024 バイト
- magic は superblock + 0x38（0xEF53）
- UUID は superblock + 0x68（16バイト）

疑似コード：

```text
static int read_ext4_uuid_from_device(dev_path, out16)
{
    1. open(dev_path, O_RDONLY)
       → 失敗なら -1

    2. pread(fd, sb, 1024, offset=1024) で superblock 付近を読む
       → 読めなければ -1

    3. sb[0x38..0x39] から magic を取り出す
       → magic != 0xEF53 なら「ext4ではない」として -1

    4. sb[0x68..0x77] を out16 にコピー

    5. 0 を返す
}
```

---

## 8. デバイス探索（/dev 走査）

### 8.1 static bool is_candidate_dev_name(name)

目的：
- `/dev` 配下の全エントリを対象にするとノイズが多いため、候補名だけに絞る。

候補（最低限）：
- `vd*`（virtio-blk）
- `sd*`（SCSI）
- `nvme*`（NVMe）

疑似コード：

```text
static bool is_candidate_dev_name(name)
{
    1. name が "vd" で始まり、3文字目が英字なら true
    2. name が "sd" で始まり、3文字目が英字なら true
    3. name が "nvme" で始まるなら true
    4. それ以外は false
}
```

### 8.2 static bool is_block_device(path)

目的：
- 対象がブロックデバイス（S_ISBLK）か確認する。

疑似コード：

```text
static bool is_block_device(path)
{
    1. stat(path) する
       → 失敗なら false
    2. st_mode がブロックデバイスなら true
    3. それ以外は false
}
```

### 8.3 static int find_device_by_uuid(want_uuid16, out_path)

目的：
- `/dev` を走査し、ext4 UUID が `want_uuid16` と一致するブロックデバイスを探す。

観測点：
- `scan: /dev/...` を複数出し、何を走査したか残す
- 一致したら `matched: dev=... uuid=...` を出す

疑似コード：

```text
static int find_device_by_uuid(want_uuid16, out_path)
{
    1. opendir("/dev")
       → 失敗なら errno をログに出して -1

    2. readdir で /dev 配下を列挙:
       - "." で始まるものはスキップ
       - is_candidate_dev_name で候補以外はスキップ
       - dev_path = "/dev/" + name
       - is_block_device(dev_path) が false ならスキップ

       - scanned++
       - "scan: dev_path" をログに出す

       - read_ext4_uuid_from_device(dev_path, got_uuid16)
         → 失敗ならスキップ

       - got_uuid16 == want_uuid16 なら
         - out_path に dev_path を格納
         - "matched: dev=... uuid=..." をログ
         - closedir して 0 を返す

    3. 全部見つからなければ
       - "scan done: no match" をログ
       - -1 を返す
}
```

（補助）候補のUUID一覧を出す `dump_ext4_candidate_uuids()` は、
- タイムアウト時の切り分け用に、ext4候補の UUID を列挙してログに残す。

---

## 9. main（全体フロー）

目的：
- `UEFI→GRUB→kernel→initramfs` の後、永続 ext4（disk.img）へ移行して `switch_root` する。

大きな流れ：
1) proc/sys/dev/devpts を mount（観測性の土台）
2) cmdline から root UUID を取り出す
3) /dev を走査して UUID一致デバイスを探す
4) /newroot に ext4 を mount
5) /bin/switch_root で /sbin/init（ext4側）へ移行

疑似コード：

```text
int main()
{
    0. "init start" をログに出す

    1. 最低限のFSを mount する（失敗したら emergency_loop）
       - mount_if_needed("proc",   "/proc",    "proc")
       - mount_if_needed("sysfs",  "/sys",     "sysfs")
       - mount_if_needed("devtmpfs","/dev",    "devtmpfs")
       - "devtmpfs mounted" をログ
       - mount_if_needed("devpts", "/dev/pts", "devpts")

    2. root UUID を取得する
       - parse_root_uuid_from_cmdline(want_uuid16)
       - 失敗したら emergency_loop

    3. ルートデバイス探索と mount（リトライ付き）
       - max_tries と wait_us を決める（例：30秒程度）

       ループ i = 1..max_tries:
         a) find_device_by_uuid(want_uuid16, dev_path)
            - 見つからなければ "retry (device not found yet)" をログして待つ

         b) 見つかったら "root device found" をログ

         c) ensure_dir("/newroot")
            - 失敗したら emergency_loop

         d) mount(dev_path, "/newroot", "ext4", flags=0, data="")
            - 成功したら
              - "mount root ok" と "mounted /newroot" をログ
              - （混線防止）永続側へ書かず、必要ファイルが存在するかだけ access() で確認
              - ループを抜ける
            - 失敗したら
              - errno 付きでログ
              - "retry" をログして待つ

       - タイムアウトしたら
         - 欲しいUUIDを再度ログ
         - dump_ext4_candidate_uuids() で候補のUUIDを列挙
         - emergency_loop

    4. switch_root
       - "switching root" をログ
       - "exec: /bin/switch_root /newroot /sbin/init" をログ
       - execv("/bin/switch_root", ["switch_root", "/newroot", "/sbin/init"])

       - exec が失敗したら
         - errno 付きでログ
         - 必要物（/bin/switch_root, /newroot/sbin/init, /newroot/etc/inittab など）の注意をログ
         - emergency_loop
}
```

---

## 10. 観測点チェック（ログで判断するためのキーワード）

initramfs 段階で、少なくとも以下がシリアルへ出ることを期待する。

- `/proc/cmdline: ...`（root=UUID を含む）
- `cmdline parsed: root=UUID=...`
- `want root UUID: ...`
- `devtmpfs mounted`
- `scan: /dev/...`（複数）
- `matched: dev=... uuid=...`（一致時）
- `mount root ok` / `mounted /newroot`
- `exec: /bin/switch_root /newroot /sbin/init`

※この後のログインや永続ログ追記は ext4 側の責務。
