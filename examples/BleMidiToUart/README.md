# BleMidiToUart

[日本語](README.ja.md)

**A wireless BLE MIDI keyboard plays a MIDI DIN sound module.** No PC involved.

This is UC5 in [../../docs/USE_CASES.ja.md](../../docs/USE_CASES.ja.md). It also works the other way: anything arriving on the MIDI DIN input is sent to the keyboard, so a controller with lights or a display can be driven back.

## Setup

```text
BLE MIDI keyboard ──→ MIDI DIN OUT (sound module)
MIDI DIN IN ────────→ BLE MIDI keyboard
```

| Constant | Default | Meaning |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT, to the sound module |

## What to look at

**Scanning and connecting is the sketch's job; what is on the connection is the port's.** That is why `discover()` appears nowhere here — BLE itself is outside EspMidi (see [../../docs/CORE_DESIGN.ja.md](../../docs/CORE_DESIGN.ja.md)).

**The keyboard is not named anywhere.** Both routes are written against groups, so neither is rebuilt when it comes and goes.

```cpp
router.addRoute(espmidi::InGroup::all(), din.out());   // whatever plays -> the module
router.addRoute(din.in(), espmidi::OutGroup::all());   // the DIN input -> whatever is connected
```

**A route never sends a message back to the endpoint it came from**, so the second one reaches the keyboard and not the module's own output.

**It gets its seat back on reconnect.** A BLE address is the identity, so the routes survive the keyboard going out of range.

**These are the only ports with a timestamp.** 13-bit milliseconds (`TimestampUnit::Milliseconds13`), passed through without being interpreted.
