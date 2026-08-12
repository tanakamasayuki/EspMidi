# Routing

[日本語](ROUTING.ja.md)

The specification for forwarding between ports: the pipeline, the driving model, the SysEx rules and loop prevention. This document states **the specification**. For implementation status see [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md), and for the reasoning [DECISIONS.ja.md](DECISIONS.ja.md) (both Japanese).

The message and port shapes it builds on are in [DATA_MODEL.md](DATA_MODEL.md).

## A route is a first-class object

The correspondence between a source and a destination is a `Route`. Handles are numbered automatically.

```cpp
espmidi::Route r = midi.addRoute(inPort, outPort);
midi.addRoute(inPort, synths);   // either end may be a port group
```

That gives all four shapes from [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md):

| Shape | How it is expressed |
| --- | --- |
| one to one | one route |
| one to many | several routes from the same input |
| many to one | several routes into the same output |
| many to many | a combination of the above |

It is not a fixed "every input to every output": each combination is managed individually. Routes can be added and removed while running.

## The pipeline has three stages

```text
input port pre-stage ──→ route (filter + transform) ──→ output port post-stage

"this device only        "this path transposes        "this output is always
 uses ch1"               up two semitones"             forced to ch1"
```

All three stages can carry filters and transforms. Restricting them to routes alone would mean writing "this device is ch1 only" on every route, and there would be no way to write a shared post-stage for a port group.

Each stage can carry **three kinds of rule**, applied in this order, always:

```text
filter (declarative) → transform (declarative) → callback (user code)
```

- **A filter** only narrows: kind, channel, note range and control change number decide what passes.
- **A transform** only rewrites: channel, transposition, velocity, control change number and value, pressure.
- **A callback** can do both: rewrite in place, or drop with `Verdict::Drop`.

An unconfigured stage passes everything through.

```cpp
espmidi::Filter onlyVolume;
onlyVolume.kinds = espmidi::KindControlChange;
onlyVolume.ccMin = onlyVolume.ccMax = 7;
router.setRouteFilter(route, onlyVolume);

espmidi::Transform toExpression;
toExpression.controller = 11;                              // CC7 → CC11
toExpression.velocity = espmidi::ValueMap::scale7(0, 63);  // half the volume
router.setRouteTransform(route, toExpression);

router.setRouteCallback(route, myCode, this);              // arbitrary code
```

**"Turn CC7 into CC11" is a filter narrowed to CC7 plus a transform that sets the number.** That is why a transform carries no mapping table: several remappings are several routes.

To emit a different kind of message, or to turn one message into many, write to an application port from a callback (below).

### Value resolution

Velocity and control change values are 7-bit in MIDI 1.0 and 16- or 32-bit in MIDI 2.0. **`ValueMap` holds its endpoints normalised**, so a rule written today keeps its meaning when the width grows (decision 1 in [DECISIONS.ja.md](DECISIONS.ja.md)).

```cpp
espmidi::ValueMap::scale7(0, 63);          // the whole input into the lower half (a volume limit)
espmidi::ValueMap::range7(32, 96, 0, 127); // 32–96 spread over the full range (outside is clamped)
espmidi::ValueMap::fixed7(100);            // always the same value (a pad with no touch sensitivity)
espmidi::ValueMap::range7(0, 127, 127, 0); // reversed (a pedal wired backwards)
```

Note numbers, channels and control change numbers keep the same width in MIDI 2.0, so they are plain integers.

### The order things apply in

- When one input has several routes, they run **in the order the routes were added**.
- Routes are independent: one route's filter or transform never affects another's.
- The three stages are always input pre-stage → route → output post-stage.

That determinism is what allows the whole pipeline's behaviour to be fixed by tests on the host.

## Driving: a queue plus an explicit `update()`

Received messages arrive through each transport library's callback. Those run in the context of a USB Host task or a NimBLE host task, so nothing is sent to another transport from there.

**`receive()` may be called from several tasks at once.** The queue is a lock-free MPSC ring, and everything past it runs single-threaded inside `update()` ([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)). **It is not fair when full** — order is preserved per sender and refusals are counted, but one transport getting nothing through is possible.

```text
a port's callback (the transport's task)
        ↓  the message is copied into the queue
      queue
        ↓  an explicit update() from loop()
routing → filter → transform → send to the output port
```

- A MIDI 1.0 message is at most three bytes, so a fixed-size queue entry suffices and the copy costs nothing worth counting.
- SysEx is queued by **copying the chunk into a staging buffer**.
- When a buffer or the queue overflows, the message is **dropped and counted**. The counters are readable as diagnostics ("was input discarded?" in [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)).
- `update()` is called by the sketch's `loop()`. The core starts no task of its own ([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)).
- **`update()` only processes what was queued when it was called.** A message injected from inside a stage waits for the next `update()`, so application feedback cannot make one `update()` run forever.

The rule from [DATA_MODEL.md](DATA_MODEL.md) that "pointers are valid only during the callback" is what makes this structure work. The core copies into the queue at the moment it receives, so a port can hand over the transport's own buffer without knowing anything about the destination.

## The SysEx stream rules

Accepting long SysEx makes three rules necessary.

### Rule 1: the path is fixed when the stream starts

The path and the filter decision are settled at `chunkStart` and held until `chunkEnd`. If the routing configuration changes midway, a stream already in progress keeps the path it was given.

Filters are evaluated **only at `chunkStart`**. A decision that changed midway would put a truncated, broken stream on the output.

### Rule 2: a disconnect closes the stream

When an input port disconnects, a SysEx in progress is discarded — but **any output that has already started receiving it is sent an `0xF7`** to close it, so the receiver is not left waiting.

### Rule 3: SysEx is exclusive per output port

In a many-to-one merge, two inputs' SysEx reaching the same output at once would interleave and corrupt both. While an output port is sending a stream, no second stream is passed to it (refused and counted). The source's other destinations are unaffected.

**System Real-Time (0xF8–0xFF) is the one exception**, as the MIDI specification requires, so MIDI Clock does not stop during a transfer.

**Everything else is dropped and counted.** The original design said "delayed", but delaying needs a per-output buffer and brings capacity management and ordering guarantees with it. On the MIDI wire an ordinary message cannot go between an `0xF0` and its `0xF7` either, so **knowing it was dropped is more diagnosable than having it arrive late**. The counters make it visible.

These three rules are unchanged by MIDI 2.0: in UMP a data message stream is also limited to one at a time per group, so it carries over directly.

## Loop prevention

### The default: never back to the same endpoint

A message that came in on an endpoint's input port is **not sent to an output port of the same endpoint by default**. Returning a note from a USB MIDI keyboard to that keyboard is rarely what anyone wants.

[DATA_MODEL.md](DATA_MODEL.md) makes an input port's and an output port's endpoint identifiable, so the rule holds structurally.

It can be lifted per route for configurations that deliberately echo back. The default is to forbid it.

### Multi-hop cycles cannot happen inside

Setting up both `A in → B out` and `B in → A out` **does not cycle inside EspMidi**. Routing is one-way, In → Out, with no internal Out → In edge. A's playing goes out to B and stops there; it never comes back to B's input port.

It would only come back if device B echoed it, and that is external wiring EspMidi cannot see. So **there is no static cycle check when a route is added** — there is nothing to check.

The two that do exist are both covered by the default rule above:

- sending back to the same endpoint
- an application port looping to itself (`app.in()` → `app.out()`, the same endpoint, so the same rule prevents it)

If an application builds a ring using two application ports, it advances one lap per queue pass. `update()` only handles what was queued when it was called, so one `update()` cannot keep going round.

## Ports that are not usable

- No messages arrive from a disconnected input port. The route stays.
- Sending to a disconnected output port returns failure. The route stays and a counter rises.
- Whether an output port is congested (its queue full) is readable as a diagnostic.
- Which port had the problem is identifiable from the port handle and its metadata.

A route pointing at an unusable port is not an error. Devices being plugged and unplugged is normal operation, not an exception.

## The application port

A port with no transport behind it: the sketch injects messages into it and receives from it. **The application is not a special case — it is one port alongside a UART or a USB cable**, with the same seats, the same groups and the same loop rule.

```cpp
espmidi::AppPort app(router, "monitor");

// watching: receive what the pipeline delivers
app.onMessage(handler, this);
router.addRoute(usbHostIn, app.out());

// injecting: put what you made into the pipeline
router.addRoute(app.in(), uartOut);
app.sendShort(0xb0, 7, 64);
```

Which to use for what:

| What you want | What to use |
| --- | --- |
| watch / log / drive an LED or display | an application port (its output side) |
| create / inject (buttons, encoders, generating MIDI Clock) | an application port (its input side) |
| pass, drop or rewrite (one to one) | a stage callback |
| one to many, or emit a different kind of message | `send()` to an application port from a callback |

The control mapping helpers ([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)) are built on this.

**`send()` never recurses immediately.** An injected message is queued and handled by the next `update()`, so calling it from inside a stage or a callback cannot interrupt the pipeline that is running.

## How chunks are handled

**A chunk meets only the filter, and only on the first chunk.** Since rule 1 fixes the path when the stream starts, a decision must never change midway. Transforms and callbacks never see a chunk at all — interpreting the contents of a data stream is not EspMidi's job. To process a SysEx, receive it on an application port and do it there.

If a filter rejects the first chunk, that output **does not claim the stream**. Claiming it would leave the output blocked by a stream that is not being sent.

**Chunks are split to fit the queue.** One entry carries `ESPMIDI_CHUNK_BYTES` of payload (48 by default), so a larger chunk is spread over several entries. **Only the first fragment keeps `chunkStart` and only the last keeps `chunkEnd`**, so downstream still sees one stream. The split is safe because a chunk boundary carries no meaning of its own (they are not rejoined).

## Diagnostic counters

A message that was not delivered is always counted somewhere (the visibility requirement in [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md)).

| Counter | Meaning |
| --- | --- |
| `received` | taken in from a port |
| `queueFull` | dropped because the queue was full |
| `delivered` | sent successfully |
| `sendFailed` | the port refused to send (disconnected, and so on) |
| `droppedByFilter` | a declarative filter rejected it |
| `droppedByStage` | a transform could not express the result, or a callback returned `Drop` |
| `sysExRejected` | a second stream arrived at an output already sending one (rule 3) |
| `blockedBySysEx` | could not be sent because a stream was in progress (rule 3) |
| `noRoute` | there was no destination at all |
