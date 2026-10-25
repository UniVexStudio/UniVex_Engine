# Cooked native runtime smoke fixture

This small cooked project has a camera, a cube, one material and a 4x4 texture. Android CI packages
it with `--cooked-project` and requires actual project launch, rendered pixels, native touch,
surface recovery, and persistent application data across process restart.

It was generated through the real authoring/cooking tools, rather than assembled by the APK builder:

```sh
build/Engine/Runtime/Pack/uve_asset_workload --create-smoke-fixture /tmp/smoke-source
build/Engine/Runtime/Pack/uve_pack --project /tmp/smoke-source/sample.uvproject --runtime build/Engine/App/uve_runtime --output /tmp/smoke-cooked --cook android
```

Copy only `sample.uvproject`, `.uvassetdb`, `cooked.json`, and `content/` into this directory when
regenerating. The runtime executable is built separately; it is not checked in. Keep the generated
GUID catalog and the scene/material references together. `runtimeFiles` deliberately includes the
hidden catalog, which Android asset packaging must preserve.

For memory/loading measurements use the full `--create-fixture` workload instead. This fixture
proves correctness and is not a representative performance workload.
