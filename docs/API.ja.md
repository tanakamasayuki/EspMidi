# API リファレンス

[English](API.md)

公開 API の一覧です。**何をするものかは [GUIDE.ja.md](GUIDE.ja.md)、なぜその形かは [DATA_MODEL.ja.md](DATA_MODEL.ja.md) / [ROUTING.ja.md](ROUTING.ja.md)** にあります。ここは「名前と意味」だけを引くための場所です。

すべて `namespace espmidi` です。`EspMidi.h` を include すると core が全部入り、ポートは個別に include します。

## 目次

| | |
| --- | --- |
| [メッセージ](#メッセージ) | `Message` / `MessageType` / `Timestamp` / `PortId` |
| [ステータスバイトの判定](#ステータスバイトの判定) | `isStatusByte()` など |
| [ポートレジストリ](#ポートレジストリ) | `PortRegistry` / `InPort` / `OutPort` / 群 / 通知 |
| [ルーティング](#ルーティング) | `Router` / `Route` / `RouterCounters` |
| [アプリケーションポート](#アプリケーションポート) | `AppPort` |
| [フィルタと変換](#フィルタと変換) | `Filter` / `Transform` / `ValueMap` / `MessageKind` |
| [ワイヤ形式のコーデック](#ワイヤ形式のコーデック) | `Parser` / `Serializer` / USB パケット |
| [Control Mapping](#control-mapping) | `Button` / `Analog` / `Encoder` / `ControlOutput` / クロック |
| [ポート](#ポート) | UART / USB / BLE |
| [コンパイル時の設定](#コンパイル時の設定) | `ESPMIDI_*` |

---

## メッセージ

### `struct Message`

```cpp
PortId      port;        // 送信元(受信時)。sink では送信元のまま
MessageType type;
Timestamp   timestamp;
uint8_t     status, data1, data2;
uint8_t     dataLength;  // 有効なデータバイト数 0..2
const uint8_t *raw;      // ワイヤのバイト列。コールバック中のみ有効
size_t         length;
bool           chunk, chunkStart, chunkEnd;
const uint8_t *chunkData;   // コールバック中のみ有効
size_t         chunkLength;
uint8_t channel() const;    // Channel Voice のとき下位ニブル(0..15)。他は 0
uint8_t command() const;    // Channel Voice のとき上位ニブル。他は status
```

**`raw` と `chunkData` はコールバックの実行中だけ有効です。** 保存するならコピーします。

### `enum class MessageType : uint8_t`

`Utility = 0x0` / `System = 0x1` / `Midi1ChannelVoice = 0x2` / `Data7 = 0x3`。UMP の Message Type と同じ番号です。

### `struct Timestamp` / `enum class TimestampUnit`

```cpp
uint16_t      value;
TimestampUnit unit;   // None / Milliseconds13 / JrTicks31250
bool present() const;
```

BLE MIDI だけが `Milliseconds13` を持ちます。**値は解釈されません。**

### `struct PortId`

不透明なポート識別子。`static constexpr uint16_t Invalid = 0xffff`、`bool valid() const`。

### 定数と関数

| | |
| --- | --- |
| `MaxPortsPerEndpoint` | 16 |
| `MaxShortMessageBytes` | 3 |
| `size_t buildShortMessage(Message&, uint8_t *dst, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)` | `dst` にバイトを書き、`Message` をそこへ向ける。書いたバイト数、失敗で 0 |
| `size_t serializeShortMessage(const Message&, uint8_t *dst, size_t capacity)` | ワイヤのバイト列を書き出す。チャンクは 0 |

## ステータスバイトの判定

すべて純粋関数です。

| | |
| --- | --- |
| `bool isStatusByte(uint8_t)` | 0x80 以上 |
| `bool isDataByte(uint8_t)` | 0x80 未満 |
| `bool isSystemRealTime(uint8_t)` | 0xF8..0xFF |
| `bool isSystemCommon(uint8_t)` | 0xF0..0xF7 |
| `bool isChannelVoice(uint8_t)` | 0x80..0xEF |
| `bool isSysExStart(uint8_t)` / `isSysExEnd(uint8_t)` | 0xF0 / 0xF7 |
| `int messageDataLength(uint8_t)` | データバイト数。負の値は特別扱い(`-1` ステータスでない / `-2` SysEx 開始 / `-3` SysEx 終了 / `-4` 未定義) |
| `MessageType messageTypeForStatus(uint8_t)` | |

## ポートレジストリ

### ハンドル

| 型 | 意味 |
| --- | --- |
| `EndpointId` | 接続の単位。切断はここで起きる |
| `InPort` / `OutPort` | ルーティングの座標。方向で型が分かれている |
| `InGroup` / `OutGroup` | ポート群。`InGroup::all()` / `OutGroup::all()` は**予約ハンドル** |

`InPort` / `OutPort` は `.port`(`PortId`)を持ち、`valid()` と `==` / `!=` があります。

### `enum class Transport : uint8_t`

`Unknown` / `Uart` / `UsbDevice` / `UsbHost` / `BleDevice` / `BleHost` / `Application`。

### `enum class Direction` / `PortState`

`Direction`: `In`(EspMidi へ入る)/ `Out`(EspMidi から出る)。
`PortState`: `Unconnected` / `Disconnected` / `Available`。

### `struct EndpointIdentity`

```cpp
Transport transport;
uint8_t   index;       // 同じトランスポート内での区別
uint16_t  vendorId, productId;
char      serial[ESPMIDI_SERIAL_MAX];
bool hasSerial() const;
bool isStatic() const;        // Uart / UsbDevice / BleDevice / Application
bool identifiable() const;    // 再接続で同じ席に戻せるか
bool matches(const EndpointIdentity &other) const;
```

**識別できない機器は接続ごとに新しい席になります。**

### `struct EndpointInfo` / `struct PortInfo`

`PortInfo` は `transport` / `index` / `name` / `direction` / `state` / `endpoint` を持ちます。

### `class PortRegistry`

**席を供給するのはポート実装です。** スケッチが直接呼ぶのは通常 `portInfo()` などの参照系と `addListener()` だけです。

| | |
| --- | --- |
| `EndpointId attachEndpoint(const EndpointIdentity&, const char *name = nullptr)` | **冪等**。既存の席があれば同じものを返し、`Available` にする |
| `bool detachEndpoint(EndpointId)` | 配下のポートを `Disconnected` にする。**席は消えない** |
| `InPort attachInPort(EndpointId, uint8_t index)` | 冪等 |
| `OutPort attachOutPort(EndpointId, uint8_t index)` | 冪等 |
| `size_t endpointCount() const` / `size_t portCount() const` | |
| `bool endpointInfo(EndpointId, EndpointInfo&) const` | |
| `bool portInfo(PortId, PortInfo&) const` | |
| `PortState portState(PortId) const` | |
| `bool portAvailable(PortId) const` | |
| `Direction portDirection(PortId) const` | |
| `EndpointId portEndpoint(PortId) const` | |
| `bool sameEndpoint(PortId, PortId) const` | ループ規則の土台 |
| `PortId portAt(size_t) const` / `EndpointId endpointAt(size_t) const` | 全走査用 |

### ポート群

| | |
| --- | --- |
| `InGroup addInGroup(const char *name)` / `OutGroup addOutGroup(const char*)` | |
| `bool addToGroup(InGroup, InPort)` / `bool addToGroup(OutGroup, OutPort)` | |
| `bool removeFromGroup(...)` | |
| `bool groupContains(...) const` | |
| `const char *groupName(...) const` | |
| `size_t groupCount() const` | |

`InGroup::all()` / `OutGroup::all()` は**誰も維持しない予約ハンドル**なので、後から現れた席も自動的に含まれます。

### 通知

```cpp
using PortEventCallback = void (*)(void *context, const PortEvent &event);
bool addListener(PortEventCallback callback, void *context = nullptr);
void clearListeners();
```

`PortEvent` は `type`(`PortAdded` / `PortStateChanged`)と `port` を持ちます。**`PortRemoved` はありません** — 席は削除されないためです。

## ルーティング

### `class Router`

```cpp
explicit Router(PortRegistry &registry);
PortRegistry &registry();
```

#### ルート

| | |
| --- | --- |
| `Route addRoute(InPort, OutPort)` | 4 つの組み合わせすべてに overload あり |
| `Route addRoute(InPort, OutGroup)` | |
| `Route addRoute(InGroup, OutPort)` | |
| `Route addRoute(InGroup, OutGroup)` | |
| `bool removeRoute(Route)` | |
| `bool setRouteEnabled(Route, bool)` | |
| `bool setRouteAllowSameEndpoint(Route, bool)` | **既定は false**(来た元へ返さない) |
| `size_t routeCount() const` | |

#### 段の規則

適用順は常に**フィルタ → 変換 → コールバック**です。

| ルート | 入力ポート | 出力ポート |
| --- | --- | --- |
| `setRouteFilter(Route, const Filter&)` | `setInPortFilter(InPort, ...)` | `setOutPortFilter(OutPort, ...)` |
| `setRouteTransform(Route, const Transform&)` | `setInPortTransform(...)` | `setOutPortTransform(...)` |
| `setRouteCallback(Route, TransformCallback, void* = nullptr)` | `setInPortCallback(...)` | `setOutPortCallback(...)` |

```cpp
enum class Verdict : uint8_t { Pass, Drop };
using TransformCallback = Verdict (*)(void *context, Message &message);
```

**チャンクはフィルタしか通りません**(しかも最初のチャンクだけ)。

#### 駆動

| | |
| --- | --- |
| `bool receive(const Message&)` | **どのタスクからでも、同時に呼べる**。キューへ写す。満杯で false |
| `void update()` | パイプラインを走らせる。**呼び出し時点で積まれていた分だけ** |
| `size_t queued() const` | |
| `bool outputBusy(OutPort) const` | SysEx 送信中か |

#### 出力の登録(ポート実装向け)

```cpp
using OutputSink = bool (*)(void *context, const Message &message);
bool setOutputSink(OutPort, OutputSink, void *context = nullptr);
```

#### 診断

```cpp
RouterCounters counters() const;   // 参照ではなくスナップショット
void resetCounters();
```

`RouterCounters` は `received` / `queueFull` / `delivered` / `sendFailed` / `droppedByFilter` / `droppedByStage` / `sysExRejected` / `blockedBySysEx` / `noRoute`。意味は [ROUTING.ja.md](ROUTING.ja.md) にあります。

## アプリケーションポート

### `class AppPort`

```cpp
AppPort(Router &router, const char *name = "application", uint8_t index = 0);
EndpointId endpoint() const;
InPort in() const;    // ここへ注入する
OutPort out() const;  // ここへ届いたものを受け取る
void onMessage(MessageCallback callback, void *context = nullptr);
bool send(const Message&);
bool sendShort(uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0);
```

```cpp
using MessageCallback = void (*)(void *context, const Message &message);
```

**`send()` は即時再帰しません。** 注入されたメッセージは次の `update()` で処理されます。

## フィルタと変換

### `MessageKind`(ビットマスク)

`KindNoteOff` / `KindNoteOn` / `KindPolyPressure` / `KindControlChange` / `KindProgramChange` / `KindChannelPressure` / `KindPitchBend` / `KindSystemCommon` / `KindSystemRealTime` / `KindData`。

まとめ: `KindNotes` / `KindChannelVoice` / `KindSystem` / `KindAll`。

`uint16_t messageKind(const Message&)` — **velocity 0 のノートオンは `KindNoteOff`** です。

### `struct Filter`

```cpp
uint16_t kinds = KindAll;
uint16_t channels = 0xffff;   // ビットマスク
uint8_t  noteMin = 0, noteMax = 127;
uint8_t  ccMin = 0,   ccMax = 127;
bool accepts(const Message&) const;
void allowOnlyChannel(uint8_t);
void allowChannel(uint8_t);
void blockChannel(uint8_t);
```

チャンネルを持たないメッセージはチャンネル条件で落ちません。ノートを持たないメッセージはノート範囲で落ちません。

### `struct Transform`

```cpp
int8_t   channel = -1;        // 0..15 で設定、負で変更しない
int8_t   channelOffset = 0;   // 16 で巻き戻る
int16_t  transpose = 0;       // 範囲外に出たノートは捨てる
int16_t  noteOffset = 0;      // transpose と独立(合成できる)
int16_t  controller = -1;     // 0..127 で設定、負で変更しない
ValueMap velocity, controllerValue, pressure;
bool apply(Message&) const;   // false で破棄
```

**velocity 0 のノートオンは触りません。**

### `struct ValueMap`

```cpp
static ValueMap range7(uint8_t inLow, uint8_t inHigh, uint8_t outLow, uint8_t outHigh);
static ValueMap scale7(uint8_t outLow, uint8_t outHigh);
static ValueMap fixed7(uint8_t value);
uint16_t apply(uint16_t) const;
uint8_t  apply7(uint8_t) const;
```

**端点を正規化して保持する**ので、MIDI 2.0 で幅が広がっても規則の意味が変わりません。出力側を反転させると逆向きになります。

`uint16_t normalizeFrom7(uint8_t)` / `uint8_t denormalizeTo7(uint16_t)` も公開されています。

## ワイヤ形式のコーデック

通常はポートが使うもので、スケッチが直接触る必要はありません。

### `class Parser`(MIDI 1.0 バイト列 → `Message`)

```cpp
explicit Parser(PortId port);
void setPort(PortId);
PortId port() const;
void reset();
bool inSysEx() const;
template <typename Fn> void parse(const uint8_t *data, size_t length, Fn &&onMessage);
```

running status を解決し、real-time の割り込みを処理し、SysEx をチャンクにします。**チャンクは入力バッファを指します。**

### `class Serializer`(`Message` → MIDI 1.0 バイト列)

```cpp
void reset();
bool inStream() const;
template <typename Fn> bool serialize(const Message&, Fn &&write);
template <typename Fn> bool closeStream(Fn &&write);
```

`write(const uint8_t*, size_t) -> bool`。`0xF0` / `0xF7` の枠付けはこちらが行います。**送信に running status は使いません。**

### USB MIDI パケット

```cpp
static constexpr size_t UsbPacketBytes = 4;
enum class UsbCin : uint8_t { ... };
uint8_t usbPacketCable(const uint8_t *packet);
UsbCin  usbPacketCin(const uint8_t *packet);
uint8_t usbCinLength(UsbCin);
bool    usbCinIsSysEx(UsbCin);
UsbCin  usbCinForStatus(uint8_t status);
```

`class UsbPacketDecoder`: `reset()` / `resetCable(uint8_t)` / `setCablePort(uint8_t, PortId)` / `cablePort(uint8_t)` / `inSysEx(uint8_t)` / `decodePacket(...)` / `decode(...)`。**SysEx 状態は cable ごと**です。

`class UsbPacketEncoder`: `setCable(uint8_t)` / `cable()` / `reset()` / `inSysEx()` / `maxEncodedBytes(const Message&)` / `encode(const Message&, uint8_t *dst, size_t capacity)`。書き出しは常に 4 の倍数です。

## Control Mapping

**どれもピンにも時刻にも触りません。** 読んだ値と今の時刻を渡します。

### `class Button`

```cpp
explicit Button(AppPort&, const ButtonConfig& = ButtonConfig());
ButtonConfig &config();
bool on() const;
bool update(bool pressed, uint32_t nowMs);   // 送ったら true
bool resend();
```

`ButtonConfig`: `channel` / `note`(true でノート、false で CC)/ `number` / `onValue` / `offValue` / `debounceMs`(既定 20)/ `latch`。

**最初の読み取りは何も送りません**(起動時に押されていても報告しない)。

### `class Analog`

```cpp
explicit Analog(AppPort&, const AnalogConfig& = AnalogConfig());
AnalogConfig &config();
uint8_t value() const;
uint16_t raw() const;
bool update(uint16_t raw);   // 値が変わって送ったら true
bool resend();
```

`AnalogConfig`: `channel` / `controller` / `rawMin` / `rawMax`(入れ替えると反転)/ `outLow` / `outHigh` / `hysteresis`(既定 8)/ `smoothing`(シフト数、既定 0)。

### `class Encoder`

```cpp
explicit Encoder(AppPort&, const EncoderConfig& = EncoderConfig());
EncoderConfig &config();
uint8_t value() const;
bool update(int32_t position);   // 累積位置を渡す
bool turn(int32_t detents);      // 差分を渡す
```

`EncoderConfig`: `channel` / `controller` / `mode` / `step` / `value`。

`enum class EncoderMode`: `Absolute` / `RelativeTwosComplement` / `RelativeSignedBit` / `RelativeBinaryOffset`。**標準が無いので 3 形式あります。**

### `class ControlOutput`(受信 → LED など)

```cpp
ControlOutput(const Filter&, LevelCallback, void *context = nullptr);
Filter &filter();
void onLevel(LevelCallback, void *context = nullptr);
uint8_t level() const;
bool handle(const Message&);                          // 一致したら true
static void receive(void *context, const Message&);   // AppPort::onMessage に渡せる形
```

```cpp
using LevelCallback = void (*)(void *context, uint8_t level, const Message &message);
uint8_t messageLevel(const Message&);   // ノートオフと velocity 0 は 0
```

### クロック

```cpp
static constexpr uint8_t MidiClockTicksPerQuarter = 24;
uint32_t microsPerClockTick(uint32_t bpmTimes100);
uint32_t bpmTimes100FromTick(uint32_t microsPerTick);
```

テンポは **BPM の 100 倍の整数**です(120.00 BPM = 12000)。

`class ClockGenerator`:

```cpp
explicit ClockGenerator(AppPort&);
void setTempo(uint32_t bpmTimes100);
void setMicrosPerTick(uint32_t);
uint32_t microsPerTick() const;
uint32_t bpmTimes100() const;
bool running() const;
bool start(uint32_t nowMicros);    // 0xFA
bool resume(uint32_t nowMicros);   // 0xFB
bool stop();                       // 0xFC
size_t update(uint32_t nowMicros); // 送った tick 数
```

**1 回の `update()` で送るのは `MaxCatchUpTicks`(24)まで**で、それを超える遅れはスケジュールを取り直します。

`class ClockCounter`:

```cpp
bool handle(const Message&, uint32_t nowMicros);   // clock / transport なら true
bool running() const;
uint8_t tick() const;        // 0..23
uint32_t quarters() const;
bool onQuarter() const;
uint32_t microsPerTick() const;
uint32_t bpmTimes100() const;
void reset();
```

## ポート

共通の形は「コンストラクタでスタックを受け取り、`begin()` で席を供給し、`update()` で受信を取り込む」です。詳細と制約は [PORTS.ja.md](PORTS.ja.md)、自作は [PORT_AUTHORING.ja.md](PORT_AUTHORING.ja.md) にあります。

### `EspMidiUart.h`

```cpp
espmidi::UartPort port(Router&, HardwareSerial&, uint8_t index = 0);
bool begin(const char *name = "UART MIDI", int8_t rxPin = -1, int8_t txPin = -1);
void end();          // 送信中のストリームを 0xF7 で閉じてから
void update();
InPort in() const;  OutPort out() const;
EndpointId endpoint() const;  bool started() const;
```

`static constexpr unsigned long UartMidiBaud = 31250;`。本体は `BasicUartPort<SerialType>`。

### `EspMidiEspUsbDevice.h`

```cpp
espmidi::UsbDevicePort port(Router&, EspUsbDeviceMidi&, EspUsbDevice&, uint8_t index = 0);
bool begin(const char *name = "USB MIDI");   // usb.begin() の後で
void end();  void update();
uint8_t inPortCount() const;   // = outCableCount()。ホスト → 機器
uint8_t outPortCount() const;  // = inCableCount()。機器 → ホスト
InPort in(uint8_t cable = 0) const;  OutPort out(uint8_t cable = 0) const;
bool available() const;
uint32_t unknownCablePackets() const;
```

**cable 数の向きは反転します。**

### `EspMidiEspUsbHost.h`

```cpp
espmidi::UsbHostPort port(Router&, EspUsbHost&);
bool begin();
void end();
void update();                 // Arduino では millis() を使う版
void update(uint32_t nowMs);
size_t deviceCount() const;
uint8_t addressAt(size_t index) const;
EndpointId endpointFor(uint8_t address) const;
uint8_t inPortCount(uint8_t address) const;   // = inCableCount。反転しない
uint8_t outPortCount(uint8_t address) const;
InPort in(uint8_t address, uint8_t cable = 0) const;
OutPort out(uint8_t address, uint8_t cable = 0) const;
uint32_t unknownCablePackets() const;
uint32_t droppedPackets() const;
uint32_t refusedDevices() const;
```

### `EspMidiEspBle.h`

```cpp
espmidi::BleDevicePort port(Router&, EspBleMidiDevice&, uint8_t index = 0);
bool begin(const char *name = "BLE MIDI");
void end();  void update();
InPort in() const;  OutPort out() const;
bool available() const;                  // 相手が購読しているか
uint32_t oversizedStreams() const;

espmidi::BleHostPort host(Router&, EspBleMidiHost&, EspBle&);
bool begin();  void end();  void update();
size_t deviceCount() const;
uint16_t connectionAt(size_t index) const;
InPort in(uint16_t connectionId) const;
OutPort out(uint16_t connectionId) const;
EndpointId endpointFor(uint16_t connectionId) const;
uint32_t oversizedStreams() const;
uint32_t droppedEvents() const;
uint32_t refusedConnections() const;
```

`class BleSysExBuffer` も公開されています(ダンプの再組み立て)。

## コンパイル時の設定

`EspMidi.h` を include する**前**に `#define` します。効果の実測は [FOOTPRINT.ja.md](FOOTPRINT.ja.md) にあります。

| マクロ | 既定 | |
| --- | --- | --- |
| `ESPMIDI_MAX_ENDPOINTS` | 8 | |
| `ESPMIDI_MAX_PORTS` | 32 | 全ポートの合計 |
| `ESPMIDI_MAX_PORT_GROUPS` | 8 | |
| `ESPMIDI_MAX_PORT_LISTENERS` | 4 | |
| `ESPMIDI_NAME_MAX` | 32 | |
| `ESPMIDI_SERIAL_MAX` | 24 | |
| `ESPMIDI_MAX_ROUTES` | 16 | |
| `ESPMIDI_QUEUE_ENTRIES` | 32 | |
| `ESPMIDI_CHUNK_BYTES` | 48 | 1 エントリが運ぶペイロード |
| `ESPMIDI_UART_RX_BYTES` | 64 | 1 回の `update()` で読むバイト数 |
| `ESPMIDI_UART_CONFIG` | `SERIAL_8N1` | |
| `ESPMIDI_USB_PACKETS_PER_UPDATE` | 32 | |
| `ESPMIDI_MAX_USB_HOST_DEVICES` | 4 | |
| `ESPMIDI_USB_HOST_MAX_CABLES` | 8 | |
| `ESPMIDI_USB_HOST_PACKETS` | 64 | |
| `ESPMIDI_USB_HOST_POLL_MS` | 100 | |
| `ESPMIDI_MAX_BLE_CONNECTIONS` | 4 | |
| `ESPMIDI_BLE_SYSEX_BYTES` | 320 | ダンプ再組み立ての上限 |
| `ESPMIDI_BLE_EVENTS` | 8 | |

バージョンは `ESPMIDI_VERSION_MAJOR` / `_MINOR` / `_PATCH` / `_STR` です。
