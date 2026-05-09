<#
.SYNOPSIS
    Detect-System.ps1 — RAM, CPU, and NUMA Enumeration
    CONFIG-003 (RAM), CONFIG-004 (CPU), CONFIG-005 (NUMA)

.DESCRIPTION
    Detects system resources:
    - Total RAM via Win32_ComputerSystem
    - Logical/physical CPU count via Win32_Processor
    - NUMA node topology via Win32_NumaNode
    
    Outputs JSON to $env:TEMP\symbiose_system.json

.NOTES
    Reference: Interactive_Plan.md §IX·1 → hardware.ram_allocation_gb,
               hardware.vcpu_count, hardware.numa_*
    Reference: Interactive_Plan.md §XI CONFIG-003/004/005
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Write-Host "[CONFIG-003/004/005] Detecting system resources..." -ForegroundColor Cyan

# ── RAM Detection (CONFIG-003) ────────────────────────────────────────
$cs = Get-CimInstance -ClassName Win32_ComputerSystem
$totalRamBytes = [uint64]$cs.TotalPhysicalMemory
$totalRamGb = [math]::Round($totalRamBytes / 1GB, 1)

# Available RAM (minus what Windows is using)
$os = Get-CimInstance -ClassName Win32_OperatingSystem
$freeRamGb = [math]::Round([uint64]$os.FreePhysicalMemory * 1KB / 1GB, 1)

# Calculate safe allocation range
# §IX·1: Min 8GB for guest; reserve 4GB for host
$minAllocation = 8
$maxAllocation = [math]::Floor($totalRamGb - 4)
$defaultAllocation = [math]::Floor($totalRamGb * 0.75)  # 75% default

Write-Host "  [RAM] Total: ${totalRamGb}GB, Free: ${freeRamGb}GB" -ForegroundColor Green
Write-Host "  [RAM] Allocation range: ${minAllocation}GB - ${maxAllocation}GB (default: ${defaultAllocation}GB)" -ForegroundColor Green

# ── CPU Detection (CONFIG-004) ────────────────────────────────────────
$processors = Get-CimInstance -ClassName Win32_Processor
$physicalCores = ($processors | Measure-Object -Property NumberOfCores -Sum).Sum
$logicalProcessors = ($processors | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
$socketCount = $processors.Count

# Reserve 2 cores for host (§IX·1)
$maxVcpu = $logicalProcessors - 2
$defaultVcpu = [math]::Floor($logicalProcessors * 0.75)

Write-Host "  [CPU] Sockets: $socketCount, Physical cores: $physicalCores, Logical: $logicalProcessors" -ForegroundColor Green
Write-Host "  [CPU] vCPU range: 1 - $maxVcpu (default: $defaultVcpu)" -ForegroundColor Green

# ── NUMA Detection (CONFIG-005) ───────────────────────────────────────
$numaNodes = Get-CimInstance -ClassName Win32_NumaNode -ErrorAction SilentlyContinue
$numaTopology = @()

if ($numaNodes) {
    foreach ($node in $numaNodes) {
        $nodeInfo = @{
            node_id           = $node.NodeId
            available_memory  = [math]::Round([uint64]$node.AvailableMemory * 1KB / 1GB, 1)
        }
        $numaTopology += $nodeInfo
        Write-Host "  [NUMA] Node $($node.NodeId): $([math]::Round([uint64]$node.AvailableMemory * 1KB / 1GB, 1))GB available" -ForegroundColor Green
    }
} else {
    Write-Host "  [NUMA] Single NUMA node (UMA system)" -ForegroundColor DarkGray
}

$isMultiNuma = $numaTopology.Count -gt 1

# ── Processor features ────────────────────────────────────────────────
$procInfo = $processors | Select-Object -First 1
$cpuName = $procInfo.Name.Trim()
$vmxSupport = $false

# Check for VMX/VT-x support
try {
    $vmxSupport = (Get-CimInstance -Namespace "root\cimv2" -ClassName Win32_Processor).VirtualizationFirmwareEnabled
} catch {
    Write-Warning "[CONFIG-004] Could not detect VMX support"
}

# ── Build system info JSON ────────────────────────────────────────────
$systemInfo = @{
    ram = @{
        total_gb          = $totalRamGb
        total_bytes       = $totalRamBytes
        free_gb           = $freeRamGb
        min_allocation_gb = $minAllocation
        max_allocation_gb = $maxAllocation
        default_gb        = $defaultAllocation
    }
    cpu = @{
        name               = $cpuName
        socket_count       = $socketCount
        physical_cores     = $physicalCores
        logical_processors = $logicalProcessors
        max_vcpu           = $maxVcpu
        default_vcpu       = $defaultVcpu
        vmx_support        = $vmxSupport
    }
    numa = @{
        is_multi_numa = $isMultiNuma
        node_count    = [math]::Max($numaTopology.Count, 1)
        nodes         = $numaTopology
    }
}

# ── Write output JSON ─────────────────────────────────────────────────
$outputPath = Join-Path $env:TEMP "symbiose_system.json"
$systemInfo | ConvertTo-Json -Depth 4 | Out-File -FilePath $outputPath -Encoding UTF8 -Force

Write-Host "[CONFIG-003/004/005] System info saved to $outputPath" -ForegroundColor Cyan
