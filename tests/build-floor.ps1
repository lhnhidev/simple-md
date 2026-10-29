$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location $projectRoot
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd=Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Force -Path build | Out-Null
$command='"'+$devCmd+'" -arch=x64 -host_arch=x64 >nul && cl /nologo /O1 /GS- /DUNICODE /D_UNICODE tests\minimal-window.cpp /Fobuild\floor.obj /Febuild\minimal-window.exe /link /ENTRY:Entry /SUBSYSTEM:WINDOWS /NODEFAULTLIB kernel32.lib user32.lib'
& $env:ComSpec /d /s /c $command
if($LASTEXITCODE -ne 0){throw 'Control-window build failed.'}
& (Join-Path $PSScriptRoot 'inspect-memory.ps1') -ExecutablePath 'build\minimal-window.exe' -OutputPath 'build\memory-floor.json'
