# 03-起動フロー全体（UEFI→GRUB→kernel→initramfs→switch_root）

UmuOS 0.1.1 の起動は、特殊な技術の寄せ集めではない。
Linux が提供する標準的なブートの枠組み（UEFI/GRUB/initramfs）を使い、
その中で「観測しやすい」責務分界を意図して作られている。

ここでは、細部に入る前に、責務を “段階” ごとに分けて説明する。

## 1. まずは全体図（責務の境界線）

```mermaid
flowchart LR
  A[電源投入] --> B[UEFI (OVMF)]
  B --> C[GRUB (ISO内)]
  C --> D[Linux kernel]
  D --> E[initramfs を展開]
  E --> F[/init を実行]
  F --> G[disk.img(ext4) を見つけて /newroot に mount]
  G --> H[switch_root で / を切替]
  H --> I[/sbin/init を exec]
  I --> J[rcS などで初期化]
  J --> K[getty/login]
```

この図の重要点は 2つある。

第一に、initramfs `/init` は **永続 root に到達するまでの橋渡し**であり、
永続 root 側の初期化（rcS 等）とは責務が分かれている。

第二に、UmuOS 0.1.1 が「成立」とみなす地点は、
`switch_root` が成立して `/` が ext4（disk.img）に切り替わった後まで含む。

## 2. 各段階の役割を“ひとことで”言う

ここでは各段階をひとことで言い切り、後の章で肉付けする。

### 2.1 UEFI（OVMF）

QEMU 上で UEFI を提供し、次の段階（GRUB）へ制御を渡す。
実機の UEFI と同じく、NVRAM（変化する領域）を持つ。

### 2.2 GRUB

ISO 内の `grub.cfg` を読み、カーネルと initrd をロードする。
さらにカーネル引数（`root=UUID=...` など）を決める。

UmuOS 0.1.1 の GRUB は「観測しやすさ」のため、シリアルコンソール出力も意識している。

### 2.3 Linux カーネル

ハードウェア（QEMU の仮想デバイス）を初期化し、
initramfs を展開し、`/init` を実行する。

ここで重要なのは、カーネルが起動直後に使う root は “永続ディスク” ではなく、
initramfs である点。

### 2.4 initramfs（early userspace）

永続 root を mount するための最小環境。
UmuOS 0.1.1 では、ここに自作 `/init`（C）と BusyBox（静的）を置き、
余計な依存（udev 等）を減らしている。

### 2.5 switch_root

initramfs の root と、永続ディスクの root を切り替える操作。
成功すると、以後の `/` は disk.img になり、`/sbin/init` が起動される。

## 3. UmuOS 0.1.1 の“核心”はどこか

UmuOS 0.1.1 を理解する核心は、次の 2点に集約される。

1. どうやって永続 root（disk.img）を特定するか
2. どうやって initramfs から永続 root へ移行するか

1 は GRUB の `root=UUID=...` と initramfs `/init` の実装に繋がる。
2 は `switch_root` に繋がる。

この 2点が理解できると、UmuOS 0.1.1 の起動は「既知の構造」に見え始める。

## 4. 次章以降の読み順（なぜこう並べるか）

本書では、次の順に説明を深める。

- GRUB の設定とカーネル引数（入力が何か）
- カーネル側の成立条件（何が必要か）
- initramfs の設計（何を入れて何を避けるか）
- `/init` の実装（実際に何をしているか）
- `switch_root` 後のユーザーランド（誰に責務が移るか）

この順にすると、
「いきなり init.c を読む」よりも、設計意図と責務の分界線を見失いにくい。
