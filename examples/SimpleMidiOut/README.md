# SimpleMidiOut

[日本語](README.ja.md)

**The smallest sketch here.** One port, sending only: it plays a C major scale over and over.

Connect a sound module to the MIDI OUT and it makes a noise as soon as the board is powered. Start here.

## Wiring

`TX_PIN` (GPIO19 by default) → a MIDI OUT circuit (two 220Ω resistors) → the module's MIDI IN.

**The MIDI IN side is unused.** This sketch only sends, so `begin()` is given `-1` for `rxPin`.

## What to look at

**Nothing reaches the wire until `router.update()` runs.**

```cpp
sketch.sendShort(0x90, 60, 100);  // queued
router.update();                  // sent
```

**`espmidi::AppPort` is "the sketch, as a port".** It is routed like any other port, so "also send it to a PC" is one more route rather than a rewrite.

**Forget the note off and the note sustains forever.** The MIDI caveats worth knowing are collected in [../../docs/MIDI_BASICS.md](../../docs/MIDI_BASICS.md).

Next: [`../SimpleMidiIn/`](../SimpleMidiIn/), then [`../SameCodeAnyPort/`](../SameCodeAnyPort/). The guide is [../../docs/GUIDE.md](../../docs/GUIDE.md).

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [SimpleMidiOut.ino](SimpleMidiOut.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiUart.h>`

3. After upload, observe the display or other output used by this example. If using Serial, match its initialization in the source.

## Follow the sketch

The smallest useful EspMidi sketch: send MIDI out of one port.

It plays a C major scale over and over, so a sound module connected to the MIDI
OUT makes a noise as soon as the board is powered. **One port, one direction** —
which is the simplest thing this library does, and the shape everything else is
built from.

Wiring: TX_PIN through a MIDI OUT circuit (two 220Ω resistors) to the sound
module's MIDI IN. Nothing else is needed; the MIDI IN side is not used here.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `din.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.
3. `router.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.

Review these application constants before adapting the sketch: `TX_PIN`. Match pins, channels and capacities to your application.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Verify and troubleshoot

Send MIDI messages to an output port and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
