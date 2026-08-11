import re


def test_public_header_builds_for_the_host(cpp_test):
    output = cpp_test("version_test.cpp")
    match = re.search(r"TEST done (\d+)/(\d+)", output)
    assert match, output
    assert match.group(1) == match.group(2), output
