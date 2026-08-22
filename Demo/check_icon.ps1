$exe = "D:\Cpp_Udp_vid\ctfmon.exe"
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class ERes {
    [DllImport("kernel32.dll")] public static extern IntPtr BeginUpdateResource(string f, bool d);
    [DllImport("kernel32.dll")] public static extern bool EnumResourceNames(IntPtr h, IntPtr t, EnumProc cb, IntPtr p);
    public delegate bool EnumProc(IntPtr h, IntPtr t, IntPtr n, IntPtr p);
    public static string? last;
}
"@
# Use libgcc-free approach: load exe resources via LoadLibraryEx
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class Loader {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr LoadLibraryEx(string path, IntPtr h, uint flags);
    [DllImport("kernel32.dll", SetLastError=true) ]
    public static extern bool EnumResourceNames(IntPtr h, IntPtr tp, Callback cb, IntPtr p);
    public delegate bool Callback(IntPtr h, IntPtr type, IntPtr name, IntPtr p);
    [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr h);
}
"@
$h = [Loader]::LoadLibraryEx($exe, [IntPtr]::Zero, 0x2) # LOAD_LIBRARY_AS_DATAFILE
Write-Host "module=$h"
if($h -ne [IntPtr]::Zero){
  $found = @()
  [Loader]::EnumResourceNames($h, [IntPtr]10, {
    param($h, $type, $name, $p)
    $script:found += $name
    return $true
  }, [IntPtr]::Zero) | Out-Null
  Write-Host "RT_ICON resources: $($found.Count)"
  $found2 = @()
  [Loader]::EnumResourceNames($h, [IntPtr]14, {
    param($h, $type, $name, $p)
    $script:found2 += $name
    return $true
  }, [IntPtr]::Zero) | Out-Null
  Write-Host "RT_GROUP_ICON resources: $($found2.Count)"
  [Loader]::FreeLibrary($h) | Out-Null
}