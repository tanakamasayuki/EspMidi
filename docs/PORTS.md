# Ports

[日本語](PORTS.ja.md)

The bundled ports: what each one does, what it depends on, how far it is verified, and how it looks from a PC.

**What the status column means.** The distinction is whether it has been checked on real hardware, so that the column can be trusted when deciding whether to release.

- **planned**: not started.
- **implemented (hardware check pending)**: the code exists but has not run on a board.
- **implemented (verified on hardware)**: confirmed working on a board.

## The list

| Port | Header | Depends on | Target SoC | Status |
| --- | --- | --- | --- | --- |
| UART MIDI | `EspMidiUart.h` | — (`HardwareSerial`) | every ESP32 | **implemented (verified on hardware)** |
| USB Device MIDI | `EspMidiEspUsbDevice.h` | `EspUsbDevice` 2.0.2+ | S2 / S3 / P4 | **implemented (verified on hardware)** |
| USB Host MIDI | `EspMidiEspUsbHost.h` | `EspUsbHost` 2.7.5+ | S2 / S3 / P4 | **implemented (verified on hardware)** |
| BLE MIDI Device | `EspMidiEspBle.h` | `EspBle` 1.2.0+ | any SoC with BLE | **implemented (verified on hardware)** |
| BLE MIDI Host | `EspMidiEspBle.h` | `EspBle` 1.2.0+ | any SoC with BLE | **implemented (verified on hardware)** |

Ports are header-only, so **a sketch takes on a dependency only for the ports it includes**. A sketch that includes just `EspMidi.h` requires neither `EspUsbHost` nor `EspBle`.

## Each port

### UART MIDI

Sends and receives a MIDI 1.0 byte stream at 31250 baud. The protocol and the physical UART grew up together, so this port is bundled here — but kept separate from the integration core ([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md), Japanese).

- Endpoints supplied: one, statically. One input port and one output port.
- Timestamps: none (`TimestampUnit::None`).
- Resolving running status and message lengths is the core parser's work.
- SysEx is detected at its boundaries (`0xF0` / `0xF7`) and delivered in chunks.
- `begin(name, rxPin, txPin)` opens the `HardwareSerial` at 31250 baud, supplies the seats and registers the output sink. It is **idempotent**: reconfiguring is calling `begin()` again.
- **A serial that was already open is closed first.** An ESP32 reaches a pad through the GPIO matrix, and **opening a second pad does not take the first one back**. Reopening on different pins without closing leaves the old ones attached, and the board transmits on a pin nobody asked for (seen on a bench).
- Receiving is polled from `update()`. **At most `ESPMIDI_UART_RX_BYTES` (64 by default) per call**, so a device streaming a dump cannot hold `loop()`.
- Sending does not use running status. One output port carries messages that came from several inputs, and a receiver that loses one byte of a compressed stream misreads everything after it.

```cpp
espmidi::UartPort uart(router, Serial1, 1);
uart.begin("MIDI DIN", RX_PIN, TX_PIN);

void loop() {
  uart.update();
  router.update();
}
```

**Framing belongs to the port.** A chunk carries only the payload, so `espmidi::Serializer` puts `0xF0` in front of the first chunk and `0xF7` after the last. `end()` **closes a stream that is still being sent with an `0xF7`** first (rule 2), so the device on the other end is not left holding half a dump.

**`espmidi::BasicUartPort<T>` is the real class and `UartPort` is it with `HardwareSerial` applied.** It is a template so that the port's own behaviour — supplying seats, receiving reaching the router, framing, a full transmit buffer, terminating an interrupted dump — is fixed by tests on the host. What is left for hardware is **only that the bytes really cross the pad**.

**Real MIDI DIN sockets.** The 5V current loop, the optocoupler and the 220Ω resistors are outside this port. What the TX and RX pins connect to is the sketch's and the hardware's business; the automated tests cover the UART byte layer ([../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md), Japanese). See also [MIDI_BASICS.md](MIDI_BASICS.md).

### USB Device MIDI

Rides the raw packet API of `EspUsbDevice`'s `EspUsbDeviceMidi` (`readPacket` / `writePacket`).

- Endpoints supplied: one, statically, with one input and one output port per cable.
- Timestamps: none.
- Converting USB MIDI event packets (4 bytes: high nibble = cable, low nibble = Code Index Number) to and from `espmidi::Message` is the core's work.
- SysEx is assembled from CIN 0x4–0x7.
- Receiving is polled through `readPacket()`, which suits `update()`. **At most `ESPMIDI_USB_PACKETS_PER_UPDATE` (32 by default) packets per call.**
- Call `begin()` **after starting the stack**: that is when the cable counts are final, and the seats are made from what they are then.
- The host decides when a seat is usable. `update()` follows `EspUsbDevice::ready()` (`tud_mounted()`), so a mount makes it `Available` and an unmount `Disconnected`. **The seats themselves never go away**, so unplugging does not mean rebuilding routes.
- **A packet on a cable that was never declared is dropped.** The cable number is read straight from the packet header, so keeping it would land it on some other seat. `unknownCablePackets()` counts them.
- Cable counts are declared with `EspUsbDeviceMidi(device, cableCount)` or `(device, inCableCount, outCableCount)`. **The two directions can differ.** `MAX_CABLES` is 16, matching this library's per-endpoint limit.

**The cable counts invert.** This is the easiest thing to get wrong here.

| `EspUsbDeviceMidi` | Meaning | `EspMidi` |
| --- | --- | --- |
| `inCableCount()` | device → host (we send) | **output ports**, `outPortCount()` |
| `outCableCount()` | host → device (we receive) | **input ports**, `inPortCount()` |

`espmidi::UsbDevicePort` counts in this library's directions instead (`inPortCount()` / `outPortCount()`). Getting it backwards makes every port work the wrong way, and **a symmetric cable configuration cannot catch it in a test**: a received message's cable number comes from its own header, so a round trip looks correct either way. That is why the peer test's device is an asymmetric 2 / 3.

```cpp
EspUsbDeviceMidi usbMidi(usb, 2, 3);          // 2 sending / 3 receiving (host view)
espmidi::UsbDevicePort port(router, usbMidi, usb);
usb.begin(config);
port.begin("USB MIDI");                        // 2 outputs / 3 inputs
```

**`espmidi::BasicUsbDevicePort<M, D>` is the real class and `UsbDevicePort` is an alias.** Like UART it is a template, so the cable-to-seat mapping, the mount handling and the dropping of undeclared cables are all fixed by tests on the host.

**How it looks from a PC.** One MIDI interface with one port per cable. It works on its own or as part of a composite USB device alongside HID, CDC or MSC.

**Implementation notes** on `EspUsbDevice`'s multi-cable support (request 1 in [LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md), Japanese):

- **The descriptor grows**: 92 bytes for one cable, 572 for sixteen. `MAX_CONFIG_DESCRIPTOR` was raised to 704 bytes, but a composite configuration with HID, CDC or MSC can still exceed it. `descriptorLength()` tells you the size in advance, and `configurationDescriptorForSpeed()` returns 0 if it does not fit. **Declare the cables you need, not more.**
- **Out-of-range cables are refused.** `EspUsbDevice`'s helpers return false for a cable at or beyond `cableCount()` rather than quietly putting it on a port the host does not know about. `EspMidi` uses the raw packets but refuses out-of-range the same way.

**Current limitation.** Cables cannot be named individually: `TUD_MIDI_DESC_JACK_DESC` applies one string index to all four jacks, and `EspUsbDevice` has no string table. Port naming is left to the host.

### USB Host MIDI

Rides `EspUsbHost`'s MIDI message listener and raw byte send (`midiSend`).

- Endpoints supplied: **0 to N, dynamically.** They come and go with the devices.
- Timestamps: none.
- `EspUsbHostMidiMessage` arrives as a decomposed 4-byte packet. `EspUsbHost` does not join SysEx back together, so the core does.
- Sending hands raw bytes to `midiSend()`; the core sets the cable nibble.
- The identity used to match a seat again is the VID, PID and serial from `EspUsbHostDeviceInfo`.
- The port count is settled at connection time from `getMidiPortInfo(info, address)`.

**The cable counts do not invert here.** `EspUsbHost` already names them from the host's point of view, and this library is that host.

| `EspUsbHostMidiPortInfo` | Meaning | `EspMidi` |
| --- | --- | --- |
| `inCableCount` | device → host (we receive) | **input ports** |
| `outCableCount` | host → device (we send) | **output ports** |

**That is the opposite of the USB Device port**, where the same host-view names describe a device and therefore do invert. Read both before changing either.

**Connections are found by polling from `update()`, not by a callback.** `getDevices()` is asked every `ESPMIDI_USB_HOST_POLL_MS` (100 by default) and the difference makes the seats. Enumeration takes far longer than that, so a device is never noticeably late.

**The reason is to keep everything that touches a seat on one task.** `EspUsbHost`'s MIDI callback runs on the library's own task, and all it does there is **copy the raw four bytes into a lock-free ring**. The decoder, the cable map and the registry are only ever touched from `update()`.

- A device can be enumerated before its MIDI interface has been claimed. If `getMidiPortInfo()` fails, **look again on the next poll** rather than giving up.
- Seats are matched on the identity. **An address is only the number the stack handed out this time**, so it is not used for matching.
- On disconnect the seat becomes `Disconnected` and stays. **Sending to it fails**, so it cannot reach whatever device takes that address next.
- Storage is fixed-size: `ESPMIDI_MAX_USB_HOST_DEVICES` (4), `ESPMIDI_USB_HOST_MAX_CABLES` (8), `ESPMIDI_USB_HOST_PACKETS` (64).
- Diagnostics: `unknownCablePackets()`, `droppedPackets()`, `refusedDevices()`. **A device that cannot be identified takes a fresh seat on every connection**, so swapping such devices repeatedly exhausts them — `refusedDevices()` exists to make that visible instead of mysterious.

**Configurations covered.** Dynamic connect and disconnect, several MIDI devices, devices behind a USB hub, coexistence with non-MIDI USB devices, composite devices that include MIDI, and one connection carrying several logical ports.

**Implementation notes** on `EspUsbHost`'s cable discovery (request 2 in [LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md), Japanese):

- `inCableCount` / `outCableCount` are **the host's directions**. The USB class specification names embedded jacks from the device's side, so reading a descriptor directly inverts them.
- Only **the first MIDI Streaming interface, and one bulk endpoint per direction**, are tracked. A device with several MS bulk endpoints in the same direction cannot be represented. That is the same limit as `midiSend()` and the receive callback, so **one device is one endpoint** lines up with it.
- **A cable count of 0 means more than "no such direction".** Do not assume one port; when it is 0, no port is made for that direction.

**Current limitation.** Per-cable jack names cannot be read yet (the second half of request 2). The port name falls back to the device's product string.

### BLE MIDI Device / Host

Rides `EspBle`'s `EspBleMidiDevice` and `EspBleMidiHost`. It never descends to raw GATT (decision 2 in [DECISIONS.ja.md](DECISIONS.ja.md), Japanese).

- Endpoints supplied: one statically on the device side; **one per connection, dynamically**, on the host side.
- Timestamps: **13-bit milliseconds (`TimestampUnit::Milliseconds13`)**. The only one of the ports that has them.
- The BLE MIDI packet format, resolving running status and splitting a dump across packets all belong to `EspBle`. The core only converts `EspBleMidiMessage` into `espmidi::Message`.
- The identity used to match a seat again is the BLE address.

**Who owns the GATT service.** The sketch creates the `EspBleMidiDevice` or `EspBleMidiHost` and hands it over. `EspMidi` never registers a MIDI GATT service itself, so a sketch already using `EspBle`'s MIDI directly cannot end up registering it twice.

**Coexistence.** BLE MIDI can run alongside BLE HID and custom GATT services. The BLE stack, connections, security and advertising are outside `EspMidi`.

**The device side's seat is static, the host side's dynamic.** That is why `Transport` splits into `BleDevice` and `BleHost` (the same reason USB does). The device side's seat belongs to this board, so it comes back to the same seat with no identity of its own; the host side matches on the BLE address.

**Scanning and connecting belong to the sketch; what is on the connection belongs to the port.** The port calls `discover()`, so the sketch does not. A seat appears **when discovery and subscription have finished**, not when the link comes up.

**This is the only port that reassembles a dump.** `EspBle` takes a complete `0xF0..0xF7` message and does the splitting itself, so the chunks routing delivers are joined back together here. **The limit is `ESPMIDI_BLE_SYSEX_BYTES` (320 by default, matching EspBle's own)**, and a longer dump is refused and counted (`oversizedStreams()`) rather than truncated — a half-sent patch is worse than none.

**Received messages go straight into the router's queue from the BLE task.** `Router::receive()` is thread-safe for exactly this ([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md), Japanese), and it is what keeps a dump from being copied: the chunk still points into NimBLE's buffer while the queue copies it. The BLE task and `update()` share **one port handle per connection and nothing else**.

**`EspMidi` takes the MIDI callback.** `EspBleMidiDevice::onMessage()` and `EspBleMidiHost::onMidiMessage()` are each that object's single primary callback, so a sketch that wants to see BLE MIDI itself **reads it through routing**. Connection notifications use additional listeners, so the sketch's own `onConnected()` is left alone.

- Storage is fixed-size: `ESPMIDI_MAX_BLE_CONNECTIONS` (4), `ESPMIDI_BLE_SYSEX_BYTES` (320), `ESPMIDI_BLE_EVENTS` (8).
- Diagnostics: `oversizedStreams()`, plus `droppedEvents()` and `refusedConnections()` on the host side.

## Control mapping is not a port

The helpers for buttons, knobs, encoders and clock live in `src/EspMidiControl.h` and are **part of the core**. They touch no pin and read no clock — the reading and the current time are parameters — so they bring no transport dependency with them.

What they produce goes onto an application port, so **a control change from a knob can be routed to any of the ports above**.

## Possible future ports

From the extensibility list in [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) (Japanese), the items that could become ports.

| Candidate | Note |
| --- | --- |
| MIDI 2.0 / UMP | the representation is already one step from it (decision 1 in [DECISIONS.ja.md](DECISIONS.ja.md)) |
| RTP-MIDI / AppleMIDI | session management is a large responsibility; outside the initial scope |
| More UART ports | just more UART ports, so easy to add |
| A MIDI bridge over SPI or I2C | |
| CV/Gate, DIN Sync | closer to a conversion helper than to MIDI |

Ports are header-only, so **what gets added does not have to live in this repository**. An external library can take part as a port.
