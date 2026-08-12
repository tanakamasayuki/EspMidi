import re


def test_control_mapping(cpp_test):
    output = cpp_test("control_mapping_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
