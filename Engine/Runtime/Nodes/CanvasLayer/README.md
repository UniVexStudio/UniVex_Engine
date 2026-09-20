# Engine/Runtime/Nodes/CanvasLayer

The CanvasLayer-family scene nodes. The four previously Inspector-only UI kinds — Canvas,
UI Text, UI Image, UI Button — live here as per-kind `NodeDefinition` `.h` + `.cpp` pairs
(`Expose/uve/nodes/canvas_layer/<name>_uve.h` + `Internal/<name>_uve.cpp`), matching the
convention established in `Engine/Runtime/Nodes/3D`: each file holds that kind's creation
recipe (components to attach, authored defaults, default entity name), while the component
structs themselves stay in `Engine/Runtime/Component` — one truth per concept.

These kinds are registered in the Scene node registry (`SceneNodeKindUVE::Canvas` /
`UIText` / `UIImage` / `UIButton`) and are creatable from the editor's Add-Node list, giving
UI authoring the same single entry point 3D nodes have. `SCENE_NODES_ROADMAP.md` at the repo
root tracks the full checklist of CanvasLayer/UI node types still to build (containers,
more buttons, text fields, lists, windows/dialogs, etc.) — each new kind gets its own file
pair here.
