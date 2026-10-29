# Read-only process memory diagnostics. Never trim or change the target working set.
if(-not ('ResidentPages' -as [type])){
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ResidentPages {
    [StructLayout(LayoutKind.Sequential)] public struct Page { public IntPtr Address; public UIntPtr Attributes; }
    [DllImport("psapi.dll",SetLastError=true)] static extern bool QueryWorkingSetEx(IntPtr process,[In,Out] Page[] pages,int bytes);
    [DllImport("psapi.dll",SetLastError=true)] static extern bool QueryWorkingSet(IntPtr process,IntPtr buffer,int bytes);
    public static long PrivateWorkingSet(IntPtr process) {
        int size=1024*1024;IntPtr buffer=Marshal.AllocHGlobal(size);
        try {
            if(!QueryWorkingSet(process,buffer,size))throw new System.ComponentModel.Win32Exception();
            long count=Marshal.ReadIntPtr(buffer).ToInt64(),pages=0;
            for(int i=0;i<count;i++){ulong flags=(ulong)Marshal.ReadIntPtr(buffer,(i+1)*IntPtr.Size).ToInt64();if((flags&0x100)==0)pages++;}
            return pages*4096;
        } finally { Marshal.FreeHGlobal(buffer); }
    }
    public static long Count(IntPtr process,long address,int bytes) {
        int count=(bytes+4095)/4096;var pages=new Page[count];
        for(int i=0;i<count;i++)pages[i].Address=new IntPtr(address+i*4096L);
        if(!QueryWorkingSetEx(process,pages,count*Marshal.SizeOf(typeof(Page))))throw new System.ComponentModel.Win32Exception();
        long resident=0;foreach(var page in pages)if((page.Attributes.ToUInt64()&1)!=0)resident+=4096;return resident;
    }
}
'@
}
