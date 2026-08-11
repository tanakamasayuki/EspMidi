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

まだありません。[../TEST_PLAN.ja.md](../TEST_PLAN.ja.md) のカバレッジ計画を参照してください。

## 追加予定

- `uart_midi`: UART ポート 2 つの間で `EspMidi` がルーティングする(Phase 5)。
- `usb_host_device`: ESP32-P4 1台で USB Host ポートと USB Device ポートを同時に動かし、`EspMidi` が両者の間を転送する(Phase 6・7)。
