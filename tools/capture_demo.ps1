# capture_demo.ps1 - lance minidisk sur un dossier et capture la fenetre dans
# build\demo.png. Sert aux captures de livraison des tickets.
#   powershell -ExecutionPolicy Bypass -File tools\capture_demo.ps1 -Folder "C:\musique" -Query "the"
param(
    [string]$Folder = "$env:TEMP\minidisk_demo",
    [string]$Query = "",
    # Plan a ouvrir au demarrage (.mdplan ou .mdplan.txt), cf. gen_demo_plan.py.
    [string]$Plan = "",
    [string]$Out = "build\demo.png",
    [int]$WaitMs = 5000
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Cap {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    // Sans ca, PowerShell est DPI-unaware : GetWindowRect et CopyFromScreen
    // renvoient des coordonnees virtualisees et la capture est floue et rognee.
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

[void][Win32Cap]::SetProcessDPIAware()

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\minidisk.exe"
# -Folder "" : on ne passe aucun dossier, ce qui donne l'etat vide de T-013.
$args = ""
if ($Folder -ne "") { $args = "--scan `"$Folder`"" }
if ($Query -ne "") { $args += " --query `"$Query`"" }
if ($Plan -ne "") { $args += " --plan `"$Plan`"" }
if ($args -eq "") { $args = "--noscan" }

$proc = Start-Process -FilePath $exe -ArgumentList $args -PassThru
Start-Sleep -Milliseconds $WaitMs
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { $proc.Kill(); throw "pas de fenetre" }
[void][Win32Cap]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 700

$rect = New-Object Win32Cap+RECT
[void][Win32Cap]::GetWindowRect($hwnd, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
$path = Join-Path $root $Out
$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
$proc.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 500
if (-not $proc.HasExited) { $proc.Kill() }
Write-Output "$path ($width x $height)"
