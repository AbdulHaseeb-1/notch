# fix-crashes-round2.ps1
# Round 2: keep Windows Update quiet (re-apply + verify), raise pagefile headroom,
# inventory IObit, collect more evidence, register the freeze-watch logon task.
# Run as Administrator.

$log = "$env:USERPROFILE\Desktop\fix-log-round2-2026-10-18.txt"
"=== Round 2 started $(Get-Date) ===" | Set-Content -Path $log
function Log([string]$m) {
  $line = "{0}  {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
  Add-Content -Path $log -Value $line
  Write-Host $line
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
Log "Admin: $isAdmin"
if (-not $isAdmin) { Log 'ERROR: needs admin.'; exit 1 }

# --- 1) Windows Update quiet-mode (re-apply, then verify) ---
try {
  New-Item 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' -Force | Out-Null
  Set-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' -Name NoAutoUpdate -Value 1 -Type DWord
  $v = (Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' -ErrorAction Stop).NoAutoUpdate
  Log "NoAutoUpdate set, reads back as: $v"
} catch { Log "NoAutoUpdate FAILED: $($_.Exception.Message)" }
$q = (reg query 'HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' /v NoAutoUpdate 2>&1) -join ' | '
Log "reg query: $q"

try {
  $ux = 'HKLM:\SOFTWARE\Microsoft\Windows\WindowsUpdate\UX\Settings'
  New-Item $ux -Force | Out-Null
  $start = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
  $end = (Get-Date).AddDays(7).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
  'PauseFeatureUpdatesStartTime','PauseQualityUpdatesStartTime' | ForEach-Object { Set-ItemProperty $ux -Name $_ -Value $start -Type String }
  'PauseFeatureUpdatesEndTime','PauseQualityUpdatesEndTime','PauseUpdatesExpiryTime' | ForEach-Object { Set-ItemProperty $ux -Name $_ -Value $end -Type String }
  Log "Pause keys set: $start -> $end"
} catch { Log "Pause keys FAILED: $($_.Exception.Message)" }

try {
  New-Item 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Device Metadata' -Force | Out-Null
  Set-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Device Metadata' -Name PreventDeviceMetadataFromNetwork -Value 1 -Type DWord
  Log 'PreventDeviceMetadataFromNetwork=1'
} catch { Log "Device Metadata FAILED: $($_.Exception.Message)" }

# --- 2) Update services quiet ---
foreach ($s in 'wuauserv','UsoSvc','DoSvc','BITS') { try { Stop-Service $s -Force -ErrorAction SilentlyContinue } catch {} }
Get-Process MoUsoCoreWorker, usoclient, UsoClient -ErrorAction SilentlyContinue | ForEach-Object { try { Stop-Process -Id $_.Id -Force; Log "Stopped process $($_.Name)" } catch {} }
foreach ($s in 'UsoSvc','DoSvc') { try { Set-Service $s -StartupType Manual; Log "$s -> Manual" } catch { Log "$s startup change failed: $($_.Exception.Message)" } }
try { Set-Service WaaSMedicSvc -StartupType Disabled -ErrorAction Stop; Log 'WaaSMedicSvc -> Disabled' } catch { Log "WaaSMedicSvc change failed (usually protected, harmless): $($_.Exception.Message)" }

# --- 3) Disable WU-related scheduled tasks (reversible; see re-enable notes) ---
$tasks = @(
  '\Microsoft\Windows\UpdateOrchestrator\Schedule Scan',
  '\Microsoft\Windows\UpdateOrchestrator\Schedule Scan Static Task',
  '\Microsoft\Windows\UpdateOrchestrator\UUS Failover Task',
  '\Microsoft\Windows\UpdateOrchestrator\Reboot_AC',
  '\Microsoft\Windows\UpdateOrchestrator\Reboot_DC',
  '\Microsoft\Windows\UpdateOrchestrator\USO_UxBroker_Display',
  '\Microsoft\Windows\WindowsUpdate\Scheduled Start',
  '\Microsoft\Windows\WaaSMedic\PerformRemediation'
)
foreach ($t in $tasks) {
  $r = (schtasks /Change /TN $t /Disable 2>&1) -join ' '
  Log ("task {0} -> {1}" -f $t, $r)
}

# --- 4) Pagefile -> system managed (takes effect after reboot) ---
try {
  $cs = Get-CimInstance Win32_ComputerSystem
  Log "AutomaticManagedPagefile before: $($cs.AutomaticManagedPagefile)"
  Set-CimInstance -InputObject $cs -Property @{ AutomaticManagedPagefile = $true } -ErrorAction Stop
  Log "AutomaticManagedPagefile after: $((Get-CimInstance Win32_ComputerSystem).AutomaticManagedPagefile)"
} catch { Log "Pagefile change FAILED: $($_.Exception.Message)" }

# --- 5) IObit inventory + re-disable tasks ---
$ev = "$env:USERPROFILE\Desktop\crash-evidence-2026-10-18"
try {
  Get-ChildItem 'C:\Program Files (x86)\IObit' -Recurse -Depth 2 -Force -ErrorAction SilentlyContinue |
    Select-Object FullName, @{n='KB';e={[math]::Round($_.Length/1KB)}}, LastWriteTime |
    Out-String -Width 250 | Out-File "$ev\iobit-files.txt"
  Log 'Saved iobit-files.txt'
} catch { Log "IObit listing failed: $($_.Exception.Message)" }
try {
  (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*','HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
    Where-Object { $_.DisplayName -match 'IObit|Booster|Advanced System' } |
    Select-Object DisplayName, DisplayVersion, UninstallString | Out-String) | Out-File "$ev\iobit-uninstall.txt"
  Log 'Saved iobit-uninstall.txt'
} catch { Log "Uninstall search failed: $($_.Exception.Message)" }
Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object { $_.TaskName -match 'IObit|Driver Booster' -or $_.TaskPath -match 'IObit' } |
  ForEach-Object { try { Disable-ScheduledTask -TaskName $_.TaskName -TaskPath $_.TaskPath | Out-Null; Log "Disabled task: $($_.TaskPath)$($_.TaskName)" } catch {} }

# --- 6) Extra evidence ---
try { systeminfo | Out-File "$ev\systeminfo.txt"; Log 'Saved systeminfo.txt' } catch { Log "systeminfo failed: $($_.Exception.Message)" }
try { powercfg /sleepstudy /output "$ev\sleepstudy.html" | Out-Null; Log 'Saved sleepstudy.html' } catch { Log "sleepstudy failed: $($_.Exception.Message)" }

# --- 7) Register freeze-watch to auto-run at logon ---
try {
  $script = Join-Path $PSScriptRoot 'freeze-watch.ps1'
  if (-not (Test-Path $script)) { throw "missing $script" }
  $act = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}"' -f $script)
  $trg = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
  $set = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
  Register-ScheduledTask -TaskName 'opencode-freeze-watch' -Action $act -Trigger $trg -Settings $set -Force -ErrorAction Stop | Out-Null
  Log 'freeze-watch logon task registered'
} catch { Log "freeze-watch task FAILED: $($_.Exception.Message)" }

# --- 8) Final verification ---
try {
  $s1 = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\DriverSearching').SearchOrderConfig
  $s2 = (Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate').ExcludeWUDriversInQualityUpdate
  $s3 = (Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' -ErrorAction SilentlyContinue).NoAutoUpdate
  Log "VERIFY: SearchOrderConfig=$s1 ExcludeWUDrivers=$s2 NoAutoUpdate=$s3"
} catch { Log "VERIFY failed: $($_.Exception.Message)" }

Log 'Round 2 DONE.'
Start-Sleep -Seconds 4
