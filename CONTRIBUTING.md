# Contributing

[日本語](CONTRIBUTING.ja.md)

How work is done in this repository, and what it promises. The design background starts at [docs/README.md](docs/README.md).

## Running the tests

```sh
cd tests
uv run pytest unit/            # no hardware, a few seconds
```

`unit/` runs on `g++` alone. **No arduino-cli, no board package.** `unit/version` is compiled with no Arduino core on the include path, so **it fails the moment the core starts depending on Arduino.**

```sh
uv run pytest arduino_smoke/      # does it resolve as an Arduino library?
uv run pytest examples_compile/   # do all the examples build?
```

The ones that need hardware want a `.env` first (copy `.env.example` and fix the ports).

```sh
uv run --env-file .env pytest loopback/   # one board; UART needs no wiring
uv run --env-file .env pytest peer/       # two boards
```

**`peer/` flashes the always-connected boards.** They are shared with sibling projects, so running it overwrites their firmware.

## Where a test belongs

| What you are checking | Where |
| --- | --- |
| the specification (messages, routing, filters, **a port's behaviour**) | `unit/` |
| that it resolves as an Arduino library | `arduino_smoke/` |
| that the examples build | `examples_compile/` |
| a round trip across several ports on one board | `loopback/` |
| a port boundary between two boards | `peer/` |
| anything needing human eyes or hands | `manual/` (procedures only; **never mixed into the automated pass criteria**) |

**Push as much as possible into `unit/`.** A port made into a template with a stand-in can be fixed there too (see [docs/PORT_AUTHORING.md](docs/PORT_AUTHORING.md)). What is left for hardware should be **only what hardware can show**.

## Writing a sketch for a hardware test

**Do not print results from `setup()`.** The flashing tool resets the board and the console is opened afterwards, so anything said in the first moments is gone before anyone is listening — leaving an empty log.

- Announce readiness **repeatedly**.
- Run the real thing only when the host asks. **Match one specific character**: starting on any byte means a stray one from the flashing tool releasing the line starts the run, and everything finishes before the console opens (this actually happened on an ESP32-P4).

## Code conventions

- **The core depends on no Arduino, no ESP-IDF and no hardware.** Anything included from `src/EspMidi.h` stays portable C++.
- **No heap.** Storage is fixed-size, with limits changeable through `ESPMIDI_*`.
- **No `std::function` on a per-message path.** Use a function pointer plus a `void *context` (notifications run from a transport's task during device enumeration).
- **No exceptions, no RTTI.**
- Compiled with `-std=c++17 -funsigned-char -Wall -Wextra -Werror`. **A warning in a header is a defect.**
- Never drop the namespace. Even documentation examples write `espmidi::`.

### Comments

**Say why, not what** — and especially, make the option that was not taken visible.

```cpp
// Note off rather than a note on with velocity 0: both stop the note, and
// the explicit one is what a receiver with release velocity expects.
```

A comment that only restates a declaration does not get written.

## Documentation conventions

There are three tiers of translation ([docs/README.md](docs/README.md)).

| Group | Languages |
| --- | --- |
| For users (README, GUIDE, MIDI_BASICS, RECIPES, API, FOOTPRINT, PORT_AUTHORING, examples) | **both** |
| The settled specification (DATA_MODEL, ROUTING, PORTS) | **both** |
| Internal records and working notes (REQUIREMENTS, USE_CASES, CORE_DESIGN, CONFIGURATION, DECISIONS, DEVELOPMENT_PLAN, LIBRARY_REQUESTS, the test plan) | Japanese only |

**The Japanese version is the original.** `tests/unit/test_repository_structure.py` fixes the pairing, so adding only one half fails.

- **Design documents state the specification and not the implementation status.** Status lives in the status columns of `DEVELOPMENT_PLAN.ja.md` and `PORTS.ja.md`.
- **User-facing documents do not state the specification**; they link to it. The same thing written twice means one copy goes stale.
- **Code in documents is compiled by `tests/unit/docs_snippets`.** Renaming an API breaks that test, so documents cannot rot silently.
- When the design changes, record the reasoning and the rejected alternative in `DECISIONS.ja.md`.

## Example conventions

**Every example is practical**: a sketch that can be flashed as it is.

```text
examples/<Name>/
  <Name>.ino      the sketch name matches its directory
  README.ja.md
  README.md
  sketch.yaml     with default_profile set
```

- They all follow the same three steps: 1) start the stacks, 2) create the ports, 3) build the routes.
- The structure test fixes that the README list matches what is there.
- Sibling libraries are **pinned to published versions**, with a `*_local` profile alongside for trying a development checkout.

## Releasing

Follow [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md). `tools/bump_version.py` and `.github/workflows/release.yml` are **copied from the shared toolkit and are not edited here**.

The `manual/` procedures have to be **walked through by a person before a release**. They are the parts deliberately not automated.
