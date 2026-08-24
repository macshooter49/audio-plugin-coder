<#
.SYNOPSIS
    Terrain Instrument -- one-command Windows setup. Installs what is missing, clones, builds, runs.
.DESCRIPTION
    Written for a machine that has never built this project. Every step announces what it is doing
    and why, checks that it worked, and stops with a plain-English message if it did not.
    Safe to re-run: it skips anything already done and pulls updates instead of re-cloning.
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\windows-quickstart.ps1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\windows-quickstart.ps1 -InstallPlugin

.NOTES
    *** THIS FILE MUST STAY PURE ASCII. DO NOT ADD BOX-DRAWING OR TYPOGRAPHIC DASHES. ***

    Windows PowerShell 5.1 -- which is what ships with Windows and what runs this -- reads a .ps1
    with NO byte-order mark as Windows-1252, not UTF-8. A UTF-8 em dash is the three bytes
    E2 80 94, and CP1252 renders that third byte as U+201D, a RIGHT DOUBLE QUOTATION MARK, which
    PowerShell accepts as a STRING DELIMITER. So every pretty dash silently opens or closes a
    string and the parser then fails somewhere else entirely with "Missing closing '}'".

    This actually happened: 487 such characters in the comment banners took the whole script down
    at a brace 80 lines away from any of them, and it passed both a character-balance check and a
    hand-written UTF-8 tokenizer, because both read the file the way it was WRITTEN rather than the
    way PowerShell READS it. Pure ASCII is immune to the question.
#>
[CmdletBinding()]
param(
    [string] $Root      = 'C:\dev',
    [string] $Branch    = 'windows-test',
    [switch] $InstallPlugin,      # also copy the VST3 into Program Files (needs an elevated shell)
    [switch] $SkipPrereqs         # do not attempt any winget installs
)

$ErrorActionPreference = 'Stop'
$RepoUrl  = 'https://github.com/macshooter49/audio-plugin-coder.git'
$RepoDir  = Join-Path $Root 'audio-plugin-coder'
$Artefact = 'build\plugins\TerrainInstrument\TerrainInstrument_artefacts\Release'

function Say  ($m) { Write-Host "`n=== $m" -ForegroundColor Cyan }
function Note ($m) { Write-Host "    $m" -ForegroundColor DarkGray }
function Good ($m) { Write-Host "    OK  $m" -ForegroundColor Green }
function Die  ($m) { Write-Host "`nSTOPPED: $m" -ForegroundColor Red; Write-Host "Copy the red text above and send it back for a fix.`n" -ForegroundColor Yellow; exit 1 }

function Have ($exe) { $null -ne (Get-Command $exe -ErrorAction SilentlyContinue) }

# vswhere is installed with any Visual Studio and is the only reliable way to ask whether the C++
# toolchain (not merely the IDE) is present. Its absence is itself the answer.
function Get-VsState {
    # Returns 'none' (no Visual Studio at all), 'no-cpp' (VS present but the C++ workload is not),
    # or the installation path (C++ toolchain present and usable).
    #
    # Telling those two failures apart is the whole point. "Visual Studio is installed but WITHOUT
    # the C++ workload" is the single most common mistake anyone makes here, and it needs a COMPLETELY
    # DIFFERENT instruction from "no Visual Studio": winget will happily report the Community edition
    # as already-installed and add nothing, leaving him stuck in a loop with no idea why.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return 'none' }
    $withCpp = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if (-not [string]::IsNullOrWhiteSpace($withCpp)) { return ($withCpp | Select-Object -First 1) }
    $anyVs = & $vswhere -latest -products * -property installationPath 2>$null
    if (-not [string]::IsNullOrWhiteSpace($anyVs)) { return 'no-cpp' }
    return 'none'
}

Write-Host ''
Write-Host '  TERRAIN INSTRUMENT -- WINDOWS SETUP' -ForegroundColor White
Write-Host '  This will install what is missing, fetch the code, build it, and open it.' -ForegroundColor DarkGray
Write-Host '  The first build takes 10-20 minutes. Everything after that is quick.' -ForegroundColor DarkGray

# -- 1. PREREQUISITES -------------------------------------------------------------------------
Say '1/6  Checking what you already have'
$needGit   = -not (Have git)
$needCMake = -not (Have cmake)
$vsState   = Get-VsState
$needVS    = ($vsState -eq 'none')
$vsNoCpp   = ($vsState -eq 'no-cpp')

if (-not $needGit)   { Good "Git   $((git --version) -replace 'git version ','')" }   else { Note 'Git is missing' }
if (-not $needCMake) { Good "CMake $((cmake --version | Select-Object -First 1) -replace 'cmake version ','')" } else { Note 'CMake is missing' }
if     ($needVS)     { Note 'Visual Studio is not installed' }
elseif ($vsNoCpp)    { Note 'Visual Studio IS installed, but WITHOUT the C++ part' }
else                 { Good "Visual Studio C++ tools at $vsState" }

# This one cannot be fixed by winget, so handle it before anything else and say exactly what to click.
if ($vsNoCpp) {
    Die @"
Visual Studio is installed, but the C++ compiler is not.

This is the usual mistake and it is a two-minute fix:

  1. Open 'Visual Studio Installer' from the Start menu
  2. Click 'Modify' on Visual Studio 2022
  3. Tick 'Desktop development with C++'   <-- this is the one that matters
  4. Click 'Modify' to install it (a few GB, several minutes)
  5. Run this script again

(Installing Visual Studio again would not help -- Windows already considers it installed, so it
would add nothing. The workload has to be added through the Installer.)
"@
}

if ($needGit -or $needCMake -or $needVS) {
    if ($SkipPrereqs) { Die 'Something is missing and -SkipPrereqs was given. Install the items listed above and re-run.' }
    if (-not (Have winget)) {
        Die @"
Missing tools, and winget is not available to install them automatically.
Install these by hand, then run this script again:
  Git                : https://git-scm.com/download/win
  CMake              : https://cmake.org/download   (tick 'Add CMake to the system PATH')
  Visual Studio 2022 : https://visualstudio.microsoft.com/downloads
                       During install you MUST tick the 'Desktop development with C++' workload.
"@
    }
    Say '2/6  Installing what is missing (winget may ask you to approve)'
    # winget returns non-zero on a real failure. Unchecked, a failed install would send him round the
    # loop for ever with the same "it is missing" message and no clue that the install itself broke.
    if ($needGit) {
        Note 'Installing Git...'
        winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -ne 0) { Die "Installing Git failed (winget exit $LASTEXITCODE). Install it by hand from https://git-scm.com/download/win and re-run." }
    }
    if ($needCMake) {
        Note 'Installing CMake...'
        winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -ne 0) { Die "Installing CMake failed (winget exit $LASTEXITCODE). Install it by hand from https://cmake.org/download and TICK 'Add CMake to the system PATH', then re-run." }
    }
    if ($needVS) {
        Note 'Installing Visual Studio 2022 Community WITH the C++ workload.'
        Note 'This is several GB and can take 20+ minutes. Leave it alone until it finishes.'
        winget install --id Microsoft.VisualStudio.2022.Community -e --accept-package-agreements --accept-source-agreements --override '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended'
        if ($LASTEXITCODE -ne 0) {
            Die @"
Installing Visual Studio failed (winget exit $LASTEXITCODE).

Do it by hand instead -- it is reliable and not hard:
  1. Go to https://visualstudio.microsoft.com/downloads
  2. Download Visual Studio 2022 COMMUNITY (free)
  3. In the installer, tick 'Desktop development with C++'   <-- required
  4. Install, then run this script again
"@
        }
    }
    Die @"
Installed. Windows does not update an already-open terminal's PATH, so:

  1. CLOSE this PowerShell window
  2. Open a NEW one
  3. Run this same command again

It will skip everything it just installed and carry straight on.
"@
} else { Say '2/6  Nothing to install' ; Good 'All three tools present' }

# -- 3. THE CODE ------------------------------------------------------------------------------
Say '3/6  Fetching the code'
# A short root path matters: Windows gives up past 260 characters and this project nests deeply.
if ($Root -match '\s') { Die "The folder '$Root' contains a space. Use a path without spaces, e.g. -Root C:\dev" }
if (-not (Test-Path $Root)) { New-Item -ItemType Directory -Path $Root -Force | Out-Null; Note "Created $Root" }
git config --global core.longpaths true 2>$null | Out-Null

if (Test-Path (Join-Path $RepoDir '.git')) {
    Note 'Already cloned -- updating instead'
    Push-Location $RepoDir
    git fetch origin $Branch  ; if ($LASTEXITCODE -ne 0) { Pop-Location; Die 'git fetch failed. Are you online?' }
    git checkout $Branch      ; if ($LASTEXITCODE -ne 0) { Pop-Location; Die "Could not switch to branch '$Branch'." }
    git pull --ff-only origin $Branch | Out-Null
    git submodule update --init --recursive
    Pop-Location
} elseif (Test-Path $RepoDir) {
    # A folder is there but it is not a git checkout. git clone would refuse with "destination path
    # already exists", and the old message blamed the network -- the wrong diagnosis entirely.
    Die @"
The folder already exists but is not a git checkout:
  $RepoDir

Either delete it and run this again, or if you know it is good, open it and run:
  git submodule update --init --recursive
"@
} else {
    Note "Cloning into $RepoDir (about 130 MB of libraries, a few minutes)"
    Push-Location $Root
    git clone --recurse-submodules -b $Branch $RepoUrl
    $ok = ($LASTEXITCODE -eq 0)
    Pop-Location
    if (-not $ok) { Die "git clone failed (exit $LASTEXITCODE). Check that you are online and can reach github.com, then run this again." }
}
if (-not (Test-Path (Join-Path $RepoDir '_tools\JUCE\CMakeLists.txt'))) {
    Note 'The JUCE library did not come down with the clone. Fetching it now.'
    Push-Location $RepoDir; git submodule update --init --recursive; Pop-Location
    if (-not (Test-Path (Join-Path $RepoDir '_tools\JUCE\CMakeLists.txt'))) { Die 'JUCE is still missing from _tools\JUCE. The submodules did not download.' }
}
Good 'Code and libraries are in place'

# The interface runs on Microsoft's WebView2 Runtime. Win11 ships it; some Win10 machines lack it.
$wvrKeys = @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
             'HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}')
$wvrOk = $false
foreach ($k in $wvrKeys) { if (Test-Path $k) { $wvrOk = $true } }
if (-not $wvrOk) {
    Note 'WebView2 Runtime not found -- installing it (the UI cannot draw without it)'
    winget install --id Microsoft.EdgeWebView2Runtime -e --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) { Note 'winget could not install it. If the UI comes up blank, install "WebView2 Evergreen Runtime" from Microsoft by hand.' }
} else { Good 'WebView2 Runtime present' }

# JUCE has a Windows-only bug: if WebView2 controller creation fails, JUCE retries in an infinite
# loop on the UI thread -- in FL Studio that freezes the ENTIRE DAW with no error. The repo carries
# a 3-attempt cap as a patch; apply it to the JUCE checkout (idempotent across re-runs).
$patch = Join-Path $RepoDir 'scripts\juce-webview2-failure-cap.patch'
$juceDir = Join-Path $RepoDir '_tools\JUCE'
git -C $juceDir apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -eq 0) { Good 'JUCE freeze patch already applied' }
else {
    git -C $juceDir apply --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) { Die 'The JUCE freeze patch no longer applies -- send this line back.' }
    git -C $juceDir apply $patch
    Good 'JUCE freeze patch applied'
}

# -- 4. CONFIGURE -----------------------------------------------------------------------------
Say '4/6  Setting up the build (a minute or two)'

# JUCE's web UI needs Microsoft's WebView2 loader library at build time. It ships as a NuGet
# package; JUCE's find script wants a folder that CONTAINS a 'Microsoft.Web.WebView2*' directory
# with build\native inside. We fetch the exact version JUCE pins and point CMake at it -- no
# NuGet tooling involved, it is just a zip.
$wvVersion = '1.0.3485.44'
$wvParent  = Join-Path $Root 'nuget'
$wvDir     = Join-Path $wvParent "Microsoft.Web.WebView2.$wvVersion"
$wvLib     = Join-Path $wvDir 'build\native\x64\WebView2LoaderStatic.lib'
if (-not (Test-Path $wvLib)) {
    Note "Fetching the WebView2 SDK $wvVersion (about 9 MB)"
    New-Item -ItemType Directory -Path $wvDir -Force | Out-Null
    # Older Windows PowerShell refuses modern TLS unless asked; harmless where it is already on.
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072 } catch {}
    $wvZip = Join-Path $env:TEMP "webview2-$wvVersion.zip"
    try {
        Invoke-WebRequest -UseBasicParsing "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$wvVersion" -OutFile $wvZip
    } catch {
        Die "Could not download the WebView2 SDK from nuget.org. Check that you are online, then run this again. ($($_.Exception.Message))"
    }
    Expand-Archive -Path $wvZip -DestinationPath $wvDir -Force
    Remove-Item $wvZip -ErrorAction SilentlyContinue
    if (-not (Test-Path $wvLib)) { Die "The WebView2 SDK unpacked but the library is not where JUCE expects it. Send this back: $wvLib is missing." }
}
Good 'WebView2 SDK in place'
$wvParentFwd = $wvParent -replace '\\','/'
Push-Location $RepoDir
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -D "JUCE_WEBVIEW2_PACKAGE_LOCATION=$wvParentFwd"
if ($LASTEXITCODE -ne 0) { Pop-Location; Die 'CMake could not set the build up. The error is in the text above.' }
Good 'Build configured'

# -- 5. BUILD ---------------------------------------------------------------------------------
Say '5/6  Building the standalone app  --  THIS IS THE SLOW ONE, 10-20 minutes'
Note 'Pages of scrolling text are normal. Only lines containing the word "error" matter.'
cmake --build build --config Release --target TerrainInstrument_Standalone
if ($LASTEXITCODE -ne 0) { Pop-Location; Die 'The standalone build failed. Scroll up to the first line containing "error" and send me that.' }

$exe = Join-Path $RepoDir "$Artefact\Standalone\Terrain Instrument.exe"
if (-not (Test-Path $exe)) { Pop-Location; Die "The build reported success but I cannot find:`n  $exe" }
Good 'Standalone built'

Say '6/6  Building the VST3 plugin (much quicker now)'
cmake --build build --config Release --target TerrainInstrument_VST3
$vstOk = ($LASTEXITCODE -eq 0)
$vst3  = Join-Path $RepoDir "$Artefact\VST3\Terrain Instrument.vst3"
if ($vstOk -and (Test-Path $vst3)) { Good 'VST3 built' } else { Note 'The VST3 did not build, but the standalone did -- that is still a successful test of the synth.' }
Pop-Location

# -- OPTIONAL INSTALL -------------------------------------------------------------------------
if ($InstallPlugin -and $vstOk -and (Test-Path $vst3)) {
    Say 'Installing the VST3 for your DAW'
    $me = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    $elevated = $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    $dest = 'C:\Program Files\Common Files\VST3'
    if (-not $elevated) {
        Note 'Skipped: copying into Program Files needs an Administrator window.'
        Note 'Right-click PowerShell, "Run as administrator", and run this again with -InstallPlugin.'
    } else {
        if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest -Force | Out-Null }
        Copy-Item -LiteralPath $vst3 -Destination $dest -Recurse -Force
        Good "Installed to $dest -- rescan plugins in your DAW"
    }
}

# -- DONE -------------------------------------------------------------------------------------
Write-Host ''
Write-Host '  BUILT.' -ForegroundColor Green
Write-Host "  Opening the synth now. If the window appears, Terrain works on Windows." -ForegroundColor White
Write-Host ''
Write-Host '  If the window opens but is BLANK or WHITE, the interface needs Microsoft WebView2:' -ForegroundColor DarkGray
Write-Host '    winget install Microsoft.EdgeWebView2Runtime' -ForegroundColor DarkGray
Write-Host ''
Write-Host "  Standalone : $exe" -ForegroundColor DarkGray
if (Test-Path $vst3) { Write-Host "  VST3       : $vst3" -ForegroundColor DarkGray }
Write-Host ''
Start-Process -FilePath $exe
