# What CI uploads

One workflow, `.github/workflows/ci.yml`, checks every push and makes every release.

## Releases

Every push to `master` is a development release, and a tag `v<version>` is a versioned release.
Every job builds as on any check; the Release jobs then package what they built, and
**Publish the release** puts the engine packages into one GitHub Release:

- `univex-engine-<version>-<linux|windows|android-arm64-v8a|android-x86_64>.uvpackage` - the engine,
  per platform. The Android packages ship the in-process editor library (`uve_editor`) and Android
  runtime library (`libunivex_android.so`), which UniVex Hub loads via `dlopen` per `contracts/v1/`;
  `Release / Android` builds and verifies ELF symbol isolation, and `Publish the release` packages
  them with the release's tag.

each with a `.sha256` beside it. UniVex Hub installers (`UniVexHubSetup-*`, `.deb`, and signed
`UniVexHub-*.apk`) are built and released separately from `UniVexStudio/UniVex_Hub`. The files
travel between jobs as artifacts, which are deleted once the release is published (and kept for
seven days if publishing fails, so the builds are not lost).

The repository variable `PUBLISH_RELEASES` decides where the release goes:

- not `true` (default) - the development repository (`DEV_RELEASES_REPOSITORY`). The Hub does not
  read it; install its engines with the Hub's **Engines → Import package...**.
- `true` - tag releases go to the repository named in `Engine/Distribution/engine_feed.json`, which
  the Hub reads. Master builds always stay in the development repository, as the prerelease
  `v<engine version>-beta`, whose files each later push of that version replaces.

## Measurements - off by default

The repository variable `DIAGNOSTIC_ARTIFACTS` set to `true` uploads the test log of a failed Linux
run. Nothing else changes when it is off; errors and test results remain in the job log.

## Everything else

Checks and builds do not upload anything. Release / Windows still stages the editor with its DLLs and
starts it, so the package a release would ship is proven to run.
