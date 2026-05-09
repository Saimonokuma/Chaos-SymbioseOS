<#
.SYNOPSIS
    Detect-NVMe.ps1 — NVMe/Storage Enumeration for SymbioseOS Phase 0
    CONFIG-002: Enumerate NVMe and SATA drives; detect Windows partitions.

.DESCRIPTION
    Enumerates all physical disks using Get-PhysicalDisk, Get-Disk,
    and Get-Partition. For each drive:
    - Identifies NVMe vs SATA vs SAS bus type
    - Detects Windows partitions (warns user)
    - Captures PCI location path for SymbioseNull isolation
    - Flags CCD (Continuous Context Drive) candidates

.NOTES
    Reference: Interactive_Plan.md §IX·1 → hardware.nvme[]
    Reference: Interactive_Plan.md §XI CONFIG-002 (line 4940)
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Write-Host "[CONFIG-002] Enumerating storage devices..." -ForegroundColor Cyan

# ── Collect physical disks ────────────────────────────────────────────
$physicalDisks = Get-PhysicalDisk | Where-Object { $_.MediaType -ne "Unspecified" -or $_.Size -gt 0 }

$driveList = @()

foreach ($disk in $physicalDisks) {
    $sizeGb = [math]::Round($disk.Size / 1GB, 1)
    $busType = $disk.BusType  # NVMe, SATA, SAS, etc.
    $mediaType = $disk.MediaType  # SSD, HDD, etc.
    
    # Get associated disk object for partition info
    $diskObj = Get-Disk | Where-Object { $_.UniqueId -eq $disk.UniqueId } |
        Select-Object -First 1
    
    # Check for Windows partitions
    $hasWindowsPartitions = $false
    $mountPoints = @()
    
    if ($diskObj) {
        $partitions = Get-Partition -DiskNumber $diskObj.Number -ErrorAction SilentlyContinue
        foreach ($part in $partitions) {
            if ($part.DriveLetter) {
                $mountPoints += "$($part.DriveLetter):"
                
                # Check if this partition contains Windows
                $winDir = Join-Path "$($part.DriveLetter):" "Windows\System32"
                if (Test-Path $winDir) {
                    $hasWindowsPartitions = $true
                }
            }
        }
    }
    
    # Get PCI location path via PnP device matching
    $pciLocation = ""
    try {
        $pnpDisk = Get-PnpDevice -Class DiskDrive -Status OK -ErrorAction SilentlyContinue |
            Where-Object { $_.FriendlyName -eq $disk.FriendlyName } |
            Select-Object -First 1
        if ($pnpDisk) {
            $pciLocation = $pnpDisk.InstanceId
        }
    } catch {
        Write-Warning "[CONFIG-002] PnP lookup failed for $($disk.FriendlyName)"
    }

    $driveInfo = @{
        friendly_name         = $disk.FriendlyName
        size_gb               = $sizeGb
        bus_type              = $busType
        media_type            = $mediaType
        pci_location          = $pciLocation
        has_windows_partitions = $hasWindowsPartitions
        mount_points          = $mountPoints -join ", "
        is_ccd                = $false
        health_status         = $disk.HealthStatus
        firmware_version      = $disk.FirmwareVersion
    }

    $driveList += $driveInfo

    $statusColor = if ($hasWindowsPartitions) { "Yellow" } else { "Green" }
    $warnTag = if ($hasWindowsPartitions) { " ⚠️ HAS WINDOWS" } else { "" }
    Write-Host "  [DISK] $($disk.FriendlyName) — ${sizeGb}GB $busType $mediaType$warnTag" -ForegroundColor $statusColor
    
    if ($mountPoints.Count -gt 0) {
        Write-Host "         Mounts: $($mountPoints -join ', ')" -ForegroundColor DarkGray
    }
}

# ── Warn about Windows partitions ─────────────────────────────────────
$winDrives = $driveList | Where-Object { $_.has_windows_partitions }
if ($winDrives.Count -gt 0) {
    Write-Host ""
    Write-Host "  ⚠️  WARNING: $($winDrives.Count) drive(s) contain Windows partitions!" -ForegroundColor Yellow
    Write-Host "  ⚠️  Selecting these for CCD will make Windows unbootable." -ForegroundColor Yellow
    Write-Host "  ⚠️  Only select drives that do NOT contain your Windows installation." -ForegroundColor Yellow
    Write-Host ""
}

# ── Write output JSON ─────────────────────────────────────────────────
$outputPath = Join-Path $env:TEMP "symbiose_drives.json"
$driveList | ConvertTo-Json -Depth 3 | Out-File -FilePath $outputPath -Encoding UTF8 -Force

Write-Host "[CONFIG-002] Found $($driveList.Count) drive(s) — saved to $outputPath" -ForegroundColor Cyan
