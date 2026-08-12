import re


def test_port_model(cpp_test):
    output = cpp_test("port_model_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
