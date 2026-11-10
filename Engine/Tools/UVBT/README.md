# UniVexBuildTool (UVBT)

**UniVexBuildTool (UVBT)** is the dedicated C# build system for the **UniVex Engine**, directly inspired by Unreal Engine's UnrealBuildTool (UBT). It replaces complex CMake configurations with clean, type-safe, and modular C# build scripts (`*.Build.cs` and `*.Target.cs`).

---

## 🚀 Key Features

1. **Unreal-Style C# Build Architecture**:
   - Each engine module defines its build rules in a `[ModuleName].Build.cs` inheriting from `UniVexBuildTool.ModuleRules`.
   - Each target binary (Game, Editor, Tools, Tests) defines a `[TargetName].Target.cs` inheriting from `UniVexBuildTool.TargetRules`.
   - Dependency propagation: Public dependencies export include paths, macro definitions, and system libraries down the DAG.
2. **True Standalone Portable Runner**:
   - Includes `uvbt.py`, `uvbt.sh` (Linux/macOS), and `uvbt.bat` (Windows).
   - Can execute directly on development machines without requiring a pre-installed .NET SDK runtime.
   - Automatically handles embedded code generation (e.g. `embed_file.py` for humanoid rig references and TTF fonts).
3. **High-Performance Parallel & Incremental Compilation**:
   - Direct compiler toolchain invocation (`g++`, `clang++`, `cl.exe`).
   - Fast incremental caching (`.uvbt/cache.json`) with header dependency tracking (`-MMD -MF`).
   - Parallel worker pool (`-j N`).
4. **Side-by-Side Coexistence**:
   - Works side-by-side with existing `CMakeLists.txt` workflows without breaking existing CMake builds.

---

## 📁 Directory Structure

```
Engine/Tools/UVBT/
├── UVBT.Core/              # Core build engine library (ModuleRules, TargetRules, ActionGraph, Toolchains)
│   ├── ModuleRules.cs
│   ├── TargetRules.cs
│   ├── ActionGraph/
│   └── Toolchains/
├── UVBT.App/               # C# CLI application (Program.cs)
├── UVBT.csproj             # .NET 8.0 SDK project
├── uvbt.py                 # Standalone Python runner with integrated C# parser and parallel toolchain
├── README.md               # This documentation
uvbt.sh                     # Root Linux/macOS launcher
uvbt.bat                    # Root Windows launcher
```

---

## 🛠️ Usage

From the engine root directory:

### 1. Build a Target
```bash
# Build the standalone project packager
./uvbt.sh build UniVexPack Linux Debug

# Build the runtime engine binary (headless / game)
./uvbt.sh build UniVexRuntime Linux Debug

# Build all engine tests
./uvbt.sh build UniVexTests Linux Debug

# Customize parallel jobs (default: all CPU cores)
./uvbt.sh build UniVexPack Linux Debug -j 4
```

### 2. Inspect Discovered Modules & Targets
```bash
# List all 40+ engine modules discovered across Engine/Runtime and Engine/Editor
./uvbt.sh --list-modules

# List all registered engine targets
./uvbt.sh --list-targets
```

### 3. Clean Build Artifacts
```bash
./uvbt.sh clean
```

---

## 📝 Authoring Rules

### Module Rules (`*.Build.cs`)
```csharp
using UniVexBuildTool;

public class MyModule : ModuleRules
{
    public MyModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Math"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Utilities"
        });

        PublicDefinitions.AddRange(new string[] {
            "MY_MODULE_API_VERSION=1"
        });

        ExcludedFiles.AddRange(new string[] {
            "unused_file.cpp"
        });
    }
}
```

### Target Rules (`*.Target.cs`)
```csharp
using UniVexBuildTool;

public class MyGameTarget : TargetRules
{
    public MyGameTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        OutputName = "my_game";

        ExtraModuleNames.AddRange(new string[] {
            "Engine",
            "MyGameplayModule"
        });
    }
}
```

---

## 🎯 Engine Targets

| Target | Type | Output Binary | Description |
| :--- | :--- | :--- | :--- |
| `UniVexPack` | Program | `.uvbt/bin/<Platform>/<Config>/uve_pack` | Standalone asset cooker and project packaging tool |
| `UniVexRuntime` | Game | `.uvbt/bin/<Platform>/<Config>/uve_runtime` | Standalone engine executable (supports `--headless`) |
| `UniVexEditor` | Editor | `.uvbt/bin/<Platform>/<Config>/uve_editor` | UniVex Editor Foundation application |
| `UniVexTests` | Test | `.uvbt/bin/<Platform>/<Config>/UniVexTests` | Full engine test suite linking GoogleTest |
