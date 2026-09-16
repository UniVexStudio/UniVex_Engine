# Engine/Runtime/Nodes/CanvasLayer

Placeholder — the current UI primitives (Canvas, UI Text, UI Image, UI Button) live in
`Engine/Runtime/Component` as Inspector-addable components, not as placeable Scene-panel nodes
yet. See `SCENE_NODES_ROADMAP.md` at the repository root for the full checklist of CanvasLayer/UI
node types still to build (containers, buttons, text fields, lists, windows/dialogs, etc.). When
CanvasLayer nodes are promoted into the Scene node registry (or new ones are added), they get
their own `Expose/uve/nodes/canvas_layer/<name>_uve.h` + `Internal/<name>_uve.cpp` pair here,
matching the convention established in `Engine/Runtime/Nodes/3D`.
