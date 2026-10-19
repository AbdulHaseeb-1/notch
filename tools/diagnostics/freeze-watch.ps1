# freeze-watch.ps1 - lightweight resource logger for freeze diagnosis.
# Writes one line every 15 seconds to Desktop\freeze-watch.log.
# If the PC hard-freezes again, the tail of this log shows what was building up.

$log = "$env:USERPROFILE\Desktop\freeze-watch.log"
"=== session start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===" | Add-Content -Path $log

while ($true) {
  try {
    $m  = Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory -ErrorAction Stop
    $pr = Get-Process -ErrorAction SilentlyContinue
    $top = $pr | Sort-Object WorkingSet64 -Descending | Select-Object -First 3
    $topStr = ($top | ForEach-Object { "{0}:{1}M" -f $_.ProcessName, [math]::Round($_.WorkingSet64/1MB) }) -join ' '
    $line = "{0} | avail {1}M | commit {2}/{3}M | poolNP {4}M | poolP {5}M | procs {6} | handles {7} | top {8}" -f `
      (Get-Date -Format 'HH:mm:ss'), `
      [math]::Round($m.AvailableBytes/1MB), `
      [math]::Round($m.CommittedBytes/1MB), `
      [math]::Round($m.CommitLimit/1MB), `
      [math]::Round($m.PoolNonpagedBytes/1MB), `
      [math]::Round($m.PoolPagedBytes/1MB), `
      $pr.Count, `
      ($pr | Measure-Object HandleCount -Sum).Sum, `
      $topStr
    Add-Content -Path $log -Value $line
  } catch {
    Add-Content -Path $log -Value ("{0} | ERROR {1}" -f (Get-Date -Format 'HH:mm:ss'), $_.Exception.Message)
  }
  Start-Sleep -Seconds 15
}
