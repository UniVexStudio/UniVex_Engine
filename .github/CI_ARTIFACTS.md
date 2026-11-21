# What CI uploads

One workflow, `.github/workflows/ci.yml`, checks every push and makes every release.

## Releases

A release is a push of the tag `v<version>` or of a branch named `release/v<version>`. The `linux`,
`windows` and `android` jobs build as on any push; `linux-release` adds the Linux Release build, and
`publish` puts everything into one GitHub Release:

- `univex-engine-<version>-<linux|windows>.uvpackage` - the engine, per platform
- `UniVexHubSetup-<hub version>-<linux|windows>` - the Hub installers
- `UniVexHub-<hub version>-android-<abi>.apk` - the Hub for Android

each with a `.sha256` beside it. The files travel between jobs as artifacts, which are deleted once
the release is published (and kept for seven days if publishing fails, so the builds are not lost).

The repository variable `PUBLISH_RELEASES` decides where the release goes:

- not `true` (default) - the development repository (`DEV_RELEASES_REPOSITORY`). The Hub does not
  read it; install its engines with the Hub's **Engines → Import package...**.
- `true` - the repository named in `Engine/Distribution/engine_feed.json`, which the Hub reads.

## Measurements - off by default

The repository variable `DIAGNOSTIC_ARTIFACTS` set to `true` uploads the test log of a failed Linux
run. Nothing else changes when it is off; errors and test results remain in the job log.

## Everything else

Checks and builds do not upload anything. The Windows job still stages the editor with its DLLs and
starts it, so the package a release would ship is proven to run.
