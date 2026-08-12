# ポート一覧

同梱ポートの一覧、実装状況、依存ライブラリ、PC から見える構成です。

**状況列の意味。** リリース可否の判断でこの列をそのまま信用できるようにするため、実機検証の有無を区別します。

- **予定**: 未着手。
- **実装済み(実機検証待ち)**: コードはあるが実機で確認していない。
- **実装済み(実機検証済み)**: 実機で動作を確認した。

## 一覧

| ポート | ヘッダ | 依存 | 対象 SoC | 状況 |
| --- | --- | --- | --- | --- |
| UART MIDI | `EspMidiUart.h` | なし(`HardwareSerial`) | 全 ESP32 | **実装済み(実機検証済み)** |
| USB Device MIDI | `EspMidiEspUsbDevice.h` | `EspUsbDevice` 2.0.2 以降 | S2 / S3 / P4 | **実装済み(実機検証済み)** |
| USB Host MIDI | `EspMidiEspUsbHost.h` | `EspUsbHost` 2.7.5 以降 | S2 / S3 / P4 | **実装済み(実機検証済み)** |
| BLE MIDI Device | `EspMidiEspBle.h` | `EspBle` 1.2.0 以降 | BLE を持つ SoC | **実装済み(実機検証済み)** |
| BLE MIDI Host | `EspMidiEspBle.h` | `EspBle` 1.2.0 以降 | BLE を持つ SoC | **実装済み(実機検証済み)** |

Phase は [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) の実装順です。

ポートは header-only なので、**スケッチが include したポートの分だけ依存が発生します**。`EspMidi.h` だけを include したスケッチは `EspUsbHost` も `EspBle` も要求しません。

## 各ポートの仕様

### UART MIDI

MIDI 1.0 のバイトストリームを 31250 baud で送受信します。MIDI プロトコルと物理 UART の結び付きが強いので本リポジトリへ同梱しますが、統合コアとは分離します([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md))。

- 供給するエンドポイント: 静的に 1 個。入力ポート 1 / 出力ポート 1
- タイムスタンプ: なし(`TimestampUnit::None`)
- running status の解決とデータ長の判定は core のパーサが行う
- SysEx は境界(`0xF0` / `0xF7`)で判定してチャンクにする
- `begin(name, rxPin, txPin)` が `HardwareSerial` を 31250 baud で開き、席を供給し、出力の sink を登録する。**冪等**で、再設定は `begin()` をもう一度呼ぶだけ
- 受信は `update()` のポーリング。**1 回で読むのは `ESPMIDI_UART_RX_BYTES`(既定 64)まで**で、ダンプを流す機器が `loop()` を占有しない
- 送信は running status を使わない。1 つの出力ポートには複数の入力から来たメッセージが乗るうえ、圧縮したストリームは 1 バイト落ちると以降すべてが読み違いになるため

```cpp
espmidi::UartPort uart(router, Serial1, 1);
uart.begin("MIDI DIN", RX_PIN, TX_PIN);

void loop() {
  uart.update();
  router.update();
}
```

**枠付けはポートの仕事です。** チャンクが運ぶのはペイロードだけなので、`0xF0` は最初のチャンクの前、`0xF7` は最後のチャンクの後ろに `espmidi::Serializer` が付けます。`end()` は**送信途中のストリームを `0xF7` で閉じてから**閉じます(規則 2)。相手の機器が中途半端なダンプを抱えたまま待ち続けないためです。

**`espmidi::BasicUartPort<T>` が本体で、`UartPort` は `HardwareSerial` を当てた別名です。** テンプレートにしてあるのは、ポートの挙動(席の供給、受信の router 到達、枠付け、送信バッファ満杯、中断したダンプの終端)をホスト上のテストで固定するためです。実機のテストに残るのは**バイトが本当にパッドを渡ること**だけになります。

**実 MIDI DIN との関係。** 5V カレントループ、フォトカプラ、220Ω といった物理層は本ポートの外側です。UART の TX / RX に何をつなぐかはスケッチとハードウェアの責任で、自動テストは UART バイト層までを対象とします([../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md))。

### USB Device MIDI

`EspUsbDevice` の `EspUsbDeviceMidi` の生パケット API(`readPacket` / `writePacket`)に乗ります。

- 供給するエンドポイント: 静的に 1 個。cable 数だけ入力ポートと出力ポートを持つ
- タイムスタンプ: なし
- USB MIDI イベントパケット(4 バイト、上位ニブル = cable、下位ニブル = CIN)⇄ `espmidi::Message` の変換は core が持つ
- SysEx は CIN 0x4〜0x7 で組み立てる
- 受信は `readPacket()` のポーリング。`update()` 駆動と相性が良い。**1 回で読むのは `ESPMIDI_USB_PACKETS_PER_UPDATE`(既定 32)パケットまで**
- `begin()` は**スタックを起動した後**に呼ぶ。cable 数が確定するのはそれからで、席はその時点の本数で作られる
- 席が使えるかどうかはホストが決める。`EspUsbDevice::ready()`(= `tud_mounted()`)を `update()` が追い、mount で `Available`、unmount で `Disconnected` になる。**席そのものは消えない**ので、抜き差ししてもルートは張り直さない
- **宣言されていない cable のパケットは捨てる。** cable 番号はパケットヘッダから読んだ値なので、捨てなければ別の席に載る。`unknownCablePackets()` で数が読める
- cable 数は `EspUsbDeviceMidi(device, cableCount)` または `(device, inCableCount, outCableCount)` で宣言する。**方向ごとに本数を変えられる。** `MAX_CABLES` は 16 で、`EspMidi` の 1 エンドポイント上限と一致する
- 方向の呼び方はホスト視点で `EspUsbHostMidiPortInfo` と統一されている(IN = device → host)

**cable 数の向きは反転します。** ここが Phase 6 で最も間違えやすい点です。

| `EspUsbDeviceMidi` | 意味 | `EspMidi` |
| --- | --- | --- |
| `inCableCount()` | device → host(送る) | **出力ポート** `outPortCount()` |
| `outCableCount()` | host → device(受ける) | **入力ポート** `inPortCount()` |

`espmidi::UsbDevicePort` の側はこのライブラリの向きで数えます(`inPortCount()` / `outPortCount()`)。取り違えると全ポートが逆向きに動き、**対称な cable 構成ではテストで気付けません**(受信メッセージの cable 番号は自分のヘッダから読んだ値なので往復しても正しく見える)。だから peer テストの機器は 2 / 3 の非対称にしてあります。

```cpp
EspUsbDeviceMidi usbMidi(usb, 2, 3);          // 2 送信 / 3 受信(ホスト視点)
espmidi::UsbDevicePort port(router, usbMidi, usb);
usb.begin(config);
port.begin("USB MIDI");                        // 出力 2 / 入力 3
```

**`espmidi::BasicUsbDevicePort<M, D>` が本体で、`UsbDevicePort` はその別名です。** UART と同じくテンプレートなので、cable と席の対応・mount の扱い・未宣言 cable の破棄まですべてホスト上のテストで固定されています。

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

**cable 数の向きは反転しません。** `EspUsbHost` の数え方は既にホスト視点で、このライブラリがそのホストだからです。

| `EspUsbHostMidiPortInfo` | 意味 | `EspMidi` |
| --- | --- | --- |
| `inCableCount` | device → host(受ける) | **入力ポート** |
| `outCableCount` | host → device(送る) | **出力ポート** |

**USB Device ポートとは逆**です(あちらは同じホスト視点の名前が機器を指すので反転する)。片方を直すときは両方を読んでください。

**接続は callback ではなく `update()` からの polling で見つけます。** `getDevices()` を `ESPMIDI_USB_HOST_POLL_MS`(既定 100)ごとに引き、差分で席を作ります。列挙は polling 間隔よりずっと長くかかるので機器が遅れて見えることはありません。

**理由は席に触るコードを 1 つのタスクに閉じ込めるためです。** `EspUsbHost` の MIDI コールバックはライブラリのタスクで走るので、そこでやるのは**生パケット 4 バイトをロックフリーのリングに写すこと**だけです。デコーダも cable の対応もレジストリも `update()` からしか触りません。

- 機器が列挙された時点では MIDI インターフェースがまだ claim されていないことがある。`getMidiPortInfo()` が失敗したら**次の polling でもう一度見る**(諦めない)
- 席は識別子で照合する。**アドレスはその回にスタックが配っただけの番号**なので照合には使わない
- 切断すると席は `Disconnected` になり、そのまま残る。**その席への送信は失敗する**(次にそのアドレスを取った別の機器へ届かせないため)
- 記憶域は固定長。`ESPMIDI_MAX_USB_HOST_DEVICES`(既定 4)/ `ESPMIDI_USB_HOST_MAX_CABLES`(既定 8)/ `ESPMIDI_USB_HOST_PACKETS`(既定 64)
- 診断は `unknownCablePackets()` / `droppedPackets()` / `refusedDevices()`。**識別できない機器は接続ごとに新しい席を取る**ので、そういう機器を挿し替え続けると席が尽きる。`refusedDevices()` はそれが見えるようにするためにある

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

**Device 側の席は静的、Host 側は動的です。** そのため `Transport` も `BleDevice` と `BleHost` に分かれています(USB と同じ理由)。Device 側の席はこのボードのものなので識別子を持たなくても毎回同じ席に戻り、Host 側は BLE アドレスで照合します。

**スキャンと接続はスケッチ、接続の中身はポートです。** ポートが `discover()` を呼ぶので、スケッチは `discover()` を呼びません。席ができるのは**サービスの発見と購読が終わったとき**で、リンクが繋がった時点ではありません。

**ダンプはこのポートだけ再組み立てします。** `EspBle` は `0xF0..0xF7` の完全なメッセージを受け取って自分で分割するので、ルーティングが渡すチャンクをここで繋ぎ直します。**上限は `ESPMIDI_BLE_SYSEX_BYTES`(既定 320、`EspBle` 側の上限と同じ)**で、超えたダンプは切り詰めずに拒否して数えます(`oversizedStreams()`)。中途半端なダンプは送らないほうがましだからです。

**受信は BLE タスクからそのままルータのキューへ入ります。** `Router::receive()` はそのためにスレッドセーフにしてあり([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md))、こうするとダンプがコピーされません(チャンクは NimBLE のバッファを指したまま、キューがコピーする)。BLE タスクと `update()` が共有するのは**接続ごとのポートハンドル 1 語だけ**です。

**MIDI のコールバックは `EspMidi` が取ります。** `EspBleMidiDevice::onMessage()` / `EspBleMidiHost::onMidiMessage()` はどちらもその 1 個だけの primary callback なので、スケッチが BLE MIDI を見たいときは**ルーティング経由で読みます**。接続の通知は additional listener を使うので、スケッチの `onConnected()` は奪いません。

- 記憶域は固定長。`ESPMIDI_MAX_BLE_CONNECTIONS`(既定 4)/ `ESPMIDI_BLE_SYSEX_BYTES`(既定 320)/ `ESPMIDI_BLE_EVENTS`(既定 8)
- 診断は `oversizedStreams()`、Host 側は加えて `droppedEvents()` / `refusedConnections()`


## Control Mapping はポートではありません

ボタン・つまみ・エンコーダ・クロックのヘルパーは `src/EspMidiControl.h` にあり、**core の一部**です([DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) の Phase 9)。ピンにも時刻にも触らず、読んだ値と今の時刻を引数で受け取るので、トランスポートライブラリへの依存が発生しません。

出力先はアプリケーションポートなので、**つまみが作った Control Change はここに並んだどのポートへでもルーティングできます**。

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
