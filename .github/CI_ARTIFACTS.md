# Optional CI artifacts

Artifacts default to OFF. Builds and correctness tests still run.

For a manual run, open Actions, select CI, choose Run workflow, and turn on
**Create downloadable build and diagnostic artifacts**. The manual checkbox
overrides the repository default for that run.

For push and pull-request runs, set the Actions repository variable
`CREATE_ARTIFACTS` to `true` to enable uploads. Set it to `false` or leave it
unset to disable uploads. This is a variable, not a secret. Only lowercase
`true` enables it. No repository settings are changed by these workflows.

The workflow's `android-runtime` job builds the apk for arm64 and x86_64 and
uploads it under the same checkbox and repository variable. APKs use a
generated debug signing key for local testing.

It uploads three things per run:

- `UniVex-Android-<abi>-Demo` - the runtime's own touch-cube scene, no project content.
  Install this one first: black here means the runtime, the EGL surface or the GLES
  driver, and the logcat line saying which is `UVE_ANDROID_FAILED <reason>`.
- `UniVex-Android-<abi>-Cooked` - the same runtime with `Samples/CookedRuntimeSmoke`
  packed in. Renders in the demo and black here means the content, not the engine.
- `UniVex-Android-<abi>-Smoke` - only for x86_64, and uploaded even when the run
  failed: the device screenshot (including the black one), the two smoke reports,
  and the logcat around them. This is the artifact that explains a black screen
  after the fact.

When OFF, Linux skips the process baseline and all artifact uploads, including
failure-log uploads. Errors and test results remain in the job log. Windows
still prepares its runtime DLL folder and runs the packaged executable startup
check; it skips uploading the downloadable ZIP. Keeping this startup check
verifies the package dependencies even when no download is requested.
