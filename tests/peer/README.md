# Peer Tests

[日本語](README.ja.md)

Automated tests that use two ESP32-S3 boards.

In `EspMidi` the correctness of the core is fixed in [`../unit/`](../unit/). Peer tests are limited to smoke tests of the port boundaries (USB / BLE / UART).

Anything that can be checked on a single board goes in [`../loopback/`](../loopback/) instead, so it does not occupy the always-connected pair.

## Hardware

Two permanently connected ESP32-S3 boards.

- Host board: runs the USB Host / BLE Host side ports.
- Device board: runs the USB Device / BLE Device side ports.

USB tests connect the USB data lines directly (BLE tests need no wiring but use the same two boards).

| Host board | Device board |
| --- | --- |
| GPIO19 (D-) | GPIO19 (D-) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

If the boards are powered separately (for example from a PC), leave VBUS unconnected.

### UART reuses the same wiring as a crossover

The wiring above is straight through, but **assigning TX and RX the other way round per role turns it into a crossover**. No extra wiring is needed.

| | Host role | Device role |
| --- | --- | --- |
| GPIO19 | TX | RX |
| GPIO20 | RX | TX |

At 31250 baud the USB series resistors are not a problem. The one condition is that the profile must not use native USB (the console goes to UART0, the board's external USB-serial chip).

## Running

Each `sketch.yaml` sets `default_profile` to its board, so `--profile` is normally not passed.

```sh
uv run --env-file .env pytest peer/
```

`.env` uses the same variable names as the sibling projects such as `EspUsbDevice`.

```sh
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyUSB0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB1
```

To check against a sibling library still under development, select the matching `*_local` profile.

## Symmetry

Peer tests are easier to write here than in ESP32KeyBridge. **USB Device MIDI ↔ USB Host MIDI**, **BLE MIDI Device ↔ BLE MIDI Host** and **UART ↔ UART** are all symmetric pairs, so both directions are covered by swapping the sending and observing roles.

BLE tests clear the bonds on both sides at the start and at the end, so a pairing left over from a previous run cannot change the result. They also keep one test function per file: a BLE link is stateful and expensive to establish, so pairing once and asserting along the way is more deterministic.

## Already covered

- `uart_midi`: both directions of the UART port, reusing the existing wiring as a crossover (Phase 5). Notes, control changes and SysEx go each way. The round trip that fits on one board is in [`../loopback/uart_midi/`](../loopback/uart_midi/); what this adds is a **second clock**, each board resolving the bit timing of a signal it did not generate.
- `usb_midi`: the USB Device port boundary (Phase 6). It **asserts the cable counts themselves through `getMidiPortInfo()`** before checking round trips across cables and SysEx. The DUT side is a plain `EspUsbHost`: if both ends built their packets with the same code, a wrong cable nibble would cancel out.

## Planned

- the USB Host port half of `usb_midi` (Phase 7), replacing the DUT with EspMidi's USB Host port.
- `ble_midi`: the BLE MIDI Device and Host port boundaries, including timestamps and SysEx splitting (Phase 8).
