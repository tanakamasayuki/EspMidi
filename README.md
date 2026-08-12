# EspMidi

[日本語](README.ja.md)

An Arduino library that sits between the MIDI interfaces an ESP32 can reach and merges, duplicates and splits between them. MIDI inputs and outputs arriving over USB Host, USB Device, BLE and UART all become the same kind of logical port, and messages are routed between them.

> **Not released yet.** The implementation plan is complete and **every bundled port has been verified on real ESP32-S3 hardware**. The API can still change. See [docs/DEVELOPMENT_PLAN.ja.md](docs/DEVELOPMENT_PLAN.ja.md) for the current position and [docs/PORTS.ja.md](docs/PORTS.ja.md) for per-port status. The design documents are Japanese only.

## A USB MIDI keyboard into a MIDI DIN sound module, in fifteen lines

```cpp
#include <EspMidiEspUsbHost.h>
#include <EspMidiUart.h>

EspUsbHost usb;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UsbHostPort keyboards(router, usb);
espmidi::UartPort din(router, Serial1, 1);

void setup() {
  usb.begin();                    // 1) the stacks belong to the sketch
  keyboards.begin();              // 2) the ports
  din.begin("MIDI DIN", 20, 19);
  router.addRoute(espmidi::InGroup::all(), din.out());  // 3) the routes
}

void loop() {
  keyboards.update();
  din.update();
  router.update();
}
```

**No keyboard is named anywhere.** A USB Host seat appears when something is plugged in, so the route is written against *every* input. Unplug it and the route stays; plug it back in and it carries on. Sending the same playing somewhere else is one more route.

Every example follows the same three steps: 1) start the stacks, 2) create the ports, 3) build the routes.

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

## It is worth using with a single port too

The main target is where **several interfaces meet**, but a single port benefits as well: **the API is the same whichever interface it is.**

```cpp
// only this line changes
espmidi::UartPort      port(router, Serial1, 1);
espmidi::UsbDevicePort port(router, usbMidi, usb);
espmidi::BleDevicePort port(router, bleMidi);
```

**Everything below it is byte-for-byte identical** — routes, filters, transforms, how SysEx arrives, how velocity 0 is treated, how messages are received. See [`examples/SameCodeAnyPort`](examples/SameCodeAnyPort/).

- Built on UART, later moved to USB MIDI? The port declaration and `begin()`.
- Built on USB, now BLE as well? One more port and one more route.
- Using each transport's own API instead spreads those differences through **your** code.

**If it really is one port, one direction, and never going to grow**, using `EspUsbHost`, `EspUsbDevice` or `EspBle` directly is lighter; each keeps its own MIDI convenience API and examples for exactly that.

## Guides

- **[docs/GUIDE.ja.md](docs/GUIDE.ja.md)** — the usage guide, starting from sending on a single port. Includes **troubleshooting** and how to read the diagnostic counters.
- **[docs/MIDI_BASICS.ja.md](docs/MIDI_BASICS.ja.md)** — **the caveats of MIDI itself** (a note on with velocity 0, running status, 0-based channels, bandwidth) and **per-interface notes** (MIDI DIN isolation, cables, enumeration, BLE latency and limits).

Both are Japanese only. If you are new to MIDI, the second one saves the most time.

## Ports

Ports are header-only, so a sketch pulls in a dependency only for the ports it includes. A sketch that includes just `EspMidi.h` requires neither `EspUsbHost` nor `EspBle`.

| Port | Header | Depends on | Status |
| --- | --- | --- | --- |
| UART MIDI | `EspMidiUart.h` | — (`HardwareSerial`) | **verified on hardware** |
| USB Device MIDI | `EspMidiEspUsbDevice.h` | [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) 2.0.2+ | **verified on hardware** |
| USB Host MIDI | `EspMidiEspUsbHost.h` | [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) 2.7.5+ | **verified on hardware** |
| BLE MIDI Device / Host | `EspMidiEspBle.h` | [EspBle](https://github.com/tanakamasayuki/EspBle) 1.2.0+ | **verified on hardware** |

**A port can live outside this repository.** Ports are header-only and nothing about them is privileged; UART is the smallest bundled one and the quickest to read as an example.

## It can be a MIDI controller too

Helpers for knobs, buttons, encoders and clock come with `EspMidi.h`: `espmidi::Button`, `Analog`, `Encoder`, `ControlOutput`, `ClockGenerator`, `ClockCounter`.

**None of them touches a pin, and none of them reads the time.** The sketch hands over what it read and what time it is, which is why the same helper works for an ADC, a port expander, a touch sensor or a value off a network — and why a bouncing switch or a tempo change can be reproduced in a test on the host.

```cpp
knob.update(analogRead(KNOB_PIN));                     // just the reading
button.update(digitalRead(BUTTON_PIN) == LOW, millis());
```

What they produce goes onto an application port, so **a control change from a knob can reach USB, MIDI DIN and BLE at once**.

## Examples

All of them are **practical**: sketches you can flash as they are.

**New to it? Start with the first three.**

| Example | What it is |
| --- | --- |
| [`SimpleMidiOut`](examples/SimpleMidiOut/) | **the smallest**: one port, sending only, plays a scale |
| [`SimpleMidiIn`](examples/SimpleMidiIn/) | one port, receiving only; prints what arrives |
| [`SameCodeAnyPort`](examples/SameCodeAnyPort/) | **the same code over UART, USB and BLE — one line changes** |
| [`UartMidiMonitor`](examples/UartMidiMonitor/) | prints UART MIDI while passing it on to a second UART unchanged |
| [`UsbMidiDevice`](examples/UsbMidiDevice/) | appears to a PC as a two-port USB MIDI interface |
| [`UsbHostToUart`](examples/UsbHostToUart/) | a USB keyboard plays a DIN module while a PC also hears it |
| [`BleMidiToUart`](examples/BleMidiToUart/) | a wireless BLE MIDI keyboard plays a DIN module |
| [`GpioControls`](examples/GpioControls/) | a MIDI controller of knobs, buttons and an encoder |

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
- **A seat outlives the device in it.** A port handle is never invalidated by a disconnect; only its state changes, so **routes are not rebuilt when something is unplugged**.
- **The sketch's `loop()` drives everything.** The core starts no task of its own. What arrives on a transport's task is copied into a queue, and the pipeline runs only inside `update()` — which is why `Router::receive()` is thread-safe and nothing else needs to be.

## Documentation

[docs/README.md](docs/README.md) is the guide to which document to read in what order.

## Tests

```sh
cd tests
uv run pytest unit/          # no hardware, a few seconds
```

`unit/` runs on `g++` alone — no arduino-cli, no board package. **It fails the moment the core starts depending on Arduino.**

The ports are in `unit/` as well: each is a template over the object it borrows, so a stand-in replaces `HardwareSerial` or `EspUsbHost` and the port's behaviour is fixed on the host. What is left for real hardware is **only what real hardware can show**.

```sh
uv run --env-file .env pytest loopback/   # one board; UART needs no wiring at all
uv run --env-file .env pytest peer/       # two boards
```

## Related libraries

- [ESP32KeyBridge](https://github.com/tanakamasayuki/ESP32KeyBridge) — the same integration approach for keyboard-style input
- [EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) / [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) / [EspBle](https://github.com/tanakamasayuki/EspBle) — the transport libraries

## License

MIT License ([LICENSE](LICENSE))
