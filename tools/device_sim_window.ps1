# tools/device_sim_window.ps1 -- give the running OpenBA window a client area of exactly W x H.
#
# tools/device_sim.sh writes the device's size into settings.cfg and the game creates its window at
# that size, but a desktop window manager may move and resize a window when it is first shown
# (PowerToys FancyZones "move newly created windows to their last known zone" snapped a 3392x2400
# window into a 2528x1345 zone). This puts the client size back with SetWindowPos; the game takes
# the WM_SIZE like any user resize (the back buffer follows the client area). No focus change, no
# activation, no new window.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/device_sim_window.ps1 -Width W -Height H
# Exit:  0 = the client area is W x H; 1 = no window; 2 = the size was not granted.

param(
    [int]$Width,
    [int]$Height,
    [string]$Process = "OpenBA"
)

$src = @"
using System;
using System.Runtime.InteropServices;
public class DevSimWin {
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int ht, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
if (-not ("DevSimWin" -as [type])) { Add-Type -TypeDefinition $src }

$p = Get-Process -Name $Process -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output "no $Process process"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "no main window handle"; exit 1 }

function ClientSize() {
    $c = New-Object DevSimWin+RECT
    [void][DevSimWin]::GetClientRect($h, [ref]$c)
    return @(($c.R - $c.L), ($c.B - $c.T))
}

$before = ClientSize
if ($before[0] -eq $Width -and $before[1] -eq $Height) {
    Write-Output "client already ${Width}x${Height}"
    exit 0
}
$w = New-Object DevSimWin+RECT
[void][DevSimWin]::GetWindowRect($h, [ref]$w)
$frameW = ($w.R - $w.L) - $before[0]
$frameH = ($w.B - $w.T) - $before[1]
# SWP_NOZORDER 0x4 | SWP_NOACTIVATE 0x10 | SWP_NOSENDCHANGING 0x400: without the last, DefWindowProc's
# WM_GETMINMAXINFO clamps the size to the virtual screen (a 2400-high client came back 1421 high on
# 1440-high monitors). The window's left edge goes to the primary display's origin.
[void][DevSimWin]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, $Width + $frameW, $Height + $frameH, 0x414)
Start-Sleep -Milliseconds 500
$after = ClientSize
Write-Output ("client {0}x{1} -> {2}x{3} (asked {4}x{5})" -f $before[0], $before[1], $after[0], $after[1], $Width, $Height)
if ($after[0] -ne $Width -or $after[1] -ne $Height) { exit 2 }
exit 0
