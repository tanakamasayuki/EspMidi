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

The procedures themselves are Japanese only.

- [`uart_midi_din.ja.md`](uart_midi_din.ja.md): the physical layer of a real MIDI DIN socket (optocoupler, 220Ω, 5V current loop). The automated tests stop at the UART byte layer.
- [`usb_device_host_os.ja.md`](usb_device_host_os.ja.md): how a PC, a Mac and a DAW see it. `peer/usb_midi` asserts the port counts; **how they are named in a list is up to the OS**.
- [`usb_host_real_devices.ja.md`](usb_host_real_devices.ja.md): real USB MIDI devices, including several at once, through a hub, composite devices, and **devices with no serial number**.
- [`ble_midi_pairing.ja.md`](ble_midi_pairing.ja.md): real BLE MIDI devices and how an OS sees the board, including pairing and **devices with a random address**.
- [`sysex_dump.ja.md`](sysex_dump.ja.md): a real patch dump. At a few kilobytes the queue splitting, the output exclusivity and a disconnect mid-dump all start to matter.
- [`control_mapping.ja.md`](control_mapping.ja.md): the feel of real buttons, knobs and encoders. `unit/control_mapping` fixes how values and time are handled; **how that feels under a human hand cannot be asserted**.

## Planned

Nothing: the procedures are all written. **Each has to be walked through by a person at least once** — they are release gates (see [../../docs/RELEASE_CHECKLIST.md](../../docs/RELEASE_CHECKLIST.md)).
