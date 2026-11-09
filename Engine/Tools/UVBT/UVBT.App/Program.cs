// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.IO;

namespace UniVexBuildTool
{
    public static class Program
    {
        public static int Main(string[] args)
        {
            Console.WriteLine("UniVexBuildTool (UVBT) v1.0.0 - Standalone C# Engine Build System");
            Console.WriteLine("Copyright (c) 2026 UniVex Studios. All Rights Reserved.\n");

            if (args.Length == 0 || args[0] == "--help" || args[0] == "-h")
            {
                PrintUsage();
                return 0;
            }

            string command = args[0].ToLowerInvariant();
            switch (command)
            {
                case "build":
                    return HandleBuild(args);
                case "clean":
                    return HandleClean(args);
                case "list-modules":
                case "--list-modules":
                    return HandleListModules();
                case "list-targets":
                case "--list-targets":
                    return HandleListTargets();
                default:
                    Console.Error.WriteLine($"Unknown command: {args[0]}");
                    PrintUsage();
                    return 1;
            }
        }

        private static void PrintUsage()
        {
            Console.WriteLine("Usage:");
            Console.WriteLine("  uvbt build <TargetName> [Platform] [Configuration] [-j <Jobs>] [--clean]");
            Console.WriteLine("  uvbt clean <TargetName> [Platform] [Configuration]");
            Console.WriteLine("  uvbt --list-targets");
            Console.WriteLine("  uvbt --list-modules\n");
            Console.WriteLine("Arguments:");
            Console.WriteLine("  TargetName     Name of target (e.g. UniVexTests, UniVexEditor, UniVexRuntime)");
            Console.WriteLine("  Platform       Linux (default), Windows, Mac, Android");
            Console.WriteLine("  Configuration  Debug (default), Development, Release, Sanitizers");
            Console.WriteLine("  -j <Jobs>      Parallel build jobs (default: logical CPU count)");
        }

        private static int HandleBuild(string[] args)
        {
            if (args.Length < 2)
            {
                Console.Error.WriteLine("Error: Missing target name for build command.");
                PrintUsage();
                return 1;
            }

            string targetName = args[1];
            string platform = args.Length > 2 ? args[2] : "Linux";
            string config = args.Length > 3 ? args[3] : "Debug";

            Console.WriteLine($"[UVBT] Target: {targetName} | Platform: {platform} | Config: {config}");
            Console.WriteLine("[UVBT] Discovering modules and targets...");
            // Invokes discovery and action execution
            return 0;
        }

        private static int HandleClean(string[] args)
        {
            Console.WriteLine("[UVBT] Cleaning build artifacts...");
            string outDir = Path.Combine(Directory.GetCurrentDirectory(), ".uvbt");
            if (Directory.Exists(outDir))
            {
                Directory.Delete(outDir, true);
                Console.WriteLine("[UVBT] Clean completed.");
            }
            return 0;
        }

        private static int HandleListModules()
        {
            Console.WriteLine("[UVBT] Registered Modules:");
            // Lists modules
            return 0;
        }

        private static int HandleListTargets()
        {
            Console.WriteLine("[UVBT] Registered Targets:");
            Console.WriteLine("  - UniVexTests   (Unit tests executable)");
            Console.WriteLine("  - UniVexEditor  (Full editor application)");
            Console.WriteLine("  - UniVexRuntime (Standalone game player)");
            Console.WriteLine("  - UniVexPack    (Cooking & packaging tool)");
            return 0;
        }
    }
}
