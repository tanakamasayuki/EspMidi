# Data model

[日本語](DATA_MODEL.ja.md)

The specification for `EspMidi`'s intermediate representation and its port model. This document states **the specification**; for how far the implementation has got, see [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md), and for the reasoning and the rejected alternatives, [DECISIONS.ja.md](DECISIONS.ja.md) (both Japanese).

Code examples never drop the namespace (`espmidi::`).

## The representation is MIDI 1.0 bytes

The canonical internal representation is a MIDI 1.0 byte sequence. MIDI 2.0's Universal MIDI Packet (UMP) is **not** used internally.

The reason is long SysEx. UMP's SysEx7 carries a fixed six bytes per packet, so a 10 KB patch dump becomes about 1700 packets, and even a straight USB → UART pass-through would repack every six bytes. "Handles long SysEx" is a requirement ([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)), so this is a conflict with the requirements rather than a matter of convenience. With a pointer-and-length chunk, several hundred bytes that just arrived can be handed on as one message.

Instead, **only the places where MIDI 1.0 would be a dead end are dealt with in advance**. There are five of them, below.

## The message

```cpp
namespace espmidi {

// Numbered exactly as UMP's Message Type. Only System, Midi1ChannelVoice and
// Data7 occur at the moment.
enum class MessageType : uint8_t {
  Utility           = 0x0,
  System            = 0x1,  // System Common / System Real-Time
  Midi1ChannelVoice = 0x2,
  Data7             = 0x3,  // SysEx7
  // later: Midi2ChannelVoice = 0x4, Data128 = 0x5, FlexData = 0xD, Stream = 0xF
};

// The unit is part of the type. EspMidi does not interpret the value.
enum class TimestampUnit : uint8_t {
  None,             // the source has no timestamp (USB / UART)
  Milliseconds13,   // BLE MIDI's 13-bit milliseconds (0..8191)
  JrTicks31250,     // later: MIDI 2.0's JR Timestamp (1/31250 s)
};

struct Timestamp {
  uint16_t      value = 0;
  TimestampUnit unit  = TimestampUnit::None;
};

struct Message {
  PortId      port;       // the coordinate that absorbs cable / group
  MessageType type = MessageType::Midi1ChannelVoice;
  Timestamp   timestamp;

  uint8_t status     = 0; // the full status byte (running status resolved)
  uint8_t data1      = 0;
  uint8_t data2      = 0;
  uint8_t dataLength = 0; // how many data bytes are valid (0..2)

  const uint8_t *raw    = nullptr; // the bytes as they are on the wire
  size_t         length = 0;

  // A chunk of a data message. Not a SysEx-only concept.
  bool           chunk       = false;
  bool           chunkStart  = false;
  bool           chunkEnd    = false;
  const uint8_t *chunkData   = nullptr;
  size_t         chunkLength = 0;

  uint8_t channel() const;  // the low nibble, for Channel Voice
  uint8_t command() const;  // the high nibble for Channel Voice, else the status
};

} // namespace espmidi
```

**Having `status` / `data1` / `data2` as first-class fields is a MIDI 1.0 convenience, but `raw` + `length` sit alongside them, so the container's size is not fixed by it.** When MIDI 2.0's 64- and 128-bit messages arrive, a `MessageType` is added and `raw` gets longer; nothing else changes.

### Pointer lifetime

**`raw` and `chunkData` are valid only while the callback is running.** A caller that wants to keep the data copies it.

The rule applies in two places:

1. when a port hands a message to the core (the core copies it into the queue)
2. when the core hands it to a user filter or transform callback

This rule is what allows a long SysEx to pass through without being copied. It is the same rule as `EspBle`'s `EspBleMidiMessage`, so the BLE port's conversion matches it as well.

## Message kinds follow UMP's Message Type

`MessageType`'s numbering is UMP's, so that nothing has to be renumbered when MIDI 2.0 is added.

| `MessageType` | Value | What it means in UMP | Today |
| --- | --- | --- | --- |
| `Utility` | 0x0 | NOOP / JR Clock / JR Timestamp | does not occur |
| `System` | 0x1 | System Common / System Real-Time | occurs |
| `Midi1ChannelVoice` | 0x2 | MIDI 1.0 Channel Voice | occurs |
| `Data7` | 0x3 | SysEx7 | occurs |
| — | 0x4 | MIDI 2.0 Channel Voice | later |
| — | 0x5 | SysEx8 / Mixed Data Set | later |
| — | 0xD | Flex Data | later |
| — | 0xF | UMP Stream | later |

## The channel coordinate is (port, channel)

A MIDI 1.0 channel is 4 bits, so there are only 16 — but UMP has group (4 bits) × channel (4 bits) = 256. Using `channel` alone as the coordinate dead-ends at 16.

`PortId` absorbs USB's cable and UMP's group, so **the coordinate is a 256-wide `(PortId, channel)` space from the start**. The routing and filtering rules do not change between MIDI 1.0 and MIDI 2.0.

## Timestamps are carried, not interpreted

BLE MIDI's is 13-bit milliseconds; MIDI 2.0's JR Timestamp is 16 bits of 1/31250 s. Both are relative timestamps that differ only in unit.

`Timestamp` is carried as **an opaque value whose unit is part of its type**. `EspMidi` never reads it to reorder or schedule anything (design principle 3 in [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)). Forwarding to a port with a different unit drops it to `TimestampUnit::None`. Precise clock synchronisation between different schemes is out of scope.

## Chunks are not SysEx-only

SysEx7 (UMP MT 0x3), SysEx8 (0x5) and Flex Data (0xD) all have a start / continue / end chunk structure. Using the general `chunk` / `chunkStart` / `chunkEnd` shape rather than a `sysEx` flag means future data messages fit as they are.

Where a chunk happens to be split carries no meaning. Only `chunkStart` and `chunkEnd` do. So `EspMidi` **splits chunks when it needs to** (when one does not fit in a queue entry) and **never rejoins them**. After a split, only the first fragment keeps `chunkStart` and only the last keeps `chunkEnd`, so downstream still sees one stream. The rules about how chunks flow are in [ROUTING.md](ROUTING.md).

## Value resolution

MIDI 1.0 has 7-bit velocity and control change values; MIDI 2.0 has 16-bit velocity and 32-bit controllers.

**The transformation API does not call itself "7-bit".** A public API shaped as "give velocity as 0..127" would have to be rebuilt for MIDI 2.0. MIDI 2.0 defines the 1.0 ⇔ 2.0 scaling (min-center-max) normatively, so the wider version can be added later without breaking anything.

Filter conditions (note range, control change number) do not depend on the width and carry over unchanged.

## The port model

Two levels.

```text
Endpoint (the unit of connection; disconnects happen here)
 └─ Port (the routing coordinate; a USB cable, a MIDI 2.0 group)
     ├─ In  Port
     └─ Out Port
```

```text
For example:
  Endpoint "USB:addr3 Roland A-88"     ← a disconnect happens at this level
   ├─ In  Port 0  (cable 0)
   ├─ In  Port 1  (cable 1)
   └─ Out Port 0  (cable 0)

  Endpoint "BLE:xx:xx:xx WIDI Master"
   ├─ In  Port 0
   └─ Out Port 0

  Endpoint "UART1"
   ├─ In  Port 0
   └─ Out Port 0
```

### At most 16 ports per endpoint

A USB MIDI 1.0 cable number is 4 bits (16 at most) and a UMP group is 4 bits too. The limit follows that coincidence: 16. Moving to MIDI 2.0 does not change it.

### Input and output ports are separate things

MIDI is physically separate in each direction as well (DIN uses different connectors, USB different endpoints), so they are separate routing coordinates here too.

**Which endpoint each belongs to is knowable**, though, and that is what gives loop prevention its default structurally ([ROUTING.md](ROUTING.md)).

### A handle is a logical seat

`espmidi::InPort` and `espmidi::OutPort` are opaque handles. There is no public API that takes a number and range-checks it; handles are numbered automatically and returned.

**A handle is a seat, and devices arrive and leave.**

```text
at startup       seats In#0 (UART1) and In#1 (USB Device cable 0) exist statically
a USB device     seat In#2 appears → onPortAdded
routing          In#2 → Out#0 is configured
it is unplugged  seat In#2 remains; its state becomes "disconnected"; the routing is kept
it comes back    the same seat In#2 becomes "available" again; the routing still works
```

If a disconnect invalidated the handle, all the routing configuration would break and the application would be forced to reconfigure. The seat model satisfies "the configuration can change while connected", "disconnects can be followed" and "an unusable port can be handled safely" at the same time.

Sending to a disconnected port returns failure, but is not treated as fatal.

### Matching a seat on reconnect

Seats are matched on an identity, and a match returns the same seat.

| Port | Identity |
| --- | --- |
| USB Host | VID / PID / serial |
| BLE Host | the BLE address |
| USB Device / BLE Device / UART | a static seat, so no matching is needed |

A device that cannot be identified — no serial number, for instance — gets a new seat.

### Static and dynamic ports

| Port | What it supplies |
| --- | --- |
| UART | one endpoint, statically |
| USB Device | one endpoint, statically (with one port per cable) |
| BLE Device | one endpoint, statically |
| USB Host | **0 to N endpoints, dynamically** (they come and go with devices) |
| BLE Host | **0 to N endpoints, dynamically** |
| Application | one endpoint, statically, per one the sketch creates |

**An application port** has no transport behind it: the sketch injects messages into it and receives from it. As `Transport::Application` it follows the same seat, group and loop rules as every other port. See [ROUTING.md](ROUTING.md).

Ports are supplied by port implementations (adapters). **An adapter is not "one port" but "a supplier of ports".** One USB Host adapter discovers several devices and supplies several endpoints with several ports.

### Metadata

Endpoints and ports carry attributes, used both for diagnostics (the visibility requirement in [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)) and for defining port groups.

- the kind of transport (`usb_host` / `usb_device` / `ble` / `uart`)
- a name (a USB product string, a BLE device name, a cable's jack name)
- an identity (VID / PID / serial, a BLE address)
- a direction
- a state (available / disconnected / not connected)

### Port groups

Several ports can be gathered into a group for a purpose. Handles are numbered automatically.

```cpp
espmidi::OutGroup synths = midi.addOutGroup("synths");
```

Initially there is only **explicit membership**, plus the implicit "every input" and "every output" groups. "Ports matching a condition join automatically" is left out: the interaction between dynamic ports arriving and leaving and a group's definition gets complicated.

The implicit groups are **reserved handles**, `InGroup::all()` and `OutGroup::all()`. Nobody maintains them, so **a device plugged in later is included with no update**, and a route to "every output" does not break each time something is plugged in. Groups are typed by direction, so an input cannot be put into an output group.

## One step from MIDI 2.0

How today's concepts line up with the future:

| `EspMidi` | USB MIDI 1.0 | UMP / MIDI 2.0 |
| --- | --- | --- |
| Endpoint | a USB device plus its MIDI interface | UMP Endpoint |
| Port | cable | **group** |
| Port group | — | close to a **Function Block** (a named, directional subset of groups) |
| `MessageType` | derived from the CIN | **the same numbering as Message Type** |
| `Timestamp` | none | JR Timestamp (`JrTicks31250`) |
| `chunk` | SysEx splitting | SysEx7 / SysEx8 / Flex Data chunks |

Adding MIDI 2.0 means adding 0x4 and 0x5 to `MessageType`, adding accessors for 64- and 128-bit messages, and putting the MIDI 1.0 ⇔ 2.0 scaling at the edge of a port. **The routing, filtering, port management and port group code is not touched.**
