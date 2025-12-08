# Step4(Step3のBug修正)

# Step3でのバグの修正手順

## 問題点
1. suができない
2. suができないからpoweroffできない（且つ、rootユーザでもpoweroffコマンドが動かない）
3. tamaUSERでexitするとkernel panic
   [  535.277051] R13: 0000000000000001 R14: 0000000000000000 R15: 0000000000606720
   [  535.277051]  </TASK>
   [  535.277051] Kernel Offset: 0x3aa00000 from 0xffffffff81000000 (relocation range: 0xffffffff80000000-0xffffffffbfffffff)
   [  535.277051] ---[ end Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000000 ]---
4. kill -kill PID で強制終了することとなる

## 解決策（修正案）
1. 問題1: suができない
   原因：
   BusyBoxのsuコマンドにSUID権限が設定されていない
   SUID権限がないと、一般ユーザーが他のユーザー（root）に切り替えられない
   解決策：
   cd ~/umu/step3/initramfs/rootfs/bin
   chmod u+s su
   ls -l su
   # -rwsr-xr-x と表示されればOK（sがSUID権限）

2. 問題2: poweroffコマンドが動かない
   原因：
   方法1: poweroff -f（強制シャットダウン）を使う
   BusyBoxのpoweroffはrebootシステムコールを使用するが、initramfs環境では正しく動作しないことがある
   または、カーネルのACPI機能が有効になっていない
   解決策：
   poweroff -f
   # または
   halt -f
   # または
   reboot -f  # 再起動

   方法2: カーネルパラメータでACPIを有効化
   # ~/umu/step3/iso_root/boot/grub/grub.cfg を修正
   set timeout=20
   set default=0

   menuentry "Umu Project Linux kernel 6.6.58" {
     linux /boot/vmlinuz-6.6.58 ro console=ttyS0,115200 acpi=force
     initrd /boot/initrd.img-6.6.58
   }

   menuentry "Umu Project rescue 6.6.58" {
     linux /boot/vmlinuz-6.6.58 ro single console=ttyS0,115200 acpi=force
     initrd /boot/initrd.img-6.6.58
   }

3. 問題3: tamaユーザーでexitするとkernel panic
   原因：
   initスクリプトでexec /bin/gettyを使っているため、initプロセス（PID 1）がgettyに置き換わる
   ログアウト（exit）するとgettyが終了し、PID 1が終了するためkernel panic
   解決策：initスクリプトを修正（gettyを無限ループで再起動）

   cat > ~/umu/step3/initramfs/rootfs/init << 'EOF'
   #!/bin/sh

   mount -t proc none /proc
   mount -t sysfs none /sys
   mount -t devtmpfs none /dev

   CMDLINE=$(cat /proc/cmdline)

   if echo "$CMDLINE" | grep -q "single"; then
     echo "Umu Project step3: Single-user rescue mode"
     exec /bin/sh
   else
     echo "Umu Project step3: Multi-user mode"
  
   # gettyを無限ループで再起動（ログアウト後も再表示）
   while true; do
     /bin/getty -L ttyS0 115200 vt100
     echo "[INFO] getty exited, restarting..."
     sleep 1
   done
   fi
   EOF

   chmod +x ~/umu/step3/initramfs/rootfs/init

   この修正により：
   ログアウト（exit）しても、再度ログインプロンプトが表示される
   initプロセス（PID 1）が終了しないため、kernel panicが発生しない

4. 問題4: QEMUから抜ける方法
   - kill -kill <PID>    で、強制終了   もしくは、
   - rootユーザーになり  poweroff -f    ※suできないと厳しいかも


# 手順
Step1: suにSUID権限を設定（上記記載）
Step2: initスクリプトを修正（上記記載）
Step3: GRUBにACPI有効化パラメータを追加（上記記載）
Step4: initramfs再作成・ISO更新
    cd ~/umu/step3/initramfs/rootfs
    find . | cpio -o -H newc | gzip > ../initrd.img-6.6.58

    cd ~/umu/step3
    cp initramfs/initrd.img-6.6.58 iso_root/boot/
    grub-mkrescue -o step3-boot.iso iso_root
Step5: QEMU検証
    cd ~/umu/step3
    qemu-system-x86_64 -enable-kvm -m 2048 -cdrom step3-boot.iso -nographic

# 動作確認手順
1. ログイン
    login: tama
    Password: UmuT1207
2. suでroot昇格
    su -
    Password: UmuR1207
3. ログアウトしてもkernel panicが発生しないか確認
    exit  # rootから抜ける
    exit  # tamaから抜ける → 再度ログインプロンプト表示されるか確認
4. シャットダウン
    su -
    poweroff -f
    ※shutdownできない場合  Ctrl + A Ctrl + X もしくは kill -kill <PID>
    


