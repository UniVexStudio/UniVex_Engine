# What CI uploads

One workflow, `.github/workflows/ci.yml`, checks every push and makes every release.

## Releases

Every push to `master` is a development release, and a tag `v<version>` is a versioned release.
Every job builds as on any check; the Hub and Release jobs then package what they built, and
**Publish the release** puts everything into one GitHub Release:

- `univex-engine-<version>-<linux|windows|android>.uvpackage` - the engine, per platform. The
  Android package is the editor as a library (`uve_editor`), which UniVex Hub on Android loads to
  open a project; Release / Android builds it and Publish packages it with the release's tag
- `UniVexHubSetup-<hub version>-<linux|windows>` - the Hub installers: on Windows a setup wizard
  (Inno Setup, `Engine/Distribution/windows/UniVexHub.iss`), which Hub / Windows installs, starts
  and uninstalls on every run with only Windows' own folders on `PATH`
- `UniVexHub-<hub version>-android-<abi>.apk` - the Hub for Android

each with a `.sha256` beside it. The files travel between jobs as artifacts, which are deleted once
the release is published (and kept for seven days if publishing fails, so the builds are not lost).

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
