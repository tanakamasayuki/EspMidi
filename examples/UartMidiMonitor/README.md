# UartMidiMonitor

[日本語](README.ja.md)

Prints a UART MIDI 1.0 stream to the console while passing it on to a second UART unchanged. **Put it between a keyboard and a sound module and the playing keeps working.**

It shows what a keyboard actually sends without taking the sound module out of the chain.

## Setup

```text
MIDI IN  ──→ GPIO20 (Serial1 RX) ──┬──→ GPIO18 (Serial2 TX) ──→ MIDI OUT ──→ sound module
                                   └──→ monitor (printed to the console)
```

| Constant | Default | Meaning |
| --- | --- | --- |
| `RX_PIN` | 20 | the MIDI IN being watched |
| `TX_PIN` | 19 | the same UART's transmit (set it even if unused) |
| `THRU_RX_PIN` | 17 | receive of the pass-through link |
| `THRU_TX_PIN` | 18 | the MIDI OUT it is passed on to |

Set `THRU_RX_PIN` and `THRU_TX_PIN` to `-1` to monitor without passing anything on.

The console is UART0, the board's external USB-serial chip, separate from the MIDI UARTs.

## Connecting a real MIDI DIN

**The 5V current loop is outside this library.** A MIDI IN needs an optocoupler and a MIDI OUT needs a 220Ω pair. Do not wire a MIDI DIN socket straight to an ESP32 GPIO.

## What to look at

**The monitor is an application port.** It does not sit in the pass-through path; it is at the end of a *second* route from the same input. Slow printing cannot delay the sound.

**The clock is dropped at the monitor's output port.** It is 24 messages per beat and would fill the console, but the filter applies only to the monitor, so **the stream reaching the sound module is still complete**.

**SysEx is not copied.** `chunkData` points straight into the buffer it arrived in and is valid for the duration of the callback. A patch dump passes through untouched.
