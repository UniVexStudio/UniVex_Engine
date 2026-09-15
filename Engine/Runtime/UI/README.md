# Engine/Runtime/UI

`UIRuntimeUVE` - per-frame reconciliation of authored screen-space UI components
(`Scene::CanvasComponentUVE`/`UITextComponentUVE`/`UIImageComponentUVE`/`UIButtonComponentUVE`,
`Engine/Runtime/Component`) into real button hit-testing against `IInputSystemUVE` and a
plain-data `UIDrawBatchUVE` snapshot. `UIFontAtlasUVE` bakes the module's own embedded font
(`assets/fonts/`) into a glyph bitmap via vendored `stb_truetype.h` (`thirdparty/`).

No GPU resource is created or touched anywhere in this module - the actual rendering of a
`UIDrawBatchUVE` (uploading the font atlas, drawing the quads) is a later, separate phase.
