"""Shared fixtures for the EspMidi test suites.

The `cpp_test` fixture is the harness for `unit/`: it compiles a plain C++ file
against `src/` with the host compiler and runs it. The core is portable C++ with
no Arduino dependency (docs/CORE_DESIGN.ja.md), so the suite that fixes the
specification needs no Arduino toolchain and finishes in seconds.

The Arduino library build path is checked separately, in `arduino_smoke/`.
"""

import shutil
import subprocess
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = REPO_ROOT / "src"

# -Werror is deliberate: these are the library's own headers, and a warning in a
# header a sketch includes is a defect. -funsigned-char matches the Arduino
# toolchain so `char` signedness cannot change behaviour between host and target.
COMPILER_FLAGS = [
    "-std=c++17",
    "-funsigned-char",
    "-Wall",
    "-Wextra",
    "-Werror",
]


@pytest.fixture
def cpp_test(request):
    """Compile and run a C++ test file from the calling test's directory.

    Usage:

        def test_message(cpp_test):
            cpp_test("message_test.cpp")

    The binary goes into an `output/` directory next to the source, which is
    ignored by git. A non-zero exit status from either the compiler or the test
    binary fails the test with its full output attached.
    """

    def _run(source_name, extra_sources=()):
        if shutil.which("g++") is None:
            pytest.skip("g++ is not available")

        here = Path(request.path).parent
        source = here / source_name
        assert source.exists(), f"missing test source: {source}"

        output_dir = here / "output"
        output_dir.mkdir(exist_ok=True)
        binary = output_dir / source.stem

        command = [
            "g++",
            *COMPILER_FLAGS,
            "-I",
            str(SRC_DIR),
            str(source),
            *[str(here / name) for name in extra_sources],
            "-o",
            str(binary),
        ]
        compiled = subprocess.run(command, capture_output=True, text=True, check=False)
        assert compiled.returncode == 0, compiled.stderr

        ran = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
        assert ran.returncode == 0, ran.stdout + ran.stderr
        return ran.stdout

    return _run
