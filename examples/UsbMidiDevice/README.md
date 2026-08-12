# UsbMidiDevice

[日本語](README.ja.md)

The board appears to a PC as a **two-port USB MIDI interface**.

- **Port 1** is the UART MIDI DIN pair: what arrives on MIDI IN goes to the PC, and what the PC sends goes out of MIDI OUT.
- **Port 2** is the board itself: the sketch sends notes on it and receives what the PC sends back.

**Port 2 is what a USB MIDI interface for sale cannot do.** The same board is a MIDI device and a MIDI router at once, and to the routing both halves are just ports.

## Setup

```text
MIDI IN  ──→ GPIO20 ──→ [port 1] ──→ PC
MIDI OUT ←── GPIO19 ←── [port 1] ←── PC
        button ──────→ [port 2] ──→ PC
       console ←─────── [port 2] ←── PC
```

| Constant | Default | Meaning |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT |
| `BUTTON_PIN` | 0 | plays a note on port 2 |
| `IN_CABLES` / `OUT_CABLES` | 2 / 2 | cables declared (**host's point of view**) |

The console is the board's external USB-serial chip, separate from the native USB the PC sees.

## What to look at

**The cable counts are named from the host's point of view.** EspUsbDevice's IN is device to host (this library's **output** ports) and OUT is host to device (its **input** ports). Getting that backwards makes every port work the wrong way, so `espmidi::UsbDevicePort` counts in its own directions instead: `inPortCount()` / `outPortCount()`.

**`usbPort.begin()` comes after `usb.begin()`.** The cable counts are only final once the stack is up.

**A port the PC has not configured yet refuses what it is handed**, and routing counts it as `sendFailed`. It is the first thing to look at when nothing seems to arrive.

**Do not declare more cables than needed.** The descriptor grows, and a composite configuration with HID or CDC can hit the limit (see [../../docs/PORTS.ja.md](../../docs/PORTS.ja.md)).

## Profiles

EspUsbDevice's multi-cable MIDI is not released yet, so the only profile is `esp32s3_local`, which builds against the local checkout.
