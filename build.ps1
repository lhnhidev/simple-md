$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
New-Item -ItemType Directory -Force -Path build,dist | Out-Null
$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$buildCommand = '"' + $devCmd + '" -arch=x64 -host_arch=x64 >nul && rc /nologo /fo build\app.res src\app.rc && cl /nologo /std:c++17 /utf-8 /O1 /GL /MT /W4 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Ivendor src\main.cpp src\document.cpp src\markdown.cpp src\mcp.cpp vendor\md4c.c /Fobuild\ /Fedist\simple-md.exe /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest build\app.res user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib dwmapi.lib'
& $env:ComSpec /d /s /c $buildCommand
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Get-Item dist\simple-md.exe | Select-Object FullName,Length
