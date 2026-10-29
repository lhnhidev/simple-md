param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
if(-not $SkipBuild){& (Join-Path $PSScriptRoot 'build.ps1')}
$releaseName='simple-md-1.0.0-win-x64'
$releasePath=Join-Path $PSScriptRoot ('dist\'+$releaseName)
$licensePath=Join-Path $releasePath 'licenses'
New-Item -ItemType Directory -Force -Path $licensePath | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'dist\simple-md.exe') -Destination (Join-Path $releasePath 'simple-md.exe') -Force
foreach($name in @('README.md','HUONG_DAN.md','THIRD_PARTY_NOTICES.md','PROGRESS.md')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $releasePath $name) -Force}
foreach($name in @('md4c-LICENSE.md','json-LICENSE.txt')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('vendor\'+$name)) -Destination (Join-Path $licensePath $name) -Force}
$benchmarkPath=Join-Path $releasePath 'benchmarks'
New-Item -ItemType Directory -Force -Path $benchmarkPath | Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'benchmarks') -Filter '*.json' -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $benchmarkPath $_.Name) -Force}
$zipPath=Join-Path $PSScriptRoot ('dist\'+$releaseName+'.zip')
Compress-Archive -LiteralPath $releasePath -DestinationPath $zipPath -CompressionLevel Optimal -Force
$hash=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($zipPath+'.sha256'),($hash+'  '+$releaseName+'.zip'+[Environment]::NewLine),(New-Object Text.UTF8Encoding($false)))
Get-Item -LiteralPath $zipPath | Select-Object FullName,Length
Write-Output ('SHA256: '+$hash)
