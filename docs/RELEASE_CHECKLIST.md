# Release Checklist

[日本語](RELEASE_CHECKLIST.ja.md)

What to check before releasing `EspMidi`. Version bumps and releases follow the shared [arduino-library-release-toolkit](https://github.com/tanakamasayuki/arduino-library-release-toolkit); `tools/bump_version.py` and `.github/workflows/release.yml` are not edited in this repository (fix them in the toolkit and sync).

## Documentation

- The feature, port and platform tables in `README.ja.md` / `README.md` match the implementation.
- The status column in `docs/PORTS.ja.md` (**planned** / **implemented, hardware-verification pending** / **implemented and hardware-verified**) matches reality.
- The lists in `examples/README.ja.md` / `examples/README.md` match what is in `examples/` (checked automatically by `tests/unit/test_repository_structure.py`).
- The gap between the requirements in `docs/REQUIREMENTS.ja.md` and what is implemented can be explained.
- The specifications in `docs/DATA_MODEL.ja.md` / `docs/ROUTING.ja.md` match the implementation. If a specification changed, the reason is recorded in `docs/DECISIONS.ja.md`.
- The core / port / example boundaries and callback execution contexts in `docs/CORE_DESIGN.ja.md` match the implementation.
- `docs/DEVELOPMENT_PLAN.ja.md` reflects the current position and remaining work.
- The request states in `docs/LIBRARY_REQUESTS.ja.md` are current (proposed / implemented / withdrawn).
- `tests/README.ja.md` / `tests/TEST_PLAN.ja.md` match the current test layout and coverage.

## Metadata

- `name`, `sentence`, `paragraph`, `architectures` and `includes` in `library.properties` match what is published.
- `keywords.txt` lists the public classes, methods and constants (add entries when a port or API is added).
- The minimum dependency versions (EspUsbHost / EspUsbDevice / EspBle) agree across the README, `docs/PORTS.ja.md` and `examples/**/sketch.yaml`.
- `## Unreleased` in `CHANGELOG.md` covers everything going into this release (the bump moves it into a new version section).

## Tests

The three hardware-free suites also run in CI (`.github/workflows/tests.yml`), but run them locally before a release too.

```sh
cd tests
uv run pytest unit/ arduino_smoke/ examples_compile/
```

Run the hardware suites as far as the connected boards allow, and reflect the results in `tests/TEST_PLAN.ja.md`.

```sh
uv run --env-file .env pytest loopback/
uv run --env-file .env pytest peer/
```

Keep the `manual/` procedures in `tests/manual/README.ja.md` and out of the automated pass criteria.

## Releasing

1. Settle the contents of `## Unreleased` and the bump level (major / minor / patch).

   ```sh
   python tools/bump_version.py --preview
   ```

2. Run the `Release` workflow (workflow_dispatch) in GitHub Actions. The shared workflow will:
   - update the version in `library.properties`, move `## Unreleased` in `CHANGELOG.md` into a new version section, regenerate `src/espmidi_version.h`
   - commit and push to the default branch
   - recreate the `release` branch (rewrite `dir: ../../` in `examples/**/sketch.yaml` to `EspMidi (<version>)` and commit, remove `tests/`)
   - create the tag, ZIP and GitHub release

3. Confirm the final diff contains no build artifacts, caches or local-profile-specific changes.

## After the release

- Confirm the new version appears in the Arduino Library Manager (it can take a few hours).
- Confirm `examples/**/sketch.yaml` still uses the `dir:` reference on the default branch (the rewrite happens only on `release`).
