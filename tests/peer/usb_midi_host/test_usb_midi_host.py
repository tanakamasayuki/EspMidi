"""USB Host MIDI port across the peer pair.

Both ends are EspMidi: the DUT runs the USB Host port and the peer the USB Device
port, the same asymmetric 2/3 device as `peer/usb_midi`. That test keeps its plain
`EspUsbHost` observer so the device half stays proven against something that does
not share code with it; this one adds what it cannot show — **the dynamic side**.
The DUT names no device in advance. Seats appear when one is plugged in, and the
route is from `InGroup::all()`.

The port counts the two boards report are mirror images, which is the whole point:
EspUsbHost's counts are already host-view so the host port does not invert them,
while the device port does.
"""


def test_usb_midi_host_discovers_the_device(dut, peers):
    device = peers["device"]
    dut.expect_exact("HOST_READY")
    device.expect_exact("PEER_READY")

    device.write("?")
    device.expect_exact("PORTS in=3 out=2")

    # The same device seen from the host: 2 cables arriving, 3 leaving. No seat was
    # declared by the sketch — the port found the device and made them.
    dut.write("d")
    dut.expect_exact("DEVICES 1")
    dut.expect_exact("DEVICE in=2 out=3")


def test_usb_midi_host_to_device(dut, peers):
    device = peers["device"]

    # Output port 2 exists only because the device declared 3 host-to-device
    # cables. A mixed-up direction would have no such port.
    dut.write("n")
    dut.expect_exact("TX port=2")
    device.expect_exact("RX port=2 90 3C 64")

    dut.write("c")
    dut.expect_exact("TX port=0")
    device.expect_exact("RX port=0 B0 07 40")


def test_usb_midi_device_to_host(dut, peers):
    device = peers["device"]

    device.write("0")
    device.expect_exact("TX port=0")
    dut.expect_exact("RX port=0 90 40 50")

    device.write("1")
    device.expect_exact("TX port=1")
    dut.expect_exact("RX port=1 B0 0B 20")


def test_usb_midi_sysex_both_ways(dut, peers):
    device = peers["device"]

    # EspUsbHost hands over one packet at a time and does not join a dump back
    # together, so the concatenation on this side is the core decoder's work.
    device.write("s")
    device.expect_exact("TX_SYSEX port=1")
    dut.expect_exact("RX_SYSEX port=1 7D 21 22")

    dut.write("s")
    dut.expect_exact("TX port=1")
    device.expect_exact("RX_SYSEX port=1 7D 11 12")


def test_usb_midi_host_diagnostics_are_clean(dut, peers):
    # Nothing should have been dropped or landed on a cable nobody declared. This
    # is the check that says the round trips above were not partly luck.
    dut.write("x")
    dut.expect_exact("DIAG unknown=0 dropped=0 refused=0")
