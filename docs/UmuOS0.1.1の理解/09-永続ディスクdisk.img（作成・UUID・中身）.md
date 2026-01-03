# 09-永続ディスクdisk.img（作成・UUID・中身）

UmuOS 0.1.1 における永続 root は、ホスト上の 1 ファイル `disk/disk.img` である。
しかもこれは「ディスクイメージ（パーティション表を持つ仮想ディスク）」ではなく、
**パーティション無しの ext4 ファイルシステムをそのまま 1 ファイルに入れたもの**になっている。

この章の目的は次の 2 つだ。

- なぜ「パーティション無し ext4」なのかを、起動の都合（/init の設計）と結びつけて言語化する。
- 実際に `disk.img` を作成し、UUID を GRUB 引数に反映し、中身（ユーザーランド）を成立させるまでを“再現できる粒度”で説明する。

前章（第8章）で扱った `/sbin/init` / `/etc/inittab` / `/etc/init.d/rcS` は、最終的に **この disk.img の中**に存在している必要がある。
第7章（initramfs の `/init`）は、その disk.img を見つけて `/newroot` に mount し、`switch_root` するための橋渡しだった。
本章は「橋の向こう岸を、どう作るか」である。

---

## 0. disk.img はどこに接続され、どこで読まれるか

まず、disk.img が起動全体のどこに位置するかを 1 枚の図で固定する。

```mermaid
flowchart TD
  subgraph Host[ホスト（開発機）]
    A[disk/disk.img
パーティション無し ext4] -->|qemu -drive if=virtio| B[QEMU]
  end

  subgraph Kernel[ゲスト：Linux kernel]
    B --> C[/dev/vda として見える
（virtio-blk）]
  end

  subgraph Initramfs[ゲスト：initramfs /init]
    C --> D[/dev を走査]
    D --> E[ext4 superblock を読んで UUID を比較]
    E --> F[/newroot に mount]
    F --> G[exec: switch_root /newroot /sbin/init]
  end

  subgraph Userspace[ゲスト：永続 root（ext4）]
    G --> H[PID1: /sbin/init]
    H --> I[/etc/inittab -> rcS, getty]
  end
```

重要な観点は 2 つある。

1) `disk.img` は「ホストのファイル」であり、QEMU へ渡す入力である。
2) initramfs `/init` は `root=UUID=...` を入力として受け取り、`/dev` 上のブロックデバイス（典型的には `/dev/vda`）から ext4 の UUID を読み、最終 root を決める。

つまり disk.img の UUID と、GRUB が渡す `root=UUID=...` が一致していないと、起動は成立しない（第7章で見たタイムアウトや探索失敗に繋がる）。

---

## 1. なぜ「パーティション無し ext4」なのか

UmuOS 0.1.1 の disk.img は、詳細設計に明記されている通り、
「4GiB、パーティション無しの ext4」として作られる。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 4.1

ここでの設計判断は、単に「楽だから」ではない。
第7章の `/init` の方針（依存を増やさず、観測しやすく、確実に `switch_root` まで到達する）と整合している。

パーティション無し ext4 にすることの意味を、起動の観点で言い換える。

- `/dev/vda` “そのもの”が ext4 になっているため、`/init` は「パーティションテーブルを解釈して `/dev/vda1` を見つける」といった追加の段階を持たなくてよい。
- ext4 の UUID はファイルシステムの superblock から読めるため、`/init` は `blkid` や `/dev/disk/by-uuid`（= udev の成果物）に依存せずに「どれが root か」を決定できる。

この結果、UmuOS 0.1.1 は「最小の仕掛けで、root を自前で解決する」構造になる。
それが“観測優先”の方針に噛み合う。

---

## 2. disk.img の作成（4GiB、ext4）

詳細設計の手順は次の通りである。

```bash
cd ~/umu/umu_project/UmuOS-0.1.1/disk
rm -f disk.img
truncate -s 4G disk.img
mkfs.ext4 -F disk.img
```

ここで押さえるべき点は 2 つ。

- `truncate -s 4G` は「空のファイルを 4GiB に拡張する」操作で、実体（ブロック）が最初から全て確保されるとは限らない。
- `mkfs.ext4 -F` は、通常のブロックデバイスではなく通常ファイルを対象に ext4 を作るために必要になることが多い。

なお、容量（4GiB）は仕様上の要点ではない。
要点は「ext4 の UUID を持つ永続 root が 1 つあり、それを `root=UUID=...` で指名できる」ことだ。

---

## 3. UUID の取得と、GRUB 引数との整合

disk.img は ext4 としてフォーマットされるため、UUID を持つ。
UmuOS 0.1.1 は、その UUID を GRUB のカーネル引数 `root=UUID=...` に埋め込み、
さらに initramfs `/init` は `/proc/cmdline` からそれを読み取って root を特定する。

したがって UUID は「起動の入力」そのものであり、最重要パラメータになる。

### 3.1 UUID の取得

```bash
cd ~/umu/umu_project/UmuOS-0.1.1/disk
sudo blkid -p -o value -s UUID disk.img
```

この値を控え、本書では `DISK_UUID` と呼ぶ。

### 3.2 UUID を更新すべき場所

disk.img を作り直すと、基本的に UUID は変わる。
その場合、少なくとも次の整合が必要になる。

- `UmuOS-0.1.1/iso_root/boot/grub/grub.cfg` の `root=UUID=...`

参考として、0.1.1 の `grub.cfg` は次のような形をしている。

```properties
linux /boot/vmlinuz-6.18.1 \
    root=UUID=... \
    rw \
    console=tty0 console=ttyS0,115200n8 \
    loglevel=7 \
    panic=-1
```

「`root=UUID=...` が正しいか」は、第7章の `/init` が最初に観測するポイントでもある。
ここがズレると、/init はいつまでも一致する ext4 を見つけられず、`switch_root` に到達できない。

---

## 4. disk.img の中身を作る（ext4 ルートファイルシステムの構築）

disk.img はフォーマットしただけでは空である。
この中に、永続 root として最低限成立するユーザーランド（ディレクトリ、BusyBox、設定）を置く。

以下は、詳細設計の「ext4 ルート（disk.img）中身を作る」の手順（5章）に沿う。

### 4.1 一時マウント（loop）

```bash
sudo mkdir -p /mnt/umuos011
sudo mount -o loop ~/umu/umu_project/UmuOS-0.1.1/disk/disk.img /mnt/umuos011
findmnt /mnt/umuos011
```

`mount: ... はマウント済みです` は「壊れた」ではなく、既に mount 済みの可能性が高い。
必ず `findmnt` で状態を確かめ、必要なら `sudo umount /mnt/umuos011` してからやり直す。

### 4.2 最小ディレクトリ

```bash
sudo mkdir -p /mnt/umuos011/{bin,sbin,etc,proc,sys,dev,dev/pts,run,home,root,tmp,logs,etc/init.d}
```

ここで作っているのは「永続 root 側が期待する土台」である。
proc/sys/dev/devpts は rcS で mount するが、mount 先そのものが無ければ混乱する。

### 4.3 BusyBox の配置（できれば静的）

```bash
sudo cp /bin/busybox /mnt/umuos011/bin/busybox
sudo chown root:root /mnt/umuos011/bin/busybox
sudo chmod 755 /mnt/umuos011/bin/busybox
file /mnt/umuos011/bin/busybox
```

ここで `file` が `statically linked` だと、最小 rootfs でも動きやすい。
逆に `dynamically linked` の BusyBox を置いた場合、必要なランタイムライブラリが rootfs に無いと `busybox` 自体が起動できず、ユーザーランドが崩れる。

### 4.4 BusyBox applet の生成（必ず chroot）

これは disk.img 構築で最も壊しやすい点の 1 つである。
ホスト側のマウントパス（`/mnt/umuos011`）上で `busybox --install` を実行すると、
symlink がホスト絶対パスを指して壊れることがある。

したがって、**必ず chroot して**「ゲスト側のパス基準」で applet を生成する。

```bash
sudo chroot /mnt/umuos011 /bin/busybox --install -s /bin
sudo chroot /mnt/umuos011 /bin/busybox --install -s /sbin

sudo ls -l /mnt/umuos011/bin/sh
sudo ls -l /mnt/umuos011/sbin/init
```

ここで期待する最小の成立条件は、少なくとも次が満たされることだ。

- `/bin/sh -> /bin/busybox`
- `/sbin/init` が存在する（BusyBox applet でも symlink でもよい）

### 4.5 `/sbin/init` を BusyBox に向ける（保険）

applet 生成で `/sbin/init` が出来ていれば良いが、念のため明示することもできる。

```bash
sudo ln -sf /bin/busybox /mnt/umuos011/sbin/init
sudo ls -l /mnt/umuos011/sbin/init
```

この `/sbin/init` の意味は第8章で扱った通りで、以後のユーザーランド成立の中心になる。

---

## 5. `/etc/inittab` と `rcS`：起動後の“台本”を入れる

disk.img に載せる設定のうち、UmuOS 0.1.1 の成立に直結するのは次の 2 つである。

- `/etc/inittab`（BusyBox init が読む台本）
- `/etc/init.d/rcS`（最初の初期化）

### 5.1 inittab（ttyS0 でログインできるようにする）

詳細設計の内容は次の通りである。

```bash
sudo tee /mnt/umuos011/etc/inittab >/dev/null <<'EOF'
::sysinit:/etc/init.d/rcS

# ttyS0（シリアル）でログイン
ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100

# 予備：画面側（tty0）も出したい場合は有効化
# tty1::respawn:/sbin/getty 38400 tty1

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a
EOF
```

この inittab は「rcS を 1 回実行し、ttyS0 に getty を常駐させる」最小構成である。
第8章で見た通り、ttyS0 の getty が出てログインできることが、0.1.1 の重要観測点になっている。

### 5.2 rcS（初期化。失敗しても止めない）

UmuOS 0.1.1 の rcS は、方針として「失敗しても止めない（観測を優先）」を取る。
これは initramfs `/init` が「失敗で止める（混線を防ぐ）」のに対し、
永続 root の rcS は「できる範囲で環境を整え、ログイン観測点へ到達する」ことを重視するためだ。

```bash
sudo tee /mnt/umuos011/etc/init.d/rcS >/dev/null <<'EOF'
#!/bin/sh

# 失敗しても止めない（観測を優先）
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

# 任意：永続ログ（/logs/boot.log）
# initramfsでは書かない。ここで追記する。
(
  mkdir -p /logs
  echo "[rcS] boot: $(date -Iseconds)" >> /logs/boot.log
) 2>/dev/null || true

# 任意：簡単な表示
/bin/echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
EOF

sudo chmod 755 /mnt/umuos011/etc/init.d/rcS
```

rcS の中身の読み方は第8章で既に扱った。
ここでは「このファイルは disk.img 内に存在し、実際に PID1 が起動する初期化の起点である」点を、配置の事実として固定する。

---

## 6. ログインを成立させる：ユーザー情報（/etc/passwd, /etc/shadow）

UmuOS 0.1.1 は「ログイン成功」を成立の固定点にする。
そのため、ユーザー情報を disk.img に入れることは“任意”ではなく、観測のための必須条件になる。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 5.8

### 6.1 `/etc/passwd` と `/etc/group`

```bash
sudo tee /mnt/umuos011/etc/passwd >/dev/null <<'EOF'
root:x:0:0:root:/root:/bin/sh
tama:x:1000:1000:tama:/home/tama:/bin/sh
EOF

sudo tee /mnt/umuos011/etc/group >/dev/null <<'EOF'
root:x:0:
users:x:100:
tama:x:1000:
EOF
```

### 6.2 `/etc/shadow`（権限 600 が重要）

パスワードハッシュを生成し、`/etc/shadow` に書き込む。
詳細設計では `openssl passwd -6` を使う案が示されている。

```bash
openssl passwd -6
```

出力された `\$6\$...` を使って `/etc/shadow` を作る。

```bash
sudo tee /mnt/umuos011/etc/shadow >/dev/null <<'EOF'
root:$6$REPLACE_WITH_HASH_FOR_ROOT:20000:0:99999:7:::
tama:$6$REPLACE_WITH_HASH_FOR_TAMA:20000:0:99999:7:::
EOF

sudo chown root:root /mnt/umuos011/etc/shadow
sudo chmod 600 /mnt/umuos011/etc/shadow
```

ここはつまずきやすい。
`/etc/shadow` がプレースホルダのまま、あるいは権限が 600 でないと、ログイン失敗の原因が混ざって切り分けにくくなる。

### 6.3 ホームディレクトリ

```bash
sudo mkdir -p /mnt/umuos011/root
sudo mkdir -p /mnt/umuos011/home/tama
sudo chown 1000:1000 /mnt/umuos011/home/tama
```

---

## 7. 観測点：disk.img の中身が「起動に必要な形」になっているか

disk.img をアンマウントする前に、最低限次を確認する。

```bash
sudo ls -l /mnt/umuos011/sbin/init
sudo ls -l /mnt/umuos011/etc/inittab
sudo ls -l /mnt/umuos011/etc/init.d/rcS
sudo ls -l /mnt/umuos011/etc/passwd
sudo ls -l /mnt/umuos011/etc/shadow
```

そしてアンマウントする。

```bash
sync
sudo umount /mnt/umuos011
```

この「ホストで mount して内容を見られる」こと自体が、UmuOS 0.1.1 における重要な観測技法である。
initramfs の `/init` は永続へ書かない方針なので、永続側の初期状態が正しいかどうかは、まずホストで確かめるのが一番確実になる。

---

## 8. よくある詰まりどころ（disk.img 編）

この章で触れた作業は、失敗すると第7章・第8章の失敗に直結する。
典型的な因果を、最小限の言葉で整理する。

- disk.img を作り直したのに `grub.cfg` の `root=UUID=...` を更新していない → `/init` が一致する ext4 を見つけられない。
- BusyBox applet を chroot せずに生成した → `/bin/sh` や `/sbin/init` の symlink が壊れ、`switch_root` 後にユーザーランドが起動しない。
- 動的リンク BusyBox を置いたが必要ライブラリが無い → `busybox` 自体が起動できず、`/sbin/init` も `getty` も成立しない。
- mount 済みのまま上書き作業を続けた／umount し忘れた → どの状態が正なのか分からなくなり、観測が混線する。

UmuOS 0.1.1 は「混線防止」を強く意識しているので、
迷ったら `findmnt /mnt/umuos011` で状態を固定し、手順を巻き戻せる場所（mount/umount の境界）まで戻るのが良い。

---

## まとめ

この章では、UmuOS 0.1.1 の永続 root が `disk/disk.img` という 1 ファイル（パーティション無し ext4）である理由と、
その作り方（UUID の取得、BusyBox と設定の配置、ログイン成立の固定点）を、起動の責務分界に結びつけて説明した。

次章では、この disk.img と initrd と kernel を 1 つに束ねる「起動メディア（ISO）」の作り方へ進む。
