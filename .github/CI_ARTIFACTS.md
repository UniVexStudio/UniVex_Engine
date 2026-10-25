# What CI uploads

One workflow (`.github/workflows/ci.yml`), two switches. They answer different
questions, so they default differently.

## Builds somebody can run - on by default

`CREATE_ARTIFACTS` gates the uploads that are a build you can actually try:

- `UniVex-Editor-Windows-x64` - the Windows editor package, with the DLLs its exe
  imports collected beside it, from the `windows-editor` job.
- `UniVex-Android-<abi>-Demo` - the runtime's own touch-cube scene, no project
  content. Install this one first: a black screen here means the runtime, the EGL
  surface or the GLES driver, and the logcat line that says which is
  `UVE_ANDROID_FAILED <reason>`.
- `UniVex-Android-<abi>-Cooked` - the same runtime with
  `Samples/CookedRuntimeSmoke` packed in. Renders in the demo and black here means
  the content, not the engine.
- `UniVex-Android-<abi>-Smoke` - x86_64 only, and uploaded even when the run failed:
  the device screenshot (including the black one), the smoke reports and the logcat
  around them. This is the artifact that explains a black screen after the fact.

APKs use a generated debug signing key for local testing; that key belongs to the
build directory and is not a release signature.

Set the Actions repository variable `CREATE_ARTIFACTS` to `false` to stop these, or
pass `create_artifacts=false` on a manual run (Actions, select CI, Run workflow). The
manual checkbox overrides the repository variable for that run. This is a variable,
not a secret.

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
folder and runs the packaged executable startup check; it skips uploading the ZIP.
Android still builds and verifies both apks for both ABIs and still smokes them on the
emulator - the APKs are just not kept afterwards.
