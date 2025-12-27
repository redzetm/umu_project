# プログラム設計書(init.c)

対象：UmuOS ver0.1.1 の initramfs 上で動作する自作 init

- 実装：UmuOSver011/initramfs/src/init.c
- 設計ゴール：
  - 「何が起きているか」がログで追える
  - 依存を増やさない（udev/blkid 等に頼らず自前で UUID を読む）
  - switch_root までを確実に行う

---

## 1. プログラムの全体像

initramfs は「仮の root」です。最終的に使いたい root（ext4 の disk.img）を見つけて mount し、`switch_root` で切り替えます。

この `init.c` は次の3段階だけを担当します。

1) 準備：/proc, /sys, /dev などを mount して情報を取れる状態にする
2) 探索：`root=UUID=...` に一致する ext4 デバイスを `/dev` から探す
3) 移行：見つけたデバイスを `/newroot` に mount → `/bin/switch_root` 実行

---

## 2. モジュール分割（ファイルは1つ、役割は分ける）

### 2.1 ログ
- `log_printf(fmt, ...)`
  - stdout/stderr に `[init]` 付きで出す
  - エラー時は `errno` も合わせて出す（原因追跡）

### 2.2 マウント/ディレクトリ作成
- `ensure_dir(path, mode)`
  - `/newroot` や `/dev/pts` などの存在を保証する
- `mount_if_needed(source, target, fstype, flags, data)`
  - 既に mount 済みでも安全（重複で落ちない）

### 2.3 文字列・UUID処理
- `read_file_to_buf(path, buf, buflen)`
  - `/proc/cmdline` を読む
- `parse_uuid_string("xxxxxxxx-....", out16)`
  - UUID文字列を16バイトに変換
- `uuid_to_string(in16, outstr)`
  - 16バイトをログ出力しやすい文字列へ変換
- `parse_root_uuid_from_cmdline(out16)`
  - `/proc/cmdline` → `root=UUID=` 抽出 → 16バイト化

### 2.4 ext4 UUID 読み取り
- `read_ext4_uuid_from_device(devpath, out16)`
  - superblock から UUID を読み取る
  - 読み取り位置：`1024 + 0x68`

### 2.5 デバイス探索
- `find_device_by_uuid(want_uuid16, out_devpath, outlen)`
  - `/dev` を走査して一致するデバイスを探す
  - 候補名（例）：vd*, sd*, nvme*

### 2.6 永続ログ追記
- `append_boot_log_under_newroot(newroot)`
  - `/newroot/logs/boot.log` に追記して「永続側まで到達」した証拠を残す

### 2.7 失敗時停止
- `emergency_loop(reason)`
  - ログを出して `sleep(1)` で停止
  - 受入時の切り分けのため「落ちる」より「止まる」

---

## 3. 主要データ（設計として意識するもの）

- `uint8_t root_uuid[16]`
  - `root=UUID=` の期待値（バイナリ16バイト）
- `char devpath[... ]`
  - 一致したデバイスパス（例：`/dev/vda`）
- `char cmdline[... ]`
  - `/proc/cmdline` の生文字列

---

## 4. メイン処理の擬似コード（読みやすさ重視）

```text
main:
  log("start")

  ensure_dir("/proc")
  mount_if_needed("proc", "/proc", "proc")

  ensure_dir("/sys")
  mount_if_needed("sysfs", "/sys", "sysfs")

  ensure_dir("/dev")
  mount_if_needed("devtmpfs", "/dev", "devtmpfs")

  ensure_dir("/dev/pts")
  mount_if_needed("devpts", "/dev/pts", "devpts")

  if parse_root_uuid_from_cmdline(root_uuid) fails:
    emergency_loop("missing root=UUID")

  dev = find_device_by_uuid(root_uuid)
  if not found:
    emergency_loop("root device not found")

  ensure_dir("/newroot")

  retry loop:
    if mount(dev, "/newroot", "ext4", rw) succeeds:
      break
  if mount fails:
    emergency_loop("mount failed")

  append_boot_log_under_newroot("/newroot")

  execv("/bin/switch_root", ["switch_root", "/newroot", "/sbin/init"])
  emergency_loop("exec switch_root failed")
```

---

## 5. 例外系（失敗モードと観測点）

- `/proc/cmdline` が読めない
  - ログ：`read /proc/cmdline failed`
  - 原因候補：/proc が mount できていない

- `root=UUID=` が無い / UUID が壊れている
  - ログ：`root=UUID not found` / `invalid uuid string`

- `/dev` にデバイスが出ない
  - ログ：`opendir /dev failed` または `no candidates`
  - 原因候補：devtmpfs が mount されていない、ドライバ不足（virtio等）

- ext4 superblock が読めない
  - ログ：`read ext4 uuid failed`（errno 付き）
  - 原因候補：ブロックデバイスでない、権限、デバイスがまだ準備できていない

- mount 失敗
  - ログ：`mount root failed`（errno 付き）
  - 原因候補：ext4 サポート不足、ファイルシステム破損、デバイス違い

- switch_root 失敗
  - ログ：`exec /bin/switch_root failed`
  - 原因候補：busybox applet 不足、/bin/switch_root が無い、/newroot に /sbin/init が無い

---

## 6. テスト観点（ログで確認）

- `want root UUID:` が出る → cmdline 解析OK
- `scan:` が複数出る → /dev 走査OK
- `matched:` が出る → UUID照合OK
- `append /newroot/logs/boot.log ok` が出る → 永続側に到達
- `exec: /bin/switch_root ...` が最後に出る → 以降は ext4 側へ

---

## 7. 変更しやすいポイント（将来の拡張余地）

- 候補デバイス名の追加（mmcblk 等）
- UUID 以外の指定（例：`root=/dev/vda`）サポート
- ログ出力先の切り替え（シリアル以外）

※現段階では「受入テストで必要な最小」に留める。
