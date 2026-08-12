"""USB Device MIDI port across the peer pair.

DUT = the USB host, a plain EspUsbHost. Peer = EspMidi's USB Device port on an
asymmetric multi-cable EspUsbDevice: 2 cables device-to-host and 3 the other way.

The asymmetry is the point. EspUsbDevice names its cable counts from the host's
point of view, the opposite of this library's port directions, so a port that
mixed the two up could not be caught by a symmetric device — and not by any round
trip either, since a received packet's cable number is read out of its own header.
"""


def test_usb_midi_cable_counts(dut, peers):
    device = peers["device"]
    # Both banners repeat until the board is spoken to; see tests/TEST_PLAN.ja.md.
    dut.expect_exact("HOST_READY")
    device.expect_exact("PEER_READY")

    # This library's view: 3 input ports (the host's OUT cables) and 2 output
    # ports (the host's IN cables).
    device.write("?")
    device.expect_exact("PORTS in=3 out=2")

    # The host's view of the same device, decoded from the descriptor. This is the
    # only check that proves the device really advertised its cables.
    dut.write("i")
    dut.expect_exact("MIDI_PORT_INFO ok=1 in=2 out=3")


def test_usb_midi_host_to_device(dut, peers):
    device = peers["device"]

    # Cable 2 exists only host-to-device, so nothing but the right mapping puts it
    # on input port 2.
    dut.write("n")
    dut.expect_exact("MIDI_TX_CABLE2 1")
    device.expect_exact("RX port=2 90 3C 64")

    dut.write("c")
    dut.expect_exact("MIDI_TX_CABLE0 1")
    device.expect_exact("RX port=0 B0 07 40")


def test_usb_midi_device_to_host(dut, peers):
    device = peers["device"]

    device.write("0")
    device.expect_exact("TX port=0")
    dut.expect_exact("MIDI_RX cable=0 cin=09 status=90 data1=64 data2=80")

    device.write("1")
    device.expect_exact("TX port=1")
    dut.expect_exact("MIDI_RX cable=1 cin=0b status=b0 data1=11 data2=32")


def test_usb_midi_sysex_both_ways(dut, peers):
    device = peers["device"]

    # The framing bytes belong to the wire; a route sees the payload.
    dut.write("S")
    dut.expect_exact("MIDI_TX_SYSEX 1")
    device.expect_exact("RX_SYSEX port=2 7D 11 12")

    # Out on port 1, so the cable nibble has to be 1 in both packets.
    device.write("s")
    device.expect_exact("TX_SYSEX port=1")
    dut.expect_exact("MIDI_RX cable=1 cin=04 status=f0 data1=125 data2=33")
    dut.expect_exact("MIDI_RX cable=1 cin=06 status=22 data1=247 data2=0")


def test_usb_midi_undeclared_cable_is_dropped(dut, peers):
    device = peers["device"]

    dut.write("x")
    dut.expect_exact("MIDI_TX_CABLE7 1")
    device.write("u")
    device.expect_exact("UNKNOWN_CABLE 1")

    # And the port still works afterwards: the unknown cable was dropped, not
    # something that left the decoder confused.
    dut.write("c")
    dut.expect_exact("MIDI_TX_CABLE0 1")
    device.expect_exact("RX port=0 B0 07 40")
