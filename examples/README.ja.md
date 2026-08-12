# Examples

[English](README.md)

examples はすべて**実用例**です。実ハードウェア構成でそのまま書き込んで使うスケッチにします。

中身は「1) スタック起動 → 2) ポートの生成 → 3) ルートとフィルタを組む」の 3 段構成に統一します。`EspMidi` はスタックを所有しないので、1 段目はスケッチの仕事です。

## Examples

**はじめての方は最初の 3 つを順に**読んでください。使い方ガイドは [../docs/GUIDE.ja.md](../docs/GUIDE.ja.md)、MIDI 自体の注意点は [../docs/MIDI_BASICS.ja.md](../docs/MIDI_BASICS.ja.md) です。

### まずここから(ポート 1 つ)

- `SimpleMidiOut`: 最小のスケッチ。1 ポート・送信のみ。ドレミを鳴らすので、音源をつないで電源を入れれば音が出ます。
- `SimpleMidiIn`: 1 ポート・受信のみ。届いた MIDI を表示するので、キーボードが何を送っているかが見えます。
- `SameCodeAnyPort`: **同じ MIDI コードを UART・USB・BLE で動かします。1 行変えるだけ。** ポートが 1 つでもこのライブラリを使う理由がこれです。

### 実用構成

- `UartMidiMonitor`: UART の MIDI をコンソールに表示しながら、もう 1 本の UART へそのまま流します。キーボードと音源の間に挟んでも演奏が止まりません。
- `UsbMidiDevice`: PC に 2 ポートの USB MIDI インターフェースとして見えます。ポート 1 は MIDI DIN 対、ポート 2 はボード自身。
- `UsbHostToUart`: USB MIDI キーボードで MIDI DIN の音源を鳴らしながら、同じ演奏を PC にも流します(UC1)。
- `BleMidiToUart`: ワイヤレスの BLE MIDI キーボードで MIDI DIN の音源を鳴らします(UC5)。
- `GpioControls`: つまみ・ボタン・エンコーダの MIDI コントローラ。USB MIDI と MIDI DIN の両方に出ます(UC10)。

## 置き方

```text
examples/<Name>/
  <Name>.ino      スケッチ名とディレクトリ名を一致させる
  README.ja.md
  README.md
  sketch.yaml     対象ボードの profile。default_profile を設定する
```

`tests/unit/test_repository_structure.py` がこの構成と、この README の一覧が実体と一致していることを自動確認します。

`sketch.yaml` は default branch では `dir: ../../` で `EspMidi` を参照し、リリース時に共通 workflow がバージョン指定へ書き換えます([../docs/RELEASE_CHECKLIST.ja.md](../docs/RELEASE_CHECKLIST.ja.md))。

## 自作のポートをつなぎたいとき

ポートは header-only なので、`EspMidi` リポジトリの外にも書けます。インターフェースの正本は `src/EspMidi.h` のコメントで、同梱ポートのうち UART が最も小さいので実装例としては UART を読むのが早いです。設計の背景は [../docs/CORE_DESIGN.ja.md](../docs/CORE_DESIGN.ja.md) にあります。
