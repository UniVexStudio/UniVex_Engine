// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace UniVexBuildTool
{
    public enum ActionType
    {
        CompileCpp,
        ArchiveStaticLib,
        LinkExecutable
    }

    public class BuildAction
    {
        public string Description { get; set; } = string.Empty;
        public ActionType Type { get; set; }
        public string Executable { get; set; } = string.Empty;
        public string Arguments { get; set; } = string.Empty;
        public List<string> InputFiles { get; set; } = new List<string>();
        public List<string> OutputFiles { get; set; } = new List<string>();
        public string DependencyFile { get; set; } = string.Empty;
        public List<BuildAction> Prerequisites { get; set; } = new List<BuildAction>();
    }

    public class ActionGraph
    {
        public List<BuildAction> Actions { get; } = new List<BuildAction>();

        public void AddAction(BuildAction action)
        {
            Actions.Add(action);
        }
    }
}
