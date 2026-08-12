# ポート一覧

同梱ポートの一覧、実装状況、依存ライブラリ、PC から見える構成です。

**状況列の意味。** リリース可否の判断でこの列をそのまま信用できるようにするため、実機検証の有無を区別します。

- **予定**: 未着手。
- **実装済み(実機検証待ち)**: コードはあるが実機で確認していない。
- **実装済み(実機検証済み)**: 実機で動作を確認した。

## 一覧

| ポート | ヘッダ | 依存 | 対象 SoC | 状況 |
| --- | --- | --- | --- | --- |
| UART MIDI | `EspMidiUart.h` | なし(`HardwareSerial`) | 全 ESP32 | 予定(Phase 5) |
| USB Device MIDI | `EspMidiEspUsbDevice.h` | `EspUsbDevice` | S2 / S3 / P4 | 予定(Phase 6) |
| USB Host MIDI | `EspMidiEspUsbHost.h` | `EspUsbHost` | S2 / S3 / P4 | 予定(Phase 7) |
| BLE MIDI Device | `EspMidiEspBle.h` | `EspBle` | BLE を持つ SoC | 予定(Phase 8) |
| BLE MIDI Host | `EspMidiEspBle.h` | `EspBle` | BLE を持つ SoC | 予定(Phase 8) |

Phase は [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) の実装順です。

ポートは header-only なので、**スケッチが include したポートの分だけ依存が発生します**。`EspMidi.h` だけを include したスケッチは `EspUsbHost` も `EspBle` も要求しません。

## 各ポートの仕様

### UART MIDI

MIDI 1.0 のバイトストリームを 31250 baud で送受信します。MIDI プロトコルと物理 UART の結び付きが強いので本リポジトリへ同梱しますが、統合コアとは分離します([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md))。

- 供給するエンドポイント: 静的に 1 個。入力ポート 1 / 出力ポート 1
- タイムスタンプ: なし(`TimestampUnit::None`)
- running status の解決とデータ長の判定は core のパーサが行う
- SysEx は境界(`0xF0` / `0xF7`)で判定してチャンクにする

**実 MIDI DIN との関係。** 5V カレントループ、フォトカプラ、220Ω といった物理層は本ポートの外側です。UART の TX / RX に何をつなぐかはスケッチとハードウェアの責任で、自動テストは UART バイト層までを対象とします([../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md))。

### USB Device MIDI

`EspUsbDevice` の `EspUsbDeviceMidi` の生パケット API(`readPacket` / `writePacket`)に乗ります。

- 供給するエンドポイント: 静的に 1 個。cable 数だけ入力ポートと出力ポートを持つ
- タイムスタンプ: なし
- USB MIDI イベントパケット(4 バイト、上位ニブル = cable、下位ニブル = CIN)⇄ `espmidi::Message` の変換は core が持つ
- SysEx は CIN 0x4〜0x7 で組み立てる
- 受信は `readPacket()` のポーリング。`update()` 駆動と相性が良い
- cable 数は `EspUsbDeviceMidi(device, cableCount)` または `(device, inCableCount, outCableCount)` で宣言する。**方向ごとに本数を変えられる。** `MAX_CABLES` は 16 で、`EspMidi` の 1 エンドポイント上限と一致する
- 方向の呼び方はホスト視点で `EspUsbHostMidiPortInfo` と統一されている(IN = device → host)

**PC から見える構成。** MIDI インターフェース 1 個で、cable 数だけポートが並びます。MIDI 単独構成でも、HID / CDC / MSC を含む複合 USB Device の一部としても使えます。

**実装上の注意。** `EspUsbDevice` 側の複数 cable 対応には次の性質があります([LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md) の依頼 1)。

- **descriptor が大きくなります。** 1 cable で 92 バイト、16 cable で 572 バイト。`MAX_CONFIG_DESCRIPTOR` は 704 バイトへ拡張されましたが、他のクラス(HID / CDC / MSC)と併用する複合構成では合計が上限に当たりえます。`descriptorLength()` で事前にサイズが分かり、収まらない場合は `configurationDescriptorForSpeed()` が 0 を返します。**cable 数を無制限に増やさず、必要な本数だけ宣言します。**
- **cable 範囲外は拒否します。** `EspUsbDevice` の helper は `cableCount()` 以上の cable で false を返し、Host が知らないポートへ黙って載せません。`EspMidi` は生パケットを使いますが、同じ方針で範囲外を拒否します。

**現状の制約。** cable ごとの名前は指定できません。`TUD_MIDI_DESC_JACK_DESC` が 1 つの string index を 4 つの jack すべてに付ける構造で、かつ `EspUsbDevice` 側に文字列テーブルが無いためです。ポート名は Host 側の命名に任せます。

### USB Host MIDI

`EspUsbHost` の MIDI メッセージリスナ(`addMidiMessageListener`)と生バイト送信(`midiSend`)に乗ります。

- 供給するエンドポイント: **動的に 0〜N 個**。機器の接続・切断で増減する
- タイムスタンプ: なし
- `EspUsbHostMidiMessage` は 4 バイトパケットを分解した形で届く。SysEx の連結は `EspUsbHost` にないので core が行う
- 送信は `midiSend()` に生バイトを渡す。cable ニブルは core が立てる
- 接続・切断は `addDeviceConnectedListener` / `addDeviceDisconnectedListener` で追う
- 席の再照合に使う識別子は `EspUsbHostDeviceInfo` の VID / PID / serial
- ポート数は `getMidiPortInfo(info, address)` の `inCableCount` / `outCableCount` で接続時に確定させる

**対象とする構成。** 動的な接続・切断、複数の MIDI 機器、USB ハブ経由の機器、MIDI 以外の USB 機器との共存、MIDI を含む複合機器、1 接続が複数の論理ポートを持つ場合。

**実装上の注意。** `EspUsbHost` 側の cable 数取得には次の性質があります([LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md) の依頼 2)。

- `inCableCount` / `outCableCount` は**ホストから見た方向**です。USB のクラス仕様は embedded jack を機器側から見た名前で呼ぶため、descriptor を直接読む場面では方向が反転します。
- 追跡されるのは**最初の MIDI Streaming インターフェースと、方向ごとに 1 本の bulk endpoint** だけです。同一方向の MS bulk endpoint を複数持つ機器は表現できません。`midiSend()` と受信コールバックの対象範囲と同じ制約なので、**1 機器 = 1 エンドポイント**で対応が付きます。
- **cable 数 0 は「その方向が無い」以外の意味も持ちます。** ポートを 1 本と決め打ちせず、0 ならその方向のポートを作りません。

**現状の制約。** cable ごとの jack 名はまだ取得できません(依頼 2 の後半)。ポート名は機器の product 文字列で代替します。

### BLE MIDI Device / Host

`EspBle` の `EspBleMidiDevice` / `EspBleMidiHost` に乗ります。生 GATT には降りません([DECISIONS.ja.md](DECISIONS.ja.md) の決定 2)。

- 供給するエンドポイント: Device 側は静的に 1 個。Host 側は**接続ごとに動的**
- タイムスタンプ: **13 bit ミリ秒(`TimestampUnit::Milliseconds13`)**。4 つのポートの中で唯一タイムスタンプを持つ
- BLE MIDI のパケット形式、running status の解決、SysEx の分割送信は `EspBle` が持つ。core は `EspBleMidiMessage` を `espmidi::Message` に変換するだけ
- 接続・切断は `EspBle` の `addConnectedListener` / `addDisconnectedListener` で追う
- 席の再照合に使う識別子は BLE アドレス

**GATT サービスの所有。** スケッチが `EspBleMidiDevice` / `EspBleMidiHost` を作って `EspMidi` に渡します。`EspMidi` が自前で MIDI GATT サービスを登録することはないので、`EspBle` の MIDI をスケッチが直接使っていても二重登録は起きません。

**共存。** BLE MIDI は BLE HID や独自 GATT サービスと共存できます。BLE スタック、接続、セキュリティ、Advertising の管理は `EspMidi` の担当外です。

## 将来候補

[REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) の拡張性に挙げたもののうち、ポートとして追加しうるもの。

| 候補 | 備考 |
| --- | --- |
| MIDI 2.0 / UMP | 器の側は地続きにしてある([DECISIONS.ja.md](DECISIONS.ja.md) の決定 1) |
| RTP-MIDI / AppleMIDI | セッション管理の責務が大きく初期スコープ外 |
| 外部 UART 拡張 | UART ポートを複数持つだけなので追加は容易 |
| SPI / I2C 経由の MIDI ブリッジ | |
| CV/Gate、DIN Sync | MIDI ではなく変換ヘルパー寄り |

ポートは header-only なので、**追加対象が本リポジトリ内に存在する必要はありません**。外部ライブラリが共通ポートとして参加できます。
