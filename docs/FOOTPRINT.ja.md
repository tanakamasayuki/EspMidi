# メモリと遅延

[English](FOOTPRINT.md)

RAM をどれだけ使い、どこを削れるか、遅延はどこで決まるかの**実測値**です。

**すべて ESP32-S3 / arduino-esp32 3.3.11 でのコンパイル時実測です。** ホスト(64 bit)とは値が違うので、この表はターゲットで測り直したものを載せています。

## 型ごとの大きさ

| 型 | バイト | 備考 |
| --- | --- | --- |
| `espmidi::Message` | 32 | 値渡ししても安い |
| `espmidi::PortRegistry` | 1072 | 既定 8 エンドポイント / 32 ポート / 8 群 |
| `espmidi::Router` | 5888 | **ここが一番大きい**。内訳は下記 |
| `espmidi::AppPort` | 20 | いくつ作っても安い |
| `espmidi::UartPort` | 32 | |
| `espmidi::UsbDevicePort` | 380 | cable 16 本ぶんのエンコーダと席の表 |
| `espmidi::UsbHostPort` | 1284 | 機器 4 台 + パケットのリング 64 |
| `espmidi::BleDevicePort` | 356 | ダンプ再組み立ての 320 バイトが主 |
| `espmidi::BleHostPort` | 1476 | 接続 4 本 × 再組み立てバッファ |
| `espmidi::Button` / `Analog` | 24 | 1 つあたり。100 個並べても 2.4KB |

`Filter` は 8 バイト、`Transform` は 38 バイトです。段ごとに両方を持つので、ポート 1 つあたり約 56 バイトが `Router` の中にあります。

## `Router` の内訳と削り方

既定の 5888 バイトのうち、大きいのは 3 つです。

| 何が | 何に比例するか |
| --- | --- |
| キュー | `ESPMIDI_QUEUE_ENTRIES` × (`ESPMIDI_CHUNK_BYTES` + 約 16) |
| 段の規則 | `ESPMIDI_MAX_PORTS` × 約 56(フィルタ + 変換を出入り両方に) |
| ルート | `ESPMIDI_MAX_ROUTES` × 約 60 |

**実測した削り方**(`Router` のバイト数):

| 設定 | バイト | 削減 |
| --- | --- | --- |
| 既定(32 ポート / 32 エントリ / 16 ルート) | 5888 | — |
| `ESPMIDI_MAX_PORTS 8` | 4016 | −1872 |
| `ESPMIDI_QUEUE_ENTRIES 8` | 4256 | −1632 |
| `ESPMIDI_MAX_ROUTES 4` | 5024 | −864 |
| `ESPMIDI_CHUNK_BYTES 16` | 4864 | −1024 |
| **上の 3 つ(ポート / エントリ / ルート)を同時に** | **1520** | **−4368** |

`PortRegistry` も同様に縮みます。

| 設定 | バイト |
| --- | --- |
| 既定(8 エンドポイント / 32 ポート / 8 群) | 1072 |
| 2 エンドポイント / 8 ポート / 2 群 | 304 |

つまり **UART 2 本だけのスケッチなら、core は約 1.8KB に収まります**(5888 + 1072 → 1520 + 304)。

```cpp
// スケッチの先頭、EspMidi.h より前に置く
#define ESPMIDI_MAX_PORTS 8
#define ESPMIDI_QUEUE_ENTRIES 8
#define ESPMIDI_MAX_ROUTES 4
#define ESPMIDI_MAX_ENDPOINTS 2
#define ESPMIDI_MAX_PORT_GROUPS 2
#include <EspMidiUart.h>
```

**削りすぎると落ちるのはメッセージです。** キューを浅くすると `queueFull` が増え、ポートを減らすと席が作れず、ルートを減らすと `addRoute()` が無効なハンドルを返します。どれも黙って壊れず**カウンタか戻り値に出ます**([GUIDE.ja.md](GUIDE.ja.md) の「よくあるつまずき」)。

## example のビルドサイズ

ESP32-S3、既定の設定のままです。**ライブラリ本体ではなく、使うスタックが支配します。**

| example | Flash | 静的 RAM |
| --- | --- | --- |
| `SimpleMidiOut` | 278 KB | 28.6 KB |
| `SimpleMidiIn` | 280 KB | 28.6 KB |
| `UartMidiMonitor` | 280 KB | 28.6 KB |
| `SameCodeAnyPort`(UART) | 282 KB | 29.0 KB |
| `UsbMidiDevice` | 328 KB | 52.3 KB |
| `GpioControls` | 370 KB | 53.1 KB |
| `UsbHostToUart`(P4) | 580 KB | 85.1 KB |
| `BleMidiToUart` | 648 KB | 39.0 KB |

**UART だけなら 278 KB / 28.6 KB** で、これは Arduino-ESP32 の最小スケッチとほぼ同じです。**USB Device を足すと RAM が +24 KB、USB Host を足すと更に +32 KB、BLE は Flash が +370 KB** — どれも `EspMidi` ではなくそのスタックの取り分です。

## 遅延

`EspMidi` 自身は待ちません。遅延を決めるのは次の 3 つです。

### 1. `loop()` の周期

受信は**次の `update()` まで待ちます**。`loop()` が 1 ms で回っていれば遅延は最大 1 ms、10 ms なら最大 10 ms です。

**`delay()` を入れるとそのまま遅延になります。** 演奏経路では避けてください。`Serial.print()` も、115200 baud なら 1 行 1 ms 前後かかります。

### 2. トランスポートの速さ

| | 3 バイトのメッセージ |
| --- | --- |
| UART(31250 baud) | **約 1 ms** |
| USB Full Speed | 1 ms フレームに複数パケット。実測で 1 ms 未満 |
| BLE | **接続間隔に縛られる**(最短 7.5 ms、実際は 15〜30 ms が普通) |

BLE が遅いのは仕様です。演奏経路には UART か USB を選んでください([MIDI_BASICS.ja.md](MIDI_BASICS.ja.md))。

### 3. パイプラインそのもの

ルートの本数とポート数に比例した線形走査です。**既定の上限(16 ルート / 32 ポート)でも 1 メッセージあたりマイクロ秒のオーダー**で、上の 2 つに埋もれます。最適化する場所ではありません。

## メッセージのコピー回数

**通常メッセージは 1 回**、キューへ写すときだけです。ポートから受け取ったバイトはそのまま `Message` のポインタで運ばれ、キューが 32 バイトの固定長エントリへ写します。

**SysEx はチャンクごとに 1 回**です。ポインタで受け取り、キューのエントリ(既定 48 バイト)へ写します。**BLE 出力だけは追加で 1 回**、ダンプ全体を組み直すためです([PORTS.ja.md](PORTS.ja.md))。

10 KB の音色ダンプが USB Host → UART を通るとき、コピーされるのは**チャンク 1 回ぶんだけ**です。UMP を内部表現に採らなかったのはこのためです([DATA_MODEL.ja.md](DATA_MODEL.ja.md))。

## 測り直す方法

この文書の数字はコンパイル時に取り出せます。ボードは要りません。

```cpp
template <int N> struct Show;
Show<sizeof(espmidi::Router)> probe;   // エラーメッセージに実際の値が出る
```

example のサイズは `arduino-cli compile` の出力そのままです。
