# SameCodeAnyPort

[日本語](README.ja.md)

**The same MIDI code over UART, USB and BLE.** One line changes.

```cpp
#define MIDI_PORT MIDI_PORT_UART   // MIDI_PORT_USB / MIDI_PORT_BLE
```

**This is the reason to use the library even with a single interface.**

## What it does

It echoes every note it receives back **an octave higher**, and prints what it saw. The echo itself is not the point.

## What to look at

Everything below the `#if` is **byte-for-byte identical** whichever interface is selected.

```cpp
espmidi::Filter notesOnly;
notesOnly.kinds = espmidi::KindNotes;
router.setRouteFilter(echo, notesOnly);

espmidi::Transform octaveUp;
octaveUp.transpose = 12;
router.setRouteTransform(echo, octaveUp);
```

**Only starting the stack differs.**

| | UART | USB Device | BLE Device |
| --- | --- | --- | --- |
| starting the stack | nothing (the port opens its serial port) | `usb.begin(config)` | `bleMidi.begin()` → `ble.begin()` → advertise |
| pumped in `loop()` | — | `usb.task()` | `ble.update()` |
| **routes, filters, transforms** | **same** | **same** | **same** |

So:

- built on UART, later moved to USB MIDI → the port declaration and `begin()`
- built on USB, BLE added → one more port and one more route
- "how does SysEx arrive?", "what about velocity 0?" → **the same answer on all of them**

Using each transport's own API directly spreads those differences through the application instead.

**This echo deliberately goes back where it came from.** A route never returns a message to its own endpoint by default, so `setRouteAllowSameEndpoint(echo, true)` says so explicitly — the same shape as a MIDI Thru. Beware that **if the other end does the same thing, it loops forever**.

## Profiles

All three transports are listed in `sketch.yaml` so any setting of `MIDI_PORT` builds. **A real sketch only needs the one it uses.**

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [SameCodeAnyPort.ino](SameCodeAnyPort.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiUart.h>`, `<EspMidiEspUsbDevice.h>`, `<EspMidiEspBle.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

The same MIDI code over UART, USB or BLE — change one line.

This sketch does one thing: it echoes every note it receives back an octave
higher, and prints what it saw. What is worth looking at is not the echo but
**how little changes when the interface changes**.

Change MIDI_PORT below and rebuild. Everything from "the MIDI part" down stays
exactly as it is:

  MIDI_PORT_UART  a MIDI DIN socket on a UART
  MIDI_PORT_USB   a USB MIDI port a PC sees
  MIDI_PORT_BLE   a BLE MIDI service a phone connects to

**That is the reason to use this library even with a single interface.** The
transport-specific part is starting the stack; a route, a filter and a transform
are written once and mean the same thing on all three.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `port.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `ble.advertising().start(...)` — Start work using the configured entry point; the context and objects must remain valid while it runs.
3. `ble.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.
4. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Inputs and observable output

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
in:
ready
```

## Verify and troubleshoot

Use common routing logic with different transports and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
