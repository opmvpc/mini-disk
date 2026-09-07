# Installs WinUSB on the Sony Net MD Walkman (VID 054C, PID 0084) by driving Zadig
# through UI Automation. Must run elevated. Writes a log next to itself.
$ErrorActionPreference = 'Continue'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$log = Join-Path $dir 'install_winusb.log'
function Log($m) { $line = "$(Get-Date -Format HH:mm:ss) $m"; Add-Content -Path $log -Value $line; Write-Host $line }
Set-Content -Path $log -Value "start"

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Windows.Forms
$A = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]::Children
$TD = [System.Windows.Automation.TreeScope]::Descendants

$zadig = Join-Path $dir 'zadig-2.9.exe'
Log "launching $zadig"
$p = Start-Process -FilePath $zadig -PassThru
$win = $null
for ($i = 0; $i -lt 60 -and -not $win; $i++) {
    Start-Sleep -Milliseconds 500
    $win = $A::RootElement.FindAll($TC, [System.Windows.Automation.Condition]::TrueCondition) |
        Where-Object { $_.Current.ProcessId -eq $p.Id -and $_.Current.Name -match 'Zadig' } | Select-Object -First 1
}
if (-not $win) { Log "zadig window not found"; exit 2 }
Log "window: $($win.Current.Name)"
# Zadig opens an "update check" dialog sometimes; dismiss any dialog belonging to the process.
Start-Sleep -Milliseconds 800
$dlgs = $A::RootElement.FindAll($TC, [System.Windows.Automation.Condition]::TrueCondition) |
    Where-Object { $_.Current.ProcessId -eq $p.Id -and $_.Current.NativeWindowHandle -ne $win.Current.NativeWindowHandle }
foreach ($d in $dlgs) {
    Log "extra dialog: $($d.Current.Name)"
    $no = $d.FindAll($TD, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -match '^(No|Cancel|Close|OK)$' } | Select-Object -First 1
    if ($no) { $no.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Log "dismissed with $($no.Current.Name)" }
}

function Dump($el) {
    $el.FindAll($TD, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object {
        Log ("  [{0}] '{1}' id={2}" -f $_.Current.ControlType.ProgrammaticName, $_.Current.Name, $_.Current.AutomationId)
    }
}
Dump $win

# The device combo box: expand it and pick the Net MD Walkman.
$combos = $win.FindAll($TD, (New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, [System.Windows.Automation.ControlType]::ComboBox)))
Log "combos: $($combos.Count)"
$deviceCombo = $combos | Select-Object -First 1
$sel = $null
if ($deviceCombo) {
    try { $deviceCombo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand() } catch { Log "expand failed: $_" }
    Start-Sleep -Milliseconds 500
    $items = $A::RootElement.FindAll($TD, (New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)))
    foreach ($it in $items) { Log "  item: '$($it.Current.Name)'" }
    $sel = $items | Where-Object { $_.Current.Name -match 'Net MD' } | Select-Object -First 1
    if ($sel) {
        $sel.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Log "selected: $($sel.Current.Name)"
    } else {
        Log "Net MD item not found in list"
        try { $deviceCombo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Collapse() } catch {}
    }
}
if (-not $sel) {
    # Fallback: the combo may already show the only driverless device.
    $edits = $win.FindAll($TD, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -match 'Net MD' }
    if ($edits.Count -gt 0) { Log "Net MD already displayed" } else { Log "device not visible; aborting"; $p.Kill(); exit 3 }
}
Start-Sleep -Milliseconds 500
Dump $win

# Target driver must read WinUSB (it is Zadig's default). The button text is
# "Install Driver" (or "Replace Driver" / "Reinstall Driver").
$btn = $win.FindAll($TD, (New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button))) |
    Where-Object { $_.Current.Name -match 'Install|Replace|Reinstall' } | Select-Object -First 1
if (-not $btn) { Log "install button not found"; $p.Kill(); exit 4 }
$winusb = $win.FindAll($TD, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -match 'WinUSB' } | Select-Object -First 1
if (-not $winusb) { Log "WinUSB target not shown; aborting"; $p.Kill(); exit 5 }
Log "target: $($winusb.Current.Name); clicking '$($btn.Current.Name)'"
$btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()

# Installation takes a while; watch for the result dialog and any security prompt.
$done = $false
for ($i = 0; $i -lt 240 -and -not $done; $i++) {
    Start-Sleep -Seconds 1
    $dlgs = $A::RootElement.FindAll($TC, [System.Windows.Automation.Condition]::TrueCondition) |
        Where-Object { $_.Current.ProcessId -eq $p.Id -and $_.Current.NativeWindowHandle -ne $win.Current.NativeWindowHandle }
    foreach ($d in $dlgs) {
        $texts = ($d.FindAll($TD, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name }) -join ' | '
        Log "dialog: '$($d.Current.Name)' :: $texts"
        if ($texts -match 'success|succ') { $done = $true }
        $close = $d.FindAll($TD, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -match '^(Close|OK)$' } | Select-Object -First 1
        if ($close) { $close.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
    }
    # Windows security dialog ("Would you like to install this device software?") lives in another process.
    $sec = $A::RootElement.FindAll($TC, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -match 'Windows Security|Sécurité de Windows' } | Select-Object -First 1
    if ($sec) {
        $inst = $sec.FindAll($TD, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -match '^Install|^Installer' } | Select-Object -First 1
        if ($inst) { Log "security prompt: clicking $($inst.Current.Name)"; $inst.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
    }
}
Log "done=$done"
Start-Sleep -Seconds 1
try { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 1; if (-not $p.HasExited) { $p.Kill() } } catch {}
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_054C&PID_0084' } | ForEach-Object { Log "device: $($_.Status) class=$($_.Class) $($_.FriendlyName)" }
Log "end"
