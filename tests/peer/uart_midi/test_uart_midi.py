"""UART MIDI across the peer pair, on the wiring that is already there.

The existing straight-through link (GPIO19 to GPIO19, GPIO20 to GPIO20, common
ground) becomes a crossover by giving the two roles opposite pins: no rewiring,
and at 31250 baud the series resistors in the USB data path do not matter.

What this adds over `loopback/uart_midi` is a second clock: each side resolves
the bit timing of a signal it did not generate.
"""


def test_uart_midi_dut_to_peer(dut, peers):
    device = peers["device"]
    # Both banners repeat until the board is spoken to: each is reset by the
    # flashing tool and its console is opened after that, so a sketch that says
    # it is ready once in setup() has said it before anyone is listening.
    dut.expect_exact("DUT_READY")
    device.expect_exact("PEER_READY")

    dut.write("n")
    dut.expect_exact("TX_NOTE_ON")
    device.expect_exact("RX 90 3C 64")

    dut.write("c")
    dut.expect_exact("TX_CC")
    device.expect_exact("RX B0 07 40")


def test_uart_midi_peer_to_dut(dut, peers):
    device = peers["device"]

    device.write("n")
    device.expect_exact("TX_NOTE_ON")
    dut.expect_exact("RX 90 40 50")

    device.write("c")
    device.expect_exact("TX_CC")
    dut.expect_exact("RX B0 0B 20")


def test_uart_midi_sysex_both_ways(dut, peers):
    device = peers["device"]

    # The 0xF0 and 0xF7 are added by the sending port and taken off by the
    # receiving one, so each side sees the payload the other handed over.
    dut.write("s")
    dut.expect_exact("TX_SYSEX")
    device.expect_exact("RX_SYSEX 7D 11 12")

    device.write("s")
    device.expect_exact("TX_SYSEX")
    dut.expect_exact("RX_SYSEX 7D 21 22")
