# UartMidiMonitor

[日本語](README.ja.md)

Prints a UART MIDI 1.0 stream to the console while passing it on to a second UART unchanged. **Put it between a keyboard and a sound module and the playing keeps working.**

It shows what a keyboard actually sends without taking the sound module out of the chain.

## Setup

```text
MIDI IN  ──→ GPIO20 (Serial1 RX) ──┬──→ GPIO18 (Serial2 TX) ──→ MIDI OUT ──→ sound module
                                   └──→ monitor (printed to the console)
```

| Constant | Default | Meaning |
| --- | --- | --- |
| `RX_PIN` | 20 | the MIDI IN being watched |
| `TX_PIN` | 19 | the same UART's transmit (set it even if unused) |
| `THRU_RX_PIN` | 17 | receive of the pass-through link |
| `THRU_TX_PIN` | 18 | the MIDI OUT it is passed on to |

Set `THRU_RX_PIN` and `THRU_TX_PIN` to `-1` to monitor without passing anything on.

The console is UART0, the board's external USB-serial chip, separate from the MIDI UARTs.

## Connecting a real MIDI DIN

**The 5V current loop is outside this library.** A MIDI IN needs an optocoupler and a MIDI OUT needs a 220Ω pair. Do not wire a MIDI DIN socket straight to an ESP32 GPIO.

## What to look at

**The monitor is an application port.** It does not sit in the pass-through path; it is at the end of a *second* route from the same input. Slow printing cannot delay the sound.

**The clock is dropped at the monitor's output port.** It is 24 messages per beat and would fill the console, but the filter applies only to the monitor, so **the stream reaching the sound module is still complete**.

**SysEx is not copied.** `chunkData` points straight into the buffer it arrived in and is valid for the duration of the callback. A patch dump passes through untouched.

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [UartMidiMonitor.ino](UartMidiMonitor.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiUart.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

UART MIDI monitor.

Watches a MIDI 1.0 stream on a UART and prints what arrives to the console,
while passing it through to a second UART unchanged. Put it between a
keyboard and a sound module and you can see what the keyboard actually sends
without taking the sound module out of the chain.

Wiring: the MIDI IN of the monitored link goes to RX_PIN, and TX_PIN drives
the MIDI OUT that carries it onward. A real MIDI DIN socket needs an
optocoupler on the input and a 220Ω pair on the output; that is outside this
library, which deals in bytes.

The console is UART0, the USB-serial chip on the board — not the MIDI UARTs.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `linkPort.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.
3. `linkPort.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.

Review these application constants before adapting the sketch: `RX_PIN`, `TX_PIN`, `THRU_RX_PIN`, `THRU_TX_PIN`. Match pins, channels and capacities to your application.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Inputs and observable output

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
SysEx
 bytes
 (end)
```

## Verify and troubleshoot

Observe MIDI arriving on UART and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
