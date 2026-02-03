# UmuOS-0.1.4-base-stable 実装後の手動設定部分

実装後に手動で設定する部分（次期バージョンでは UmuOS 実装時に含める）。

## 手動で設定する項目

- DNS：`/etc/resolv.conf` を Google DNS `8.8.8.8` にして外界通信可能にする
- タイムゾーン：JST（`Japan/Tokyo`）にする
- NTP：BusyBox の `ntpd` で時刻を同期する
- Busyboxのsuでsetuidが効かない問題。自作suコマンド作成する
- ush の alias 機能が未実装のため、当面はエイリアス代替としてラッパースクリプトを `/ush_bin` に追加する（例：`/ush_bin/ll`）
  - 将来的に UmuOS ビルド時に `PATH` の先頭を `/ush_bin` にすることを検討（`/bin/ush` 起動前に設定される必要がある）
  - `/ush_bin` は root のみ書き込み可能にする（例：`root:root`, `0755`）


※適時追加を予定