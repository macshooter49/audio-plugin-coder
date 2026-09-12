# ==============================================================================================
#  INSTALL THE NEWEST TERRAIN WINDOWS BUILD — one command, no clicking through GitHub.
#
#      powershell -ExecutionPolicy Bypass -File C:\dev\audio-plugin-coder\scripts\win-install-latest.ps1
#
#  What it does: finds the newest "Terrain Windows" CI run on the windows-test branch, WAITS for it
#  if it is still building, downloads its artifact, and copies Terrain.vst3 (plus Terrain Glitch /
#  Terrain Chop) into C:\Program Files\Common Files\VST3, and the Standalone into
#  %LOCALAPPDATA%\Terrain\Standalone. It refuses to run while a DAW is open (Windows locks a loaded
#  plugin), and it never installs a FAILED build.
#
#  One-time setup:  winget install GitHub.cli   then   gh auth login
#  No UAC prompt for Terrain.vst3 if you ran scripts\win-allow-vst3-install.ps1 once as admin;
#  otherwise the copy asks for elevation once.
#
#  Options:  -NoWait    fail instead of waiting for a running build
#            -LastGood  install the newest SUCCESSFUL build even if a newer one failed
# ==============================================================================================
param(
    [string]$Repo   = 'macshooter49/audio-plugin-coder',
    [string]$Branch = 'windows-test',
    [switch]$NoWait,
    [switch]$LastGood
)
# Windows PowerShell 5.1 turns a native command's stderr into a terminating error under 'Stop' (gh prints even its
# "logged in" status on stderr), so native calls are judged by $LASTEXITCODE and file operations opt in with
# -ErrorAction Stop.
$ErrorActionPreference = 'Continue'

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host "GitHub CLI not found. Install it once:  winget install GitHub.cli   then run:  gh auth login" -ForegroundColor Yellow
    exit 1
}
& gh auth status *> $null
if ($LASTEXITCODE -ne 0) { Write-Host "Sign in once:  gh auth login" -ForegroundColor Yellow; exit 1 }

$daws = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(FL64|FL|Ableton.*|reaper|Bitwig.*|Cubase.*|Studio One|Terrain)$' }
if ($daws) {
    Write-Host ("Close these first (Windows locks a loaded plugin): " + (($daws | Select-Object -ExpandProperty ProcessName -Unique) -join ', ')) -ForegroundColor Red
    exit 1
}

function Get-Run([string]$extra) {
    $json = & gh run list --repo $Repo --workflow terrain-windows.yml --branch $Branch --limit 1 --json databaseId,status,conclusion,headSha,createdAt $extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
    $r = $json | ConvertFrom-Json
    if (-not $r) { return $null }
    return $r[0]
}

$run = if ($LastGood) { Get-Run '--status success' } else { Get-Run '' }
if (-not $run) { Write-Host "No Windows build found on $Branch." -ForegroundColor Red; exit 1 }
$sha = $run.headSha.Substring(0, 7)

if ($run.status -ne 'completed') {
    if ($NoWait) { Write-Host "Build $sha is still running." -ForegroundColor Yellow; exit 1 }
    Write-Host "Build $sha is still compiling - waiting for it (usually 15-40 min)..." -ForegroundColor Cyan
    & gh run watch $run.databaseId --repo $Repo --interval 30 *> $null
    $run = & gh run view $run.databaseId --repo $Repo --json databaseId,status,conclusion,headSha,createdAt | ConvertFrom-Json
}
if ($run.conclusion -ne 'success') {
    Write-Host "Build $sha did not succeed ($($run.conclusion)) - not installing it." -ForegroundColor Red
    Write-Host "Log: https://github.com/$Repo/actions/runs/$($run.databaseId)   (or re-run with -LastGood)" -ForegroundColor Red
    exit 1
}

$tmp = Join-Path $env:TEMP ("terrain-build-" + $run.databaseId)
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
Write-Host "Downloading build $sha ($($run.createdAt))..." -ForegroundColor Cyan
& gh run download $run.databaseId --repo $Repo --name Terrain-windows-vst3 --dir $tmp
if ($LASTEXITCODE -ne 0) { Write-Host "Download failed." -ForegroundColor Red; exit 1 }
# Terrain Glitch + Terrain Chop ship as their own artifact since fb636 (absent on older runs - not an error).
& gh run download $run.databaseId --repo $Repo --name TerrainFX-cards-windows-vst3 --dir (Join-Path $tmp 'cards') *> $null

$vst3Dst  = 'C:\Program Files\Common Files\VST3'
$bundles  = Get-ChildItem -Path $tmp -Recurse -Directory -Filter '*.vst3' | Where-Object { $_.Parent.Name -eq 'VST3' }
if (-not $bundles) { Write-Host "No .vst3 bundle in the download." -ForegroundColor Red; exit 1 }

# Replace each bundle's CONTENTS in place (the win-allow grant covers the contents of Terrain.vst3).
$needAdmin = @()
foreach ($b in $bundles) {
    $target = Join-Path $vst3Dst $b.Name
    try {
        if (-not (Test-Path $target)) { New-Item -ItemType Directory -Path $target -Force -ErrorAction Stop | Out-Null }
        Get-ChildItem -Path $target -Force -ErrorAction Stop | Remove-Item -Recurse -Force -ErrorAction Stop
        Copy-Item -Path (Join-Path $b.FullName '*') -Destination $target -Recurse -Force -ErrorAction Stop
        Write-Host "installed $($b.Name)" -ForegroundColor Green
    } catch {
        $needAdmin += $b
    }
}
if ($needAdmin.Count -gt 0) {
    Write-Host "Administrator rights needed for: $(($needAdmin | ForEach-Object Name) -join ', ') - approve the prompt." -ForegroundColor Yellow
    $cmds = foreach ($b in $needAdmin) {
        $t = Join-Path $vst3Dst $b.Name
        "if (Test-Path '$t') { Remove-Item -Recurse -Force '$t' }; Copy-Item -Recurse -Force '$($b.FullName)' '$vst3Dst'"
    }
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList @('-NoProfile', '-Command', ($cmds -join '; '))
    if ($p.ExitCode -ne 0) { Write-Host "Elevated copy failed (exit $($p.ExitCode))." -ForegroundColor Red; exit 1 }
    foreach ($b in $needAdmin) { Write-Host "installed $($b.Name)" -ForegroundColor Green }
}

$exe = Get-ChildItem -Path $tmp -Recurse -File -Filter 'Terrain.exe' | Select-Object -First 1
if ($exe) {
    $sa = Join-Path $env:LOCALAPPDATA 'Terrain\Standalone'
    New-Item -ItemType Directory -Path $sa -Force | Out-Null
    Copy-Item -Path (Join-Path $exe.Directory.FullName '*') -Destination $sa -Recurse -Force
    Write-Host "Standalone: $sa\Terrain.exe" -ForegroundColor Green
}

Remove-Item -Recurse -Force $tmp
Write-Host "Done - Terrain build $sha is installed. Rescan plugins in your DAW if it does not show up." -ForegroundColor Green
