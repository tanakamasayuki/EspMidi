import re


def test_uart_port(cpp_test):
    output = cpp_test("uart_port_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
