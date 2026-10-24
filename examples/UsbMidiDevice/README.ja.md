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

## 準備と実行

1. [サンプル入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)で導入条件と必要な機器を確認します。対応するボード・外部ライブラリはルートREADMEを参照します。
2. [UsbMidiDevice.ino](UsbMidiDevice.ino)を開き、実際のボードとポートを選びます。次のincludeで使う依存機能も準備してください。

   `<EspMidiEspUsbDevice.h>`, `<EspMidiUart.h>`

3. 書き込んだらシリアルモニタを115200 baudへ合わせます。起動ログとソースの操作条件を確認します。

## 動作の流れ

USB DeviceのMIDI ポートをルートへ接続する例です。詳細なAPIとデータの扱いは次の項目で確認します。

以下はソース内で最初に現れるAPIの順です。コールバック登録の位置は、実際の呼び出し順序とは異なります。

1. `usb.begin(...)` — 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
2. `router.addRoute(...)` — 切断と意図しないループを考慮して、入力と出力のEndpoint／グループを登録します。
3. `usbPort.update(...)` — 例にある実行場所から定期的に呼び、対象の処理を進めます。

変更する前に確認する定数: `RX_PIN`, `TX_PIN`, `BUTTON_PIN`, `IN_CABLES`, `OUT_CABLES`。GPIOやチャンネル、容量などは用途に合わせます。

## APIの使い方と注意点

各Transportとポートを初期化し、ルートを登録してから、例にあるRouter／ポート／スタックの処理を継続します。MIDIを転送するだけでは音は鳴らず、楽器やソフト音源へ接続します。USB／BLEの接続でポートの可用性は変わるため、グループや安定した識別子と、現在のEndpointは別の役割です。

双方向ルートを追加する際は意図しないループを防ぎ、注入したNote OnにNote Offを対応させます。UART MIDIにはDIN用の適切な電気回路が必要で、Socketへ直接GPIOをつなぐ配線ではありません。Filter追加時もPacket長とSysEx viewの寿命を保ちます。

コールバック引数への参照は永続領域ではありません。

## 操作と出力の見方

出力書式の一部は次のとおりです。printfの書式記号は実行時の値に変わります。表示順や数値は実行条件に応じて変わるので、その行へ進む条件と併せて確認します。

```text
from the PC:
EspMidi USB MIDI interface ready
```

## 確認と問題の切り分け

USB DeviceのMIDI ポートをルートへ接続することを出力から確認します。まず例のままの入力で試し、その後に設定やデータ源を1つずつ変えます。

| 症状 | 最初に確認すること |
|---|---|
| 出力がない | ボード／依存ライブラリ、初期化結果、表示先とシリアル速度 |
| 開始するが途中で止まる | 待機条件、バッファの空き、相手や入力の準備、継続呼び出し |
| 値が不自然・不安定 | 長さと単位、形式、寿命、共有状態、部分的な処理結果 |

共通の考え方は[入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)、別の例は[サンプル一覧](../README.ja.md)へ進んでください。
