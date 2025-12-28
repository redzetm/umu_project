# 詳細設計書(init.c)

対象：UmuOS ver0.1.1 の initramfs 上で動く「自作 init」

- 実装ファイル：UmuOSver011/initramfs/src/init.c
- 役割：initramfs から永続 ext4（disk.img）へ切り替える「移行専用 init」
- 重要方針：**研究・観測のため、止まる理由が分かるログを必ず出す**

---

## 1. 目的（何を達成するか）

initramfs 環境で以下を達成する。

1. `/proc/cmdline` から `root=UUID=...` を取得する
2. `/dev` 配下のブロックデバイスを走査し、ext4 superblock の UUID を読み取る
3. UUID が一致したデバイスを `/newroot` に ext4 でマウントする
4. `/newroot/logs/boot.log` に最低限のログを追記する（永続ログの確認用）
5. `/bin/switch_root` を **フルパスで exec** し、`/sbin/init`（ext4側）へ移行する

---

## 2. 入力 / 出力（外部との約束）

### 2.1 入力
- kernel cmdline：`root=UUID=<UUID文字列>`
  - 形式：`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`（36文字、ハイフンあり）
- `/dev`：devtmpfs によりブロックデバイスが見えること

### 2.2 出力
- 標準エラー出力（シリアル `ttyS0` へ流れる想定）：`[init] ...` 形式のログ
- 成功時：`/bin/switch_root` が起動し、以降は ext4 側の `/sbin/init` が実行される
- 失敗時：理由をログに出した上で `sleep` ループに入る（停止した理由が残る）

---

## 3. 前提条件（動作に必要なもの）

### 3.1 initramfs 内
- `/bin/busybox` が存在し、`/bin/switch_root` が applet として存在する
- `/proc` `/sys` `/dev` `/dev/pts` をマウントできる

### 3.2 kernel 設定（詳細設計書のチェック項目）
- `CONFIG_DEVTMPFS=y` / `CONFIG_DEVTMPFS_MOUNT=y`
- `CONFIG_EXT4_FS=y`
- `CONFIG_BLK_DEV_INITRD=y`
- initrd が gzip の場合：`CONFIG_RD_GZIP=y`

---

## 4. 処理フロー（全体）

### 4.1 起動直後
1. ログ出力開始：`[init] UmuOS initramfs init start`
2. `/proc` `/sys` `/dev` `/dev/pts` の順でマウント（既にマウント済みならスキップ）

### 4.2 root UUID の取得
1. `/proc/cmdline` を読み取り、ログに出す
2. `root=UUID=` を検索
3. UUID 文字列（36文字想定）を 16バイトへ変換
4. 変換した UUID を整形してログ出力：`want root UUID: ...`

### 4.3 デバイス探索
1. `/dev` を opendir して走査
2. 候補名のみ対象（最低限）：
   - `vd*`（virtio-blk）
   - `sd*`（SCSI）
   - `nvme*`（NVMe）
3. ブロックデバイスかどうか `stat()` で確認
4. 各候補について `scan: /dev/...` をログ出力
5. ext4 superblock の UUID を読む（後述）
6. 一致したら `matched: dev=... uuid=...` をログ出力して探索終了

### 4.4 マウント（/newroot）
- 見つけたデバイスを `mount(dev, "/newroot", "ext4", 0, "")` で rw マウント
- 失敗した場合はログに理由を出し、一定回数リトライ

### 4.5 永続ログ追記
- `/newroot/logs/boot.log` を open(O_APPEND) し、1行追記

### 4.6 switch_root
- `execv("/bin/switch_root", {"switch_root", "/newroot", "/sbin/init", NULL})`
- 失敗時：理由をログに出して停止

---

## 5. 重要な内部仕様（どこが研究ポイントか）

### 5.1 ext4 UUID 読み取り
- ext4 superblock は「デバイス先頭から 1024バイト」地点
- UUID フィールドは superblock 先頭から `0x68` オフセット、長さ 16バイト
- したがって読み取り位置は：`1024 + 0x68`

### 5.2 UUID 文字列→16バイト
- ハイフンを無視して、16進2桁＝1バイトとして順に埋める
- 失敗条件：
  - 16進数以外の文字
  - 桁数不足/過剰
  - 途中で終端

### 5.3 失敗時のふるまい
- 「なぜ止まったか」がログに残ることを最優先する
- そのため panic 的に exit せず、`sleep(1)` ループで停止する

---

## 6. ログ設計（観測のための最低限）

### 6.1 目的
- いま何をしているか
- 何を探したか（候補デバイス）
- 何が見つかったか（一致デバイスとUUID）
- 何が失敗したか（errno とメッセージ）

### 6.2 主なログ例
- `[init] /proc/cmdline: ...`
- `[init] want root UUID: ...`
- `[init] scan: /dev/vda`
- `[init] matched: dev=/dev/vda uuid=...`
- `[init] mount root failed: ... errno=...`
- `[init] exec: /bin/switch_root /newroot /sbin/init`

---

## 7. 成功条件（このプログラム単体）

- `root=UUID=...` の UUID と一致する ext4 デバイスを見つけられる
- `/newroot` に ext4 をマウントできる
- `/bin/switch_root` を実行できる

（この後のログインやネットワークは ext4 側の `/sbin/init` / rcS の責務）
