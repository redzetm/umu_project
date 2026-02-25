# nc（BusyBox版）

## 概要
TCP/UDPで接続してデータを送受信するためのコマンド。
UmuOSでは「疎通確認」「1行リクエストを投げる」用途でよく使う。

## よく使う

- TCPで接続
  - `nc <host> <port>`
- 1行送って終了（例）
  - `printf 'hello\n' | nc <host> <port>`

## 例

### 1) TCP疎通（接続できるかだけ確認したい）

- `nc <host> <port>`

接続できれば、入力した内容がそのまま送られる（相手側がエコーするとは限らない）。

### 2) 1行だけ送って返答を受ける（プロキシ等）

- `printf 'suggest shell %s\n' 'netstatの使い方を教えてください' | nc 192.168.0.203 7777`

## 注意

- BusyBox版の `nc` は、フル機能のnetcatとオプションが違うことがある
- `-w <sec>`（タイムアウト）が使えるかどうかは、BusyBoxのビルド設定次第

## 関連

- `telnet`
- `ping`
