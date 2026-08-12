"""BLE MIDI across the peer pair, over the air.

DUT = EspMidi's BLE **Host** port. Peer = EspMidi's BLE **Device** port. No wiring
is involved, but it is the same two boards.

**One test function**, per the convention in `../TEST_PLAN.ja.md`: a BLE link is
stateful and expensive to establish, so pairing up once and asserting along the way
is more deterministic than reconnecting per test. Nothing here bonds, so there is
no bond state to clear between runs.

What this adds over the unit tests is the part no stand-in can show: a real BLE
link, a real service discovery, and **a real timestamp** — BLE MIDI is the only
transport that carries one, and `unit=1` is `TimestampUnit::Milliseconds13`.
"""

import re
import time

DEVICES_PATTERN = re.compile(rb"DEVICES (\d+)")
AVAILABLE_PATTERN = re.compile(rb"PEER_AVAILABLE (\d)")


def _wait_for_seat(dut, attempts=30):
    for _ in range(attempts):
        dut.write("r")
        match = dut.expect(DEVICES_PATTERN, timeout=10)
        if int(match.group(1)) >= 1:
            return True
        time.sleep(0.5)
    return False


def _wait_for_subscription(device, attempts=20):
    for _ in range(attempts):
        device.write("r")
        match = device.expect(AVAILABLE_PATTERN, timeout=10)
        if match.group(1) == b"1":
            return True
        time.sleep(0.5)
    return False


def test_ble_midi(dut, peers):
    device = peers["device"]
    dut.expect_exact("HOST_READY", timeout=20)
    device.expect_exact("PEER_READY", timeout=20)

    device.write("?")
    device.expect_exact("ADVERTISING 1", timeout=20)

    dut.write("z")
    dut.expect_exact("SCAN_STARTED", timeout=10)

    # The seat appears when the MIDI service has been discovered and subscribed —
    # not when the link comes up. The sketch never asked for a seat; the port made
    # one and the route from InGroup::all() picked it up.
    assert _wait_for_seat(dut), "the host port never seated the connection"
    assert _wait_for_subscription(device), "the device port never became available"

    # Host to device. unit=1 is TimestampUnit::Milliseconds13.
    dut.write("n")
    dut.expect_exact("TX ok", timeout=10)
    device.expect_exact("RX 90 3C 64 unit=1", timeout=10)

    dut.write("c")
    dut.expect_exact("TX ok", timeout=10)
    device.expect_exact("RX B0 07 40 unit=1", timeout=10)

    # Device to host.
    device.write("0")
    device.expect_exact("TX_NOTE", timeout=10)
    dut.expect_exact("RX 90 40 50 unit=1", timeout=10)

    device.write("1")
    device.expect_exact("TX_CC", timeout=10)
    dut.expect_exact("RX B0 0B 20 unit=1", timeout=10)

    # SysEx both ways. EspBle splits a dump across BLE packets and reassembles the
    # incoming one, so what each side hands over and receives is the payload.
    dut.write("y")
    dut.expect_exact("TX ok", timeout=10)
    device.expect_exact("RX_SYSEX 7D 11 12", timeout=15)

    device.write("y")
    device.expect_exact("TX_SYSEX", timeout=10)
    dut.expect_exact("RX_SYSEX 7D 21 22", timeout=15)

    # Nothing was refused or dropped, so the round trips above were not partly luck.
    dut.write("x")
    dut.expect_exact("DIAG oversized=0 events=0 refused=0", timeout=10)
    device.write("x")
    device.expect_exact("DIAG oversized=0 failed=0", timeout=10)
