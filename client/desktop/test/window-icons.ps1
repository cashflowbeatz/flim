param([Parameter(Mandatory=$true)][long]$WindowHandle, [Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FlimWindowIcons {
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@
foreach ($entry in @(@{ Name='window-small'; Kind=0 }, @{ Name='window-large'; Kind=1 })) {
    $handle = [FlimWindowIcons]::SendMessage([IntPtr]$WindowHandle, 0x007F, [IntPtr]$entry.Kind, [IntPtr]::Zero)
    if ($handle -eq [IntPtr]::Zero) { throw "The running window has no $($entry.Name) icon." }
    $icon = [System.Drawing.Icon]::FromHandle($handle)
    $bitmap = $icon.ToBitmap()
    try { $bitmap.Save((Join-Path $OutputDirectory "$($entry.Name).png"), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose(); $icon.Dispose() }
}
