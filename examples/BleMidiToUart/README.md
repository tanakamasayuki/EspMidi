# BleMidiToUart

[日本語](README.ja.md)

**A wireless BLE MIDI keyboard plays a MIDI DIN sound module.** No PC involved.

This is UC5 in [../../docs/USE_CASES.ja.md](../../docs/USE_CASES.ja.md). It also works the other way: anything arriving on the MIDI DIN input is sent to the keyboard, so a controller with lights or a display can be driven back.

## Setup

```text
BLE MIDI keyboard ──→ MIDI DIN OUT (sound module)
MIDI DIN IN ────────→ BLE MIDI keyboard
```

| Constant | Default | Meaning |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT, to the sound module |

## What to look at

**Scanning and connecting is the sketch's job; what is on the connection is the port's.** That is why `discover()` appears nowhere here — BLE itself is outside EspMidi (see [../../docs/CORE_DESIGN.ja.md](../../docs/CORE_DESIGN.ja.md)).

**The keyboard is not named anywhere.** Both routes are written against groups, so neither is rebuilt when it comes and goes.

```cpp
router.addRoute(espmidi::InGroup::all(), din.out());   // whatever plays -> the module
router.addRoute(din.in(), espmidi::OutGroup::all());   // the DIN input -> whatever is connected
```

**A route never sends a message back to the endpoint it came from**, so the second one reaches the keyboard and not the module's own output.

**It gets its seat back on reconnect.** A BLE address is the identity, so the routes survive the keyboard going out of range.

**These are the only ports with a timestamp.** 13-bit milliseconds (`TimestampUnit::Milliseconds13`), passed through without being interpreted.

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [BleMidiToUart.ino](BleMidiToUart.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiEspBle.h>`, `<EspMidiUart.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

A wireless BLE MIDI keyboard playing a MIDI DIN sound module.

This is UC5 in docs/USE_CASES.ja.md: the board makes an old sound module
playable from a modern wireless keyboard, with no PC involved. It also works the
other way — anything arriving on the MIDI DIN input is sent to the keyboard, so a
controller with lights or a display can be driven back.

  BLE MIDI keyboard ──→ MIDI DIN OUT (sound module)
  MIDI DIN IN ────────→ BLE MIDI keyboard

The sketch scans and connects, because that is BLE and EspMidi does not own it.
Finding the MIDI service on the connection is the port's job, so discover() is
never called here.

**Nothing names the keyboard.** Its seat appears when the connection becomes
usable, so the routes are written from InGroup::all() and whatever arrives takes
part. The keyboard also gets its seat back when it reconnects: a BLE address is
the identity, so the routes survive it going out of range.

Wiring: RX_PIN to the MIDI IN, TX_PIN to the MIDI OUT. A real MIDI DIN socket
needs an optocoupler on the input and a 220Ω pair on the output.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `ble.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `ble.scanner().start(...)` — Start work using the configured entry point; the context and objects must remain valid while it runs.
3. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.
4. `ble.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.

Review these application constants before adapting the sketch: `RX_PIN`, `TX_PIN`. Match pins, channels and capacities to your application.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Inputs and observable output

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
EspMidi BLE bridge ready — turn on a BLE MIDI keyboard
```

## Verify and troubleshoot

Route BLE MIDI and UART MIDI bidirectionally and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
