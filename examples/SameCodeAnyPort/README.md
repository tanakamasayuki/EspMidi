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
