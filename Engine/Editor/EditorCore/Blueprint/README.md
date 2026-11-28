# Engine/Editor/EditorCore/Blueprint

Blueprint visual scripting, graph model, node catalog, compiler, virtual machine, editor UI, serialization, debugging, profiling, plugins, resources, platform integration, and automated tests for UniVex Engine.

## Boundary: Engine Runtime vs Blueprint Runtime
- `Engine/Runtime/` owns real engine systems (`Networking/`, `Physics/`, `Audio/`, `Animation/`, `RHI/`, `Input/`, `Navigation/`, `AI/`, `Gameplay/`, `VFX/`, `Prism/`, `Asset/`, ...).
- `Engine/Editor/EditorCore/Blueprint/Runtime/` owns **only** Blueprint execution infrastructure: the VM (`VirtualMachine/`), Blueprint values/variables (`Variables/`), and host object binding (`ObjectBinding/`).
- Blueprint nodes under `Engine/Editor/EditorCore/Blueprint/Nodes/` (including `Nodes/Networking/`) consume Engine Runtime systems exclusively through public host APIs. No sockets, transport, physics, or audio systems are implemented in Blueprint.

## Directory Layout Status

| Folder | Active Files | Responsibilities |
|---|---|---|
| `Core/` | `blueprint_core_types.{h,cpp}` | Graph model (`Node`, `Pin`, `Graph`, `Connection`), `TypeSystem`, `Reflection`, `Metadata`, `Identifiers`, `Events`, `Interfaces`, `Diagnostics`, `Utilities` |
| `Nodes/` | `blueprint_nodes.{h,cpp}`, `blueprint_graph_lowering.{h,cpp}`, `blueprint_node_categories.{h,cpp}`, `Networking/blueprint_networking_nodes.{h,cpp}` | Complete Blueprint node catalog, graph lowering (a graph compiles straight to the compiler's syntax tree), domain classification/search, and Networking RPC/Lobby/Relay node specs |
| `Compiler/` | `Frontend/` (`blueprint_ast.h`, `blueprint_parser.{h,cpp}`), `Intermediate/` (`blueprint_program.h`), `Backend/` (`blueprint_compiler.{h,cpp}`, `blueprint_codegen.{h,cpp}`, `blueprint_compiler_main.cpp`) | Lexer and parser (the notation node templates are written in), type checker, bytecode compiler, and native C++ codegen (`uvbc`) |
| `Runtime/` | `VirtualMachine/` (`blueprint_instance.{h,cpp}`), `Variables/` (`blueprint_value.{h,cpp}`), `ObjectBinding/` (`blueprint_host.h`, `blueprint_host_description.{h,cpp}`, `blueprint_native.{h,cpp}`) | Bytecode VM execution, Blueprint typed values, and host reflection/native bindings |
| `Editor/` | `editor_blueprint_panel.{h,cpp}`, `blueprint_editor_services.{h,cpp}` | Blueprint graph PRISM panel, UndoRedo, Clipboard, Search, NodePalette, Shortcuts, Visualization, Accessibility (compiled by `EditorCoreModule`; Entity Editor & Entity Class windows live in `Internal/shell/entity/`) |
| `Serialization/` | `blueprint_asset.cpp`, `widget_blueprint_asset.cpp`, `blueprint_serialization.{h,cpp}` | `.uvbp` / `.uvwbp` persistence, pin/node/wire/library JSON serialization, schema versioning, migration pipeline, import/export |
| `Assets/` | `blueprint_asset.h`, `widget_blueprint_asset.h`, `blueprint_asset_types.{h,cpp}`, `blueprint_asset_registry.{h,cpp}` | Blueprint Class, Widget Blueprint, FunctionLibrary, MacroLibrary, Interface definitions, asset registry, dependency resolver, and detailed validation |
| `Plugins/` | `blueprint_plugin_api.{h,cpp}` | Plugin API versioning/compatibility, extension interface, `.uvbpplugin` manifest validation & discovery, dependency-ordered loading & node registration |
| `Debug/` | `blueprint_debugger.{h,cpp}` | Breakpoints manager, Watch evaluation, LiveValues snapshots, CallStack frames, ExecutionTrace recorder, Diagnostics, and `BlueprintDebugSession` |
| `Profiling/` | `blueprint_profiler.{h,cpp}` | Execution profiler + RAII scoped timing, Memory footprint estimator, Graph complexity analyzer, and Performance report formatter |
| `Resources/` | `blueprint_resources.{h,cpp}` | Studio themes (`DarkStudio`, `LightStudio`, `HighContrast`), pin/node styles, category/pin icons, and starter Blueprint templates |
| `Platform/` | `blueprint_platform.{h,cpp}` | Platform capability profiles (`Linux`, `Windows`, `Android`, `Common`), cross-platform asset path normalization, and cache directory resolution |
| `Testing/` | `Unit/`, `Compiler/`, `Runtime/`, `Integration/`, `Serialization/`, `Plugins/`, `Performance/`, `Editor/` | Automated GoogleTest suites covering all Blueprint subsystems |

## Compatibility
Existing `.uvbp` and `.uvwbp` files remain supported without breaking existing scenes or assets.
