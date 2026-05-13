<#
.SYNOPSIS
    Reconfigure-Symbiose.ps1 — Post-Install Reconfiguration Launcher
    Re-runs hardware detection + Configurator GUI to update symbiose_config.json.

.DESCRIPTION
    After SymbioseOS is installed, the user can run this script at any time
    to re-detect hardware and modify the resource allocation (GPU, NVMe, RAM,
    vCPU, multimodal settings). The updated config takes effect on next reboot.

    This script chains:
      1. Detect-GPU.ps1       → refreshes $env:TEMP\symbiose_gpus.json
      2. Detect-NVMe.ps1      → refreshes $env:TEMP\symbiose_drives.json
      3. Detect-System.ps1    → refreshes $env:TEMP\symbiose_system.json
      4. Symbiose-Configurator.ps1 → opens the GUI wizard
      5. Write-SymbioseConfig.ps1  → writes updated symbiose_config.json

.NOTES
    Must be run as Administrator (GPU/NVMe detection requires elevated access).
    Reference: Interactive_Plan.md SIX.1 (symbiose_config.json schema)
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

# ── Check elevation ──────────────────────────────────────────────────────
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[RECONFIG] Relaunching as Administrator..." -ForegroundColor Yellow
    Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit 0
}

# ── Resolve script directory ─────────────────────────────────────────────
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# If running from ProgramData (post-install), scripts are in same directory
# If running from APBX staging, scripts are also in same directory
$requiredScripts = @(
    'Detect-GPU.ps1',
    'Detect-NVMe.ps1',
    'Detect-System.ps1',
    'Symbiose-Configurator.ps1',
    'Write-SymbioseConfig.ps1'
)

foreach ($s in $requiredScripts) {
    $path = Join-Path $scriptDir $s
    if (-not (Test-Path $path)) {
        Write-Error "[RECONFIG] Missing script: $path"
        exit 1
    }
}

Write-Host "" -ForegroundColor Cyan
Write-Host "[RECONFIG] ========================================================" -ForegroundColor Cyan
Write-Host "[RECONFIG]  SymbioseOS V3 — Post-Install Reconfiguration" -ForegroundColor Cyan
Write-Host "[RECONFIG] ========================================================" -ForegroundColor Cyan
Write-Host "[RECONFIG] This will re-detect your hardware and open the" -ForegroundColor Cyan
Write-Host "[RECONFIG] configuration wizard. Changes take effect on reboot." -ForegroundColor Cyan
Write-Host "" -ForegroundColor Cyan

# ── Step 1: Re-detect hardware ───────────────────────────────────────────
Write-Host "[RECONFIG] Step 1/5: Detecting GPUs..." -ForegroundColor Yellow
& (Join-Path $scriptDir 'Detect-GPU.ps1')

Write-Host ""
Write-Host "[RECONFIG] Step 2/5: Detecting storage devices..." -ForegroundColor Yellow
& (Join-Path $scriptDir 'Detect-NVMe.ps1')

Write-Host ""
Write-Host "[RECONFIG] Step 3/5: Detecting system resources..." -ForegroundColor Yellow
& (Join-Path $scriptDir 'Detect-System.ps1')

# ── Step 2: Open Configurator GUI ────────────────────────────────────────
Write-Host ""
Write-Host "[RECONFIG] Step 4/5: Opening Configurator..." -ForegroundColor Yellow
& (Join-Path $scriptDir 'Symbiose-Configurator.ps1')

# Check if user cancelled
if ($LASTEXITCODE -ne 0) {
    Write-Host "[RECONFIG] Cancelled by user. No changes made." -ForegroundColor Red
    exit 0
}

# ── Step 3: Write updated config ─────────────────────────────────────────
Write-Host ""
Write-Host "[RECONFIG] Step 5/5: Writing updated configuration..." -ForegroundColor Yellow
& (Join-Path $scriptDir 'Write-SymbioseConfig.ps1')

Write-Host ""
Write-Host "[RECONFIG] ========================================================" -ForegroundColor Green
Write-Host "[RECONFIG]  Reconfiguration complete!" -ForegroundColor Green
Write-Host "[RECONFIG]  Changes saved to: $env:ProgramData\SymbioseOS\symbiose_config.json" -ForegroundColor Green
Write-Host "[RECONFIG]  Restart your machine for changes to take effect." -ForegroundColor Green
Write-Host "[RECONFIG] ========================================================" -ForegroundColor Green
