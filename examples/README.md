# Examples

[日本語](README.ja.md)

Every example is a **practical** one: a sketch you can flash as-is on a real hardware setup.

They all follow the same three steps: 1) start the stacks, 2) create the ports, 3) build the routes and filters. `EspMidi` does not own any stack, so step 1 is the sketch's job.

## Examples

- `UartMidiMonitor`: prints UART MIDI to the console while passing it on to a second UART unchanged. Put it between a keyboard and a sound module and the playing keeps working.
- `UsbMidiDevice`: appears to a PC as a two-port USB MIDI interface. Port 1 is the MIDI DIN pair, port 2 is the board itself.

More are added from the phase in which each port starts working; see [../docs/DEVELOPMENT_PLAN.ja.md](../docs/DEVELOPMENT_PLAN.ja.md) for the planned list.

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
