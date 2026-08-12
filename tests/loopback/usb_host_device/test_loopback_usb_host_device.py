"""USB MIDI between two EspMidi ports on one ESP32-P4.

The board is a USB MIDI device and a USB MIDI host at the same time, with its two
connectors joined, so **both ends of a real USB cable are EspMidi ports in one
router**. It is the test this directory exists for: the library treats several
ports as one system, and here the whole system fits on one board.

The two ports invert their cable counts in opposite directions, and here that has
to cancel exactly — which is why the counts are asserted against each other rather
than against a constant.

Needs the P4 connected with its USB data lines wired; it is not part of the
always-connected rig.
"""


def test_loopback_usb_host_device(dut):
    dut.expect_exact("LOOPBACK_READY", timeout=30)
    dut.write("g")

    # The device declares 2 cables device-to-host and 3 host-to-device. Seen from
    # the host port those are 2 inputs and 3 outputs; from the device port, the
    # other way round.
    dut.expect_exact("DEVICE_PORTS in=3 out=2", timeout=30)
    dut.expect_exact("HOST_PORTS in=2 out=3", timeout=10)

    # Device to host, over the cable.
    dut.expect_exact("RX from=host port=1 90 3C 64", timeout=15)
    dut.expect_exact("RX from=host port=0 B0 07 40", timeout=15)

    # Host to device. Cable 2 exists only in this direction.
    dut.expect_exact("RX from=device port=2 90 40 50", timeout=15)
    dut.expect_exact("RX from=device port=0 B0 0B 20", timeout=15)

    # A dump each way. The framing bytes belong to the wire.
    dut.expect_exact("RX_SYSEX from=host port=1 7D 11 12", timeout=15)
    dut.expect_exact("RX_SYSEX from=device port=2 7D 21 22", timeout=15)

    dut.expect_exact("TEST_END ok", timeout=15)
    assert dut.expect_exact(["OK", "NG"]) == b"OK"
