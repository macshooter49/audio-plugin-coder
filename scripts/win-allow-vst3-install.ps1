# ==============================================================================================
#  RUN THIS ONCE, AS ADMIN. After that every Terrain build installs with NO UAC prompt.
#
#  Why this exists: C:\Program Files\Common Files\VST3 is writable only by Administrators, so
#  every rebuild needed an elevation prompt. During a debugging session that is one prompt per
#  build, which is why builds kept not reaching the DAW. This grants THIS USER Modify rights on
#  the Terrain bundle folder ONLY -- not on Program Files, not on any other plugin.
#
#  From an admin shell, use the FULL path -- an elevated PowerShell opens in C:\WINDOWS\system32,
#  so a repo-relative path does not resolve:
#      powershell -ExecutionPolicy Bypass -File "C:\dev\audio-plugin-coder\scripts\win-allow-vst3-install.ps1"
# ==============================================================================================
$ErrorActionPreference = 'Stop'

$dst = "C:\Program Files\Common Files\VST3\Terrain Instrument.vst3"

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$pr = New-Object Security.Principal.WindowsPrincipal($id)
if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "NOT ELEVATED. Re-run this from an Administrator PowerShell." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $dst)) {
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Write-Host "created $dst"
}

$user = $id.Name
& icacls "$dst" /grant "${user}:(OI)(CI)M" /T | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "OK - $user now has Modify on:" -ForegroundColor Green
    Write-Host "     $dst"
    Write-Host "Future installs need no elevation."
} else {
    Write-Host "icacls failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit 1
}
