# Loopback Tests

**1 台**のボードで複数のポートを同時に動かし、`EspMidi` 経由で往復させる自動テストです。

`EspMidi` は複数のポートを 1 つのシステムとして扱うライブラリなので、1 台で全ポートを動かす構成が最も本質に近いテストになります。常時接続の peer ボードを占有しないので、1 台で確認できるものはここに置きます。

`EspUsbDevice` の `tests/loopback/` と同じ位置づけです。

## ハードウェア

| プロファイル | ボード | 用途 | 配線 |
| --- | --- | --- | --- |
| `s3_loopback` | ESP32-S3 1台 | UART MIDI の往復 | **不要** |
| `p4_loopback` | ESP32-P4 1台 | USB Host と USB Device を同一ボードで動かす | USB データ線 |

必要なときだけ接続する前提です。常時接続の自動テスト環境には含めません。

## UART は配線ゼロ

GPIO マトリクスで **UART1_TX と UART2_RX を同一 GPIO に割り当てる**ことで、外部配線なしに実ペリフェラル 2 つを通せます。

```text
espmidi::UartPort A  ──→ UART1_TX ─┐
                                    ├─ GPIO21(配線なし)
espmidi::UartPort B  ←── UART2_RX ─┘
```

`uart_set_loop_back()` による UART 内部ループバックでも配線ゼロにできますが、GPIO マトリクス方式は 2 つの実ペリフェラルを通るのでより本物に近くなります。

**Arduino のピン管理を 1 箇所だけ迂回します。** 1 ピンにつき 1 ペリフェラルしか覚えず、2 つ目が同じピンを要求すると 1 つ目を外してしまうためです。受信側を先に `begin()` し、そのあと送信信号を `esp_rom_gpio_connect_out_signal()` でピンに重ねます。出力信号の接続はピンの出力ドライバも有効にし、受信側が張った入力経路には触らないので、1 本のピンを 2 つのペリフェラルが共有できます。

31250 baud で送受信し、`EspMidi` がポート A の入力からポート B の出力へルーティングした結果をシリアルログで確認します。

**実 MIDI DIN(フォトカプラ・220Ω・5V カレントループ)は対象外です。** それは [`../manual/`](../manual/) に隔離します。ここで確認するのは UART バイト層です。

## 実行

```sh
uv run --env-file .env pytest loopback/
uv run --env-file .env pytest loopback/uart_midi/
```

開発中の兄弟ライブラリに対して確認する場合は `*_local` プロファイルを指定します。

```sh
uv run --env-file .env pytest loopback/ --profile=p4_loopback_local
```

## 追加済み

- `uart_midi`: UART ポート 2 つの間で `EspMidi` がルーティングする(Phase 5)。ノート / CC / Clock / ピッチベンド / SysEx の往復を確認します。

ポートの挙動そのものは [`../unit/uart_port/`](../unit/uart_port/) がホスト上で固定しているので、**ここで確認するのはバイトが本当にパッドを渡ることだけ**です。

## 追加予定

- `usb_host_device`: ESP32-P4 1台で USB Host ポートと USB Device ポートを同時に動かし、`EspMidi` が両者の間を転送する(Phase 6・7)。
