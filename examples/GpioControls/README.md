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

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [GpioControls.ino](GpioControls.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<EspMidiEspUsbDevice.h>`, `<EspMidiUart.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

A MIDI controller: two knobs, two buttons and an encoder, over USB and MIDI DIN.

This is UC10 in docs/USE_CASES.ja.md. The board is a MIDI device rather than a
MIDI router here, and everything it produces goes out of both a USB MIDI port
and a MIDI DIN socket at once.

**The helpers never touch a pin.** The sketch reads whatever it has and hands
the number over, which is why the same helper works for an ADC, a port
expander, a touch sensor or a value off a network. It is also why the reading
and the moment are always parameters — see src/EspMidiControl.h.

  knobs, buttons, encoder ──→ [application port] ──→ USB MIDI
                                                 └─→ MIDI DIN OUT
  USB MIDI ──→ an LED (note 60) and a dimmer (CC 20)

The LED half is the mirror image: a DAW lights the board's LED by sending a
note, using the same Filter the routing uses everywhere else.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `usb.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `router.addRoute(...)` — Register source and destination endpoints/groups, considering disconnects and unintended loops.
3. `led.handle(...)` — Obtain a live task handle only for observation; do not retain it after the worker exits.
4. `encoder.update(...)` — Advance the relevant component regularly from the execution context shown in this sketch.

Review these application constants before adapting the sketch: `DIN_RX_PIN`, `DIN_TX_PIN`, `ENCODER_A_PIN`, `ENCODER_B_PIN`, `LED_PIN`. Match pins, channels and capacities to your application.

## API usage and precautions

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## Inputs and observable output

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
EspMidi controls ready
```

## Verify and troubleshoot

Send MIDI control events from GPIO input and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
