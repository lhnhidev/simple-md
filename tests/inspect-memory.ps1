param([string]$ExecutablePath='dist\simple-md.exe',[string]$OutputPath='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(Get-Process -Name simple-md -ErrorAction SilentlyContinue){throw 'Close simple-md before inspecting its isolated empty window.'}
. (Join-Path $PSScriptRoot 'working-set.ps1')
$start=New-Object Diagnostics.ProcessStartInfo
$start.FileName=Join-Path $projectRoot $ExecutablePath
$start.UseShellExecute=$false
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$process=New-Object Diagnostics.Process
$process.StartInfo=$start
try{
    $process.Start() | Out-Null
    $process.WaitForInputIdle(10000) | Out-Null
    Start-Sleep -Milliseconds 1200
    $process.Refresh()
    $modules=@()
    foreach($module in $process.Modules){$modules+=[pscustomobject]@{module=$module.ModuleName;residentKiB=[ResidentPages]::Count($process.Handle,$module.BaseAddress.ToInt64(),$module.ModuleMemorySize)/1KB;mappedKiB=$module.ModuleMemorySize/1KB}}
    $modules | Sort-Object residentKiB -Descending | Format-Table -AutoSize
    Write-Output ("Working set: {0:N2} MiB; private committed: {1:N2} MiB" -f ($process.WorkingSet64/1MB),($process.PrivateMemorySize64/1MB))
    Write-Output ("Private working set: {0:N2} MiB (resident pages not marked shared)" -f ([ResidentPages]::PrivateWorkingSet($process.Handle)/1MB))
    if($OutputPath){
        $report=[ordered]@{measuredAt=(Get-Date -Format o);executable=$ExecutablePath;executableBytes=(Get-Item -LiteralPath $start.FileName).Length;workingSetBytes=$process.WorkingSet64;privateCommittedBytes=$process.PrivateMemorySize64;privateWorkingSetBytes=[ResidentPages]::PrivateWorkingSet($process.Handle);modules=$modules}
        [IO.File]::WriteAllText((Join-Path $projectRoot $OutputPath),($report | ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))
    }
}finally{if($process.Id -and -not $process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
