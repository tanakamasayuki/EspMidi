# Manual Tests

[日本語](README.ja.md)

Procedures for tests where human action or judgement is part of what is being verified.

**These procedures are never mixed into the automated pass criteria.** Anything that can be asserted automatically belongs in `unit/`, `loopback/` or `peer/`.

## What belongs here

Only things that match one of these:

- Real wiring is required (an opto-isolated MIDI DIN circuit, for example).
- Host OS or DAW recognition is the subject (how port names and port counts appear).
- Bluetooth pairing interaction is the subject.
- The behaviour of a real MIDI device is the subject (device-specific SysEx quirks).
- Visual or aural confirmation is needed (does it actually make a sound, is the latency usable).

## Procedures

None yet. See the coverage plan in [../TEST_PLAN.ja.md](../TEST_PLAN.ja.md).

## Planned

- `uart_midi_din`: send to an external sound module over a real MIDI DIN circuit (opto-isolator, 220Ω, 5V current loop). The automated tests stop at the UART byte layer, so the physical layer is verified here (Phase 5).
- `usb_device_host_os`: confirm a PC or Mac recognises the board as a MIDI interface and that **one named port per cable** appears in a DAW's port list (Phase 6).
- `usb_host_real_devices`: connect real USB MIDI keyboards, pads and sound modules, including several at once, through a hub, and composite devices (Phase 7).
- `ble_midi_pairing`: confirm pairing and reconnection with real BLE MIDI devices, and how it appears from iOS, Android and a PC (Phase 8).
- `sysex_dump`: transfer a real instrument's patch dump (a long SysEx) and confirm the receiving side loads it correctly (Phase 5 onwards).
