# capture_demo.ps1 - lance minidisk sur un dossier et capture la fenetre dans
# build\demo.png. Sert aux captures de livraison des tickets.
#   powershell -ExecutionPolicy Bypass -File tools\capture_demo.ps1 -Folder "C:\musique" -Query "the"
param(
    [string]$Folder = "$env:TEMP\minidisk_demo",
    [string]$Query = "",
    # Plan a ouvrir au demarrage (.mdplan ou .mdplan.txt), cf. gen_demo_plan.py.
    [string]$Plan = "",
    [string]$Out = "build\demo.png",
    [int]$WaitMs = 5000,
    # Arguments supplementaires passes tels quels (ex: "--transfer").
    [string]$Extra = "",
    # Capture par handle de fenetre (PrintWindow) plutot que par region d ecran :
    # indispensable quand une autre fenetre recouvre la notre (T-043).
    [switch]$ByHandle
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
    // PW_RENDERFULLCONTENT (2) : sans lui une fenetre OpenGL revient noire.
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    // Notre fenetre est OpenGL : PrintWindow la rend noire. On la met donc au
    // premier plan de force le temps de la capture, ce qui garantit qu aucune
    // fenetre de l utilisateur ne se retrouve dans l image (T-043).
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
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
if ($Extra -ne "") { $args += " $Extra" }
if ($args -eq "") { $args = "--noscan" }

$proc = Start-Process -FilePath $exe -ArgumentList $args -PassThru
Start-Sleep -Milliseconds $WaitMs
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { $proc.Kill(); throw "pas de fenetre" }
# HWND_TOPMOST (-1), SWP_NOMOVE|SWP_NOSIZE (0x0003) : la fenetre passe devant
# tout le reste avant la capture, puis redevient normale (HWND_NOTOPMOST, -2).
[void][Win32Cap]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0003)
[void][Win32Cap]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 900

$rect = New-Object Win32Cap+RECT
[void][Win32Cap]::GetWindowRect($hwnd, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
if ($ByHandle) {
    # La fenetre peut etre recouverte : on demande a Windows de la redessiner
    # dans notre DC. On retombe sur la capture d ecran si PrintWindow echoue.
    $hdc = $graphics.GetHdc()
    $ok = [Win32Cap]::PrintWindow($hwnd, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    if (-not $ok) { $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size) }
} else {
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
}
[void][Win32Cap]::SetWindowPos($hwnd, [IntPtr](-2), 0, 0, 0, 0, 0x0003)
$path = Join-Path $root $Out
$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
$proc.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 500
if (-not $proc.HasExited) { $proc.Kill() }
Write-Output "$path ($width x $height)"
