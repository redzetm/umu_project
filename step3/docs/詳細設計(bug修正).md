# Step2バグの修正手順

## 前提条件
- `詳細設計(bug再現).md` の手順でkernel panicが発生している状態

---

## バグの原因

### 問題点
`busybox --install -s .` を実行すると、BusyBoxコマンドが**絶対パス**（`/usr/bin/busybox`）でシンボリックリンクされる。

```bash
lrwxrwxrwx 1 tama tama 16 12月  7 14:24 bin/getty -> /usr/bin/busybox
```

initramfs内では `/usr/bin/busybox` は存在しないため、`getty` が実行できず、initプロセスが終了してkernel panicになる。

---

## 修正手順

### 1. 問題の確認

```bash
cd ~/umu/step3/initramfs/rootfs/bin
ls -l | grep "^l" | grep "/usr/bin/busybox" | wc -l
```

→ 269個のコマンドが絶対パスでリンクされている

---

### 2. 相対パスに修正

```bash
cd ~/umu/step3/initramfs/rootfs/bin

# 全てのシンボリックリンクを相対パスに修正
for cmd in $(ls -1 | grep -v "^busybox$"); do
  rm "$cmd"
  ln -s busybox "$cmd"
done
```

---

### 3. 修正確認

```bash
ls -l getty
# lrwxrwxrwx 1 tama tama 7 12月  7 18:38 getty -> busybox
```

→ 相対パス（`busybox`）にリンクされていればOK

---

### 4. initramfs再作成

```bash
cd ~/umu/step3/initramfs/rootfs
find . | cpio -o -H newc | gzip > ../initrd.img-6.6.58
```

---

### 5. ISO更新

```bash
cd ~/umu/step3
cp initramfs/initrd.img-6.6.58 iso_root/boot/
grub-mkrescue -o step3-boot.iso iso_root
```

---

### 6. QEMU検証

```bash
cd ~/umu/step3
qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 \
  -cdrom step3-boot.iso \
  -nographic
```

---

## 成功条件

- GRUBメニューが表示される
- カーネルが起動し、`Umu Project Step2: Multi-user mode` が表示される
- ログインプロンプト（`login:`）が表示される
- rootまたはtamaでログイン可能

---

## 追加デバッグ（ログインプロンプトが出ない場合）

initスクリプトにデバッグログを追加：

```bash
#!/bin/sh

echo "[DEBUG] Mounting proc, sys, dev..."
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo "[DEBUG] Reading kernel cmdline..."
CMDLINE=$(cat /proc/cmdline)
echo "[DEBUG] CMDLINE=$CMDLINE"

if echo "$CMDLINE" | grep -q "single"; then
  echo "[DEBUG] Single-user rescue mode"
  exec /bin/sh
else
  echo "[DEBUG] Multi-user mode, starting getty..."
  
  # gettyが存在するか確認
  if [ ! -x /bin/getty ]; then
    echo "[ERROR] /bin/getty not found or not executable!"
    exec /bin/sh  # フォールバック
  fi
  
  exec /bin/getty -L ttyS0 115200 vt100
fi
```

initramfs再作成・ISO更新後、再度QEMU検証。

---

## まとめ

- **原因**: BusyBoxコマンドの絶対パスリンク
- **修正**: 相対パスリンクに変更
- **結果**: kernel panic解消、ログインプロンプト表示