# Documentation Guide

[日本語](README.ja.md)

Which document to read, and in what order. The design documents are Japanese only (`*.ja.md`); the READMEs, the release checklist and the examples are bilingual.

## Start here

| What you want | Document |
| --- | --- |
| Learn what the library does and see a working sketch | [../README.md](../README.md) |
| Find a sketch for your setup | [../examples/README.md](../examples/README.md) — every example is a complete, flashable sketch |
| Choose the ports you need and learn their limits | [PORTS.ja.md](PORTS.ja.md) |
| Understand why the MIDI message and port model look like this | [DATA_MODEL.ja.md](DATA_MODEL.ja.md) |
| Learn the routing rules (including SysEx and loop prevention) | [ROUTING.ja.md](ROUTING.ja.md) |
| Write your own port | the comments in `src/EspMidi.h` → `EspMidiUart.h` → [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) |
| See the current position and remaining work | [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) and the status column in [PORTS.ja.md](PORTS.ja.md) |
| Understand why it is designed this way | [DECISIONS.ja.md](DECISIONS.ja.md) |
| See the outlook for MIDI 2.0 | decision 1 in [DECISIONS.ja.md](DECISIONS.ja.md) and the MIDI 2.0 section of [DATA_MODEL.ja.md](DATA_MODEL.ja.md) |

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
