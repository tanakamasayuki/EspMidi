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
