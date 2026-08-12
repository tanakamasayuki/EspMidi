# SimpleMidiIn

[日本語](README.ja.md)

**One port, receiving only.** It prints what arrives to the console.

Connect a keyboard's MIDI OUT, play, and you can see exactly what it sends.

## Wiring

The keyboard's MIDI OUT → **a MIDI IN circuit (an optocoupler)** → `RX_PIN` (GPIO20 by default).

**Never wire a MIDI DIN socket straight to a GPIO.** MIDI is a 5V current loop and the input has to be isolated, or the 3.3V GPIO is destroyed. See [../../docs/MIDI_BASICS.ja.md](../../docs/MIDI_BASICS.ja.md) (Japanese).

## What to look at

**Both `din.update()` and `router.update()` are needed**: the first reads the wire, the second runs the routing and calls the callback.

**A note on with velocity 0 is a note off**, and this sketch prints them differently. Not knowing that is the most common MIDI bug there is — the note that never stops.

**Channels are 0-based here.** Add 1 to match what a device displays.

**Pointers inside `message` are valid for the duration of the callback only.** Copy what you need to keep.

Next: [`../SameCodeAnyPort/`](../SameCodeAnyPort/). The guide is [../../docs/GUIDE.ja.md](../../docs/GUIDE.ja.md).
