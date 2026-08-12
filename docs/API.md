# API reference

[日本語](API.ja.md)

The public API, listed. **What it is for is in [GUIDE.md](GUIDE.md); why it is shaped this way is in [DATA_MODEL.md](DATA_MODEL.md) and [ROUTING.md](ROUTING.md).** This page is for looking up a name and its meaning.

Everything is in `namespace espmidi`. Including `EspMidi.h` brings in the whole core; ports are included individually.

## Contents

| | |
| --- | --- |
| [Messages](#messages) | `Message` / `MessageType` / `Timestamp` / `PortId` |
| [Classifying a status byte](#classifying-a-status-byte) | `isStatusByte()` and friends |
| [The port registry](#the-port-registry) | `PortRegistry` / `InPort` / `OutPort` / groups / notifications |
| [Routing](#routing) | `Router` / `Route` / `RouterCounters` |
| [The application port](#the-application-port) | `AppPort` |
| [Filters and transforms](#filters-and-transforms) | `Filter` / `Transform` / `ValueMap` / `MessageKind` |
| [Wire format codecs](#wire-format-codecs) | `Parser` / `Serializer` / USB packets |
| [Control mapping](#control-mapping) | `Button` / `Analog` / `Encoder` / `ControlOutput` / clock |
| [Ports](#ports) | UART / USB / BLE |
| [Compile-time settings](#compile-time-settings) | `ESPMIDI_*` |

---

## Messages

### `struct Message`

```cpp
PortId      port;        // the source (when received); a sink still sees the source
MessageType type;
Timestamp   timestamp;
uint8_t     status, data1, data2;
uint8_t     dataLength;  // how many data bytes are valid, 0..2
const uint8_t *raw;      // the bytes on the wire; valid during the callback only
size_t         length;
bool           chunk, chunkStart, chunkEnd;
const uint8_t *chunkData;   // valid during the callback only
size_t         chunkLength;
uint8_t channel() const;    // the low nibble for Channel Voice (0..15), else 0
uint8_t command() const;    // the high nibble for Channel Voice, else the status
```

**`raw` and `chunkData` are valid only while the callback is running.** Copy them to keep them.

### `enum class MessageType : uint8_t`

`Utility = 0x0` / `System = 0x1` / `Midi1ChannelVoice = 0x2` / `Data7 = 0x3`. The same numbering as UMP's Message Type.

### `struct Timestamp` / `enum class TimestampUnit`

```cpp
uint16_t      value;
TimestampUnit unit;   // None / Milliseconds13 / JrTicks31250
bool present() const;
```

Only BLE MIDI produces `Milliseconds13`. **The value is never interpreted.**

### `struct PortId`

An opaque port identifier. `static constexpr uint16_t Invalid = 0xffff`, `bool valid() const`.

### Constants and functions

| | |
| --- | --- |
| `MaxPortsPerEndpoint` | 16 |
| `MaxShortMessageBytes` | 3 |
| `size_t buildShortMessage(Message&, uint8_t *dst, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)` | writes the bytes into `dst` and points the message at them. Returns the count, or 0 |
| `size_t serializeShortMessage(const Message&, uint8_t *dst, size_t capacity)` | writes the wire bytes out. 0 for a chunk |

## Classifying a status byte

All pure functions.

| | |
| --- | --- |
| `bool isStatusByte(uint8_t)` | 0x80 and above |
| `bool isDataByte(uint8_t)` | below 0x80 |
| `bool isSystemRealTime(uint8_t)` | 0xF8..0xFF |
| `bool isSystemCommon(uint8_t)` | 0xF0..0xF7 |
| `bool isChannelVoice(uint8_t)` | 0x80..0xEF |
| `bool isSysExStart(uint8_t)` / `isSysExEnd(uint8_t)` | 0xF0 / 0xF7 |
| `int messageDataLength(uint8_t)` | the number of data bytes. Negative values are special: `-1` not a status, `-2` SysEx start, `-3` SysEx end, `-4` undefined |
| `MessageType messageTypeForStatus(uint8_t)` | |

## The port registry

### Handles

| Type | Meaning |
| --- | --- |
| `EndpointId` | the unit of connection; disconnects happen here |
| `InPort` / `OutPort` | the routing coordinate, typed by direction |
| `InGroup` / `OutGroup` | a port group. `InGroup::all()` / `OutGroup::all()` are **reserved handles** |

`InPort` and `OutPort` carry `.port` (a `PortId`) and have `valid()` and `==` / `!=`.

### `enum class Transport : uint8_t`

`Unknown` / `Uart` / `UsbDevice` / `UsbHost` / `BleDevice` / `BleHost` / `Application`.

### `enum class Direction` / `PortState`

`Direction`: `In` (into EspMidi) / `Out` (out of EspMidi).
`PortState`: `Unconnected` / `Disconnected` / `Available`.

### `struct EndpointIdentity`

```cpp
Transport transport;
uint8_t   index;       // distinguishes within one transport
uint16_t  vendorId, productId;
char      serial[ESPMIDI_SERIAL_MAX];
bool hasSerial() const;
bool isStatic() const;        // Uart / UsbDevice / BleDevice / Application
bool identifiable() const;    // can it be given the same seat again?
bool matches(const EndpointIdentity &other) const;
```

**A device that cannot be identified gets a new seat on every connection.**

### `struct EndpointInfo` / `struct PortInfo`

`PortInfo` carries `transport`, `index`, `name`, `direction`, `state` and `endpoint`.

### `class PortRegistry`

**Seats are supplied by port implementations.** A sketch normally calls only the read-only accessors and `addListener()`.

| | |
| --- | --- |
| `EndpointId attachEndpoint(const EndpointIdentity&, const char *name = nullptr)` | **idempotent**: an existing seat is returned and made `Available` |
| `bool detachEndpoint(EndpointId)` | makes its ports `Disconnected`. **The seats remain** |
| `InPort attachInPort(EndpointId, uint8_t index)` | idempotent |
| `OutPort attachOutPort(EndpointId, uint8_t index)` | idempotent |
| `size_t endpointCount() const` / `size_t portCount() const` | |
| `bool endpointInfo(EndpointId, EndpointInfo&) const` | |
| `bool portInfo(PortId, PortInfo&) const` | |
| `PortState portState(PortId) const` | |
| `bool portAvailable(PortId) const` | |
| `Direction portDirection(PortId) const` | |
| `EndpointId portEndpoint(PortId) const` | |
| `bool sameEndpoint(PortId, PortId) const` | what the loop rule is built on |
| `PortId portAt(size_t) const` / `EndpointId endpointAt(size_t) const` | for walking everything |

### Port groups

| | |
| --- | --- |
| `InGroup addInGroup(const char *name)` / `OutGroup addOutGroup(const char*)` | |
| `bool addToGroup(InGroup, InPort)` / `bool addToGroup(OutGroup, OutPort)` | |
| `bool removeFromGroup(...)` | |
| `bool groupContains(...) const` | |
| `const char *groupName(...) const` | |
| `size_t groupCount() const` | |

`InGroup::all()` and `OutGroup::all()` are **reserved handles nobody maintains**, so a seat that appears later is included automatically.

### Notifications

```cpp
using PortEventCallback = void (*)(void *context, const PortEvent &event);
bool addListener(PortEventCallback callback, void *context = nullptr);
void clearListeners();
```

`PortEvent` carries `type` (`PortAdded` / `PortStateChanged`) and `port`. **There is no `PortRemoved`** — seats are never removed.

## Routing

### `class Router`

```cpp
explicit Router(PortRegistry &registry);
PortRegistry &registry();
```

#### Routes

| | |
| --- | --- |
| `Route addRoute(InPort, OutPort)` | all four combinations are overloaded |
| `Route addRoute(InPort, OutGroup)` | |
| `Route addRoute(InGroup, OutPort)` | |
| `Route addRoute(InGroup, OutGroup)` | |
| `bool removeRoute(Route)` | |
| `bool setRouteEnabled(Route, bool)` | |
| `bool setRouteAllowSameEndpoint(Route, bool)` | **false by default** (never back where it came from) |
| `size_t routeCount() const` | |

#### Stage rules

The order is always **filter → transform → callback**.

| Route | Input port | Output port |
| --- | --- | --- |
| `setRouteFilter(Route, const Filter&)` | `setInPortFilter(InPort, ...)` | `setOutPortFilter(OutPort, ...)` |
| `setRouteTransform(Route, const Transform&)` | `setInPortTransform(...)` | `setOutPortTransform(...)` |
| `setRouteCallback(Route, TransformCallback, void* = nullptr)` | `setInPortCallback(...)` | `setOutPortCallback(...)` |

```cpp
enum class Verdict : uint8_t { Pass, Drop };
using TransformCallback = Verdict (*)(void *context, Message &message);
```

**A chunk only meets the filter**, and only on the first chunk.

#### Driving

| | |
| --- | --- |
| `bool receive(const Message&)` | **callable from any task, and from several at once.** Copies into the queue; false when full |
| `void update()` | runs the pipeline, **for what was queued when it was called** |
| `size_t queued() const` | |
| `bool outputBusy(OutPort) const` | is a SysEx being sent? |

#### Registering an output (for port implementations)

```cpp
using OutputSink = bool (*)(void *context, const Message &message);
bool setOutputSink(OutPort, OutputSink, void *context = nullptr);
```

#### Diagnostics

```cpp
RouterCounters counters() const;   // a snapshot, not a reference
void resetCounters();
```

`RouterCounters` has `received`, `queueFull`, `delivered`, `sendFailed`, `droppedByFilter`, `droppedByStage`, `sysExRejected`, `blockedBySysEx` and `noRoute`. Their meanings are in [ROUTING.md](ROUTING.md).

## The application port

### `class AppPort`

```cpp
AppPort(Router &router, const char *name = "application", uint8_t index = 0);
EndpointId endpoint() const;
InPort in() const;    // inject here
OutPort out() const;  // receive what is delivered here
void onMessage(MessageCallback callback, void *context = nullptr);
bool send(const Message&);
bool sendShort(uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0);
```

```cpp
using MessageCallback = void (*)(void *context, const Message &message);
```

**`send()` never recurses immediately.** An injected message is handled by the next `update()`.

## Filters and transforms

### `MessageKind` (a bitmask)

`KindNoteOff`, `KindNoteOn`, `KindPolyPressure`, `KindControlChange`, `KindProgramChange`, `KindChannelPressure`, `KindPitchBend`, `KindSystemCommon`, `KindSystemRealTime`, `KindData`.

Combined: `KindNotes`, `KindChannelVoice`, `KindSystem`, `KindAll`.

`uint16_t messageKind(const Message&)` — **a note on with velocity 0 is `KindNoteOff`**.

### `struct Filter`

```cpp
uint16_t kinds = KindAll;
uint16_t channels = 0xffff;   // a bitmask
uint8_t  noteMin = 0, noteMax = 127;
uint8_t  ccMin = 0,   ccMax = 127;
bool accepts(const Message&) const;
void allowOnlyChannel(uint8_t);
void allowChannel(uint8_t);
void blockChannel(uint8_t);
```

A message with no channel is never rejected by a channel condition; one with no note is never rejected by a note range.

### `struct Transform`

```cpp
int8_t   channel = -1;        // 0..15 to set it, negative to keep it
int8_t   channelOffset = 0;   // wraps within the 16
int16_t  transpose = 0;       // a note pushed out of range is dropped
int16_t  noteOffset = 0;      // independent of transpose, so they compose
int16_t  controller = -1;     // 0..127 to set it, negative to keep it
ValueMap velocity, controllerValue, pressure;
bool apply(Message&) const;   // false means drop it
```

**A note on with velocity 0 is left alone.**

### `struct ValueMap`

```cpp
static ValueMap range7(uint8_t inLow, uint8_t inHigh, uint8_t outLow, uint8_t outHigh);
static ValueMap scale7(uint8_t outLow, uint8_t outHigh);
static ValueMap fixed7(uint8_t value);
uint16_t apply(uint16_t) const;
uint8_t  apply7(uint8_t) const;
```

**The endpoints are held normalised**, so a rule keeps its meaning when MIDI 2.0 widens the values. Swapping the output endpoints reverses it.

`uint16_t normalizeFrom7(uint8_t)` and `uint8_t denormalizeTo7(uint16_t)` are public too.

## Wire format codecs

Normally used by ports; a sketch does not need to touch them.

### `class Parser` (MIDI 1.0 bytes → `Message`)

```cpp
explicit Parser(PortId port);
void setPort(PortId);
PortId port() const;
void reset();
bool inSysEx() const;
template <typename Fn> void parse(const uint8_t *data, size_t length, Fn &&onMessage);
```

It resolves running status, handles real-time interruptions and turns SysEx into chunks. **A chunk points into the input buffer.**

### `class Serializer` (`Message` → MIDI 1.0 bytes)

```cpp
void reset();
bool inStream() const;
template <typename Fn> bool serialize(const Message&, Fn &&write);
template <typename Fn> bool closeStream(Fn &&write);
```

`write(const uint8_t*, size_t) -> bool`. The `0xF0` / `0xF7` framing is added here. **Running status is not used when sending.**

### USB MIDI packets

```cpp
static constexpr size_t UsbPacketBytes = 4;
enum class UsbCin : uint8_t { ... };
uint8_t usbPacketCable(const uint8_t *packet);
UsbCin  usbPacketCin(const uint8_t *packet);
uint8_t usbCinLength(UsbCin);
bool    usbCinIsSysEx(UsbCin);
UsbCin  usbCinForStatus(uint8_t status);
```

`class UsbPacketDecoder`: `reset()`, `resetCable(uint8_t)`, `setCablePort(uint8_t, PortId)`, `cablePort(uint8_t)`, `inSysEx(uint8_t)`, `decodePacket(...)`, `decode(...)`. **SysEx state is per cable.**

`class UsbPacketEncoder`: `setCable(uint8_t)`, `cable()`, `reset()`, `inSysEx()`, `maxEncodedBytes(const Message&)`, `encode(const Message&, uint8_t *dst, size_t capacity)`. Output is always a multiple of four bytes.

## Control mapping

**None of these touches a pin or reads the time.** You hand over the reading and the current time.

### `class Button`

```cpp
explicit Button(AppPort&, const ButtonConfig& = ButtonConfig());
ButtonConfig &config();
bool on() const;
bool update(bool pressed, uint32_t nowMs);   // true if something was sent
bool resend();
```

`ButtonConfig`: `channel`, `note` (true for a note, false for a control change), `number`, `onValue`, `offValue`, `debounceMs` (20), `latch`.

**The first reading sends nothing** — a pedal already held down at startup is not reported.

### `class Analog`

```cpp
explicit Analog(AppPort&, const AnalogConfig& = AnalogConfig());
AnalogConfig &config();
uint8_t value() const;
uint16_t raw() const;
bool update(uint16_t raw);   // true if the value changed and was sent
bool resend();
```

`AnalogConfig`: `channel`, `controller`, `rawMin`, `rawMax` (swap them to reverse), `outLow`, `outHigh`, `hysteresis` (8), `smoothing` (a shift count, 0).

### `class Encoder`

```cpp
explicit Encoder(AppPort&, const EncoderConfig& = EncoderConfig());
EncoderConfig &config();
uint8_t value() const;
bool update(int32_t position);   // hand over the accumulated position
bool turn(int32_t detents);      // or a delta
```

`EncoderConfig`: `channel`, `controller`, `mode`, `step`, `value`.

`enum class EncoderMode`: `Absolute`, `RelativeTwosComplement`, `RelativeSignedBit`, `RelativeBinaryOffset`. **There are three relative forms because there is no standard.**

### `class ControlOutput` (received MIDI → an LED and so on)

```cpp
ControlOutput(const Filter&, LevelCallback, void *context = nullptr);
Filter &filter();
void onLevel(LevelCallback, void *context = nullptr);
uint8_t level() const;
bool handle(const Message&);                          // true if it matched
static void receive(void *context, const Message&);   // the shape AppPort::onMessage wants
```

```cpp
using LevelCallback = void (*)(void *context, uint8_t level, const Message &message);
uint8_t messageLevel(const Message&);   // a note off, and velocity 0, are 0
```

### Clock

```cpp
static constexpr uint8_t MidiClockTicksPerQuarter = 24;
uint32_t microsPerClockTick(uint32_t bpmTimes100);
uint32_t bpmTimes100FromTick(uint32_t microsPerTick);
```

Tempo is **an integer of hundredths of a BPM** (120.00 BPM is 12000).

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
size_t update(uint32_t nowMicros); // how many ticks went out
```

**One `update()` sends at most `MaxCatchUpTicks` (24)**; a longer stall resynchronises the schedule instead.

`class ClockCounter`:

```cpp
bool handle(const Message&, uint32_t nowMicros);   // true if it was a clock or transport message
bool running() const;
uint8_t tick() const;        // 0..23
uint32_t quarters() const;
bool onQuarter() const;
uint32_t microsPerTick() const;
uint32_t bpmTimes100() const;
void reset();
```

## Ports

They share a shape: take the stack in the constructor, supply seats in `begin()`, take in what arrived in `update()`. The details and limits are in [PORTS.md](PORTS.md); writing your own is [PORT_AUTHORING.md](PORT_AUTHORING.md).

### `EspMidiUart.h`

```cpp
espmidi::UartPort port(Router&, HardwareSerial&, uint8_t index = 0);
bool begin(const char *name = "UART MIDI", int8_t rxPin = -1, int8_t txPin = -1);
void end();          // closes a stream in progress with 0xF7 first
void update();
InPort in() const;  OutPort out() const;
EndpointId endpoint() const;  bool started() const;
```

`static constexpr unsigned long UartMidiBaud = 31250;`. The real class is `BasicUartPort<SerialType>`.

### `EspMidiEspUsbDevice.h`

```cpp
espmidi::UsbDevicePort port(Router&, EspUsbDeviceMidi&, EspUsbDevice&, uint8_t index = 0);
bool begin(const char *name = "USB MIDI");   // after usb.begin()
void end();  void update();
uint8_t inPortCount() const;   // = outCableCount(): host → device
uint8_t outPortCount() const;  // = inCableCount(): device → host
InPort in(uint8_t cable = 0) const;  OutPort out(uint8_t cable = 0) const;
bool available() const;
uint32_t unknownCablePackets() const;
```

**The cable counts invert.**

### `EspMidiEspUsbHost.h`

```cpp
espmidi::UsbHostPort port(Router&, EspUsbHost&);
bool begin();
void end();
void update();                 // the millis() version, on Arduino
void update(uint32_t nowMs);
size_t deviceCount() const;
uint8_t addressAt(size_t index) const;
EndpointId endpointFor(uint8_t address) const;
uint8_t inPortCount(uint8_t address) const;   // = inCableCount: not inverted
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
bool available() const;                  // has anyone subscribed?
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

`class BleSysExBuffer` is public as well (the dump reassembly).

## Compile-time settings

`#define` them **before** including `EspMidi.h`. Measured effects are in [FOOTPRINT.md](FOOTPRINT.md).

| Macro | Default | |
| --- | --- | --- |
| `ESPMIDI_MAX_ENDPOINTS` | 8 | |
| `ESPMIDI_MAX_PORTS` | 32 | across all ports |
| `ESPMIDI_MAX_PORT_GROUPS` | 8 | |
| `ESPMIDI_MAX_PORT_LISTENERS` | 4 | |
| `ESPMIDI_NAME_MAX` | 32 | |
| `ESPMIDI_SERIAL_MAX` | 24 | |
| `ESPMIDI_MAX_ROUTES` | 16 | |
| `ESPMIDI_QUEUE_ENTRIES` | 32 | |
| `ESPMIDI_CHUNK_BYTES` | 48 | payload carried by one entry |
| `ESPMIDI_UART_RX_BYTES` | 64 | bytes read per `update()` |
| `ESPMIDI_UART_CONFIG` | `SERIAL_8N1` | |
| `ESPMIDI_USB_PACKETS_PER_UPDATE` | 32 | |
| `ESPMIDI_MAX_USB_HOST_DEVICES` | 4 | |
| `ESPMIDI_USB_HOST_MAX_CABLES` | 8 | |
| `ESPMIDI_USB_HOST_PACKETS` | 64 | |
| `ESPMIDI_USB_HOST_POLL_MS` | 100 | |
| `ESPMIDI_MAX_BLE_CONNECTIONS` | 4 | |
| `ESPMIDI_BLE_SYSEX_BYTES` | 320 | the dump reassembly limit |
| `ESPMIDI_BLE_EVENTS` | 8 | |

The version macros are `ESPMIDI_VERSION_MAJOR`, `_MINOR`, `_PATCH` and `_STR`.
