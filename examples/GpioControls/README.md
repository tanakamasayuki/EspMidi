# GpioControls

[日本語](README.ja.md)

**A MIDI controller: two knobs, two buttons and an encoder.** Everything it produces goes out of **both** a USB MIDI port and a MIDI DIN socket at once.

This is UC10 in [../../docs/USE_CASES.ja.md](../../docs/USE_CASES.ja.md). Here the board is a **MIDI device** rather than a MIDI router.

## Setup

```text
knobs, buttons, encoder ──→ [application port] ──→ USB MIDI
                                              └─→ MIDI DIN OUT
USB MIDI ──→ an LED (note 60) and a dimmer (CC 20)
```

| Control | Sends |
| --- | --- |
| knob 1 / 2 | CC 7 (volume) / CC 11 (expression) |
| button 1 | note 60 |
| button 2 | CC 64 (sustain), **latching** — a momentary switch used as a toggle |
| encoder | CC 16, relative (two's complement) |

## What to look at

**The helpers never touch a pin.** The sketch hands over what it read, which is why the same helper works for an ADC, a port expander, a touch sensor or a value off a network. **Time is a parameter too**, which is what makes debouncing and tempo testable (see [../../src/EspMidiControl.h](../../src/EspMidiControl.h)).

```cpp
knobs[i].update(analogRead(KNOB_PINS[i]));            // just the reading
buttons[i].update(digitalRead(BUTTON_PINS[i]) == LOW, millis());
```

**A knob only sends when it moves.** An ADC has 4096 steps and a control change has 128, so most movements land on the same value; on top of that `hysteresis` (8 counts by default) means **a knob nobody is touching does not fill the link with control changes**.

**The LED half is the mirror image.** Which messages it follows is written with an `espmidi::Filter` — the same one a route carries. A note on with velocity 0 turns it off, so **the LED does not latch on forever**.

**Three relative encoder conventions are available.** There is no standard, and the wrong one turns the parameter the wrong way or not at all.

**It is on an application port, so destinations are additive.** One more route sends the same controls over BLE as well.
