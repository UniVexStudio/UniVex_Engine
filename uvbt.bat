@echo off
REM Copyright (c) 2026 UniVex Studios. All Rights Reserved.
REM UniVexBuildTool (UVBT) Launcher for Windows

set SCRIPT_DIR=%~dp0

where dotnet >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    dotnet run --project "%SCRIPT_DIR%Engine\Tools\UVBT\UVBT.csproj" -- %*
) else (
    python "%SCRIPT_DIR%Engine\Tools\UVBT\uvbt.py" %*
)
