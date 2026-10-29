$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd=Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Force -Path build\tests | Out-Null
$testCommand='"'+$devCmd+'" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++17 /utf-8 /O1 /MT /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Ivendor tests\native-tests.cpp src\document.cpp src\markdown.cpp src\mcp.cpp vendor\md4c.c /Fobuild\tests\ /Febuild\native-tests.exe /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib dwmapi.lib'
& $env:ComSpec /d /s /c $testCommand
if($LASTEXITCODE -ne 0){throw 'Test compilation failed.'}
& .\build\native-tests.exe
if($LASTEXITCODE -ne 0){throw 'Native tests failed.'}
node .\tests\mcp-tests.mjs
if($LASTEXITCODE -ne 0){throw 'MCP tests failed.'}
