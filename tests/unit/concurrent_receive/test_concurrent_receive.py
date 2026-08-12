import re


def test_concurrent_receive(cpp_test):
    output = cpp_test("concurrent_receive_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
