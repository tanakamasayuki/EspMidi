# MIDI caveats, and notes per interface

[日本語](MIDI_BASICS.ja.md)

**Most of what goes wrong with MIDI is MIDI's own specification, not this library.** It is collected here. For how to use the library see [GUIDE.md](GUIDE.md); for **the MIDI DIN circuit**, [HARDWARE.md](HARDWARE.md).

**The specifications themselves.** The [MIDI 1.0 Detailed Specification](https://midi.org/midi-1-0-detailed-specification) (a free Japanese Ver 4.2 is published by [AMEI](https://amei.or.jp/midistandardcommittee/MIDIspcj.html)), and [CA-033 MIDI 1.0 Electrical Specification Update](https://www.midi.org/wp-content/uploads/wpforo/default_attachments/1709416667-ca33-MIDI-10-Electrical-Specification-Update.pdf) (2014), which replaces the Hardware section. **The 3.3 V circuit values exist only in CA-033** — the Japanese Ver 4.2 is 5 V only. The circuit is summarised in [HARDWARE.md](HARDWARE.md).


## MIDI itself

### There is no audio in it

MIDI carries "**the C key was pressed with a strength of 100**" and nothing else. The sound is made by whatever receives it. So:

- Connecting a MIDI cable to a sound module produces **whatever patch that module happens to be on**, not the sound you had in mind.
- Latency is not "the audio is late", it is "the instruction is late, so the module starts later".
- A recording is a record of gestures, not a waveform.

### Status bytes and data bytes

The top bit of a byte decides its role.

| Value | Kind |
| --- | --- |
| `0x80`–`0xFF` | **status byte** (the start of a message) |
| `0x00`–`0x7F` | **data byte** (a value: 7 bits, 0–127) |

So **almost every value in MIDI is 0–127**; 128 and above cannot be expressed. `espmidi::Message` masks on the way out, so arithmetic that overflows cannot put an invalid byte on the wire.

### Channels are 0-based here, 1-based on the device

**This is the classic mistake.**

```cpp
transform.channel = 1;   // the device displays this as "ch2"
message.channel() + 1    // what to print for a human
```

This library is consistently 0-based (`0`–`15`). Device panels and DAWs display `1`–`16`.

In General MIDI, **ch10 (9 when 0-based) is drums**. A melody sent there comes out as percussion.

### Note 60 is middle C, but its name differs by manufacturer

The number is universal; **whether that number is called C3 or C4 is not**. Being an octave out on paper while the number is correct is common. Talk in numbers.

### A note on with velocity 0 is a note off

**Not knowing this is the "note that never stops" bug.**

```
0x90 60 100  → note on
0x90 60 0    → note off (!)
0x80 60 0    → note off
```

Older devices never send the second form and silence everything with velocity-0 note ons. Check for both when you classify messages yourself.

- `espmidi::messageKind()` reports a velocity-0 note on as `KindNoteOff`.
- `espmidi::Transform` leaves velocity 0 alone (scaling it would make it un-stoppable).
- `espmidi::ControlOutput` treats it as 0, so an LED does not latch on.

### A note off is not guaranteed to arrive

Pull a cable, hang a device, crash the sender: **the note sustains forever**. Being able to send All Notes Off (`0xB0 123 0`) from the receiving side is worth having. This library never sends it by itself, because it does not interpret MIDI.

### Running status: the status byte can be omitted

When the same kind repeats, a sender may leave the status byte out.

```
0x90 60 100   note on
     62 100   ← continues the same 0x90
```

**Reading raw bytes naively breaks here.** This library's parser resolves it, so an `espmidi::Message` always carries a status. It is not used when sending.

### System Real-Time interrupts anything

`0xF8`–`0xFF` may appear **between any two bytes, including inside a SysEx**.

| | |
| --- | --- |
| `0xF8` Clock | **24 per quarter note**. At 120 BPM that is 48 a second |
| `0xFA` / `0xFB` / `0xFC` | Start / Continue / Stop |
| `0xFE` Active Sensing | about every 300 ms: "still alive" |
| `0xFF` System Reset | |

**Clock and Active Sensing will bury a log.** Filter them out when monitoring.

```cpp
espmidi::Filter quiet;
quiet.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindSystemRealTime);
```

### SysEx is long, device-specific, and can stop halfway

System Exclusive (`0xF0` … `0xF7`) carries device-specific data. A patch dump runs from a few kilobytes to tens of them.

- **The manufacturer decides the contents.** This library carries it without interpreting it.
- **It arrives in chunks.** `chunkStart` and `chunkEnd` mark one stream.
- **It can be cut short.** Any status byte other than `0xF7` also ends it.
- Its path is **fixed when it starts** and does not change halfway (rule 1, [ROUTING.md](ROUTING.md)).
- Ordinary messages during a dump are **dropped** (rule 3).

### A Program Change alone may not change the patch

Selecting a patch means Bank Select (CC 0 = MSB, CC 32 = LSB) **followed by** Program Change. The other order leaves you in the previous bank.

### Pitch Bend is 14 bits

Two data bytes, LSB then MSB, centred on 8192 (`0x00 0x40`). Not 7 bits.

### The bandwidth is narrower than it feels

At 31250 baud, 8N1, **one byte is about 0.32 ms and a three-byte message about 1 ms**.

- Press a ten-note chord and the last note is **about 10 ms late** — as specified.
- Clock plus a stream of control changes will congest it.
- USB MIDI and BLE MIDI have no such limit (both are far faster).

Not sending too many control changes when a knob moves matters in practice. That is what the hysteresis and the send-only-on-change behaviour of `espmidi::Analog` are for.

### MIDI Thru and loops

"Send back out whatever came in" becomes an **infinite loop** if the other end does the same. This library **does not return a message to the endpoint it came from** by default — but **if two ports are the two ends of one physical link** it cannot tell (they are different endpoints). Do not route between those two.

---

## Per interface

### UART / MIDI DIN

**The only interface that is electrically dangerous.**

- MIDI is a **5 mA current loop**: the signal is a current, not a voltage (logical 0 is current ON).
- **A MIDI IN must be isolated.** Use an optocoupler (PC-900V, 6N138 or similar).
- **Never wire a MIDI DIN socket straight to an ESP32 GPIO.** The 3.3 V GPIO is destroyed.
- A MIDI OUT's series resistors are **two 220Ω at 5 V, or 33Ω and 10Ω at 3.3 V** (CA-033). **At 3.3 V the 33Ω has to be a 0.5 W part.**
- Pin 2 is tied to ground **on the transmitter only**.
- **One output drives one and only one input.** Fan out with a Thru.

The values and the reasoning are in [HARDWARE.md](HARDWARE.md).

If the wiring is right, the software can be ruled out with `loopback/uart_midi` — a test that passes with no wiring at all. **If that passes and this does not, the problem is the hardware.**

The procedure is [`../tests/manual/uart_midi_din.ja.md`](../tests/manual/uart_midi_din.ja.md) (Japanese).

### USB Device (the side a PC sees)

- **One USB MIDI interface carries up to 16 "cables"**, and a PC sees each cable as a separate port.
- **The cable counts are named from the host's point of view.** `inCableCount` is device-to-host, which is an **output** port here. See [PORTS.md](PORTS.md).
- **Nothing can be sent before the PC configures the device.** Sending at startup fails and raises `sendFailed`.
- **More cables means a larger descriptor**: 92 bytes for one cable, 572 for sixteen. A composite configuration alongside HID or CDC can hit the limit.
- **Cables cannot be named individually.** The port names come from the OS.
- The ESP-IDF USB Host **refuses a configuration descriptor longer than 256 bytes** during enumeration. Sixteen cables work against a PC but not between two ESP32s.

### USB Host (the side devices plug into)

- **Enumeration takes time.** A seat does not exist the instant something is plugged in.
- **Claiming the MIDI interface can lag behind enumeration.** The port keeps looking rather than giving up (it polls).
- **A device with no serial number gets a fresh seat on every reconnect**, so that one device never inherits another's routing. Using up `ESPMIDI_MAX_USB_HOST_DEVICES` shuts out new devices (`refusedDevices()`).
- **One device is one endpoint.** A device with several bulk endpoints in the same direction cannot be represented.
- **A cable count of 0 means that direction does not exist.** A send-only keyboard gets input ports and nothing else.
- **Use a self-powered hub for bus-powered devices.** An ESP32's 5V supply is limited.

### BLE MIDI

- **The only interface with a timestamp.** 13-bit milliseconds (0–8191), wrapping about every 8 seconds. This library **carries it without interpreting it**.
- **The highest latency and jitter of the four.** It is bound by the connection interval (7.5 ms at best), which makes it the weakest choice for playing.
- **Dumps are limited to 320 bytes by default** (`ESPMIDI_BLE_SYSEX_BYTES`, matching EspBle's own limit). A longer one is **refused rather than truncated**, so a half-written patch cannot look valid to the device.
- **A seat appears when discovery and subscription finish**, not when the link comes up.
- **A device with a random address gets a new seat every time**, because seats are matched on the BLE address. Bonding is what fixes it.
- **Pairing is not this library's job.** Configure it through `EspBle`.
- iOS and macOS connect from "Bluetooth MIDI Devices". **The port does not appear in a DAW's list on its own.**

---

## What this library deliberately does not do

Parts of MIDI it stays out of, on purpose.

| | Why |
| --- | --- |
| Interpreting SysEx contents | device-specific; carrying it blindly means unknown devices work too |
| Interpreting, reordering or delaying time | timestamps are carried, not read. Order is never changed, so what happened stays traceable |
| Sending All Notes Off automatically | the other side of not interpreting MIDI. A sketch can send it |
| Sound generation, sequencing, DAW features | out of scope ([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md), Japanese) |
| MIDI 2.0 / UMP | not supported yet, but the internal representation is one step from it (decision 1 in [DECISIONS.ja.md](DECISIONS.ja.md), Japanese) |
