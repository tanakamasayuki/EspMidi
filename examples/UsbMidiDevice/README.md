# UsbMidiDevice

[日本語](README.ja.md)

The board appears to a PC as a **two-port USB MIDI interface**.

- **Port 1** is the UART MIDI DIN pair: what arrives on MIDI IN goes to the PC, and what the PC sends goes out of MIDI OUT.
- **Port 2** is the board itself: the sketch sends notes on it and receives what the PC sends back.

**Port 2 is what a USB MIDI interface for sale cannot do.** The same board is a MIDI device and a MIDI router at once, and to the routing both halves are just ports.

## Setup

```text
MIDI IN  ──→ GPIO20 ──→ [port 1] ──→ PC
MIDI OUT ←── GPIO19 ←── [port 1] ←── PC
        button ──────→ [port 2] ──→ PC
       console ←─────── [port 2] ←── PC
```

| Constant | Default | Meaning |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT |
| `BUTTON_PIN` | 0 | plays a note on port 2 |
| `IN_CABLES` / `OUT_CABLES` | 2 / 2 | cables declared (**host's point of view**) |

The console is the board's external USB-serial chip, separate from the native USB the PC sees.

## What to look at

**The cable counts are named from the host's point of view.** EspUsbDevice's IN is device to host (this library's **output** ports) and OUT is host to device (its **input** ports). Getting that backwards makes every port work the wrong way, so `espmidi::UsbDevicePort` counts in its own directions instead: `inPortCount()` / `outPortCount()`.

**`usbPort.begin()` comes after `usb.begin()`.** The cable counts are only final once the stack is up.

**A port the PC has not configured yet refuses what it is handed**, and routing counts it as `sendFailed`. It is the first thing to look at when nothing seems to arrive.

**Do not declare more cables than needed.** The descriptor grows, and a composite configuration with HID or CDC can hit the limit (see [../../docs/PORTS.ja.md](../../docs/PORTS.ja.md)).

## Profiles

EspUsbDevice's multi-cable MIDI is not released yet, so the only profile is `esp32s3_local`, which builds against the local checkout.

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [UsbMidiDevice.ino](UsbMidiDevice.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiEspUsbDevice.h>`, `<EspMidiUart.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

USB MIDI interface with two ports.

Plug the board into a PC and two MIDI ports appear. Port 1 carries whatever
arrives on the UART MIDI IN, and anything the PC sends to port 1 goes out of the
UART MIDI OUT — a USB MIDI interface for one MIDI DIN pair. Port 2 is the
board's own: the sketch plays notes on it and watches what the PC sends back.

That second port is what a USB MIDI interface for sale cannot do and this one
can: the board is a MIDI device and a MIDI router at the same time, and both
halves are just ports to the routing.

Wiring: RX_PIN to the MIDI IN, TX_PIN to the MIDI OUT. A real MIDI DIN socket
needs an optocoupler on the input and a 220Ω pair on the output; that is outside
this library, which deals in bytes.

The console is the board's external USB-serial chip, separate from the native
USB the PC sees.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `usb.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.
3. `usbPort.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.

Review these application constants before adapting the sketch: `RX_PIN`, `TX_PIN`, `BUTTON_PIN`, `IN_CABLES`, `OUT_CABLES`. Match pins, channels and capacities to your application.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Inputs and observable output

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
from the PC:
EspMidi USB MIDI interface ready
```

## Verify and troubleshoot

Expose USB Device MIDI ports for routing and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
