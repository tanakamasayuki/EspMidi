# UsbHostToUart

[日本語](README.ja.md)

A USB MIDI keyboard plays a MIDI DIN sound module, and **the same performance also reaches a PC**. Record it in a DAW while playing the module.

This is UC1 in [../../docs/USE_CASES.ja.md](../../docs/USE_CASES.ja.md), and the point of the library in one sketch: three transports that know nothing about each other, joined by routes.

## Setup

```text
USB MIDI keyboard ──→ ┐
                      ├──→ MIDI DIN OUT (sound module)
                      └──→ PC (USB Device port 1)
PC (port 1) ──────────────→ MIDI DIN OUT as well
```

A USB host port and a USB device port at once needs **two USB peripherals**, so the target is the ESP32-P4.

| Constant | Default | Meaning |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT, to the sound module |

## What to look at

**The keyboard is not named anywhere.** Its seats appear when it is plugged in, so the routes are written against `espmidi::InGroup::all()`. Unplug it and the routes stay; plug it back in and it carries on.

**A route never sends a message back to the endpoint it came from**, so "everything to the module and to the PC" cannot make the PC echo to itself or the DIN input loop back to its own output.

**Seats coming and going arrive as events.** `registry.addListener()` reports only two things — a seat appeared, and a seat changed state — because a seat is never removed.

**The cable counts invert for a device and not for a host.** EspUsbHost's counts are already host-view; see [../../docs/PORTS.ja.md](../../docs/PORTS.ja.md).

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [UsbHostToUart.ino](UsbHostToUart.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiEspUsbDevice.h>`, `<EspMidiEspUsbHost.h>`, `<EspMidiUart.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

A USB MIDI keyboard playing a MIDI DIN sound module, and a PC listening in.

Plug a USB MIDI keyboard into the board's USB host port and it plays the sound
module on the MIDI DIN out — no PC involved. Plug the board into a PC as well
and the same performance also arrives there, so a DAW can record it while the
module is being played.

This is UC1 in docs/USE_CASES.ja.md, and the point of the library in one sketch:
three transports that know nothing about each other, joined by routes.

  USB MIDI keyboard ──→ ┐
                        ├──→ MIDI DIN OUT (sound module)
                        └──→ PC (USB Device port 1)
  PC (port 1) ──────────────→ MIDI DIN OUT as well

**Nothing names the keyboard.** Its seats appear when it is plugged in, so the
routes are written from InGroup::all() and whatever arrives takes part. Unplug
it mid-phrase and the routes stay; plug it back in and it carries on.

Hardware: an ESP32-P4, or an S3 with a USB host port wired up. The console is
the board's external USB-serial chip.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `usbHost.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.
3. `keyboards.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.

Review these application constants before adapting the sketch: `RX_PIN`, `TX_PIN`. Match pins, channels and capacities to your application.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Inputs and observable output

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
EspMidi bridge ready — plug in a USB MIDI keyboard
```

## Verify and troubleshoot

Route USB Host MIDI to a UART port and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
