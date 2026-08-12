"""UART MIDI across one board, with no wiring.

UART1 transmits and UART2 receives, both on the same pin through the GPIO
matrix. What is checked here is only what the host tests cannot reach: that the
bytes actually cross the pad and come back as the same messages. The port's own
behaviour is fixed in `unit/uart_port`.
"""


def test_loopback_uart_midi(dut):
    dut.expect_exact("LOOPBACK_READY")

    dut.expect_exact("RX 90 3C 64")  # note on
    dut.expect_exact("RX B0 07 40")  # control change
    dut.expect_exact("RX F8")  # clock, a status byte with no data
    dut.expect_exact("RX E0 00 40")  # pitch bend

    # The 0xF0 and 0xF7 are framing the serializer adds and the parser takes off
    # again, so what arrives is the payload the sketch handed over.
    dut.expect_exact("RX_SYSEX 7D 01 02")

    dut.expect_exact("RX 80 3C 00")  # note off

    dut.expect_exact("TEST_END ok")
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
