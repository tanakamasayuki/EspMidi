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
