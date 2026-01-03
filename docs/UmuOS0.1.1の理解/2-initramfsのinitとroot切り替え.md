# 2-initramfs の /init と root 切り替え（UmuOS 0.1.1）

この章は、
「Linux カーネルが initramfs を展開して `/init` を起動し、永続ディスク（ext4）へ root を切り替え、ユーザーランドへ繋ぐ」
という流れを、UmuOS 0.1.1 の実装（0.1.1 の自作 `/init`）に沿って整理する。

---

## 結論：UmuOS 0.1.1 の initramfs `/init` は何か

- 実装（ソース）: [UmuOS-0.1.1/initramfs/src/init.c](UmuOS-0.1.1/initramfs/src/init.c)
- 生成物（initramfs 内の実体）: [UmuOS-0.1.1/initramfs/rootfs/init](UmuOS-0.1.1/initramfs/rootfs/init)

`rootfs/init` は ELF 実行ファイルで、ログ文字列も `init.c` のものと一致する。
つまり、「initramfs の `/init`」として動いているのは、この `init.c` 由来のバイナリ。

---

## 前提：GRUB → kernel に渡している情報

GRUB 設定は [UmuOS-0.1.1/iso_root/boot/grub/grub.cfg](UmuOS-0.1.1/iso_root/boot/grub/grub.cfg) にあり、
この中で kernel に以下のような引数を渡している。

- `root=UUID=...`
  - 永続 root（ext4）を UUID で指定
- `rw`
  - 永続 root を rw で mount する意図
- `console=ttyS0,115200n8` など
  - 観測（シリアル）を優先するため

この `root=UUID=...` が、initramfs `/init` の動作の「入力」になっている。

---

## root 切り替えの全体フロー（init.c に沿った説明）

UmuOS 0.1.1 の initramfs `/init` は、概ね次の 4 段で動く。

### 1) 最低限の擬似 FS を mount

`init.c` は最初に以下を mount する（失敗したら emergency ループに入る）。

- `/proc`（proc）
- `/sys`（sysfs）
- `/dev`（devtmpfs）
- `/dev/pts`（devpts）

意図はシンプルで、
- `/proc/cmdline` を読めるようにする
- `/dev` にブロックデバイスが見えるようにする

という「以後の処理の土台」を作る。

### 2) `/proc/cmdline` から `root=UUID=...` を取り出す

`/proc/cmdline` を読み、`root=UUID=` を探して UUID 文字列を 16byte にパースする。

ここで `root=UUID=` が見つからない／形式が壊れていると、失敗理由をログ出力して停止（emergency）。

### 3) `/dev` を走査して「UUID が一致する ext4」を探し、`/newroot` に mount

この実装の特徴は **udev や /dev/disk/by-uuid に依存せず**、
`/dev` を直接走査して ext4 の superblock を読みに行く点。

- `/dev` の候補名（最低限）
  - `vd*`（virtio-blk）
  - `sd*`（SATA/SCSI など）
  - `nvme*`
- ブロックデバイスだけを対象にする
- ext4 判定
  - superblock offset `1024`
  - magic `0xEF53`
  - UUID は superblock 内の所定オフセットから 16byte

一致するデバイスが見つかったら、
- `mount(dev, /newroot, "ext4", rw)` を試す
- mount できたら「存在確認だけ」する（混線防止）
  - `/newroot/sbin/init`
  - `/newroot/etc/inittab`
  - `/newroot/etc/init.d/rcS`

ポイント：initramfs 段階では永続側へ書かない（存在確認だけしてログに出す）。

リトライ：デバイスがまだ見えていない可能性を想定し、最大約30秒リトライする。

### 4) `switch_root` で root を切り替え、`/sbin/init` を exec

永続 root が `/newroot` に mount できたら、最後に以下を実行する。

- `execv("/bin/switch_root", {"switch_root", "/newroot", "/sbin/init"})`

ここで注意点は 2つ。

- initramfs 側の `/bin/switch_root` は BusyBox applet
  - `initramfs/rootfs/bin/switch_root -> /bin/busybox` という symlink になっている
- `switch_root` の「次の init」は **永続 root 上の** `/sbin/init`
  - initramfs 側の `sbin/` は空でも問題ない

この `execv` が成功すると、プロセスは `/sbin/init` に置き換わり、
以後は「永続 root のユーザーランド」の責務になる。

---

## 「ユーザーランドへ繋ぐ」とは具体的に何を指すか

この設計で「ユーザーランドへ繋ぐ」は、次の状態に到達することを指す。

- `/` が永続ディスク（ext4, disk.img）になっている
- PID 1 が永続 root の `/sbin/init` になっている
- `/etc/inittab` と `/etc/init.d/rcS` などにより初期化が進む

`init.c` 自体も、`/newroot/sbin/init` 等の存在を確認してログするので、
「繋ぐべき先」の要件が明確になっている。

---

## 失敗時の挙動（観測のための設計）

- 途中で失敗すると `emergency_loop()` に入り、1秒 sleep を繰り返す
- 失敗理由や走査状況は可能な限りログに出す
  - UUID が見つからない場合、候補デバイスの ext4 UUID を列挙して出す

つまり、0.1.1 の initramfs `/init` は「自動復旧」ではなく、
**原因究明（観測）を優先する** 振る舞いになっている。
