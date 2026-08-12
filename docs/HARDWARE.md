# MIDI DIN hardware

[日本語](HARDWARE.ja.md)

**The MIDI DIN physical layer is outside this library.** It is still something everyone connecting an ESP32 has to get through, so what the specification requires is collected here.

**These two documents are the authority. Check the originals when you design.**

| | |
| --- | --- |
| [MIDI 1.0 Detailed Specification (M1 v4.2.1)](https://midi.org/midi-1-0-detailed-specification) | the main document. A Japanese Ver 4.2 is published free by [AMEI](https://amei.or.jp/midistandardcommittee/MIDIspcj.html) |
| [**CA-033 MIDI 1.0 Electrical Specification Update (2014)**](https://www.midi.org/wp-content/uploads/wpforo/default_attachments/1709416667-ca33-MIDI-10-Electrical-Specification-Update.pdf) | **replaces the Hardware section**, adding 3.3 V signalling and RF grounding |

> **The circuit in the Japanese Ver 4.2 PDF is 5 V only.** The 3.3 V values arrived with CA-033 in 2014 and are not in that edition. **An ESP32 is 3.3 V, so this is the one to read.** CA-033 is approved jointly by the MMA and AMEI.

What follows summarises CA-033 for implementers. The numbers are that document's.

## The electrical basics

| | |
| --- | --- |
| Baud rate | **31.25 kbaud ±1%**, asynchronous |
| Frame | 1 start + 8 data + 1 stop = **10 bits / 320 µs** |
| Bit order | **LSB first** |
| Logic | **logical 0 = current ON** |
| Loop current | **5 mA** |
| Rise / fall time | **under 2 µs** |

**One output shall drive one and only one input.** Use a Thru to fan out.

## MIDI OUT

The transmitter is the side that **drives the current**. Isolation lives on the receiving side, so no optocoupler is needed here.

- A series resistor on pin 4 and on pin 5 (they also limit the short-circuit current).
- **Pin 2 (shield) is tied to ground on the transmitter only.**
- The UART is 8-N-1.

### Resistor values

| | +5 V ±10% | **+3.3 V ±5%** |
| --- | --- | --- |
| R_A (pin 4 → +V) | 220Ω 5% **0.25 W** | **33Ω 5% 0.5 W** |
| R_C (pin 5 → the buffer output) | 220Ω 5% **0.25 W** | **10Ω 5% 0.25 W** |

**At 3.3 V, R_A has to be a 0.5 W part.** It pulls straight up to the 3.3 V supply, so a shorted MIDI cable dissipates up to 0.383 W in it. CA-033 also offers an alternative: four 130Ω 0.125 W resistors in parallel, giving 32.5Ω at 0.5 W.

**A 3.3 V design assumes the buffer driving R_C is open collector or open drain**, which is why 0.25 W is enough there. **If you use a push-pull buffer, check that a short cannot exceed its short-circuit rating.**

### 3.3 V compatibility

CA-033 states it is **compatible with receivers that strictly follow the specification**. Two kinds may not be:

- **Receivers that are not opto-isolated.** They tend to be voltage-sensitive, and the lower signalling voltage may not clear their input-high threshold.
- **Devices that draw power from pin 4.** 3.3 V may not be enough for their supply.

Both are designs that already depart from the specification. Building the 5 V version is a defensible choice when the other end is vintage gear.

## MIDI IN

**The receiver must be isolated.** This is the heart of MIDI.

| | |
| --- | --- |
| Optocoupler | **Sharp PC-900V** or **HP 6N138** (CA-033's examples). It must **turn on with under 5 mA**, with rise and fall under 2 µs |
| R_B | **220Ω**, in series with the optocoupler's LED |
| D1 | **1N914**, reverse-voltage protection for that LED |
| R_D | depends on the optocoupler and V_RX. **280Ω is recommended for a PC900V at V_RX = 5 V** |

- **Pin 2 must have no DC path to the receiver's ground**, so no ground loop forms.
- Pins 1 and 3 are unused; leave them unconnected at both ends.
- The jack's shield connector also gets **no DC path to ground**.

### Connecting it to an ESP32

The optocoupler's output is an **open collector pulled up to V_RX**, so **making V_RX 3.3 V lets it drive an ESP32 GPIO directly.** Recalculate R_D for your optocoupler and that voltage — the 280Ω above is for a PC900V at 5 V.

> **Never wire a MIDI DIN socket straight to a GPIO.** The 5 mA the other device drives, referenced to *its* supply, would arrive at a 3.3 V pin.

## Thru

A circuit that re-emits what MIDI IN received. R_E and R_F are **chosen exactly like MIDI OUT's R_A and R_C** (the 220Ω pair at 5 V, or 33Ω / 10Ω at 3.3 V).

**Chaining has a limit.** The optocoupler's response time softens the edges, and that error **accumulates in the same direction** at every hop. CA-033 says faster optocouplers should be used for chains **longer than three instruments**.

## Connectors and cable

| | |
| --- | --- |
| Connector | **5-pin DIN (180°)** female, panel mount. For example a Switchcraft 57PC5F |
| Cable | **15 m (50 ft) maximum**, shielded twisted pair |
| Plug | 5-pin DIN male. For example a Switchcraft 05GM5M |
| Shield | **connected to pin 2 only, at both ends** |

**Do not connect the cable shield to the plug's shell (barrel).**

## Optional EMI / EMC provisions (added by CA-033)

None of these are required. **MIDI works without them**; they exist for EMC compliance.

| | |
| --- | --- |
| Ferrite beads | **1 kΩ at 100 MHz** (for example MMZ1608Y102BT) on the signal pins. **Place them as close to the jack as possible** |
| MIDI IN pin 2 | to ground through a **0.1 µF capacitor**, which keeps DC isolated while shunting RF |
| MIDI IN jack shield | the same 0.1 µF. **A direct connection is still forbidden** |
| MIDI OUT / Thru jack shield | may be unconnected, or **connected straight to ground** |

The capacitor exists because cable inductance makes the shield ineffective at high frequencies. At 0.1 µF the impedance is about 0.16Ω at 10 MHz (practically a short) and about 26.5 kΩ at 60 Hz (open).

## Telling software and hardware apart

**You can rule the software out before suspecting the wiring.**

```sh
cd tests
uv run --env-file .env pytest loopback/uart_midi/
```

That test passes with **no wiring at all** (the GPIO matrix puts UART1_TX and UART2_RX on the same pin).

- **It passes → the software is right.** The problem is the circuit, the wiring or the other device.
- **It fails** → fix that first.

The procedure for checking on real hardware is [`../tests/manual/uart_midi_din.ja.md`](../tests/manual/uart_midi_din.ja.md) (Japanese).

## See also

- [MIDI_BASICS.md](MIDI_BASICS.md) — the caveats of MIDI itself
- [PORTS.md](PORTS.md) — the UART port. **The library sees no further than the UART byte layer.**
