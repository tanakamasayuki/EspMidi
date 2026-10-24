# Examples

[日本語](README.ja.md)

Every example is a **practical** one: a sketch you can flash as-is on a real hardware setup.

They all follow the same three steps: 1) start the stacks, 2) create the ports, 3) build the routes and filters. `EspMidi` does not own any stack, so step 1 is the sketch's job.

## Examples

**If you are new, read the first three in order.** The guide is [../docs/GUIDE.md](../docs/GUIDE.md) and the MIDI caveats worth knowing are in [../docs/MIDI_BASICS.md](../docs/MIDI_BASICS.md).

### Start here — one port

- `SimpleMidiOut`: the smallest sketch. One port, sending only: it plays a scale, so a sound module makes a noise as soon as the board is powered.
- `SimpleMidiIn`: one port, receiving only. It prints what arrives, so you can see what a keyboard actually sends.
- `SameCodeAnyPort`: **the same MIDI code over UART, USB and BLE — one line changes.** This is the reason to use the library even with a single interface.

### Real setups

- `UartMidiMonitor`: prints UART MIDI to the console while passing it on to a second UART unchanged. Put it between a keyboard and a sound module and the playing keeps working.
- `UsbMidiDevice`: appears to a PC as a two-port USB MIDI interface. Port 1 is the MIDI DIN pair, port 2 is the board itself.
- `UsbHostToUart`: a USB MIDI keyboard plays a MIDI DIN sound module while the same performance also reaches a PC (UC1).
- `BleMidiToUart`: a wireless BLE MIDI keyboard plays a MIDI DIN sound module (UC5).
- `GpioControls`: a MIDI controller of knobs, buttons and an encoder, sending to USB MIDI and MIDI DIN at once (UC10).

## Layout

```text
examples/<Name>/
  <Name>.ino      the sketch name matches its directory name
  README.ja.md
  README.md
  sketch.yaml     the target board profile, with default_profile set
```

`tests/unit/test_repository_structure.py` checks this layout automatically, and that the list in this README matches what is actually there.

On the default branch `sketch.yaml` refers to `EspMidi` as `dir: ../../`; the shared release workflow rewrites it to a pinned version at release time (see [../docs/RELEASE_CHECKLIST.md](../docs/RELEASE_CHECKLIST.md)).

## Writing your own port

Ports are header-only, so they can live outside this repository. The interface is documented in the comments in `src/EspMidi.h`; UART is the smallest bundled port and the quickest one to read as an example. The background is in [../docs/CORE_DESIGN.ja.md](../docs/CORE_DESIGN.ja.md).

## Workflow and individual READMEs

Start with the [example workflow](../docs/GUIDE_EXAMPLES.md), then choose the relevant sketch.

A useful starting point is [SimpleMidiIn](SimpleMidiIn/README.md). Check its required hardware and choose another entry below if it does not match your setup.

| Example | Learn |
|---|---|
| [BleMidiToUart](BleMidiToUart/README.md) | Route BLE MIDI and UART MIDI bidirectionally |
| [GpioControls](GpioControls/README.md) | Send MIDI control events from GPIO input |
| [SameCodeAnyPort](SameCodeAnyPort/README.md) | Use common routing logic with different transports |
| [SimpleMidiIn](SimpleMidiIn/README.md) | Receive and inspect MIDI input messages |
| [SimpleMidiOut](SimpleMidiOut/README.md) | Send MIDI messages to an output port |
| [UartMidiMonitor](UartMidiMonitor/README.md) | Observe MIDI arriving on UART |
| [UsbHostToUart](UsbHostToUart/README.md) | Route USB Host MIDI to a UART port |
| [UsbMidiDevice](UsbMidiDevice/README.md) | Expose USB Device MIDI ports for routing |
