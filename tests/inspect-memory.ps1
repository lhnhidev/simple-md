$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(Get-Process -Name simple-md -ErrorAction SilentlyContinue){throw 'Close simple-md before inspecting its isolated empty window.'}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ResidentPages {
    [StructLayout(LayoutKind.Sequential)] public struct Page { public IntPtr Address; public UIntPtr Attributes; }
    [DllImport("psapi.dll",SetLastError=true)] static extern bool QueryWorkingSetEx(IntPtr process,[In,Out] Page[] pages,int bytes);
    public static long Count(IntPtr process,long address,int bytes) {
        int count=(bytes+4095)/4096;var pages=new Page[count];
        for(int i=0;i<count;i++)pages[i].Address=new IntPtr(address+i*4096L);
        if(!QueryWorkingSetEx(process,pages,count*Marshal.SizeOf(typeof(Page))))throw new System.ComponentModel.Win32Exception();
        long resident=0;foreach(var page in pages)if((page.Attributes.ToUInt64()&1)!=0)resident+=4096;return resident;
    }
}
'@
$start=New-Object Diagnostics.ProcessStartInfo
$start.FileName=Join-Path $projectRoot 'dist\simple-md.exe'
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
}finally{if($process.Id -and -not $process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
