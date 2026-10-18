# fix-crashes-2026-10-18.ps1
# Emergency stability fix for repeated hard freezes (Dell Latitude 7430 / Intel Iris Xe)
# Run as Administrator. Writes a log to the Desktop.

$ErrorActionPreference = 'Continue'
$log = "$env:USERPROFILE\Desktop\fix-log-2026-10-18.txt"
"=== Stability fix started $(Get-Date) ===" | Set-Content -Path $log

function Log([string]$m) {
  $line = "{0}  {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
  Add-Content -Path $log -Value $line
  Write-Host $line
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
Log "Admin: $isAdmin"
if (-not $isAdmin) { Log 'ERROR: needs admin. Exiting.'; exit 1 }

# 1) Create a restore point first
try { Enable-ComputerRestore -Drive "$env:SystemDrive\" -ErrorAction SilentlyContinue | Out-Null } catch {}
try { Checkpoint-Computer -Description 'Before stability fix (hard freezes)' -RestorePointType MODIFY_SETTINGS; Log 'Restore point created' } catch { Log "Restore point step: $($_.Exception.Message)" }

# 2) Stop Windows Update from installing drivers + pause auto updates
try {
  New-Item 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\DriverSearching' -Force | Out-Null
  Set-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\DriverSearching' -Name SearchOrderConfig -Value 0 -Type DWord
  Log 'OK: SearchOrderConfig=0 (Windows Update will NOT install drivers)'
} catch { Log "SearchOrderConfig failed: $($_.Exception.Message)" }

try {
  New-Item 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' -Force | Out-Null
  Set-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' -Name NoAutoUpdate -Value 1 -Type DWord
  Log 'OK: NoAutoUpdate=1 (automatic updates paused)'
} catch { Log "NoAutoUpdate failed: $($_.Exception.Message)" }

try {
  New-Item 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate' -Force | Out-Null
  Set-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate' -Name ExcludeWUDriversInQualityUpdate -Value 1 -Type DWord
  Log 'OK: ExcludeWUDriversInQualityUpdate=1'
} catch { Log "ExcludeWUDrivers failed: $($_.Exception.Message)" }

# 3) Stop update services now + clear queued (bad) driver downloads
foreach ($s in 'wuauserv','UsoSvc','BITS','DoSvc') {
  try { Stop-Service $s -Force -ErrorAction Stop; Log "Stopped $s" } catch { Log "Stop ${s}: $($_.Exception.Message)" }
}
try {
  Get-ChildItem 'C:\Windows\SoftwareDistribution\Download' -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
  Log 'Cleared SoftwareDistribution\Download (removed queued bad drivers)'
} catch { Log "Clear download failed: $($_.Exception.Message)" }

# 4) Neutralize IObit Driver Booster leftovers
foreach ($p in 'C:\Program Files (x86)\IObit','C:\Program Files\IObit',"$env:LOCALAPPDATA\IObit","$env:APPDATA\IObit") {
  if (Test-Path $p) { Log "IObit folder present: $p" }
}
Get-Service -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'iobit|booster' -or $_.DisplayName -match 'IObit|Driver Booster' } | ForEach-Object {
  try {
    Stop-Service $_.Name -Force -ErrorAction SilentlyContinue
    Set-Service $_.Name -StartupType Disabled -ErrorAction SilentlyContinue
    Log "Disabled IObit service: $($_.Name)"
  } catch { Log "IObit svc $($_.Name): $($_.Exception.Message)" }
}
Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'Booster|IObit' } | ForEach-Object {
  try { Stop-Process -Id $_.Id -Force; Log "Killed IObit process: $($_.Name)" } catch { Log "kill $($_.Name) failed" }
}
Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object { $_.TaskName -match 'IObit|Driver Booster' -or $_.TaskPath -match 'IObit' } | ForEach-Object {
  try { Disable-ScheduledTask -TaskName $_.TaskName -TaskPath $_.TaskPath | Out-Null; Log "Disabled IObit task: $($_.TaskPath)$($_.TaskName)" } catch { Log "IObit task $($_.TaskName): $($_.Exception.Message)" }
}

# 5) Collect crash evidence (for deeper analysis / Dell support)
$ev = "$env:USERPROFILE\Desktop\crash-evidence-2026-10-18"
New-Item -ItemType Directory -Path $ev -Force | Out-Null
try { Copy-Item 'C:\Windows\LiveKernelReports\*' $ev -Recurse -Force -ErrorAction SilentlyContinue; Log 'Copied LiveKernelReports (WATCHDOG dumps)' } catch { Log "Copy LiveKernelReports: $($_.Exception.Message)" }
try { Copy-Item 'C:\ProgramData\Microsoft\Windows\WER\ReportQueue\Kernel_*' $ev -Recurse -Force -ErrorAction SilentlyContinue; Log 'Copied WER Kernel reports' } catch { Log "Copy WER: $($_.Exception.Message)" }
try { pnputil /enum-drivers | Out-File "$ev\all-drivers.txt"; Log 'Saved all-drivers.txt' } catch {}
try {
  Get-WinEvent -FilterHashtable @{LogName='System'; Id=41,6008} -MaxEvents 30 -ErrorAction SilentlyContinue |
    ForEach-Object { '{0}  Id={1}  {2}' -f $_.TimeCreated, $_.Id, (($_.Message -split "`r?`n")[0]) } |
    Out-File "$ev\kernel-power-41.txt"
  Log 'Saved kernel-power-41.txt'
} catch {}
try {
  (Get-CimInstance Win32_PnPSignedDriver |
    Where-Object { $_.DeviceName -match 'Iris|Dynamic|Thermal|Management|Audio|Wi-Fi|Bluetooth' } |
    Select-Object DeviceName, DriverProviderName, DriverVersion, DriverDate |
    Format-Table -AutoSize | Out-String) | Out-File "$ev\current-drivers.txt"
  Log 'Saved current-drivers.txt'
} catch {}
try { powercfg /a | Out-File "$ev\power-states.txt" } catch {}

Log 'DONE. Reboot recommended. Log: Desktop\fix-log-2026-10-18.txt'
Start-Sleep -Seconds 4
