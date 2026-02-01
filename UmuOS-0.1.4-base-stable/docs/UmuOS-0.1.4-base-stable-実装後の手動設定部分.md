# UmuOS-0.1.4-base-stable 実装後の手動設定部分

実装後に手動で設定する部分（次期バージョンでは UmuOS 実装時に含める）。

## 手動で設定する項目

- DNS：`/etc/resolv.conf` を Google DNS `8.8.8.8` にして外界通信可能にする
- タイムゾーン：JST（`Japan/Tokyo`）にする
- NTP：BusyBox の `ntpd` で時刻を同期する

※適時追加を予定