<#
.SYNOPSIS
    Detect-GPU.ps1 — GPU Enumeration for SymbioseOS Phase 0
    CONFIG-001: Enumerate all GPUs via PnP + WMI; output JSON.

.DESCRIPTION
    Enumerates all display adapters on the host system using:
    - Get-PnpDevice -Class Display (for PCI location paths)
    - Win32_VideoController (for VRAM and friendly names)
    
    Outputs JSON array to $env:TEMP\symbiose_gpus.json containing:
    - friendly_name, vram_gb, vram_bytes, pci_location, bar1_gb,
      device_id, vendor_id, driver_version, surrendered (default false)

.NOTES
    Reference: Interactive_Plan.md §IX·1 → hardware.gpu[]
    Reference: Interactive_Plan.md §XI CONFIG-001 (line 4939)
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Write-Host "[CONFIG-001] Enumerating GPUs..." -ForegroundColor Cyan

# ── Collect from WMI (VRAM, friendly names) ───────────────────────────
$wmiGPUs = Get-CimInstance -ClassName Win32_VideoController |
    Where-Object { $_.AdapterCompatibility -ne $null }

# ── Collect from PnP (PCI location paths) ─────────────────────────────
$pnpGPUs = Get-PnpDevice -Class Display -Status OK -ErrorAction SilentlyContinue

# ── Merge and build JSON array ────────────────────────────────────────
$gpuList = @()

foreach ($gpu in $wmiGPUs) {
    $vramBytes = [uint64]$gpu.AdapterRAM
    # Win32_VideoController.AdapterRAM is uint32 — capped at 4GB.
    # For modern GPUs, query DXGI adapter or registry for true VRAM.
    $vramGb = [math]::Round($vramBytes / 1GB, 1)

    # Attempt to get real VRAM from registry (DXGI reports here)
    $pciLoc = ""
    $bar1Gb = 0

    # Match PnP device by name
    $matchPnp = $pnpGPUs | Where-Object {
        $_.FriendlyName -eq $gpu.Name -or
        $_.Description -eq $gpu.Description
    } | Select-Object -First 1

    if ($matchPnp) {
        $pciLoc = $matchPnp.InstanceId
        
        # Try to read actual VRAM from DXGI adapter registry
        # (AdapterRAM is uint32, useless for >4GB GPUs)
        try {
            $regPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
            $subkeys = Get-ChildItem -Path $regPath -ErrorAction SilentlyContinue
            foreach ($key in $subkeys) {
                $desc = Get-ItemProperty -Path $key.PSPath -Name "DriverDesc" -ErrorAction SilentlyContinue
                if ($desc -and $desc.DriverDesc -eq $gpu.Name) {
                    # qwMemorySize is the real VRAM in bytes (uint64)
                    $memSize = Get-ItemProperty -Path $key.PSPath -Name "HardwareInformation.qwMemorySize" -ErrorAction SilentlyContinue
                    if ($memSize) {
                        $realVram = $memSize.'HardwareInformation.qwMemorySize'
                        if ($realVram -gt 0) {
                            $vramBytes = [uint64]$realVram
                            $vramGb = [math]::Round($vramBytes / 1GB, 1)
                        }
                    }
                    
                    # BAR1 size (for MMIO calculation — CONFIG-008)
                    $bar1 = Get-ItemProperty -Path $key.PSPath -Name "HardwareInformation.MemorySize" -ErrorAction SilentlyContinue
                    if ($bar1) {
                        $bar1Gb = [math]::Round([uint64]$bar1.'HardwareInformation.MemorySize' / 1GB, 0)
                    }
                    break
                }
            }
        } catch {
            Write-Warning "[CONFIG-001] Registry VRAM lookup failed: $_"
        }
    }

    # Extract vendor/device IDs from PCI location
    $vendorId = ""
    $deviceId = ""
    if ($pciLoc -match "VEN_([0-9A-Fa-f]{4})") { $vendorId = $matches[1] }
    if ($pciLoc -match "DEV_([0-9A-Fa-f]{4})") { $deviceId = $matches[1] }

    $gpuInfo = @{
        friendly_name   = $gpu.Name
        vram_gb         = $vramGb
        vram_bytes      = $vramBytes
        pci_location    = $pciLoc
        bar1_gb         = $bar1Gb
        vendor_id       = $vendorId
        device_id       = $deviceId
        driver_version  = $gpu.DriverVersion
        status          = $gpu.Status
        surrendered     = $false
    }

    $gpuList += $gpuInfo

    Write-Host "  [GPU] $($gpu.Name) — VRAM: ${vramGb}GB, BAR1: ${bar1Gb}GB, PCI: $pciLoc" -ForegroundColor Green
}

# ── Warn if only 1 GPU ────────────────────────────────────────────────
if ($gpuList.Count -eq 1) {
    Write-Host ""
    Write-Host "  ⚠️  WARNING: Only 1 GPU detected!" -ForegroundColor Yellow
    Write-Host "  ⚠️  Surrendering this GPU will cause display output to drop." -ForegroundColor Yellow
    Write-Host "  ⚠️  You will need a second GPU or remote access to continue." -ForegroundColor Yellow
    Write-Host ""
}

# ── Write output JSON ─────────────────────────────────────────────────
$outputPath = Join-Path $env:TEMP "symbiose_gpus.json"
$gpuList | ConvertTo-Json -Depth 3 | Out-File -FilePath $outputPath -Encoding UTF8 -Force

Write-Host "[CONFIG-001] Found $($gpuList.Count) GPU(s) — saved to $outputPath" -ForegroundColor Cyan
