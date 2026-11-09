// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace UniVexBuildTool
{
    public enum ModuleType
    {
        StaticLibrary,
        SharedLibrary,
        HeaderOnly,
        Executable
    }

    /// <summary>
    /// Base class for all UniVex engine module build descriptors ([Module].Build.cs).
    /// Modeled after Unreal Engine's UnrealBuildTool (UBT) ModuleRules.
    /// </summary>
    public abstract class ModuleRules
    {
        public string Name { get; set; } = string.Empty;
        public string Directory { get; set; } = string.Empty;
        public ModuleType Type { get; set; } = ModuleType.StaticLibrary;

        public ReadOnlyTargetRules Target { get; }

        /// <summary>
        /// Modules whose public interfaces and symbols are exported to any dependent module.
        /// </summary>
        public List<string> PublicDependencyModuleNames { get; set; } = new List<string>();

        /// <summary>
        /// Modules used internally within this module's implementation files only.
        /// </summary>
        public List<string> PrivateDependencyModuleNames { get; set; } = new List<string>();

        /// <summary>
        /// Public include directories (relative to module directory) exported to dependents.
        /// </summary>
        public List<string> PublicIncludePaths { get; set; } = new List<string>();

        /// <summary>
        /// Private include directories (relative to module directory) visible only to this module.
        /// </summary>
        public List<string> PrivateIncludePaths { get; set; } = new List<string>();

        /// <summary>
        /// Preprocessor definitions exported to dependents.
        /// </summary>
        public List<string> PublicDefinitions { get; set; } = new List<string>();

        /// <summary>
        /// Preprocessor definitions visible only to this module.
        /// </summary>
        public List<string> PrivateDefinitions { get; set; } = new List<string>();

        /// <summary>
        /// External system or vendor libraries to link (e.g. pthread, dl, GL, vulkan).
        /// </summary>
        public List<string> PublicSystemLibraries { get; set; } = new List<string>();
        public List<string> PrivateSystemLibraries { get; set; } = new List<string>();

        /// <summary>
        /// Specific source files to exclude from auto-discovery if needed.
        /// </summary>
        public List<string> ExcludedFiles { get; set; } = new List<string>();

        /// <summary>
        /// Additional source directories outside the module root to scan for implementation files.
        /// </summary>
        public List<string> ExtraSourceDirectories { get; set; } = new List<string>();

        protected ModuleRules(ReadOnlyTargetRules target)
        {
            Target = target;
            // Default UniVex convention: Expose/ contains exported headers
            PublicIncludePaths.Add("Expose");
            PrivateIncludePaths.Add("Internal");
        }
    }
}
