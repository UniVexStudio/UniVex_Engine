@echo off
title UniVex Engine - Windows Installer
echo ========================================================
echo         UniVex Engine - Installing to Windows
echo ========================================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$installDir = \"$env:LOCALAPPDATA\Programs\UniVex_Engine\"; " ^
  "Write-Host 'Target Directory:' $installDir; " ^
  "if (-not (Test-Path $installDir)) { New-Item -ItemType Directory -Path $installDir -Force | Out-Null }; " ^
  "$srcDir = if (Test-Path '.uvbt\bin\Windows\Release\uve_editor.exe') { '.uvbt\bin\Windows\Release' } elseif (Test-Path 'package\UniVex-Editor\uve_editor.exe') { 'package\UniVex-Editor' } else { throw 'Built uve_editor.exe not found. Run ./uvbt build UniVexEditor Windows Release first.' }; " ^
  "Copy-Item \"$srcDir\uve_editor.exe\" -Destination \"$installDir\uve_editor.exe\" -Force; " ^
  "Get-ChildItem \"$srcDir\*.dll\" -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName -Destination \"$installDir\$($_.Name)\" -Force }; " ^
  "if (Test-Path \"$srcDir\prism_assets\") { Copy-Item \"$srcDir\prism_assets\" -Destination \"$installDir\prism_assets\" -Recurse -Force }; " ^
  "$contentDir = \"$installDir\Content\Viewport\"; " ^
  "if (-not (Test-Path $contentDir)) { New-Item -ItemType Directory -Path $contentDir -Force | Out-Null }; " ^
  "$wsh = New-Object -ComObject WScript.Shell; " ^
  "$desktopPaths = @([Environment]::GetFolderPath('Desktop'), \"$env:USERPROFILE\OneDrive\Desktop\", \"$env:USERPROFILE\Desktop\") | Where-Object { Test-Path $_ } | Select-Object -Unique; " ^
  "foreach ($desk in $desktopPaths) { " ^
  "  $lnk = $wsh.CreateShortcut(\"$desk\UniVex Engine Editor.lnk\"); " ^
  "  $lnk.TargetPath = \"$installDir\uve_editor.exe\"; " ^
  "  $lnk.WorkingDirectory = \"$installDir\"; " ^
  "  $lnk.Description = 'UniVex Engine Editor'; " ^
  "  $lnk.Save(); " ^
  "  Write-Host '[OK] Desktop shortcut created at:' $desk; " ^
  "}; " ^
  "$startMenu = \"$env:APPDATA\Microsoft\Windows\Start Menu\Programs\UniVex Engine\"; " ^
  "if (-not (Test-Path $startMenu)) { New-Item -ItemType Directory -Path $startMenu -Force | Out-Null }; " ^
  "$startLnk = $wsh.CreateShortcut(\"$startMenu\UniVex Engine Editor.lnk\"); " ^
  "$startLnk.TargetPath = \"$installDir\uve_editor.exe\"; " ^
  "$startLnk.WorkingDirectory = \"$installDir\"; " ^
  "$startLnk.Description = 'UniVex Engine Editor'; " ^
  "$startLnk.Save(); " ^
  "Write-Host '[OK] Start Menu shortcut created at:' $startMenu; " ^
  "Write-Host ''; " ^
  "Write-Host '========================================================'; " ^
  "Write-Host '   Installation Successful! Shortcut is on your Desktop.'; " ^
  "Write-Host '========================================================'; "

echo.
pause
