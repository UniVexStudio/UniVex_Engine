# Editor/Blueprint

Blueprint visual scripting, compiler, virtual machine, and node architecture for UniVex Engine (replacing `Engine/Runtime/UVScript/`).

## Boundary: Engine Runtime vs Blueprint Runtime
- `Engine/Runtime/` owns real engine systems (`Networking/`, `Physics/`, `Audio/`, `Animation/`, `RHI/`, `Input/`, `Navigation/`, `AI/`, `Gameplay/`, `VFX/`, ...).
- `Editor/Blueprint/Runtime/` owns **only** Blueprint execution infrastructure: the VM (`VirtualMachine/`), Blueprint values/variables (`Variables/`), and host object binding (`ObjectBinding/`).
- Blueprint nodes under `Editor/Blueprint/Nodes/` (including `Editor/Blueprint/Nodes/Networking/`) consume Engine Runtime systems exclusively through public host APIs. No sockets, transport, physics, or audio systems are implemented in Blueprint.

## Directory Layout Status

| Folder | Populated Subfolders (Real Code) | Planned / Not Yet Populated |
|---|---|---|
| `Core/` | *(Blueprint graph core currently hosted in `Engine/Runtime/Prism/` and `Engine/Runtime/Asset/Expose/blueprint_asset.h`)* | `Node`, `Pin`, `Graph`, `Connection`, `TypeSystem`, `Reflection`, `Metadata`, `Identifiers`, `Events`, `Interfaces`, `Diagnostics`, `Utilities` |
| `Nodes/` | `Networking/` (`blueprint_networking_nodes.{h,cpp}` calling `Engine/Runtime/Networking/` RPC/Lobby/Relay APIs) | `Flow`, `Logic`, `Math`, `Variable`, `Function`, `Event`, `Input`, `Transform`, `Physics`, `Collision`, `Animation`, `Rendering`, `Material`, `Camera`, `Audio`, `Particle`, `AI`, `Navigation`, `Gameplay`, `Timeline`, `Data`, `String`, `Collection`, `Debug`, `Utility` *(currently consolidated in `Engine/Runtime/Asset/Internal/blueprint_nodes.cpp`)* |
| `Compiler/` | `Frontend/` (`blueprint_ast.h`, `blueprint_parser.{h,cpp}`), `Intermediate/` (`blueprint_program.h`), `Backend/` (`blueprint_compiler.{h,cpp}`, `blueprint_codegen.{h,cpp}`, `blueprint_compiler_main.cpp`) | `Optimization`, `Analysis`, `TypeChecking`, `Dependency`, `Validation`, `Cache`, `Diagnostics` |
| `Runtime/` | `VirtualMachine/` (`blueprint_instance.{h,cpp}`), `Variables/` (`blueprint_value.{h,cpp}`), `ObjectBinding/` (`blueprint_host.h`, `blueprint_host_description.{h,cpp}`, `blueprint_native.{h,cpp}`) | `Execution`, `Memory`, `Events`, `Tasks`, `Async`, `HotReload`, `Debugging`, `Profiling` |
| `Editor/` | *(Editor graph UI currently hosted in `Engine/Editor/EditorCore/Internal/shell/blueprint/`)* | `GraphEditor`, `NodeEditor`, `PinEditor`, `ConnectionEditor`, `NodePalette`, `DetailsPanel`, `VariablePanel`, `FunctionPanel`, `ComponentPanel`, `Debugger`, `Visualization`, `Search`, `ContextMenu`, `Clipboard`, `UndoRedo`, `Shortcuts`, `Accessibility`, `Integration` |
| `Serialization/` | *(Blueprint `.uvbp` / `.uvwbp` serialization currently hosted in `Engine/Runtime/Asset/Internal/blueprint_asset.cpp` & `widget_blueprint_asset.cpp`)* | `Graph`, `Node`, `Pin`, `Asset`, `Versioning`, `Migration`, `Import`, `Export` |
| `Assets/` | *(Blueprint asset types currently hosted in `Engine/Runtime/Asset/Expose/blueprint_asset.h` & `widget_blueprint_asset.h`)* | `Blueprint`, `FunctionLibrary`, `MacroLibrary`, `Interface`, `Registry`, `Dependency`, `Validation` |
| `Plugins/` | — | `API`, `Extensions`, `Registration`, `Discovery`, `Loading`, `Validation`, `Compatibility` |
| `Debug/` | *(VM line-debugger hooks live in `Runtime/VirtualMachine/blueprint_instance.{h,cpp}`)* | `Breakpoints`, `Watch`, `ExecutionTrace`, `LiveValues`, `CallStack`, `Diagnostics` |
| `Profiling/` | — | `Execution`, `Memory`, `Graph`, `Performance` |
| `Resources/` | — | `Icons`, `Themes`, `Styles`, `Templates` |
| `Platform/` | — | `Android`, `Windows`, `Linux`, `Common` |
| `Testing/` | Blueprint compiler, VM, collections, debugger, and networking-node tests (migrated in Phase 8) | `Unit`, `Integration`, `Compiler`, `Runtime`, `Editor`, `Serialization`, `Plugins`, `Performance` |

## Compatibility
Existing `.uvbp`, `.uvwbp`, and `.uvs` script/blueprint files remain supported without breaking existing scenes or assets.
