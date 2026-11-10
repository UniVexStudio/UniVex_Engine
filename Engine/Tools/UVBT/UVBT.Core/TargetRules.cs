// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace UniVexBuildTool
{
    public enum TargetType
    {
        Game,
        Editor,
        Test,
        Server,
        Program
    }

    public enum BuildConfiguration
    {
        Debug,
        Development,
        Release,
        Sanitizers
    }

    public enum TargetPlatform
    {
        Linux,
        Windows,
        Mac,
        Android
    }

    public class TargetInfo
    {
        public TargetPlatform Platform { get; set; } = TargetPlatform.Linux;
        public BuildConfiguration Configuration { get; set; } = BuildConfiguration.Debug;
        public string Architecture { get; set; } = "x64";
    }

    public class ReadOnlyTargetRules
    {
        private readonly TargetRules _inner;

        public ReadOnlyTargetRules(TargetRules inner)
        {
            _inner = inner;
        }

        public TargetPlatform Platform => _inner.Platform;
        public BuildConfiguration Configuration => _inner.Configuration;
        public TargetType Type => _inner.Type;
        public string Architecture => _inner.Architecture;
        public bool IsDebug => _inner.Configuration == BuildConfiguration.Debug || _inner.Configuration == BuildConfiguration.Sanitizers;
        public bool EnableSanitizers => _inner.Configuration == BuildConfiguration.Sanitizers;
    }

    /// <summary>
    /// Base class for all UniVex engine target descriptors ([Target].Target.cs).
    /// Modeled after Unreal Engine's UnrealBuildTool (UBT) TargetRules.
    /// </summary>
    public abstract class TargetRules
    {
        public string Name { get; set; } = string.Empty;
        public TargetType Type { get; set; } = TargetType.Program;
        public TargetPlatform Platform { get; set; } = TargetPlatform.Linux;
        public BuildConfiguration Configuration { get; set; } = BuildConfiguration.Debug;
        public string Architecture { get; set; } = "x64";

        /// <summary>
        /// Root modules that define the target executable.
        /// </summary>
        public List<string> ExtraModuleNames { get; set; } = new List<string>();

        /// <summary>
        /// Additional global preprocessor definitions across the entire build.
        /// </summary>
        public List<string> GlobalDefinitions { get; set; } = new List<string>();

        /// <summary>
        /// C++ Language Standard (default: C++23).
        /// </summary>
        public string CppStandard { get; set; } = "c++23";

        /// <summary>
        /// Output binary file name (defaults to Target Name).
        /// </summary>
        public string OutputName { get; set; } = string.Empty;

        public ReadOnlyTargetRules AsReadOnly() => new ReadOnlyTargetRules(this);

        protected TargetRules(TargetInfo info)
        {
            if (info != null)
            {
                Platform = info.Platform;
                Configuration = info.Configuration;
                Architecture = info.Architecture;
            }
        }
    }
}
