# EspMidi — Example workflow

> 日本語版: [GUIDE_EXAMPLES.ja.md](GUIDE_EXAMPLES.ja.md)

Route MIDI events between available transports.

Check installation and platform requirements in the [library README](../README.md), then choose a sketch from the [example index](../examples/README.md). Version/support tables and internal capacity defaults stay in their original reference rather than being duplicated here.

## From setup to a verified result

1. Choose the smallest relevant sketch and check its board, hardware, dependencies and bundled files.
2. Match pins, peers and input files to your actual setup. Each README identifies important placeholders.
3. Open the .ino, select board/port and upload. Match Serial baud where used.
4. Locate the last completed stage: initialization, input, processing or output. Compilation does not test hardware I/O.
5. Reproduce the supplied example before changing one setting at a time.

## API and data handling

Initialize each transport and port, register routes, then continue the router/port/stack service calls shown in the sketch. Routing MIDI messages does not generate sound: connect a MIDI instrument or software synthesizer. Port availability can change with USB/BLE connection state, so route groups and stable identities serve a different purpose from a single current endpoint.

Prevent unintended feedback loops when extending bidirectional routes, and pair injected Note On with Note Off. UART MIDI needs a suitable DIN electrical interface rather than direct socket-to-GPIO wiring. Keep packet/frame lengths and SysEx view lifetimes intact when adding filters; a callback reference is not persistent storage.

## When adapting it

- Distinguish bool status, processed counts and handles; do not use stale output after failure.
- Registered callbacks run later from their documented context. Decide synchronization and retained-data lifetime.
- Keep bytes, samples, frames and pixels distinct; capacity is different from valid length.
- Retain views/pointers/buffers for the required period. Copying a pointer does not copy its payload.
- Add application-specific failure handling, retry and cleanup that a minimal example may omit.

## Read the individual README

Use Prepare and run for hardware and steps, Follow the sketch for the actual APIs and precautions for feature-specific constraints. If output is missing, check initialization first, continued service calls next and input/peer readiness after that.
