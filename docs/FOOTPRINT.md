# Memory and latency

[日本語](FOOTPRINT.ja.md)

**Measured** numbers: how much RAM this costs, what can be cut, and what decides the latency.

**Everything here was measured at compile time on ESP32-S3 with arduino-esp32 3.3.11.** The values differ on a 64-bit host, so this table is the one measured on the target.

## Sizes of the types

| Type | Bytes | Note |
| --- | --- | --- |
| `espmidi::Message` | 32 | cheap enough to pass by value |
| `espmidi::PortRegistry` | 1072 | the default 8 endpoints / 32 ports / 8 groups |
| `espmidi::Router` | 5888 | **the big one**; broken down below |
| `espmidi::AppPort` | 20 | make as many as you like |
| `espmidi::UartPort` | 32 | |
| `espmidi::UsbDevicePort` | 380 | encoders and a seat table for 16 cables |
| `espmidi::UsbHostPort` | 1284 | 4 devices plus a 64-packet ring |
| `espmidi::BleDevicePort` | 356 | mostly the 320-byte dump reassembly |
| `espmidi::BleHostPort` | 1476 | 4 connections, each with a reassembly buffer |
| `espmidi::Button` / `Analog` | 24 | each. A hundred of them is 2.4 KB |

A `Filter` is 8 bytes and a `Transform` 38. Every stage holds both, so each port accounts for about 56 bytes inside `Router`.

## What is in `Router`, and how to cut it

Three things dominate the default 5888 bytes.

| What | What it scales with |
| --- | --- |
| the queue | `ESPMIDI_QUEUE_ENTRIES` × (`ESPMIDI_CHUNK_BYTES` + about 16) |
| stage rules | `ESPMIDI_MAX_PORTS` × about 56 (a filter and a transform, in both directions) |
| routes | `ESPMIDI_MAX_ROUTES` × about 60 |

**Measured reductions** (bytes of `Router`):

| Setting | Bytes | Saved |
| --- | --- | --- |
| default (32 ports / 32 entries / 16 routes) | 5888 | — |
| `ESPMIDI_MAX_PORTS 8` | 4016 | −1872 |
| `ESPMIDI_QUEUE_ENTRIES 8` | 4256 | −1632 |
| `ESPMIDI_MAX_ROUTES 4` | 5024 | −864 |
| `ESPMIDI_CHUNK_BYTES 16` | 4864 | −1024 |
| **all three (ports / entries / routes) together** | **1520** | **−4368** |

`PortRegistry` shrinks the same way.

| Setting | Bytes |
| --- | --- |
| default (8 endpoints / 32 ports / 8 groups) | 1072 |
| 2 endpoints / 8 ports / 2 groups | 304 |

So **a sketch with only two UARTs fits the core into about 1.8 KB** (5888 + 1072 → 1520 + 304).

```cpp
// at the top of the sketch, before EspMidi.h
#define ESPMIDI_MAX_PORTS 8
#define ESPMIDI_QUEUE_ENTRIES 8
#define ESPMIDI_MAX_ROUTES 4
#define ESPMIDI_MAX_ENDPOINTS 2
#define ESPMIDI_MAX_PORT_GROUPS 2
#include <EspMidiUart.h>
```

**Cutting too far costs messages.** A shallower queue raises `queueFull`, fewer ports means seats cannot be created, and fewer routes means `addRoute()` returns an invalid handle. None of it breaks silently — it all shows up **in a counter or a return value** (the troubleshooting section of [GUIDE.md](GUIDE.md)).

## Example build sizes

ESP32-S3, all defaults. **The stack you use dominates, not this library.**

| Example | Flash | Static RAM |
| --- | --- | --- |
| `SimpleMidiOut` | 278 KB | 28.6 KB |
| `SimpleMidiIn` | 280 KB | 28.6 KB |
| `UartMidiMonitor` | 280 KB | 28.6 KB |
| `SameCodeAnyPort` (UART) | 282 KB | 29.0 KB |
| `UsbMidiDevice` | 328 KB | 52.3 KB |
| `GpioControls` | 370 KB | 53.1 KB |
| `UsbHostToUart` (P4) | 580 KB | 85.1 KB |
| `BleMidiToUart` | 648 KB | 39.0 KB |

**UART alone is 278 KB / 28.6 KB**, which is about what an empty Arduino-ESP32 sketch costs. **Adding USB Device costs about 24 KB of RAM, USB Host another 32 KB, and BLE about 370 KB of flash** — all of that belongs to those stacks, not to `EspMidi`.

## Latency

`EspMidi` itself never waits. Three things decide the latency.

### 1. How often `loop()` runs

A received message waits **until the next `update()`**. A 1 ms loop means at most 1 ms; a 10 ms loop means at most 10 ms.

**A `delay()` becomes latency directly.** Keep it out of a playing path. So does `Serial.print()` — about a millisecond a line at 115200 baud.

### 2. How fast the transport is

| | A three-byte message |
| --- | --- |
| UART (31250 baud) | **about 1 ms** |
| USB Full Speed | several packets per 1 ms frame; under 1 ms in practice |
| BLE | **bound by the connection interval** (7.5 ms at best, usually 15–30 ms) |

BLE being slow is inherent. Choose UART or USB for a playing path (see [MIDI_BASICS.md](MIDI_BASICS.md)).

### 3. The pipeline itself

A linear scan proportional to the number of routes and ports. **Even at the default limits (16 routes, 32 ports) it is microseconds per message**, buried under the two above. It is not the place to optimise.

## How many times a message is copied

**An ordinary message: once**, into the queue. The bytes taken from a port travel as pointers inside a `Message`, and the queue copies them into a fixed 32-byte entry.

**A SysEx: once per chunk.** It arrives as a pointer and is copied into a queue entry (48 bytes by default). **Only the BLE output adds a second copy**, to reassemble the whole dump ([PORTS.md](PORTS.md)).

When a 10 KB patch dump crosses from USB Host to UART, **it is copied once per chunk and no more**. That is why UMP was not adopted as the internal representation ([DATA_MODEL.md](DATA_MODEL.md)).

## Measuring it again

The numbers in this document can be extracted at compile time. No board needed.

```cpp
template <int N> struct Show;
Show<sizeof(espmidi::Router)> probe;   // the error message contains the real value
```

The example sizes are `arduino-cli compile`'s own output.
