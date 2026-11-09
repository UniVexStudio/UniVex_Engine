// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;

namespace UniVexBuildTool
{
    public interface IToolchain
    {
        string GetCompilerExecutable();
        string GetArchiverExecutable();
        string GetLinkerExecutable();

        string FormatCompileArgs(
            string sourcePath,
            string objectPath,
            string depFilePath,
            IEnumerable<string> includePaths,
            IEnumerable<string> definitions,
            ReadOnlyTargetRules target);

        string FormatArchiveArgs(
            string libraryPath,
            IEnumerable<string> objectPaths);

        string FormatLinkArgs(
            string outputPath,
            IEnumerable<string> objectPaths,
            IEnumerable<string> libraryPaths,
            IEnumerable<string> systemLibraries,
            ReadOnlyTargetRules target);
    }

    public class GnuToolchain : IToolchain
    {
        public string GetCompilerExecutable() => "g++";
        public string GetArchiverExecutable() => "ar";
        public string GetLinkerExecutable() => "g++";

        public string FormatCompileArgs(
            string sourcePath,
            string objectPath,
            string depFilePath,
            IEnumerable<string> includePaths,
            IEnumerable<string> definitions,
            ReadOnlyTargetRules target)
        {
            var args = new List<string>
            {
                "-c",
                $"\"{sourcePath}\"",
                "-o", $"\"{objectPath}\"",
                "-std=c++23",
                "-Wall", "-Wextra", "-Wpedantic",
                "-fPIC",
                "-MMD", "-MF", $"\"{depFilePath}\""
            };

            if (target.IsDebug)
            {
                args.Add("-g");
                args.Add("-O0");
            }
            else
            {
                args.Add("-O3");
                args.Add("-DNDEBUG");
            }

            if (target.EnableSanitizers)
            {
                args.Add("-fsanitize=address,undefined");
                args.Add("-fno-omit-frame-pointer");
            }

            foreach (var def in definitions)
            {
                args.Add($"-D{def}");
            }

            foreach (var inc in includePaths)
            {
                args.Add($"-I\"{inc}\"");
            }

            return string.Join(" ", args);
        }

        public string FormatArchiveArgs(string libraryPath, IEnumerable<string> objectPaths)
        {
            var quotedObjects = string.Join(" ", Array.ConvertAll(new List<string>(objectPaths).ToArray(), p => $"\"{p}\""));
            return $"rcs \"{libraryPath}\" {quotedObjects}";
        }

        public string FormatLinkArgs(
            string outputPath,
            IEnumerable<string> objectPaths,
            IEnumerable<string> libraryPaths,
            IEnumerable<string> systemLibraries,
            ReadOnlyTargetRules target)
        {
            var args = new List<string>
            {
                "-o", $"\"{outputPath}\""
            };

            if (target.EnableSanitizers)
            {
                args.Add("-fsanitize=address,undefined");
            }

            foreach (var obj in objectPaths)
            {
                args.Add($"\"{obj}\"");
            }

            // Link libraries with --start-group and --end-group to resolve cyclic dependencies cleanly
            args.Add("-Wl,--start-group");
            foreach (var lib in libraryPaths)
            {
                args.Add($"\"{lib}\"");
            }
            args.Add("-Wl,--end-group");

            foreach (var sysLib in systemLibraries)
            {
                args.Add($"-l{sysLib}");
            }

            return string.Join(" ", args);
        }
    }
}
