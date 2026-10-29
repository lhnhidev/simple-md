param([string]$OutputPath='build\memory-results.json')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Set-Location $projectRoot
$exe=Join-Path $projectRoot 'dist\simple-md.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'Build dist\simple-md.exe first.'}
if(Get-Process -Name simple-md -ErrorAction SilentlyContinue){throw 'Close simple-md before running the isolated memory benchmark.'}
$fixtureRoot=Join-Path $projectRoot 'build\memory-fixtures'
New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
$utf8=New-Object Text.UTF8Encoding($false)
$paragraph="# A quiet workspace`n`nA paragraph with **bold**, *italic*, and ``code``.`n`n- first`n- second`n`n"
$smallPath=Join-Path $fixtureRoot 'small.md'
$mediumPath=Join-Path $fixtureRoot 'medium.md'
$largePath=Join-Path $fixtureRoot 'large.md'
[IO.File]::WriteAllText($smallPath,($paragraph*30),$utf8)
[IO.File]::WriteAllText($mediumPath,($paragraph*2000),$utf8)
[IO.File]::WriteAllText($largePath,($paragraph*20000),$utf8)
$manyPaths=@()
foreach($index in 1..30){$path=Join-Path $fixtureRoot ("document-{0:D2}.md" -f $index);[IO.File]::WriteAllText($path,($paragraph*30),$utf8);$manyPaths+=$path}
$cases=@(
    @{Name='empty';Paths=@()},
    @{Name='one-small-file';Paths=@($smallPath)},
    @{Name='30-listed-small-files';Paths=$manyPaths},
    @{Name='medium-preview';Paths=@($mediumPath)},
    @{Name='large-file-preview-paused';Paths=@($largePath)}
)
$results=@()
foreach($case in $cases){
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$exe
    $start.WorkingDirectory=$projectRoot
    $start.UseShellExecute=$false
    $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $start.Arguments=($case.Paths | ForEach-Object {'"'+$_+'"'}) -join ' '
    $process=New-Object Diagnostics.Process
    $process.StartInfo=$start
    try{
        $watch=[Diagnostics.Stopwatch]::StartNew()
        if(-not $process.Start()){throw 'Cannot start benchmark process.'}
        if(-not $process.WaitForInputIdle(10000)){throw 'Application did not reach its message loop.'}
        $watch.Stop()
        Start-Sleep -Milliseconds 1200
        $process.Refresh()
        if($process.HasExited){throw 'Benchmark process exited unexpectedly.'}
        $cpuStart=$process.TotalProcessorTime.TotalMilliseconds
        $workingSet=$process.WorkingSet64
        $privateBytes=$process.PrivateMemorySize64
        Start-Sleep -Milliseconds 1000
        $process.Refresh()
        $results+=[pscustomobject]@{
            scenario=$case.Name
            listedFiles=$case.Paths.Count
            fixtureBytes=($case.Paths | ForEach-Object {(Get-Item -LiteralPath $_).Length} | Measure-Object -Sum).Sum
            workingSetBytes=$workingSet
            privateBytes=$privateBytes
            workingSetMiB=[Math]::Round($workingSet/1MB,2)
            privateMiB=[Math]::Round($privateBytes/1MB,2)
            startupToMessageLoopMs=$watch.ElapsedMilliseconds
            idleCpuMsInOneSecond=[Math]::Round($process.TotalProcessorTime.TotalMilliseconds-$cpuStart,2)
        }
    }finally{
        # This process was created by this benchmark with synthetic, unmodified files.
        if($process.Id -and -not $process.HasExited){$process.Kill();$process.WaitForExit()}
        $process.Dispose()
    }
}
$report=[ordered]@{
    measuredAt=(Get-Date -Format o)
    executableBytes=(Get-Item -LiteralPath $exe).Length
    os=[Environment]::OSVersion.VersionString
    processArchitecture='x64'
    methodology='Fresh hidden native application per scenario, 1.2 seconds settling, 1 second idle CPU sampling. Working set includes shared DLL pages; private bytes are committed private memory. 30 files are listed, with only the first loaded. No working-set trimming is performed.'
    results=$results
}
$output=Join-Path $projectRoot $OutputPath
[IO.File]::WriteAllText($output,($report | ConvertTo-Json -Depth 8),$utf8)
$results | Format-Table scenario,workingSetMiB,privateMiB,startupToMessageLoopMs,idleCpuMsInOneSecond -AutoSize
Write-Output "Report: $output"
