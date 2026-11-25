# Desktop Launch Contract (v1)

On Linux and Windows, UniVex Hub launches the selected Engine Editor as a separate child process.

## 1. Executable Resolution

Inside an engine installation directory `<installRoot>`:

- Linux: `<installRoot>/uve_editor`
- Windows: `<installRoot>/uve_editor.exe`

The working directory for launch is `<installRoot>` so relative `prism_assets/`, `shaders/`, and bundled runtime libraries resolve cleanly.

## 2. Command-Line Arguments

- **Version probe**:
  ```
  uve_editor --version
  ```
  Prints the engine display version (e.g. `0.1.2` or `0.1.2+build.0`) on stdout and exits with status `0`. Used by the Hub when probing a local sibling engine build that has no `engine.json`.

- **Open project**:
  ```
  uve_editor --project <absolute-path-to-project.uvproject> [--project-registry <path>]
  ```
  - `--project`: absolute path to the target `.uvproject` file.
  - `--project-registry`: passed only when the Hub itself was launched with an isolated `--project-registry <path>` override.

## 3. Hub CLI Options

UniVex Hub (`uve_hub` / `uve_hub.exe`) exposes:

- `--version`: prints `kHubVersion` and exits `0`.
- `--engine-root <dir>`: treats `<dir>` as a local development engine build (reading `engine.json` or probing `uve_editor --version`).
- `--install-package <file.uvpackage>`: installs an offline engine package (verifying `<file>.sha256` if present) and exits.
- `--project-registry <file>` / `--settings <file>`: overrides per-user registry/settings paths for isolated testing.
- `--page <projects|new-project|engines|settings>`: selects the starting page.
- `--frames <n>` / `--screenshot <png>`: runs a bounded automated session and optionally saves a screenshot.
- `<path/to/file.uvproject>`: positional `.uvproject` path opens directly in the matching installed engine.
