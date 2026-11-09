#!/usr/bin/env bash
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
# UniVexBuildTool (UVBT) Launcher for Linux / macOS

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# If dotnet is installed, we can run UVBT.csproj, otherwise fall back to portable runner
if command -v dotnet >/dev/null 2>&1; then
    dotnet run --project "$SCRIPT_DIR/Engine/Tools/UVBT/UVBT.csproj" -- "$@"
else
    python3 "$SCRIPT_DIR/Engine/Tools/UVBT/uvbt.py" "$@"
fi
