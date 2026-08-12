import re


def test_ble_port(cpp_test):
    output = cpp_test("ble_port_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
