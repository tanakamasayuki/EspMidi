# UsbMidiDevice

[English](README.md)

PC に **2 ポートの USB MIDI インターフェース**として見えます。

- **ポート 1** は UART の MIDI DIN 対。MIDI IN に来たものが PC へ、PC が送ったものが MIDI OUT へ出ます。
- **ポート 2** はボード自身。スケッチがノートを送り、PC が送ってきたものを受け取ります。

**このポート 2 が、市販の USB MIDI インターフェースにできないことです。** 同じボードが MIDI 機器でもあり MIDI ルータでもあって、どちらもルーティングから見れば同じポートです。

## 構成

```text
MIDI IN  ──→ GPIO20 ──→ [ポート 1] ──→ PC
MIDI OUT ←── GPIO19 ←── [ポート 1] ←── PC
         ボタン ──────→ [ポート 2] ──→ PC
        コンソール ←─── [ポート 2] ←── PC
```

| 定数 | 既定 | 意味 |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT |
| `BUTTON_PIN` | 0 | ポート 2 でノートを鳴らすボタン |
| `IN_CABLES` / `OUT_CABLES` | 2 / 2 | 宣言する cable 数(**ホスト視点**) |

コンソールはボードの外付け USB-serial チップで、PC が見る native USB とは別です。

## 読みどころ

**cable 数はホスト視点の名前です。** `EspUsbDevice` の IN は device → host(= このライブラリの**出力**ポート)、OUT は host → device(= **入力**ポート)です。逆に取ると全ポートが逆向きに動くので、`espmidi::UsbDevicePort` の側では `inPortCount()` / `outPortCount()` という**このライブラリの向き**で数えます。

**`usbPort.begin()` は `usb.begin()` の後に呼びます。** cable 数が確定するのはスタックが起動してからです。

**PC が構成する前のポートは送信を拒否します。** ルーティングが `sendFailed` として数えるので、「何も届かない」ときに最初に見る場所になります。

**cable 数を必要以上に増やさないでください。** descriptor が大きくなり、HID や CDC と同居する複合構成では上限に当たります([../../docs/PORTS.ja.md](../../docs/PORTS.ja.md))。

## プロファイル

`EspUsbDevice` の複数 cable 対応が未リリースなので、profile はローカルチェックアウト向けの `esp32s3_local` だけです。
