"""A USB MIDI interface and a MIDI DIN circuit, joined into one loop.

**Run this one by hand.** The file is deliberately not named `test_*.py`, so
pytest does not collect it unless it is named on the command line:

    uv run --env-file .env pytest manual/usb_if_din/usb_if_din.py

The rig is not part of the always-connected bench. It is one ESP32-S3, one
commodity USB MIDI interface plugged into its USB host connector, and two MIDI
cables joining that interface's DIN jacks back to the board's own MIDI IN and
MIDI OUT circuits. Wiring and `.env` are in README.ja.md; from there on this
test runs on its own.

What makes the rig worth building: **the two directions travel over different
physical layers.** What leaves on the DIN comes back over USB, and what leaves
over USB comes back on the DIN. The UART port and the USB host port are checked
against each other inside one router, through a real optocoupler and a third
party's firmware.

If a step here fails while `loopback/uart_midi` passes, the library is not the
suspect — the circuit, the cables or the interface is (docs/HARDWARE.ja.md).
"""

import os


def test_usb_if_din(dut):
    # The GPIO numbers belong to the bench, not to the repository, so they are
    # handed over at run time rather than compiled in.
    tx_pin = int(os.environ["ESPMIDI_DIN_TX_PIN"])
    rx_pin = int(os.environ["ESPMIDI_DIN_RX_PIN"])

    # The board repeats its banner and waits to be asked: it is reset by the
    # flashing tool and the console is opened after that.
    dut.expect_exact("DIN_RIG_READY", timeout=30)
    dut.write(f"p {tx_pin} {rx_pin}")
    dut.expect_exact(f"PINS tx={tx_pin} rx={rx_pin}", timeout=10)

    # Nothing named the interface. Its seats exist because it was plugged in.
    dut.expect(r"USB_DEVICE in=(\d+) out=(\d+)", timeout=30)

    dut.write("g")

    # One message down the DIN before the test proper. An interface with a MIDI
    # Thru sends it straight back out of its DIN OUT, and then every message
    # travels both paths and the test can tell nothing apart — so it is named here
    # rather than left to be read out of a dozen WRONG_PATH lines.
    dut.expect_exact("RIG_OK", timeout=20)

    # Out on the DIN, back over USB: the MIDI OUT circuit, the cable, and the
    # interface's own MIDI IN.
    dut.expect_exact("RX_FROM_USB 90 3C 64", timeout=20)  # note on
    dut.expect_exact("RX_FROM_USB B0 07 40", timeout=20)  # control change
    dut.expect_exact("RX_FROM_USB E0 00 40", timeout=20)  # pitch bend
    dut.expect_exact("RX_FROM_USB F8", timeout=20)  # clock, a status byte alone

    # The 0xF0 and 0xF7 are framing the serializer adds and the parser takes off
    # again, so what arrives is the payload the sketch handed over.
    dut.expect_exact("RX_SYSEX_FROM_USB 7D 01 02", timeout=20)
    dut.expect_exact("RX_FROM_USB 80 3C 00", timeout=20)  # note off

    # Out over USB, back on the DIN: the interface's MIDI OUT drives this board's
    # optocoupler. Channel 1 throughout, so the two halves cannot be confused.
    dut.expect_exact("RX_FROM_DIN 91 40 50", timeout=20)
    dut.expect_exact("RX_FROM_DIN B1 0B 20", timeout=20)
    dut.expect_exact("RX_FROM_DIN E1 00 40", timeout=20)
    dut.expect_exact("RX_FROM_DIN F8", timeout=20)
    dut.expect_exact("RX_SYSEX_FROM_DIN 7D 11 12", timeout=20)
    dut.expect_exact("RX_FROM_DIN 81 40 00", timeout=20)

    dut.expect(r"COUNTERS recv=\d+ deliv=\d+ sendFailed=0 full=0", timeout=20)

    dut.expect_exact("TEST_END ok", timeout=20)
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
