# EspMidi

[English](README.md)

ESP32 の MIDI インターフェースの間にすわり、統合・複製・振り分けをするための Arduino 向けライブラリです。USB Host、USB Device、BLE、UART でつながった MIDI 入出力を共通のポートとして扱い、その間でメッセージを転送します。

> **未リリースです。** 実装計画は完了し、**同梱ポートはすべて ESP32-S3 実機で動作を確認しています**。API はまだ変わりえます。実装の現在地は [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md)、各ポートの状況は [docs/PORTS.ja.md](docs/PORTS.ja.md) を参照してください。

## 15 行で USB MIDI キーボード → MIDI DIN 音源

```cpp
#include <EspMidiEspUsbHost.h>
#include <EspMidiUart.h>

EspUsbHost usb;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UsbHostPort keyboards(router, usb);
espmidi::UartPort din(router, Serial1, 1);

void setup() {
  usb.begin();                    // 1) スタックはスケッチが持つ
  keyboards.begin();              // 2) ポート
  din.begin("MIDI DIN", 20, 19);
  router.addRoute(espmidi::InGroup::all(), din.out());  // 3) ルート
}

void loop() {
  keyboards.update();
  din.update();
  router.update();
}
```

**キーボードをどこにも書いていません。** USB Host の席は挿したときに現れるので、ルートは「すべての入力」に対して張ります。抜き差ししてもルートは消えず、挿し直せばそのまま続きます。出力先を増やしたいならルートを 1 本足すだけです。

構成は examples すべてで「1) スタック起動 → 2) ポート生成 → 3) ルート」の 3 段に揃えてあります。

## 何をするものか

```text
USB Host MIDI ─┐                              ┌─ USB Device MIDI(PC へ)
USB Device MIDI ├─ 共通の MIDI ポート ─ ルーティング ─┼─ UART MIDI(外部音源へ)
BLE MIDI ───────┤   統合・複製・振り分け          ├─ BLE MIDI
UART MIDI ──────┘   フィルタ・基本変換            └─ ...
```

- **一対一** — USB Host の演奏を UART の音源へ
- **一対多** — 1 つの入力を PC と外部音源へ同時に
- **多対一** — 複数の入力を 1 つの音源へ統合
- **多対多** — 入力元と出力先の組み合わせを個別に管理
- **フィルタと変換** — チャンネル、ノート範囲、CC 番号での振り分け、トランスポーズ、Velocity や CC 値の変換

## 何をしないものか

**MIDI の通信方式そのものを実装しません。** USB や BLE のスタックを起動も停止もせず、所有もしません。それはスケッチと各基盤ライブラリの仕事です。

そのおかげで **MIDI 以外の機能と共存できます**。USB Device で MIDI と HID を同時に公開する、BLE MIDI と独自 GATT サービスを併用する、USB Host で MIDI 機器とキーボードを同時に使う、といった構成を妨げません。

シーケンサー、DAW 機能、ソフトウェアシンセサイザー、音声生成は対象外です。AppleMIDI / RTP-MIDI は初期スコープに含めません。詳細は [docs/REQUIREMENTS.ja.md](docs/REQUIREMENTS.ja.md) の非目標にあります。

## このライブラリは任意です

**MIDI 入力だけ、あるいは出力だけを片方向に使うなら、このライブラリは要りません。** `EspUsbHost` / `EspUsbDevice` / `EspBle` を直接使えば済みます。各ライブラリはそのための MIDI 便利 API と example を持ち続けます。

`EspMidi` に価値が出るのは、**複数のインターフェースが出会う場所**です。

## ポート

ポートは header-only です。スケッチが include したポートの分だけ依存が発生します。`EspMidi.h` だけを include したスケッチは `EspUsbHost` も `EspBle` も要求しません。

| ポート | ヘッダ | 依存 | 状況 |
| --- | --- | --- | --- |
| UART MIDI | `EspMidiUart.h` | なし(`HardwareSerial`) | **実機検証済み** |
| USB Device MIDI | `EspMidiEspUsbDevice.h` | [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) 2.0.2+ | **実機検証済み** |
| USB Host MIDI | `EspMidiEspUsbHost.h` | [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 2.7.5+ | **実機検証済み** |
| BLE MIDI Device / Host | `EspMidiEspBle.h` | [EspBle](https://github.com/tanakamasayuki/EspBle) 1.2.0+ | **実機検証済み** |

詳細と PC からの見え方は [docs/PORTS.ja.md](docs/PORTS.ja.md) にあります。

**外部のポートも参加できます。** ポートは header-only で、追加対象がこのリポジトリ内にある必要はありません。同梱ポートのうち UART が最も小さいので、書くときはそれを読むのが早いです。

## MIDI コントローラにもなります

つまみ・ボタン・エンコーダ・クロックのヘルパーが `EspMidi.h` に入っています(`espmidi::Button` / `Analog` / `Encoder` / `ControlOutput` / `ClockGenerator` / `ClockCounter`)。

**ピンにも時刻にも触りません。** スケッチが読んだ値と今の時刻を渡します。だから ADC でもポートエキスパンダでもタッチセンサでもネットワーク越しの値でも同じヘルパーが動き、跳ねるスイッチやテンポ変化がホスト上のテストで再現できます。

```cpp
knob.update(analogRead(KNOB_PIN));                     // 値を渡すだけ
button.update(digitalRead(BUTTON_PIN) == LOW, millis());
```

出力先はアプリケーションポートなので、**つまみが作った Control Change を USB と MIDI DIN と BLE へ同時に出せます**。

## examples

すべて**実用例**です。そのまま書き込めます。

| example | 内容 |
| --- | --- |
| [`UartMidiMonitor`](examples/UartMidiMonitor/) | UART の MIDI を表示しながら、もう 1 本の UART へそのまま流す |
| [`UsbMidiDevice`](examples/UsbMidiDevice/) | PC に 2 ポートの USB MIDI インターフェースとして見える |
| [`UsbHostToUart`](examples/UsbHostToUart/) | USB MIDI キーボードで DIN 音源を鳴らしながら PC にも流す |
| [`BleMidiToUart`](examples/BleMidiToUart/) | ワイヤレス BLE MIDI キーボードで DIN 音源を鳴らす |
| [`GpioControls`](examples/GpioControls/) | つまみ・ボタン・エンコーダの MIDI コントローラ |

## 対応環境

- Arduino-ESP32(Arduino フレームワーク)
- ESP32 シリーズ

使えるインターフェースは SoC で決まります。USB Host / USB Device は USB OTG を持つ ESP32-S2 / ESP32-S3 / ESP32-P4 が対象です。BLE は BLE を持つ SoC が対象です。UART は全 ESP32 で使えます。

## 設計のあらまし

- **MIDI を解釈しません。** ルーティングに必要な最小限だけを見て、SysEx の中身は解釈せず運びます。
- **時間を持ちません。** タイムスタンプは運びますが解釈しません。
- **core は移植可能な純粋 C++** です。Arduino も ESP-IDF もハードウェアも要求しないので、仕様を固定するテストはホスト上で数秒で回ります。
- **長い SysEx を前提にしています。** 音色ダンプをコピーなしで素通しできる形にしてあります。
- **MIDI 2.0 へ地続きです。** 内部表現は MIDI 1.0 バイト列ですが、メッセージ種別の番号、チャンネル座標、タイムスタンプ、チャンクの扱いを UMP に合わせてあるので、対応時にルーティングやフィルタを作り直さずに済みます([docs/DECISIONS.ja.md](docs/DECISIONS.ja.md) の決定 1)。
- **席は機器より長生きします。** ポートのハンドルは切断で無効になりません。抜き差ししても状態が変わるだけなので、**ルートを張り直す必要がありません**。
- **駆動はスケッチの `loop()` です。** core は自前のタスクを立てません。トランスポートのタスクから来た受信はキューに写され、パイプラインは `update()` の中だけで走ります。`Router::receive()` はそのためにスレッドセーフです。

## ドキュメント

[docs/README.ja.md](docs/README.ja.md) がどの文書をどの順に読むかの案内です。

- [docs/REQUIREMENTS.ja.md](docs/REQUIREMENTS.ja.md) — 何のためのライブラリで、どこまでを対象にするか
- [docs/DATA_MODEL.ja.md](docs/DATA_MODEL.ja.md) — MIDI メッセージとポートモデル
- [docs/ROUTING.ja.md](docs/ROUTING.ja.md) — ルート、パイプライン、SysEx の規則、ループ防止
- [docs/PORTS.ja.md](docs/PORTS.ja.md) — 各ポートの挙動と実装状況
- [docs/DECISIONS.ja.md](docs/DECISIONS.ja.md) — なぜそう設計したのか、採らなかった案
- [docs/CORE_DESIGN.ja.md](docs/CORE_DESIGN.ja.md) — core / ポート / example の境界と並行性
- [tests/README.ja.md](tests/README.ja.md) — テスト構成と実行方法

## テスト

```sh
cd tests
uv run pytest unit/          # ハードウェア不要、数秒
```

`unit/` は `g++` だけで回ります。arduino-cli も board package も要りません。**core が Arduino に依存し始めた瞬間に落ちます。**

ポートも `unit/` に入っています。ポートが借りるオブジェクトのテンプレートになっているので、`HardwareSerial` や `EspUsbHost` の代わりに偽物を当てて挙動を固定できます。実機のテストに残るのは、**実機でしか確認できないことだけ**です。

```sh
uv run --env-file .env pytest loopback/   # 1 台。UART は配線ゼロ
uv run --env-file .env pytest peer/       # 2 台
```

## 関連ライブラリ

- [ESP32KeyBridge](https://github.com/tanakamasayuki/ESP32KeyBridge) — 同じ統合思想でキーボード系入力を扱うライブラリ
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) / [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) / [EspBle](https://github.com/tanakamasayuki/EspBle) — 基盤ライブラリ

## ライセンス

MIT License([LICENSE](LICENSE))
