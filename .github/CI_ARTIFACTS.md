# What CI uploads

One workflow (`.github/workflows/ci.yml`), two switches. They answer different
questions, so they default differently.

## Builds somebody can run - on by default

`CREATE_ARTIFACTS` gates the upload that is a build you can actually try:

- `UniVex-Editor-Windows-x64` - the Windows editor package, with the DLLs its exes
  import collected beside it, from the `windows-editor` job. Unzip it, run
  `Start-Editor.bat`, and you are looking at the editor that CI just built.

Set the Actions repository variable `CREATE_ARTIFACTS` to `false` to stop it, or pass
`create_artifacts=false` on a manual run (Actions, select CI, Run workflow). The manual
checkbox overrides the repository variable for that run. This is a variable, not a
secret.

The Android apk is not a CI artifact any more. It is a runtime with no editor in it,
built locally with `Engine/Tools/android/build_apk.py` and inspected with
`smoke_runtime.py` when somebody is working on mobile - see
`Engine/Tools/android/README.md`. Building it for two ABIs and watching it draw on an
emulator took about half an hour of every pull request, which is the wrong price for a
build nobody could edit anything with.

## Measurements - off by default

`DIAGNOSTIC_ARTIFACTS` gates the uploads that answer questions about a run instead of
giving anybody something to run:

- `ctest-<mode>` - the full GoogleTest log.
- `asset-workload-<mode>` - the asset residency measurement.
- `editor-process-baseline-<mode>` - the headless editor process measurement.

Set the repository variable `DIAGNOSTIC_ARTIFACTS` to `true` to enable them. Nothing
else changes when they are off; the measurements still run where they are part of a
test, they are just not uploaded.

## With both off

Builds and correctness tests still run, and the checks still mean what they say. Linux
skips the editor process baseline run and every upload, including failure-log uploads;
errors and test results remain in the job log. Windows still prepares its runtime DLL
folder and runs the packaged executable startup check; it skips uploading the ZIP - the
package is built and started, just not kept.
