# Sets up Noch's local Mem0 stack: writes .env with random secrets, starts the
# containers, waits until the API answers, and stores the API key where Noch
# reads it (%LOCALAPPDATA%\noch\mem0-local.key), so nothing has to be pasted.
#   .\setup.ps1            start / update
#   .\setup.ps1 -Down      stop (data stays in the Docker volumes)
#   .\setup.ps1 -Reset     stop and delete all stored memories
param([switch]$Down, [switch]$Reset)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if ($Reset) { docker compose down -v; Remove-Item .env -ErrorAction SilentlyContinue; exit 0 }
if ($Down)  { docker compose down; exit 0 }

function New-Secret([int]$bytes) {
    $b = New-Object byte[] $bytes
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b)
    ([BitConverter]::ToString($b) -replace '-', '').ToLower()
}

if (-not (Test-Path .env)) { Copy-Item .env.example .env }
$env_ = Get-Content .env -Raw
foreach ($name in 'POSTGRES_PASSWORD', 'MEM0_SERVER_KEY') {
    if ($env_ -notmatch "(?m)^$name=\S+") {
        $env_ = [regex]::Replace($env_, "(?m)^$name=.*$", "$name=$(New-Secret 24)")
    }
}
Set-Content .env $env_ -NoNewline -Encoding utf8

$port = if ($env_ -match '(?m)^MEM0_PORT=(\d+)') { $Matches[1] } else { '8765' }
$key  = [regex]::Match($env_, '(?m)^MEM0_SERVER_KEY=(\S+)').Groups[1].Value

docker info *> $null
if ($LASTEXITCODE -ne 0) { throw 'Docker is not running. Start Docker Desktop and run this again.' }

docker compose up -d --build
if ($LASTEXITCODE -ne 0) { throw 'docker compose failed' }

$dir = Join-Path $env:LOCALAPPDATA 'noch'
New-Item -ItemType Directory -Force $dir | Out-Null
Set-Content (Join-Path $dir 'mem0-local.key') $key -NoNewline -Encoding ascii

Write-Host "Waiting for Mem0 on http://127.0.0.1:$port ..."
$deadline = (Get-Date).AddMinutes(20)   # model download dominates the first start
while ((Get-Date) -lt $deadline) {
    try {
        if ((Invoke-RestMethod "http://127.0.0.1:$port/health" -TimeoutSec 3).status -eq 'ok') {
            Write-Host 'Mem0 is up. In Noch: Settings > Agent & tools > Long-term memory (Local is the default).'
            exit 0
        }
    } catch { Start-Sleep 5 }
}
throw 'Mem0 did not become healthy in time. Check: docker compose logs'
