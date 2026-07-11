# C FingerTree Documentation

- Status: Informational
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers and AI agents implementing the C port
- Scope: C port documentation under `src/C/FingerTree/docs`

## Current Documents

- [API notes](api-notes.md) records the measured-tree, RRB, and DABA Lite C API shapes, ownership rules, and
  active differences from the C++ port.
- [Usage guide](usage.md) shows public API setup, lifetime patterns, persistent update ownership, and facade quick starts.
- [Validation](validation.md) records local build, test, sample-smoke, benchmark, warning-policy, and
  generated-output guidance for the C workspace.
- [Tests README](../tests/README.md) maps the core, RRB, and DABA Lite CTest executables, named test cases, direct
  executable paths, and runner failure behavior.
