# Usage guide

[日本語](GUIDE.ja.md)

Read in order and it takes you from sending on a single port to routing between several. **If MIDI itself is new to you, read [MIDI_BASICS.md](MIDI_BASICS.md) first** — most of what trips people up is MIDI's own specification, not this library.

Each section matches something real in `examples/`, so you can flash it rather than only read it.

## 0. Every sketch has the same three steps

```cpp
void setup() {
  // 1) Start the stacks — the sketch's job. EspMidi owns neither USB nor BLE.
  usb.begin(config);

  // 2) Create the ports — EspMidi starts here.
  port.begin("USB MIDI");

  // 3) Build the routes.
  router.addRoute(port.in(), somewhere.out());
}

void loop() {
  usb.task();      // the stack
  port.update();   // the port (takes in what arrived)
  router.update(); // run the routing
}
```

**Nothing happens until `router.update()` runs.** Both sending and receiving happen there. This is the first thing people get caught by.

The reason for the shape: what arrives comes in on **someone else's task** — a USB Host task, a BLE host task. Sending straight back from there would leak the transport's threading into the application. So a received message is copied into a queue, and the pipeline runs only inside `loop()` (see [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md), Japanese).

## 1. Sending

→ [`examples/SimpleMidiOut`](../examples/SimpleMidiOut/)

```cpp
espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::UartPort din(router, Serial1, 1);
espmidi::AppPort sketch(router, "sketch");

void setup() {
  din.begin("MIDI OUT", -1, TX_PIN);   // rxPin = -1: sending only
  router.addRoute(sketch.in(), din.out());
}

void loop() {
  sketch.sendShort(0x90, 60, 100);     // note on (not sent yet)
  router.update();                     // now it reaches the wire
  delay(250);
  sketch.sendShort(0x80, 60, 0);       // note off
  router.update();
}
```

**`espmidi::AppPort` is "the sketch, as a port".** It has no transport behind it and is routed exactly like every other port — which is why "also send it to the PC" later is **one more route**, not a rewrite.

**Do not forget the note off.** The note sustains forever.

## 2. Receiving

→ [`examples/SimpleMidiIn`](../examples/SimpleMidiIn/)

```cpp
void onMidi(void *, const espmidi::Message &message) {
  if (message.chunk) return;            // SysEx can wait
  Serial.println(message.status, HEX);
}

void setup() {
  din.begin("MIDI IN", RX_PIN, -1);
  router.addRoute(din.in(), sketch.out());
  sketch.onMessage(onMidi);
}
```

The callback runs inside `router.update()`, **on the sketch's task**. So a `Serial.print()` in there cannot stall another transport — though staying too long still delays everything.

Pointers inside `message` (`raw`, `chunkData`) are **valid for the duration of that callback only**. Copy what you need to keep.

## 3. The same code on a different interface

→ [`examples/SameCodeAnyPort`](../examples/SameCodeAnyPort/)

**This is the reason to use the library even with a single port.**

```cpp
#if MIDI_PORT == MIDI_PORT_UART
  espmidi::UartPort port(router, Serial1, 1);
#elif MIDI_PORT == MIDI_PORT_USB
  espmidi::UsbDevicePort port(router, usbMidi, usb);
#else
  espmidi::BleDevicePort port(router, bleMidi);
#endif
```

**Everything below that `#if` is byte-for-byte identical.** Routes, filters, transforms and how messages are received are all shared.

- Built on UART and later moved to USB MIDI? The port declaration and `begin()`.
- Built on USB and now BLE as well? One more port, one more route.
- "How does SysEx arrive?" "What about velocity 0?" → **the same answer on every transport.**

Using each transport's own API directly spreads those differences **through your own code**. Settling on this library's common representation once keeps them in one place.

## 4. Filters and transforms are declared, not coded

```cpp
const espmidi::Route route = router.addRoute(keys.in(), bass.out());

espmidi::Filter lower;                     // the lower half of the keyboard only
lower.kinds = espmidi::KindNotes;
lower.noteMin = 36;
lower.noteMax = 59;
router.setRouteFilter(route, lower);

espmidi::Transform toBass;                 // an octave down, on channel 2
toBass.transpose = -12;
toBass.channel = 1;                        // 0-based, so the device shows ch2
router.setRouteTransform(route, toBass);
```

A stage applies **filter → transform → callback**, and the same three can be placed **on a route, on an input port, or on an output port**.

- "This device is an octave down" → on the **input port**.
- "This route transposes up a semitone" → on the **route**.
- "This module listens on ch6" → on the **output port**.

The three are written independently and compose in that order. See [ROUTING.md](ROUTING.md).

**A note on with velocity 0 is left alone.** It is a note off on the wire, so scaling it would produce a quiet note that never stops.

## 5. Several ports

```cpp
// one to many: one performance to a module and to a PC
router.addRoute(keys.in(), din.out());
router.addRoute(keys.in(), pc.out(0));

// many to one: every input into one module
router.addRoute(espmidi::InGroup::all(), din.out());
```

**A route never sends a message back to the endpoint it came from.** So "every input to every output" cannot make an input loop into its own output. Use `setRouteAllowSameEndpoint(route, true)` only when you mean it — building a MIDI Thru, for example.

**There is no implicit all-to-all.** Build no routes and nothing is forwarded.

## 6. Ports that come and go

USB Host and BLE Host ports **appear when something is plugged in**. You cannot name the device in the sketch.

```cpp
// name no device
router.addRoute(espmidi::InGroup::all(), din.out());
```

Anything plugged in later joins on its own. **Unplugging does not remove the route** — a port handle is never invalidated by a disconnect; only its state becomes `Disconnected` (the "seat" model, [DATA_MODEL.md](DATA_MODEL.md)). Plug it back in and it carries on.

To hear about seats coming and going:

```cpp
registry.addListener([](void *, const espmidi::PortEvent &event) {
  espmidi::PortInfo info;
  if (registry.portInfo(event.port, info)) Serial.println(info.name);
});
```

There are only two events: a seat appeared, and a seat changed state. **Seats are never removed.**

## 7. Knobs, buttons and clock

```cpp
espmidi::Analog knob(sketch);
espmidi::Button button(sketch);

void loop() {
  knob.update(analogRead(KNOB_PIN));
  button.update(digitalRead(BUTTON_PIN) == LOW, millis());
  router.update();
}
```

**The helpers touch no pin and read no clock.** You hand over the reading and the current time, which is why the same helper works for an ADC, a port expander or a touch sensor. → [`examples/GpioControls`](../examples/GpioControls/)

## Troubleshooting

### No sound, or nothing arrives

In order:

1. **Are you calling `router.update()`?** The port's own `update()` is needed too — taking in what arrived is the port's job.
2. **Did you build a route?** There is no implicit all-to-all.
3. **Is the port usable yet?** A USB Device port cannot send until the PC has configured it; a BLE Device port cannot until the other side subscribes.

```cpp
Serial.println(registry.portAvailable(port.out(0).port) ? "ready" : "not ready");
```

4. **Read the counters.**

```cpp
const espmidi::RouterCounters c = router.counters();
Serial.printf("recv=%u deliv=%u noRoute=%u sendFailed=%u full=%u filtered=%u\n",
              c.received, c.delivered, c.noRoute, c.sendFailed, c.queueFull, c.droppedByFilter);
```

| Rising | What it means |
| --- | --- |
| `received` still 0 | nothing is arriving at all: wiring, isolation, baud rate, or the port's `update()` |
| `noRoute` | no route, or the route is disabled |
| `droppedByFilter` | a filter is rejecting it |
| `sendFailed` | the transport refused it (not connected, FIFO full) |
| `queueFull` | `update()` is called too rarely, or the queue is too shallow |
| `blockedBySysEx` | an ordinary message arrived while a SysEx was being sent (rule 3) |

### A note never stops

**A note on with velocity 0 is a note off.** Check for both when you classify messages yourself; `espmidi::messageKind()` already does. Forgetting to send the note off at all is just as common.

### It sounds twice, or never stops

- Did you add the same route twice?
- Does a route with `setRouteAllowSameEndpoint(true)` form a loop?
- **If two ports are the two ends of one physical link**, the loop rule cannot see it — they genuinely are different endpoints. Do not route between them.

### A knob floods the link with control changes

An ADC wanders on its own. Raise `hysteresis` (8 counts by default).

### A SysEx dump is truncated or refused

- BLE holds 320 bytes by default (`ESPMIDI_BLE_SYSEX_BYTES`). A longer dump is refused and counted (`oversizedStreams()`) rather than truncated.
- Ordinary messages during a dump are **dropped** (rule 3). That is deliberate.
- A stream's path is **fixed when it starts** (rule 1). Changing a route halfway does not redirect it.

### Wanting to rebuild routes after a reconnect

You do not need to. The seat stays. Rebuilding **duplicates the route**.

## Where to go next

| What you want | Document |
| --- | --- |
| MIDI's own caveats, and per-interface notes | [MIDI_BASICS.md](MIDI_BASICS.md) |
| The ports in detail, and their limits | [PORTS.md](PORTS.md) |
| The routing rules (SysEx, loops) | [ROUTING.md](ROUTING.md) |
| The shape of a message and a port | [DATA_MODEL.md](DATA_MODEL.md) |
| Tuning the fixed-size storage | [CONFIGURATION.ja.md](CONFIGURATION.ja.md) (Japanese) and the `ESPMIDI_*` macros in each port |
| Writing your own port | the comments in `src/EspMidi.h`, then `src/EspMidiUart.h` |
