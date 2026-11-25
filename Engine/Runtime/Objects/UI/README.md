# Engine/Runtime/Objects/UI

The Canvas-family scene nodes. The four previously Inspector-only UI kinds — Canvas,
UI Text, UI Image, UI Button — live here as per-kind `NodeDefinition` `.h` + `.cpp` pairs
(`Expose/uve/objects/canvas/<name>_uve.h` + `Internal/<name>_uve.cpp`), matching the
convention established in `Engine/Runtime/Objects/3D`: each file holds that kind's creation
recipe (components to attach, authored defaults, default entity name), while the component
structs themselves stay in `Engine/Runtime/Component` — one truth per concept.

These kinds are registered in the Scene object registry (`SceneObjectKind::Canvas` /
`UIText` / `UIImage` / `UIButton`) and are creatable from the editor's Add-Object list, giving
UI authoring the same single entry point 3D objects have; each new UI kind gets its own file
pair here.
