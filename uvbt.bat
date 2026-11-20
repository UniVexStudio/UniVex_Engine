@echo off
REM Copyright (c) 2026 UniVex Studios. All Rights Reserved.
REM UniVexBuildTool (UVBT) Launcher for Windows

set SCRIPT_DIR=%~dp0

python "%SCRIPT_DIR%Engine\Tools\UVBT\uvbt.py" %*

