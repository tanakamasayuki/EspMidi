import re


def test_sysex_rules(cpp_test):
    output = cpp_test("sysex_rules_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
