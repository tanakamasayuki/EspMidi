# Recipes

[日本語](RECIPES.ja.md)

Short fragments, indexed by what you want to do. **For a guide to read in order see [GUIDE.md](GUIDE.md)**; to look up a name, [API.md](API.md).

Every fragment assumes this much:

```cpp
espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::AppPort sketch(router, "sketch");
```

**The code on this page is compiled by `tests/unit/docs_snippets`.** A rename breaks that test, so nothing here can go stale.

## Building paths

### A MIDI Thru

Send what came in back to the same device. **The default is not to**, so it is lifted explicitly.

```cpp
const espmidi::Route thru = router.addRoute(din.in(), din.out());
router.setRouteAllowSameEndpoint(thru, true);
```

**If the other end also has a Thru, this loops forever.** Only one side should.

### Merging two inputs into one module

```cpp
router.addRoute(keysA.in(), synth.out());
router.addRoute(keysB.in(), synth.out());
```

Or merge "everything", which includes whatever is plugged in later:

```cpp
router.addRoute(espmidi::InGroup::all(), synth.out());
```

### Sending one performance to several places

```cpp
router.addRoute(keys.in(), synth.out());
router.addRoute(keys.in(), pc.out(0));
router.addRoute(keys.in(), recorder.out());
```

### A group for a purpose

```cpp
const espmidi::OutGroup synths = registry.addOutGroup("synths");
registry.addToGroup(synths, moduleA.out());
registry.addToGroup(synths, moduleB.out());
router.addRoute(keys.in(), synths);
```

### Cutting a path temporarily

Disabling rather than removing keeps the handle and the filter configuration.

```cpp
router.setRouteEnabled(route, false);
// ... later
router.setRouteEnabled(route, true);
```

## Narrowing and rewriting

### Splitting a keyboard

```cpp
const espmidi::Route bass = router.addRoute(keys.in(), bassModule.out());
espmidi::Filter lower;
lower.kinds = espmidi::KindNotes;
lower.noteMin = 36;
lower.noteMax = 59;
router.setRouteFilter(bass, lower);

const espmidi::Route lead = router.addRoute(keys.in(), leadModule.out());
espmidi::Filter upper;
upper.kinds = espmidi::KindNotes;
upper.noteMin = 60;
upper.noteMax = 96;
router.setRouteFilter(lead, upper);
```

**Note offs pass through the same range**, or notes hang.

### Down an octave

```cpp
espmidi::Transform down;
down.transpose = -12;
router.setRouteTransform(route, down);
```

A note pushed out of range is **dropped, not wrapped** (wrapping would sound at the other end of the keyboard).

### Only one channel

```cpp
espmidi::Filter drumsOnly;
drumsOnly.allowOnlyChannel(9);   // 0-based, so the device's ch10
router.setRouteFilter(route, drumsOnly);
```

### Changing the channel

```cpp
espmidi::Transform toChannel3;
toChannel3.channel = 2;          // 0-based, so the device's ch3
router.setRouteTransform(route, toChannel3);
```

### Remapping a controller number

**Narrow with a filter, then set the number with a transform.** There is no mapping table.

```cpp
espmidi::Filter onlyVolume;
onlyVolume.kinds = espmidi::KindControlChange;
onlyVolume.ccMin = onlyVolume.ccMax = 7;
router.setRouteFilter(route, onlyVolume);

espmidi::Transform toExpression;
toExpression.controller = 11;
router.setRouteTransform(route, toExpression);
```

Several remappings are **several routes**.

### Limiting the volume

```cpp
espmidi::Transform quieter;
quieter.velocity = espmidi::ValueMap::scale7(0, 100);   // never above 100
router.setRouteTransform(route, quieter);
```

### A fixed velocity for a module with no touch sensitivity

```cpp
espmidi::Transform flat;
flat.velocity = espmidi::ValueMap::fixed7(100);
router.setRouteTransform(route, flat);
```

### Reversing a pedal

```cpp
espmidi::Transform reversed;
reversed.controllerValue = espmidi::ValueMap::range7(0, 127, 127, 0);
router.setRouteTransform(route, reversed);
```

### Dropping the clock

At 24 per quarter note it gets in the way of a monitor or a log.

```cpp
espmidi::Filter quiet;
quiet.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindSystemRealTime);
router.setOutPortFilter(monitor.out(), quiet);
```

### Keeping patch dumps out

```cpp
espmidi::Filter noData;
noData.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindData);
router.setRouteFilter(route, noData);
```

### "This device is always an octave down"

Put it on the **input port**, not a route, and it applies to everything from that device.

```cpp
espmidi::Transform deviceIsLow;
deviceIsLow.noteOffset = 12;
router.setInPortTransform(oldKeyboard.in(), deviceIsLow);
```

`noteOffset` is separate from `transpose` so that **a device's quirk and a route's intent can be written independently**.

### Deciding in code

What cannot be declared becomes a callback.

```cpp
espmidi::Verdict onlyLoudNotes(void *, espmidi::Message &message) {
  if ((espmidi::messageKind(message) & espmidi::KindNoteOn) != 0 && message.data2 < 40) {
    return espmidi::Verdict::Drop;
  }
  return espmidi::Verdict::Pass;
}
router.setRouteCallback(route, onlyLoudNotes);
```

**One-to-many and different kinds of message cannot be made here.** `send()` to an application port instead.

## Watching and creating

### Monitoring without disturbing the path

Use **a separate route** from the one that carries the sound. Slow printing cannot delay it.

```cpp
router.addRoute(keys.in(), synth.out());     // the sound path
router.addRoute(keys.in(), monitor.out());   // the monitoring path
monitor.onMessage(printMessage);
```

### A knob as a control change

```cpp
espmidi::Analog knob(sketch);
knob.config().controller = 7;

void loop() {
  knob.update(analogRead(KNOB_PIN));
  router.update();
}
```

### A button as a sustain pedal

```cpp
espmidi::Button pedal(sketch);
pedal.config().note = false;
pedal.config().number = 64;   // sustain

void loop() {
  pedal.update(digitalRead(PEDAL_PIN) == LOW, millis());
  router.update();
}
```

`config().latch = true` turns a momentary switch into a toggle.

### An LED following a note

```cpp
espmidi::Filter note60;
note60.kinds = espmidi::KindNotes;
note60.noteMin = note60.noteMax = 60;
espmidi::ControlOutput lamp(note60, setLed);

// attaches straight to an application port
watcher.onMessage(&espmidi::ControlOutput::receive, &lamp);
```

**A note on with velocity 0 comes through as 0**, so the LED does not latch on.

### Sending MIDI Clock

```cpp
espmidi::ClockGenerator clock(sketch);
clock.setTempo(12000);          // 120.00 BPM
clock.start(micros());

void loop() {
  clock.update(micros());
  router.update();
}
```

### Measuring an external clock and regenerating it

```cpp
espmidi::ClockCounter counter;
espmidi::ClockGenerator clock(sketch);

void onMidi(void *, const espmidi::Message &message) {
  counter.handle(message, micros());
}

void loop() {
  if (counter.microsPerTick() != 0) {
    clock.setMicrosPerTick(counter.microsPerTick());
  }
  clock.update(micros());
  router.update();
}
```

`counter.onQuarter()` is true on the beat, which is enough to blink an LED.

### Sending a patch dump

```cpp
const uint8_t payload[] = {0x7d, 0x01, 0x02};   // no 0xF0 or 0xF7
espmidi::Message dump;
dump.type = espmidi::MessageType::Data7;
dump.status = 0xf0;
dump.chunk = true;
dump.chunkStart = true;
dump.chunkEnd = true;
dump.chunkData = payload;
dump.chunkLength = sizeof(payload);
sketch.send(dump);
router.update();
```

**The framing (`0xF0` / `0xF7`) is added by the port.** A long dump can be sent as a fragment with only `chunkStart`, fragments with neither, and a fragment with only `chunkEnd`.

## Devices coming and going

### Letting whatever is plugged in join

**The trick is not naming the device.**

```cpp
router.addRoute(espmidi::InGroup::all(), synth.out());
```

Unplugging leaves the route; plugging back in carries on. **Rebuilding it duplicates it.**

### Hearing about it

```cpp
void onPortEvent(void *, const espmidi::PortEvent &event) {
  espmidi::PortInfo info;
  if (!registry.portInfo(event.port, info)) return;
  Serial.print(info.name);
  Serial.println(info.state == espmidi::PortState::Available ? " available" : " disconnected");
}
registry.addListener(onPortEvent);
```

There are only two events: it appeared, and its state changed.

### Treating one specific device differently

```cpp
espmidi::PortInfo info;
if (registry.portInfo(port, info) && strcmp(info.name, "A-88") == 0) {
  router.addRoute(espmidi::InPort{port}, synth.out());
}
```

### Checking before sending

```cpp
if (registry.portAvailable(pc.out(0).port)) {
  sketch.sendShort(0x90, 60, 100);
}
```

You do not have to: a failure shows up in `sendFailed`, so **neither way breaks anything**.

## When something is wrong

### Printing the diagnostics periodically

```cpp
void loop() {
  router.update();

  static uint32_t next = 0;
  if (millis() >= next) {
    next = millis() + 5000;
    const espmidi::RouterCounters c = router.counters();
    Serial.printf("recv=%u deliv=%u noRoute=%u failed=%u full=%u\n",
                  c.received, c.delivered, c.noRoute, c.sendFailed, c.queueFull);
  }
}
```

How to read them is in [GUIDE.md](GUIDE.md).

### Cutting RAM

```cpp
#define ESPMIDI_MAX_PORTS 8
#define ESPMIDI_QUEUE_ENTRIES 8
#define ESPMIDI_MAX_ROUTES 4
#include <EspMidiUart.h>
```

The measured savings are in [FOOTPRINT.md](FOOTPRINT.md). **Two UARTs put the core at about 1.8 KB.**

### Stopping every note

This library does not interpret MIDI, so it never sends this itself. Send it from the sketch when you need it.

```cpp
for (uint8_t channel = 0; channel < 16; channel++) {
  sketch.sendShort(static_cast<uint8_t>(0xb0 | channel), 123, 0);   // All Notes Off
}
router.update();
```
