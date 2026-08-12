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
        "CONTRIBUTING.ja.md",
        "CONTRIBUTING.md",
        "LICENSE",
        "src/EspMidi.h",
        "src/EspMidiEspUsbDevice.h",
        "src/EspMidiControl.h",
        "src/EspMidiEspBle.h",
        "src/EspMidiEspUsbHost.h",
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
        "docs/API.ja.md",
        "docs/API.md",
        "docs/FOOTPRINT.ja.md",
        "docs/FOOTPRINT.md",
        "docs/GUIDE.ja.md",
        "docs/GUIDE.md",
        "docs/MIDI_BASICS.ja.md",
        "docs/PORT_AUTHORING.ja.md",
        "docs/PORT_AUTHORING.md",
        "docs/RECIPES.ja.md",
        "docs/RECIPES.md",
        "docs/MIDI_BASICS.md",
        "docs/REQUIREMENTS.ja.md",
        "docs/DATA_MODEL.ja.md",
        "docs/DATA_MODEL.md",
        "docs/ROUTING.ja.md",
        "docs/ROUTING.md",
        "docs/CORE_DESIGN.ja.md",
        "docs/PORTS.ja.md",
        "docs/PORTS.md",
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
        "tests/unit/usb_host_port/usb_host_port_test.cpp",
        "tests/unit/usb_host_port/test_usb_host_port.py",
        "tests/unit/concurrent_receive/concurrent_receive_test.cpp",
        "tests/unit/concurrent_receive/test_concurrent_receive.py",
        "tests/unit/ble_port/ble_port_test.cpp",
        "tests/unit/ble_port/test_ble_port.py",
        "tests/unit/control_mapping/control_mapping_test.cpp",
        "tests/unit/control_mapping/test_control_mapping.py",
        "tests/unit/docs_snippets/docs_snippets_test.cpp",
        "tests/unit/docs_snippets/test_docs_snippets.py",
        "tests/arduino_smoke/arduino_smoke.ino",
        "tests/arduino_smoke/sketch.yaml",
        "tests/arduino_smoke/test_arduino_smoke.py",
        "tests/examples_compile/test_examples_compile.py",
        "tests/loopback/README.ja.md",
        "tests/loopback/uart_midi/uart_midi.ino",
        "tests/loopback/uart_midi/sketch.yaml",
        "tests/loopback/uart_midi/test_loopback_uart_midi.py",
        "tests/loopback/usb_host_device/usb_host_device.ino",
        "tests/loopback/usb_host_device/sketch.yaml",
        "tests/loopback/usb_host_device/test_loopback_usb_host_device.py",
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
        "tests/peer/usb_midi_host/usb_midi_host.ino",
        "tests/peer/usb_midi_host/sketch.yaml",
        "tests/peer/usb_midi_host/test_usb_midi_host.py",
        "tests/peer/usb_midi_host/peer_device/peer_device.ino",
        "tests/peer/usb_midi_host/peer_device/sketch.yaml",
        "tests/peer/ble_midi/ble_midi.ino",
        "tests/peer/ble_midi/sketch.yaml",
        "tests/peer/ble_midi/test_ble_midi.py",
        "tests/peer/ble_midi/peer_device/peer_device.ino",
        "tests/peer/ble_midi/peer_device/sketch.yaml",
        "tests/manual/README.ja.md",
        "tests/manual/control_mapping.ja.md",
        "tests/manual/uart_midi_din.ja.md",
        "tests/manual/usb_device_host_os.ja.md",
        "tests/manual/usb_host_real_devices.ja.md",
        "tests/manual/ble_midi_pairing.ja.md",
        "tests/manual/sysex_dump.ja.md",
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


# Documents that exist in both languages: everything a user of the library reads,
# plus the settled specification. Internal records and working notes stay Japanese
# only, deliberately — see the conventions in docs/README.ja.md.
BILINGUAL_DOCS = [
    "GUIDE",
    "MIDI_BASICS",
    "RECIPES",
    "API",
    "FOOTPRINT",
    "PORT_AUTHORING",
    "DATA_MODEL",
    "ROUTING",
    "PORTS",
    "README",
    "RELEASE_CHECKLIST",
]

JAPANESE_ONLY_DOCS = [
    "REQUIREMENTS",
    "USE_CASES",
    "CORE_DESIGN",
    "CONFIGURATION",
    "DECISIONS",
    "DEVELOPMENT_PLAN",
    "LIBRARY_REQUESTS",
]


def test_bilingual_docs_have_both_languages():
    # A translation that goes missing is worse than none: a reader following a
    # link lands nowhere. The pairing is checked in both directions.
    missing = []
    for name in BILINGUAL_DOCS:
        for suffix in [".ja.md", ".md"]:
            path = ROOT / "docs" / f"{name}{suffix}"
            if not path.exists():
                missing.append(path.name)

    assert missing == []


def test_japanese_only_docs_are_not_half_translated():
    # These are internal records. An English half would rot silently, so if one
    # appears it is either a mistake or a decision to promote the document — and
    # promoting it means adding it to BILINGUAL_DOCS above.
    unexpected = [
        f"{name}.md" for name in JAPANESE_ONLY_DOCS if (ROOT / "docs" / f"{name}.md").exists()
    ]

    assert unexpected == []


def test_bilingual_docs_are_cross_linked():
    # Each half links to the other, so a reader can switch language from wherever
    # they landed.
    missing = []
    for name in BILINGUAL_DOCS:
        if name == "README":
            continue  # the index pair links through the language table instead
        japanese = (ROOT / "docs" / f"{name}.ja.md").read_text(encoding="utf-8")
        english = (ROOT / "docs" / f"{name}.md").read_text(encoding="utf-8")
        if f"{name}.md" not in japanese:
            missing.append(f"{name}.ja.md does not link to the English version")
        if f"{name}.ja.md" not in english:
            missing.append(f"{name}.md does not link to the Japanese version")

    assert missing == []


def markdown_files():
    skip = {".git", "build", ".venv", "output", "__pycache__", "node_modules"}
    return [
        path
        for path in sorted(ROOT.rglob("*.md"))
        if not any(part in skip for part in path.relative_to(ROOT).parts)
    ]


def strip_code_blocks(text):
    # Fenced blocks are stripped before looking for links: a lambda capture like
    # [this](const auto &m) looks exactly like a markdown link.
    out = []
    fenced = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if not fenced:
            out.append(line)
    return "\n".join(out)


def test_every_internal_link_resolves():
    # There are more than twenty documents cross-linking each other, so a moved or
    # renamed file leaves a reader at a dead end. Nothing here checks the internet,
    # only that the repository is internally consistent.
    broken = []
    for path in markdown_files():
        text = strip_code_blocks(path.read_text(encoding="utf-8"))
        for label, target in re.findall(r"\[([^\]]*)\]\(([^)]+)\)", text):
            if target.startswith(("http://", "https://", "#", "mailto:")):
                continue
            relative = target.split("#")[0]
            if not relative:
                continue
            if not (path.parent / relative).exists():
                broken.append(f"{path.relative_to(ROOT)}: [{label}]({target})")

    assert broken == []


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
