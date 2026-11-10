// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

// PRISM unit + golden visual regression tests (own test harness, own main()).
// Run: uve_prism_tests Engine/Runtime/Prism/assets
public class PrismTestsModule : ModuleRules
{
    public PrismTestsModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Prism"
        });
    }
}
