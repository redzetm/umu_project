# UmuOS-0.1.5-dev 機能追加（手動により実装）手順書：ftpdを使ってファイル転送できるようにする

- FTPサーバ：BusyBox の `ftpd` で簡易に提供できる
  - 想定用途：ファイル転送（テキスト/バイナリ）
    - 転送モードは原則 `binary` を推奨（テキストも `binary` で問題ない。`ascii` は改行コード変換等が入る可能性がある）
  - ユーザーのホームディレクトリは不要（公開ディレクトリは新設せず、既存ディレクトリを FTP のルートとして扱う。例：`/tmp`）
  - 常駐させる場合は `ftpd` 単体を常駐させるのではなく、前段の `tcpsvd` を常駐させて接続ごとに `ftpd` を起動する（`inetd` のミニ版のような役割）
  - init（BusyBox init）で常駐させる：`/etc/inittab` の `::sysinit:/etc/init.d/rcS` から実行される `/etc/init.d/rcS` に、以下のように `tcpsvd` をバックグラウンド起動して PID を保存する
    - PIDファイル保存先は `/run` を優先する（`/var/run` は歴史的経緯で `/run` への symlink のことが多い）

    ```sh
    mkdir -p /run

    # LAN 内限定にしたい場合は 0.0.0.0 の代わりに特定IPを指定する
    FTP_ROOT=/tmp
    busybox tcpsvd -vE 0.0.0.0 21 busybox ftpd "$FTP_ROOT" &
    echo $! > /run/ftpd.pid
    ```

  - 手動で起動・停止できる start/stop スクリプトを `/umu_bin` に配置する（PIDは `tcpsvd` を管理する）
    - 停止例：`kill "$(cat /run/ftpd.pid)"`（必要なら PID ファイルも削除）
  - `ftpd` の書き込み許可などのオプションは環境により異なるため、まず `busybox ftpd --help` で確認する