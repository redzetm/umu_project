# 10-ISO作成（iso_root と grub-mkrescue）

前章までで、UmuOS 0.1.1 の起動は「永続 root（disk.img）へ到達するまでの橋渡し」を中心に組み立てられていることが分かった。

- 永続 root は `disk/disk.img`（ext4）である（第9章）。
- initramfs の `/init` は、`root=UUID=...` を入力として disk.img を見つけ、`switch_root` する（第7章）。

この章のテーマは、それらを“起動可能な形”で QEMU に渡すための **ISO（起動メディア）** を作ることだ。

ここでいう ISO は、OS のデータを永続化する入れ物ではない。
あくまで「UEFI → GRUB → kernel + initrd」を起動するための、読み取り専用のブートメディアである。
永続性は disk.img が担う。

---

## 0. UmuOS 0.1.1 の ISO は何を載せるか（役割の固定）

UmuOS 0.1.1 の ISO は、次の 3 つを載せる。

- カーネル（`vmlinuz-6.18.1`）
- initrd（`initrd.img-6.18.1`）
- GRUB 設定（`grub.cfg`）

これらは `UmuOS-0.1.1/iso_root/` 以下に配置され、`grub-mkrescue` で ISO 化される。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の「iso_root の説明」および「ISO 生成」

ここでの設計意図は明快で、ISO と disk.img を明確に分離している。

- ISO：起動の入口（UEFI/GRUB/kernel/initrd）
- disk.img：起動の到達点（最終 `/` として使う ext4）

---

## 1. `iso_root/` は「ISO の材料」である

`iso_root/` は、ISO の中身をそのまま構成するディレクトリである。
詳細設計では次が想定されている。

- `iso_root/boot/grub/grub.cfg`
- `iso_root/boot/vmlinuz-6.18.1`
- `iso_root/boot/initrd.img-6.18.1`

ISO を作るとは、突き詰めれば「この 3 ファイルを揃える」ことである。

この見方をすると、トラブルの切り分けが簡単になる。

- GRUB メニューが出ない／設定が反映されない → `grub.cfg` の問題
- カーネルが起動しない → `vmlinuz` の配置・パス・形式の問題
- `/init` まで到達しない → `initrd` の配置・パス・中身の問題

---

## 2. GRUB 設定は「disk.img を指名する」

UmuOS 0.1.1 では、GRUB の `linux` 行に `root=UUID=...` を含める。
ここで指定しているのは第9章で作った disk.img の ext4 UUID である。

言い換えると、ISO は disk.img を“内包”していないが、
**disk.img を指名する情報（UUID）だけは ISO に含める**。

- `UmuOS-0.1.1/iso_root/boot/grub/grub.cfg`

`grub.cfg` の具体は第4章で扱ったが、この章の観点では次だけ覚えれば足りる。

- `root=UUID=...` が disk.img の UUID と一致していること
- `initrd /boot/initrd.img-6.18.1` が、ISO 内の initrd を正しく指していること

ここがズレると、第7章の `/init` は永続 root を見つけられない。

---

## 3. ISO の生成（`grub-mkrescue`）

詳細設計では、ISO の生成は次の 3 行で表現されている。

```bash
cd ~/umu/umu_project/UmuOS-0.1.1
rm -f UmuOS-0.1.1-boot.iso
grub-mkrescue -o UmuOS-0.1.1-boot.iso iso_root
ls -lh UmuOS-0.1.1-boot.iso
```

ここで重要なのは、`grub-mkrescue` が単体で完結しているわけではない点だ。
Ubuntu 24.04 では、詳細設計の通り `xorriso` や GRUB の関連パッケージが必要になる。

- 参照：`UmuOS-0.1.1/docs/詳細設計-0.1.1.md` の「必要パッケージ」

このパッケージ群が揃うと、`grub-mkrescue` は UEFI ブート可能な ISO を作れる。

---

## 4. ISO の中身を観測する（壊れていないかの確認）

ISO はファイルなので、ホストで mount して中身を確認できる。
確認の目的は「最低限、boot に必要な材料が入っているか」を見ることだ。

```bash
sudo mkdir -p /tmp/umuos_iso_check
sudo mount -o loop UmuOS-0.1.1-boot.iso /tmp/umuos_iso_check
find /tmp/umuos_iso_check -maxdepth 3 -type f | sort
sudo umount /tmp/umuos_iso_check
```

期待する観測は次の通り。

- `/boot/grub/grub.cfg` がある
- `/boot/vmlinuz-6.18.1` がある
- `/boot/initrd.img-6.18.1` がある

ここで確認できると、以後の起動失敗は「ISO の材料不足」ではなく、
カーネルや initrd の中身（第7章）や disk.img（第9章）へ切り分けられる。

---

## まとめ

この章では、UmuOS 0.1.1 の ISO が「永続 disk.img とは別物の、起動の入口」であり、
`iso_root/` に kernel/initrd/grub.cfg を揃えて `grub-mkrescue` で ISO 化する構造を固定した。

次章では、生成した ISO と disk.img を QEMU に接続し、ttyS0 を通して“観測できる起動”を行う。
