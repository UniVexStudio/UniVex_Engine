# Optional CI artifacts

Artifacts default to OFF. Builds and correctness tests still run.

For a manual run, open Actions, select CI or Windows Editor, choose Run workflow,
and turn on **Create downloadable build and diagnostic artifacts**.
The manual checkbox overrides the repository default for that run.

For push and pull-request runs, set the Actions repository variable
`CREATE_ARTIFACTS` to `true` to enable uploads. Set it to `false` or leave it
unset to disable uploads. This is a variable, not a secret. Only lowercase
`true` enables it. No repository settings are changed by these workflows.

CI also builds the Android arm64 runtime. Its APK upload uses the same checkbox
and repository variable. APKs use a generated debug signing key for local testing.

When OFF, Linux skips the process baseline and all artifact uploads, including
failure-log uploads. Errors and test results remain in the job log. Windows
still prepares its runtime DLL folder and runs the packaged executable startup
check; it skips uploading the downloadable ZIP. Keeping this startup check
verifies the package dependencies even when no download is requested.
