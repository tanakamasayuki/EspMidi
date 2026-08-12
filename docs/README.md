# Documentation Guide

[日本語](README.ja.md)

Which document to read, and in what order. The design documents are Japanese only (`*.ja.md`); the READMEs, the release checklist and the examples are bilingual.

## Start here

| What you want | Document |
| --- | --- |
| Learn what the library does and see a working sketch | [../README.md](../README.md) |
| **Start using it, one port at a time** | **[GUIDE.md](GUIDE.md)** → [../examples/SimpleMidiOut/](../examples/SimpleMidiOut/) |
| **Look something up by what you want to do** | **[RECIPES.md](RECIPES.md)** |
| **Look up a name and its arguments** | **[API.md](API.md)** |
| How much RAM it costs, and what to cut | [FOOTPRINT.md](FOOTPRINT.md) |
| **Hit something in MIDI itself, or a per-interface caveat** | **[MIDI_BASICS.md](MIDI_BASICS.md)** |
| **Build a MIDI DIN circuit (the 3.3 V resistor values)** | **[HARDWARE.md](HARDWARE.md)** |
| Work out why nothing is arriving | the troubleshooting section of [GUIDE.md](GUIDE.md) |
| Find a sketch for your setup | [../examples/README.md](../examples/README.md) — every example is a complete, flashable sketch |
| Choose the ports you need and learn their limits | [PORTS.md](PORTS.md) |
| Understand why the MIDI message and port model look like this | [DATA_MODEL.md](DATA_MODEL.md) |
| Learn the routing rules (including SysEx and loop prevention) | [ROUTING.md](ROUTING.md) |
| **Write your own port** | **[PORT_AUTHORING.md](PORT_AUTHORING.md)** → `src/EspMidiUart.h` |
| Contribute | [../CONTRIBUTING.md](../CONTRIBUTING.md) |
| See the current position and remaining work | [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) and the status column in [PORTS.ja.md](PORTS.ja.md) |
| Understand why it is designed this way | [DECISIONS.ja.md](DECISIONS.ja.md) |
| See the outlook for MIDI 2.0 | decision 1 in [DECISIONS.ja.md](DECISIONS.ja.md) and the MIDI 2.0 section of [DATA_MODEL.md](DATA_MODEL.md) |

**What is available in English** is everything a user of the library reads, plus the settled specification:

| Group | Languages | Documents |
| --- | --- | --- |
| For users | **both** | the READMEs, [GUIDE](GUIDE.md), [MIDI_BASICS](MIDI_BASICS.md), [HARDWARE](HARDWARE.md), [RECIPES](RECIPES.md), [API](API.md), [FOOTPRINT](FOOTPRINT.md), [PORT_AUTHORING](PORT_AUTHORING.md), the examples, the release checklist |
| **The settled specification** | **both** | [DATA_MODEL](DATA_MODEL.md), [ROUTING](ROUTING.md), [PORTS](PORTS.md) |
| Internal records and working notes | Japanese only | REQUIREMENTS, USE_CASES, CORE_DESIGN, CONFIGURATION, DECISIONS, DEVELOPMENT_PLAN, LIBRARY_REQUESTS, the test plan |

**The Japanese version is the original**; the English ones follow it.

## For users

- [GUIDE.md](GUIDE.md) — the usage guide: from sending on a single port up to routing between several, with a troubleshooting section and how to read the diagnostic counters.
- [MIDI_BASICS.md](MIDI_BASICS.md) — the caveats of MIDI itself (velocity 0, running status, 0-based channels, bandwidth) and per-interface notes (DIN isolation, cables, enumeration, BLE latency and limits).
- [HARDWARE.md](HARDWARE.md) — the MIDI DIN circuit: **the 3.3 V resistor values**, isolation, cable and EMI provisions, summarised from the specification (CA-033) with links to the originals.
- [RECIPES.md](RECIPES.md) — fragments indexed by what you want to do. **All of the code is compiled.**
- [API.md](API.md) — the public API, for looking up a name and its meaning.
- [FOOTPRINT.md](FOOTPRINT.md) — **measured** RAM and latency, and what can be cut.
- [PORT_AUTHORING.md](PORT_AUTHORING.md) — the contract for writing a port. **A port can live outside this repository.**

## All documents

**Design (read in this order for the whole picture)**

1. [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) — what the library is for and what it covers, including the non-goals.
2. [USE_CASES.ja.md](USE_CASES.ja.md) — the concrete scenarios used to validate the design, each with the rules it settled.
3. [DATA_MODEL.ja.md](DATA_MODEL.ja.md) — the intermediate representation and the port model.
4. [ROUTING.ja.md](ROUTING.ja.md) — routes, the three-stage pipeline, queue-based driving, the three SysEx rules, loop prevention.
5. [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) — the core / port / example boundaries, the dependency rules, and the time and concurrency boundaries.
6. [PORTS.ja.md](PORTS.ja.md) — every port: behaviour, implementation status, dependencies, and how it looks from the PC.
7. [CONFIGURATION.ja.md](CONFIGURATION.ja.md) — notes on the configuration model and why storage and configuration UIs live outside the core.
8. [DECISIONS.ja.md](DECISIONS.ja.md) — the design decision ledger, including the options that were rejected and why.

**Process**

- [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) — implementation order, current position, remaining work.
- [LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md) — the ledger of change requests to the transport libraries, including what was checked and found sufficient.
- [RELEASE_CHECKLIST.ja.md](RELEASE_CHECKLIST.ja.md) / [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) — pre-release checks and how the shared release workflow behaves.
- [../tests/README.ja.md](../tests/README.ja.md) — the test layout (`unit/`, `arduino_smoke/`, `examples_compile/`, `loopback/`, `peer/`, `manual/`) and how to run it.
- [../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) — the coverage table: what is checked where, and what is not started.
