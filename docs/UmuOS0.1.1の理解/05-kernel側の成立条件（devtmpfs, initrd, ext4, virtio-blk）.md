# 05-kernel側の成立条件（devtmpfs, initrd, ext4, virtio-blk）

UmuOS 0.1.1 の起動を理解するとき、
「initramfs の `/init` が頑張っている」ことに目が行きやすい。

しかし、`/init` が成立するのは、
その前に Linux カーネルが必要な機能を **built-in（=y）**として持っている場合に限られる。

この章では、UmuOS 0.1.1 が成立するために
カーネル側で最低限必要な条件を、初学者向けに理由から説明する。

## 1. なぜ “built-in（=y）” が重要なのか

カーネルの機能は大きく 2 つの形で提供される。

- built-in（カーネル本体に組み込み）
- module（必要になったらロードする）

initramfs の時点では、
「モジュールをどこから読み込むか」「ロードする仕組みが整っているか」がまだ不安定になりやすい。

UmuOS 0.1.1 は「混線防止」を優先するため、
起動に必須な要素は built-in であることを確認する方針を取っている。

この方針は、詳細設計の観測点（3.3）に明示されている。

- `UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 3.3

## 2. UmuOS 0.1.1 で確認すべきカーネル設定

詳細設計では、最低限これらが `=y` であることを確認する。

- `CONFIG_EXT4_FS=y`
- `CONFIG_DEVTMPFS=y`（可能なら `CONFIG_DEVTMPFS_MOUNT=y`）
- `CONFIG_BLK_DEV_INITRD=y`
- `CONFIG_VIRTIO_BLK=y`
- `CONFIG_RD_GZIP=y`

教科書としては、この 5 つを「なぜ必要か」で理解する。

### 2.1 `CONFIG_BLK_DEV_INITRD`：initrd を使えること

カーネルが initramfs（initrd）を展開して `/init` を実行するには、
initrd を扱う機能が必要になる。
それが `CONFIG_BLK_DEV_INITRD` である。

これが無いと、GRUB が initrd を渡しても、カーネルがそれを使えない。
結果として `/init` に到達しない。

### 2.2 `CONFIG_RD_GZIP`：gzip 圧縮の initrd を読めること

UmuOS 0.1.1 では、initrd は `cpio + gzip` で作られる。
つまり initrd は gzip 圧縮されている。

それをカーネルが展開するために `CONFIG_RD_GZIP` が必要になる。

ここで大事なのは、
initramfs の作り方（gzip にしている）と、カーネルの機能が一致している必要があるという点だ。

### 2.3 `CONFIG_DEVTMPFS`：/dev にデバイスノード（例：/dev/vda）が現れること

initramfs `/init` は永続 root を探すために `/dev` を走査する。

ところが `/dev` は「単なるディレクトリ」ではない。
ここに見える `/dev/vda` のようなデバイスファイル（デバイスノード）は、
カーネルが検出したデバイス情報を元に作られてはじめて存在できる。

そこで devtmpfs（カーネルがデバイスノードを供給するための仕組み）を `/dev` に mount する。
この仕組みを有効化するのが `CONFIG_DEVTMPFS` である。

補足として、カーネルが起動時に devtmpfs を自動で mount する設定が `CONFIG_DEVTMPFS_MOUNT` である。
ただし UmuOS 0.1.1 は、観測点を固定するために `/init` 側で明示的に mount している。

UmuOS 0.1.1 の `/init` は、
`mount("devtmpfs", "/dev", "devtmpfs", ...)` を最初に行う。
この設計は、udev のような大きな仕組みに依存せず、
カーネルが提供する最小の機能で `/dev` を成立させる方針になっている。

### 2.4 `CONFIG_VIRTIO_BLK`：QEMU の disk が見えること

UmuOS 0.1.1 は QEMU 起動時に、永続ディスクを `if=virtio`（virtio-blk）で接続する。

- `UmuOS-0.1.1/umuOSstart.sh` の `-drive ...,if=virtio`

つまり、カーネル側に virtio-blk のドライバが必要になる。
それが `CONFIG_VIRTIO_BLK` である。

これが無いと `/dev/vda` のようなデバイスが現れず、
initramfs `/init` が永続 root を見つけられない。

### 2.5 `CONFIG_EXT4_FS`：永続 root を mount できること

永続 root（disk.img）は ext4 として作られる。

- `UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の 4.1（`mkfs.ext4 -F disk.img`）

したがって、カーネルが ext4 を mount できなければ、
initramfs `/init` が `/newroot` に mount する段階で失敗する。

`CONFIG_EXT4_FS` は、そのために必須になる。

## 3. “成立条件”は観測点（チェックポイント）にできる

カーネル設定の話は抽象的に見えるが、UmuOS 0.1.1 では
「観測点」として具体的なチェック手順が固定されている。

たとえば、カーネルビルド直後に `.config` を grep して確認する。

このチェックは「後で困らないため」というより、
**困ったときに切り分けを早くする**ために重要である。

- `/dev` が無い → devtmpfs か
- disk が見えない → virtio-blk か
- mount できない → ext4 か
- `/init` に行かない → initrd か gzip か

こうして原因候補を狭めることができる。

## 4. この章のまとめ：/init の前に “カーネルが道を作る”

initramfs の `/init` は、
起動の中心に見えるが、単独では成立しない。

UmuOS 0.1.1 は、カーネル側の成立条件を最小限に固定し、
その上で initramfs を“橋渡し専用”にしている。

この設計により、起動が壊れたときでも、
「どこが壊れたか」を段階ごとに切り分けやすくなっている。
