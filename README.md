# EspMidi

[日本語](README.ja.md)

An Arduino library that sits between the MIDI interfaces an ESP32 can reach and merges, duplicates and splits between them. MIDI inputs and outputs arriving over USB Host, USB Device, BLE and UART all become the same kind of logical port, and messages are routed between them.

> **Under development.** The design is settled and the repository skeleton and test environment are in place (Phase 0 complete). **Nothing works yet.** See [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md) for the current position and [docs/PORTS.ja.md](docs/PORTS.ja.md) for per-port status. The design documents are Japanese only.

## What it does

```text
USB Host MIDI ─┐                              ┌─ USB Device MIDI (to a PC)
USB Device MIDI ├─ one kind of MIDI port ─ routing ─┼─ UART MIDI (to a sound module)
BLE MIDI ───────┤   merge / duplicate / split     ├─ BLE MIDI
UART MIDI ──────┘   filter / basic transform      └─ ...
```

- **One to one** — a USB Host keyboard into a UART sound module
- **One to many** — one input to a PC and an external module at the same time
- **Many to one** — several inputs merged into one module
- **Many to many** — every input/output combination managed individually
- **Filtering and transformation** — split by channel, note range or CC number; transpose; scale velocity and CC values

## What it does not do

**It does not implement MIDI transports.** It never starts, stops or owns a USB or BLE stack — that belongs to the sketch and the transport libraries.

That is what lets MIDI **coexist with everything else**: exposing MIDI and HID from the same USB Device, running BLE MIDI alongside a custom GATT service, or using a MIDI device and a keyboard on the same USB Host.

Sequencers, DAW features, software synthesis and audio generation are out of scope, as are AppleMIDI and RTP-MIDI for now. See the non-goals in [docs/REQUIREMENTS.ja.md](docs/REQUIREMENTS.ja.md).

## This library is optional

**If you only use MIDI in one direction — input only, or output only — you do not need it.** Use `EspUsbHost`, `EspUsbDevice` or `EspBle` directly; each keeps its own MIDI convenience API and examples for exactly that.

`EspMidi` earns its place where **several interfaces meet**.

## Ports

Ports are header-only, so a sketch pulls in a dependency only for the ports it includes. A sketch that includes just `EspMidi.h` requires neither `EspUsbHost` nor `EspBle`.

| Port | Header | Depends on | Status |
| --- | --- | --- | --- |
| UART MIDI | `EspMidiUart.h` | — | planned |
| USB Device MIDI | `EspMidiEspUsbDevice.h` | [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) | planned |
| USB Host MIDI | `EspMidiEspUsbHost.h` | [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) | planned |
| BLE MIDI Device / Host | `EspMidiEspBle.h` | [EspBle](https://github.com/tanakamasayuki/EspBle) | planned |

## Supported environments

- Arduino-ESP32 (Arduino framework)
- ESP32 series

Which interfaces are available depends on the SoC. USB Host and USB Device need USB OTG (ESP32-S2 / ESP32-S3 / ESP32-P4). BLE needs a SoC with BLE. UART works on every ESP32.

## Design in brief

- **It does not interpret MIDI.** It looks at the minimum needed to route, and carries SysEx payloads without reading them.
- **It has no clock.** Timestamps are carried, never interpreted.
- **The core is portable C++** with no Arduino, ESP-IDF or hardware dependency, so the suite that fixes the specification runs on the host in seconds.
- **Long SysEx is assumed.** A patch dump passes through without being copied.
- **It is one step from MIDI 2.0.** The internal representation is MIDI 1.0 bytes, but the message type numbering, the channel coordinate, the timestamp and the chunking all follow UMP, so adding MIDI 2.0 will not mean rewriting routing or filtering.

## Documentation

[docs/README.md](docs/README.md) is the guide to which document to read in what order.

## Related libraries

- [ESP32KeyBridge](https://github.com/tanakamasayuki/ESP32KeyBridge) — the same integration approach for keyboard-style input
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) / [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) / [EspBle](https://github.com/tanakamasayuki/EspBle) — the transport libraries

## License

MIT License ([LICENSE](LICENSE))
