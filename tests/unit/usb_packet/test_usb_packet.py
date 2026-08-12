import re


def test_usb_packet(cpp_test):
    output = cpp_test("usb_packet_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
