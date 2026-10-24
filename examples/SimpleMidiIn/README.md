# SimpleMidiIn

[日本語](README.ja.md)

**One port, receiving only.** It prints what arrives to the console.

Connect a keyboard's MIDI OUT, play, and you can see exactly what it sends.

## Wiring

The keyboard's MIDI OUT → **a MIDI IN circuit (an optocoupler)** → `RX_PIN` (GPIO20 by default).

**Never wire a MIDI DIN socket straight to a GPIO.** MIDI is a 5V current loop and the input has to be isolated, or the 3.3V GPIO is destroyed. See [../../docs/MIDI_BASICS.md](../../docs/MIDI_BASICS.md).

## What to look at

**Both `din.update()` and `router.update()` are needed**: the first reads the wire, the second runs the routing and calls the callback.

**A note on with velocity 0 is a note off**, and this sketch prints them differently. Not knowing that is the most common MIDI bug there is — the note that never stops.

**Channels are 0-based here.** Add 1 to match what a device displays.

**Pointers inside `message` are valid for the duration of the callback only.** Copy what you need to keep.

Next: [`../SameCodeAnyPort/`](../SameCodeAnyPort/). The guide is [../../docs/GUIDE.md](../../docs/GUIDE.md).

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [SimpleMidiIn.ino](SimpleMidiIn.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiUart.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

The smallest useful EspMidi sketch in the other direction: receive MIDI on one
port and print it.

Connect a keyboard's MIDI OUT and every note appears on the console. **One port,
one direction** — and the same three steps as every other sketch here.

Wiring: the keyboard's MIDI OUT through a MIDI IN circuit (an optocoupler) to
RX_PIN. **Do not connect a MIDI socket straight to a GPIO**: MIDI IN has to be
electrically isolated.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `din.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.
3. `din.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.

Review these application constants before adapting the sketch: `RX_PIN`. Match pins, channels and capacities to your application.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Inputs and observable output

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
note off
note on
control
```

## Verify and troubleshoot

Receive and inspect MIDI input messages and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
