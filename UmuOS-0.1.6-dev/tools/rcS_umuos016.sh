#!/bin/sh

export PATH=/umu_bin:/sbin:/bin
export TZ=JST-9

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mount -t devpts devpts /dev/pts 2>/dev/null || true

mkdir -p /logs /run /var/run /umu_bin 2>/dev/null || true
: > /var/run/utmp 2>/dev/null || true

chown root:root /umu_bin 2>/dev/null || true
chmod 0755 /umu_bin 2>/dev/null || true

(
	{
		echo ""
		echo "===== boot begin ====="
		echo "[boot_id]"; cat /proc/sys/kernel/random/boot_id 2>/dev/null || true
		echo "[time]"; date -R 2>/dev/null || date 2>/dev/null || true
		echo "[uptime]"; cat /proc/uptime 2>/dev/null || true
		echo "[cmdline]"; cat /proc/cmdline 2>/dev/null || true
		echo "[mount]"; mount 2>/dev/null || true
		echo "===== boot end ====="
	} >> /logs/boot.log
) 2>/dev/null || true

# ネットワーク初期化（/etc/umu/network.conf をI/Fとする）
(
	CONF=/etc/umu/network.conf
	IFNAME="$(grep -m1 '^IFNAME=' "$CONF" 2>/dev/null | cut -d= -f2)"
	MODE="$(grep -m1 '^MODE=' "$CONF" 2>/dev/null | cut -d= -f2)"
	IP="$(grep -m1 '^IP=' "$CONF" 2>/dev/null | cut -d= -f2)"
	GW="$(grep -m1 '^GW=' "$CONF" 2>/dev/null | cut -d= -f2)"

	[ -n "$IFNAME" ] || IFNAME=eth0
	[ "$MODE" = "static" ] || exit 0
	[ -n "$IP" ] || exit 0
	[ -n "$GW" ] || exit 0

	ip link set dev "$IFNAME" up 2>/dev/null || true
	ip addr add "$IP" dev "$IFNAME" 2>/dev/null || true
	ip route replace default via "$GW" dev "$IFNAME" 2>/dev/null || true
) 2>/dev/null || true

# 時刻同期（ネットワーク初期化後に1回だけ）
(
	echo "[ntp_sync] before: $(date -R 2>/dev/null || date)" >> /logs/boot.log
	/umu_bin/ntp_sync >> /logs/boot.log 2>&1 || true
	echo "[ntp_sync] after : $(date -R 2>/dev/null || date)" >> /logs/boot.log
) 2>/dev/null || true

( telnetd -p 23 -l /bin/login ) 2>/dev/null || true
( /umu_bin/ftpd_start ) 2>/dev/null || true

echo "[rcS] rcS done" > /dev/console 2>/dev/null || true
