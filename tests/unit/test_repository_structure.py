"""Repository structure checks.

These need neither hardware nor a compiler. They exist so documentation cannot
rot silently: a missing design document, an example that is not listed in the
README, or a version header that drifted from library.properties all fail here.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def test_required_project_files_exist():
    required = [
        "README.ja.md",
        "README.md",
        "library.properties",
        "keywords.txt",
        "CHANGELOG.md",
        "LICENSE",
        "src/EspMidi.h",
        "src/EspMidiEspUsbDevice.h",
        "src/EspMidiFilter.h",
        "src/EspMidiMessage.h",
        "src/EspMidiParser.h",
        "src/EspMidiPort.h",
        "src/EspMidiRouter.h",
        "src/EspMidiUart.h",
        "src/EspMidiUsbPacket.h",
        "src/espmidi_version.h",
        "tools/bump_version.py",
        ".github/workflows/tests.yml",
        ".github/workflows/release.yml",
        "examples/README.ja.md",
        "examples/README.md",
        "docs/README.ja.md",
        "docs/README.md",
        "docs/REQUIREMENTS.ja.md",
        "docs/DATA_MODEL.ja.md",
        "docs/ROUTING.ja.md",
        "docs/CORE_DESIGN.ja.md",
        "docs/PORTS.ja.md",
        "docs/USE_CASES.ja.md",
        "docs/DECISIONS.ja.md",
        "docs/CONFIGURATION.ja.md",
        "docs/DEVELOPMENT_PLAN.ja.md",
        "docs/LIBRARY_REQUESTS.ja.md",
        "docs/RELEASE_CHECKLIST.ja.md",
        "docs/RELEASE_CHECKLIST.md",
        "tests/README.ja.md",
        "tests/TEST_PLAN.ja.md",
        "tests/conftest.py",
        "tests/unit/README.ja.md",
        "tests/unit/version/version_test.cpp",
        "tests/unit/version/test_version.py",
        "tests/unit/message/message_test.cpp",
        "tests/unit/message/test_message.py",
        "tests/unit/parser/parser_test.cpp",
        "tests/unit/parser/test_parser.py",
        "tests/unit/usb_packet/usb_packet_test.cpp",
        "tests/unit/usb_packet/test_usb_packet.py",
        "tests/unit/port_model/port_model_test.cpp",
        "tests/unit/port_model/test_port_model.py",
        "tests/unit/routing/routing_test.cpp",
        "tests/unit/routing/test_routing.py",
        "tests/unit/sysex_rules/sysex_rules_test.cpp",
        "tests/unit/sysex_rules/test_sysex_rules.py",
        "tests/unit/filter/filter_test.cpp",
        "tests/unit/filter/test_filter.py",
        "tests/unit/transform/transform_test.cpp",
        "tests/unit/transform/test_transform.py",
        "tests/unit/serializer/serializer_test.cpp",
        "tests/unit/serializer/test_serializer.py",
        "tests/unit/uart_port/uart_port_test.cpp",
        "tests/unit/uart_port/test_uart_port.py",
        "tests/unit/usb_device_port/usb_device_port_test.cpp",
        "tests/unit/usb_device_port/test_usb_device_port.py",
        "tests/arduino_smoke/arduino_smoke.ino",
        "tests/arduino_smoke/sketch.yaml",
        "tests/arduino_smoke/test_arduino_smoke.py",
        "tests/examples_compile/test_examples_compile.py",
        "tests/loopback/README.ja.md",
        "tests/loopback/uart_midi/uart_midi.ino",
        "tests/loopback/uart_midi/sketch.yaml",
        "tests/loopback/uart_midi/test_loopback_uart_midi.py",
        "tests/peer/README.ja.md",
        "tests/peer/README.md",
        "tests/peer/uart_midi/uart_midi.ino",
        "tests/peer/uart_midi/sketch.yaml",
        "tests/peer/uart_midi/test_uart_midi.py",
        "tests/peer/uart_midi/peer_device/peer_device.ino",
        "tests/peer/uart_midi/peer_device/sketch.yaml",
        "tests/peer/usb_midi/usb_midi.ino",
        "tests/peer/usb_midi/sketch.yaml",
        "tests/peer/usb_midi/test_usb_midi.py",
        "tests/peer/usb_midi/peer_device/peer_device.ino",
        "tests/peer/usb_midi/peer_device/sketch.yaml",
        "tests/manual/README.ja.md",
        "tests/manual/README.md",
    ]

    missing = [path for path in required if not (ROOT / path).exists()]

    assert missing == []


def test_memo_is_replaced_by_the_requirements_document():
    # memo.ja.md was the draft the requirements were raised from. It is deleted
    # once docs/REQUIREMENTS.ja.md exists so there is only one source of truth.
    assert not (ROOT / "memo.ja.md").exists()


def test_library_properties_names_public_header():
    library_properties = (ROOT / "library.properties").read_text(encoding="utf-8")

    assert "name=EspMidi" in library_properties
    assert "includes=EspMidi.h" in library_properties


def test_version_header_matches_library_properties():
    library_properties = (ROOT / "library.properties").read_text(encoding="utf-8")
    header = (ROOT / "src" / "espmidi_version.h").read_text(encoding="utf-8")

    version = re.search(r"^version=(.+)$", library_properties, re.MULTILINE)
    assert version, "version property not found in library.properties"

    assert f'#define ESPMIDI_VERSION_STR "{version.group(1)}"' in header


def readme_section(readme, title):
    return readme.split(f"## {title}", 1)[1].split("\n## ", 1)[0]


def test_examples_readmes_list_current_examples():
    example_dirs = sorted(path.parent.name for path in (ROOT / "examples").glob("*/*.ino"))
    for readme_name in ["README.ja.md", "README.md"]:
        readme = (ROOT / "examples" / readme_name).read_text(encoding="utf-8")
        listed = sorted(re.findall(r"- `([^`]+)`:", readme_section(readme, "Examples")))

        assert listed == example_dirs, readme_name


def test_examples_are_grouped_with_docs_and_compile_profiles():
    missing = []
    for ino in sorted((ROOT / "examples").rglob("*.ino")):
        example_dir = ino.parent
        # examples/<Name>/<Name>.ino, so the sketch and its directory agree and
        # arduino-cli can compile the directory by name.
        assert ino.stem == example_dir.name, str(ino)
        for required in ["README.ja.md", "README.md", "sketch.yaml"]:
            if not (example_dir / required).exists():
                missing.append(str((example_dir / required).relative_to(ROOT)))

    assert missing == []


def test_docs_guide_references_every_design_document():
    # docs/README.ja.md is the entry point. A design document that nothing links
    # to is a document nobody finds.
    guide = (ROOT / "docs" / "README.ja.md").read_text(encoding="utf-8")
    unreferenced = [
        path.name
        for path in sorted((ROOT / "docs").glob("*.ja.md"))
        if path.name != "README.ja.md" and path.name not in guide
    ]

    assert unreferenced == []
