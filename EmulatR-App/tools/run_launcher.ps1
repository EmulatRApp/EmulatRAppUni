<#
=============================================================================
 tools/run_launcher.ps1 -- build and run EmulatrLaunch in one command
=============================================================================
 Project: EmulatR -- EmulatrLaunch (SPEC-LAUNCH-001 Rev D.2)
 Copyright (C) 2026 eNVy Systems, Inc.  All rights reserved.
 Licensed under eNVy Systems Non-Commercial License v1.1

 Project Architect: Timothy Peer
 AI Collaboration:  Claude (Anthropic)

 Version: 1.0.0-alpha   Date: 2026-07-29

 WHY THIS EXISTS.  Three separate things have to be on PATH before
 out/build/<config>/EmulatrLaunch.exe will either build or start, and none of
 them is there by default in a plain shell:

   1. the MSVC toolchain      (vcvars64.bat, else cmake cannot find cl.exe)
   2. CMake and Ninja         (they ship inside the Qt installation, not on PATH)
   3. the Qt runtime DLLs     (else the built exe dies with a silent
                               "unable to start correctly" dialog)

 This script resolves all three, then configures, builds, and runs.

 USAGE
   .\tools\run_launcher.ps1                 build + run the debug launcher
   .\tools\run_launcher.ps1 -Tests          build + run the unit tests instead
   .\tools\run_launcher.ps1 -Config release build + run the release launcher
   .\tools\run_launcher.ps1 -NoBuild        run what is already built
   .\tools\run_launcher.ps1 -Deploy         copy the Qt runtime beside the exe
                                            so it starts by double-click
   .\tools\run_launcher.ps1 -Clean          delete the build dir first

 Equivalent once a shell is already set up:
   cmake --build --preset debug --target run
=============================================================================
#>

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'relwithdebinfo')]
    [string] $Config = 'debug',

    [switch] $Tests,
    [switch] $Deploy,
    [switch] $NoBuild,
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir    = Join-Path $ProjectRoot "out\build\$Config"

function Write-Step([string] $Text) {
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Fail([string] $Text) {
    Write-Host ''
    Write-Host "ERROR: $Text" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# 1. Qt kit.  Newest first: a machine here carries several side by side, and
#    silently building against the oldest is a confusing way to lose an hour.
#    Override with $env:EMULATRLAUNCH_QTDIR when you need a specific one.
# ---------------------------------------------------------------------------
Write-Step 'Locating the Qt kit'

$QtDir = $env:EMULATRLAUNCH_QTDIR
if ([string]::IsNullOrWhiteSpace($QtDir)) {
    $candidates = @()
    foreach ($root in @('D:\Qt', 'C:\Qt')) {
        if (-not (Test-Path $root)) { continue }
        Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+' } |
            ForEach-Object {
                $kit = Join-Path $_.FullName 'msvc2022_64'
                if (Test-Path (Join-Path $kit 'lib\cmake\Qt6\Qt6Config.cmake')) {
                    $candidates += [pscustomobject]@{
                        Path    = $kit
                        Version = [version]($_.Name -replace '[^0-9.].*$', '')
                    }
                }
            }
    }
    if ($candidates.Count -eq 0) {
        Fail 'No Qt 6 msvc2022_64 kit found under D:\Qt or C:\Qt. Set $env:EMULATRLAUNCH_QTDIR to the kit directory.'
    }
    $QtDir = ($candidates | Sort-Object Version -Descending | Select-Object -First 1).Path
}

if (-not (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    Fail "Not a usable Qt kit (no Qt6Config.cmake): $QtDir"
}
Write-Host "    Qt kit : $QtDir"

$QtBin = Join-Path $QtDir 'bin'

# ---------------------------------------------------------------------------
# 2. CMake and Ninja.  Both ship inside the Qt installation's Tools tree.
# ---------------------------------------------------------------------------
$QtRoot   = Split-Path -Parent (Split-Path -Parent $QtDir)
$CMakeBin = Join-Path $QtRoot 'Tools\CMake_64\bin'
$NinjaBin = Join-Path $QtRoot 'Tools\Ninja'

$toolPaths = @()
if (Test-Path $CMakeBin) { $toolPaths += $CMakeBin }
if (Test-Path $NinjaBin) { $toolPaths += $NinjaBin }
if ($toolPaths.Count -gt 0) {
    $env:Path = ($toolPaths -join ';') + ';' + $env:Path
}

$cmakeExe = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -eq $cmakeExe) {
    Fail "cmake was not found. Looked in $CMakeBin and on PATH."
}
Write-Host "    cmake  : $($cmakeExe.Source)"

# ---------------------------------------------------------------------------
# 3. MSVC.  vswhere is the supported way to locate an install; the fixed path
#    is a fallback for machines where vswhere is missing.
# ---------------------------------------------------------------------------
Write-Step 'Locating the MSVC toolchain'

$VcVars = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsRoot = & $vswhere -latest -products * `
                         -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                         -property installationPath
    if (-not [string]::IsNullOrWhiteSpace($vsRoot)) {
        $candidate = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $candidate) { $VcVars = $candidate }
    }
}
if ($null -eq $VcVars) {
    foreach ($edition in @('Community', 'Professional', 'Enterprise', 'BuildTools')) {
        $candidate = "C:\Program Files\Microsoft Visual Studio\2022\$edition\VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $candidate) { $VcVars = $candidate; break }
    }
}
if ($null -eq $VcVars) {
    Fail 'vcvars64.bat was not found. Install the "Desktop development with C++" workload for Visual Studio 2022.'
}
Write-Host "    vcvars : $VcVars"

# ---------------------------------------------------------------------------
# 4. Clean, configure, build.
#
#    cmake runs inside cmd so vcvars64.bat's environment survives into it.
#    PowerShell cannot call a .bat and keep its exports, and the harness's
#    shells do not inherit a developer prompt.
# ---------------------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Removing $BuildDir"
    Remove-Item $BuildDir -Recurse -Force
}

$target = 'all'
if ($Tests)  { $target = 'EmulatrLaunchTests' }
if ($Deploy) { $target = 'deploy' }

if (-not $NoBuild) {
    Write-Step "Configuring and building ($Config, target $target)"

    $inner = "call `"$VcVars`" >nul 2>&1 && cd /d `"$ProjectRoot`" && " +
             "cmake --preset $Config -DCMAKE_PREFIX_PATH=`"$($QtDir -replace '\\','/')`" && " +
             "cmake --build --preset $Config --target $target"

    & cmd /c $inner
    if ($LASTEXITCODE -ne 0) { Fail "Build failed (exit $LASTEXITCODE)." }
}

# ---------------------------------------------------------------------------
# 5. Run.  Qt's bin directory goes on PATH for this process only, so the exe
#    finds Qt6Widgets.dll without anything being copied or installed.
# ---------------------------------------------------------------------------
if ($Deploy) {
    Write-Host ''
    Write-Host "Qt runtime copied beside the executable in $BuildDir." -ForegroundColor Green
    Write-Host 'It will now start by double-click from Explorer.'
    exit 0
}

$exeName = 'EmulatrLaunch.exe'
if ($Tests) { $exeName = 'EmulatrLaunchTests.exe' }
$exePath = Join-Path $BuildDir $exeName

if (-not (Test-Path $exePath)) {
    Fail "$exePath does not exist. Run without -NoBuild to build it first."
}

Write-Step "Running $exeName"
Write-Host "    $exePath"

$env:Path = "$QtBin;$env:Path"

Push-Location $BuildDir
try {
    & $exePath
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
}

Write-Host ''
if ($code -eq 0) {
    Write-Host "$exeName exited 0." -ForegroundColor Green
} else {
    Write-Host "$exeName exited $code." -ForegroundColor Yellow
}
exit $code
