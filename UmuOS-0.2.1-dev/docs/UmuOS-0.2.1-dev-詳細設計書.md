# UmuOS-0.2.1-dev 詳細設計書

この文書は「実装担当が、手順をコピペして進められる粒度」を狙う。
実機固有情報（IP/MAC/ホスト名/プロバイダ名等）は書かず、必ずプレースホルダにする。

---

## 0. 事前準備（絶対に最初）

### 0.1 変数（この文書中で使う）

以下を自分の環境に合わせて埋める。

```bash
# UmuOS rootfs を置くブロックデバイス（パーティション/LV）
UMU_DEV='<UMU_DEV>'          # 例: /dev/vdb1 または /dev/mapper/vg-umuos

# UmuOS rootfs に固定するUUID（initramfsが root=UUID=... で探す）
UMU_UUID='<UMU_UUID>'        # 例: d2c0b3c3-...

# UmuOS initramfs（/init を含む）ファイルのパス
INITRD_PATH='<INITRD_PATH>'  # 例: /tmp/initrd.img-umuos

# 3点セットを置く作業ディレクトリ（rootのみ読める場所を推奨）
WORKDIR='<WORKDIR>'          # 例: /var/lib/umuos/017

# UmuOS rootfs（disk.img）ファイルのパス（中身コピー用）
DISK_IMG_PATH='<DISK_IMG_PATH>'

# ネットワーク（staticの場合）
PUBLIC_IP='<PUBLIC_IP>'
PREFIX='<PREFIX>'
GW_IP='<GW_IP>'
DNS_IP='<DNS_IP>'
```

### 0.2 復旧経路チェック（詰まないため）

- 管理コンソール/レスキュー/ISO起動の少なくとも1つが使えること
- GRUBでRocky通常起動に戻せること

---

## 1. 起動方式（BIOS/UEFI）と grub.cfg 実体の確定

注記：本環境は **BIOS起動** と確認済み。ただし作業手順として、毎回コマンドで確認してから進める。

```bash
test -d /sys/firmware/efi && echo UEFI || echo BIOS

# BIOS環境の典型：/etc/grub2.cfg -> /boot/grub2/grub.cfg
readlink -f /etc/grub2.cfg 2>/dev/null || true

# UEFI環境の典型：/etc/grub2-efi.cfg -> /boot/efi/EFI/.../grub.cfg
readlink -f /etc/grub2-efi.cfg 2>/dev/null || true
```

以降、grub2-mkconfig の出力先は「実体パス」にする。

---

## 2. UmuOS rootfs（ブロックデバイス）を作る

### 2.1 ext4作成（UUID固定）

注意：この操作は対象デバイスを消す。`UMU_DEV` は慎重に確認する。

```bash
sudo lsblk -f
sudo blkid "$UMU_DEV" || true

sudo mkfs.ext4 -F -U "$UMU_UUID" "$UMU_DEV"
sudo blkid "$UMU_DEV"
```

### 2.2 disk.img から中身を展開（コピー）

注意：ここでの `-o loop` は **作業用に disk.img の中身を読み出すだけ**（展開用）。
起動時のrootfsは 5.1 の方針どおり、`UMU_DEV` のようなブロックデバイスにする。

```bash
sudo mkdir -p /mnt/umu_img /mnt/umu_root

sudo mount -o loop "$DISK_IMG_PATH" /mnt/umu_img
sudo mount "$UMU_DEV" /mnt/umu_root

sudo cp -a /mnt/umu_img/. /mnt/umu_root/
sync

sudo umount /mnt/umu_root
sudo umount /mnt/umu_img
```

最低限ファイルの存在確認：

```bash
sudo mount "$UMU_DEV" /mnt/umu_root
ls -l /mnt/umu_root/sbin/init /mnt/umu_root/etc/inittab /mnt/umu_root/etc/init.d/rcS
sudo umount /mnt/umu_root
```

---

## 3. UmuOSネットワーク設定（staticの場合）

```bash
sudo mount "$UMU_DEV" /mnt/umu_root

sudo tee /mnt/umu_root/etc/umu/network.conf >/dev/null <<EOF
IFNAME=eth0
MODE=static
IP=${PUBLIC_IP}/${PREFIX}
GW=${GW_IP}
DNS=${DNS_IP}
EOF

sudo umount /mnt/umu_root
```

注意：VPSによってはDHCP必須の場合がある。DHCP必須ならここで止めて設計を分岐し、`rcS` に `udhcpc` を入れる。

---

## 4. initramfs（/init含む）を /boot に配置

```bash
sudo install -m 0600 "$INITRD_PATH" /boot/initrd.img-umuos021
sudo ls -l /boot/initrd.img-umuos021
```

---

## 5. GRUBへ UmuOS 起動エントリを追加

### 5.1 Rocky kernel のパス確認

```bash
ls -l /boot/vmlinuz-*
```

### 5.2 40_customへ追記（テンプレ）

`<ROCKY_KERNEL_VERSION>` を実環境のファイル名に合わせて置換する。

```bash
sudo tee -a /etc/grub.d/40_custom >/dev/null <<'EOF'

menuentry 'UmuOS (switch_root to ext4 UUID)' {
	linux /vmlinuz-<ROCKY_KERNEL_VERSION> \
		root=UUID=<UMU_UUID> \
		rw \
		console=tty0 console=ttyS0,115200n8 \
		loglevel=7 \
		panic=-1 \
		net.ifnames=0 biosdevname=0
	initrd /initrd.img-umuos021
}
EOF
```

注意：`console=` は環境依存。まずは入れておき、見え方に応じて調整する。

### 5.3 grub.cfg 生成（実体パスへ）

```bash
GRUB_OUT="$(readlink -f /etc/grub2.cfg)"
echo "$GRUB_OUT"

sudo grub2-mkconfig -o "$GRUB_OUT"

# エントリが入ったか確認
sudo grep -n "menuentry 'UmuOS (switch_root to ext4 UUID)'" "$GRUB_OUT" || true
```

---

## 6. GRUBメニューで起動テスト（安全第一）

- GRUBのデフォルトは変えない
- `reboot` して、GRUBメニューでその回だけ `UmuOS (switch_root to ext4 UUID)` を手で選んで起動する

```bash
sudo reboot
```

---

## 7. 成功確認

### 7.1 管理コンソールから（ログ確認）

- initramfs が rootfs をマウントできたログが出る
- `switch_root` の実行ログが出る

### 7.2 外部から（telnet）

```bash
telnet <PUBLIC_IP> 23
```

`login:` が出れば到達性とユーザーランド起動が成立。

---

## 8. トラブルシュート（最小セット）

### 8.1 initramfsでrootが見つからない

- `root=UUID=<UMU_UUID>` が正しいか
- `blkid` で `UMU_DEV` のUUIDが一致しているか
- `UMU_DEV` が起動時に見えるデバイス名に乗っているか（virtio/ SATA/ NVMe 等）

### 8.2 telnetで `login:` が出ない

典型：ネットワークが上がっていない。

- `net.ifnames=0 biosdevname=0` が効いておらずIF名がズレている
- `network.conf` のIP/GW/DNSがズレている
- 上流ACL/FWで23/TCPが遮断されている

管理コンソールで入れた場合：

```sh
ip a
ip r
ps w | grep telnetd | grep -v grep || true
cat /etc/umu/network.conf
```

---

## 9. 公開用チェックリスト

- `<PUBLIC_IP>` `<GW_IP>` `<DNS_IP>` `<MAC_ADDR>` などが実値になっていない
- `root@<host>` などのプロンプト出力が貼られていない
- 個人の作業ディレクトリ絶対パスが含まれていない
- 管理画面URLやプロバイダ固有名が入っていない
