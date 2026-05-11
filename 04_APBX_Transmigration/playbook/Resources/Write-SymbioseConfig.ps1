<#
.SYNOPSIS
    Write-SymbioseConfig.ps1 - Final JSON Assembly (CONFIG-009)
    Merges all detection data + wizard selections into symbiose_config.json.

.NOTES
    Reference: Interactive_Plan.md SIX.1 (full schema)
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Write-Host "[CONFIG-009] Assembling symbiose_config.json..." -ForegroundColor Cyan

$configDir = "$env:ProgramData\SymbioseOS"
if (-not (Test-Path $configDir)) {
    New-Item -Path $configDir -ItemType Directory -Force | Out-Null
}

# -- Load wizard result -----------------------------------------------
$wizardFile = Join-Path $env:TEMP "symbiose_wizard.json"
if (-not (Test-Path $wizardFile)) {
    Write-Error "[CONFIG-009] FATAL: Wizard result not found at $wizardFile"
    exit 1
}
$wizard = Get-Content $wizardFile -Raw | ConvertFrom-Json

# -- Load model routing -----------------------------------------------
$modelFile = Join-Path $env:TEMP "symbiose_models.json"
$modelData = @{ available_models = @{} }
if (Test-Path $modelFile) {
    $modelData = Get-Content $modelFile -Raw | ConvertFrom-Json
}

# -- Build GPU array --------------------------------------------------
$gpuArray = @()
foreach ($gpu in $wizard.gpu_selections) {
    $gpuArray += @{
        friendly_name = $gpu.friendly_name
        pci_location  = $gpu.pci_location
        vram_gb       = $gpu.vram_gb
        bar1_gb       = $gpu.bar1_gb
        surrendered   = $true
    }
}

# -- Build NVMe array -------------------------------------------------
$nvmeArray = @()
foreach ($drv in $wizard.drive_selections) {
    $nvmeArray += @{
        friendly_name         = $drv.friendly_name
        pci_location          = $drv.pci_location
        size_gb               = $drv.size_gb
        is_ccd                = $true
        has_windows_partitions = $drv.has_windows_partitions
    }
}

# -- Determine rootfs drive -------------------------------------------
$rootfsDrive = ""
if ($nvmeArray.Count -gt 0) {
    $rootfsDrive = $nvmeArray[0].pci_location
}

# -- Build final config (SIX.1 schema) --------------------------------
$config = [ordered]@{
    schema_version = "3.0"
    session_id     = [guid]::NewGuid().ToString()

    hardware = [ordered]@{
        gpu              = $gpuArray
        nvme             = $nvmeArray
        ram_allocation_gb = $wizard.ram_allocation_gb
        vcpu_count       = $wizard.vcpu_count
        numa_pinned      = $wizard.numa_pinned
        numa_node        = $wizard.numa_node
        high_mmio_mb     = $wizard.high_mmio_mb
    }

    execution = [ordered]@{
        mode            = $wizard.execution_mode
        ramdisk_size_mb = if ($wizard.execution_mode -eq "ramdisk") { $wizard.ram_allocation_gb * 512 } else { 0 }
        rootfs_drive_pci = $rootfsDrive
    }

    llm = [ordered]@{
        model_path      = "$env:ProgramData\SymbioseOS\TensorStore"
        model_format    = "SafeTensors"
        precision       = "F32"
        param_count_b   = 0
        vram_required_gb = 0
        mmproj_path     = ""
        whisper_model_path = ""
        tts_model_path  = ""
        multimodal = [ordered]@{
            enabled       = $wizard.multimodal.vision_enabled
            video_enabled = $wizard.multimodal.moviola_enabled
            audio_enabled = $wizard.multimodal.stt_enabled
            chat_enabled  = $true
        }
        hardware_allocation = [ordered]@{
            mode        = "auto"
            gpu_layers  = -1
            cpu_threads = $wizard.vcpu_count
            preset      = "balanced"
        }
        moviola = [ordered]@{
            enabled          = $wizard.multimodal.moviola_enabled
            delta_threshold  = $wizard.multimodal.delta_threshold
            dvs_mode         = $wizard.multimodal.dvs_enabled
        }
    }

    cluster = [ordered]@{
        ircd_host    = "0.0.0.0"
        ircd_port    = 6697
        rdma_enabled = $false
        max_nodes    = 64
    }

    apbx = [ordered]@{
        password   = "malte"
        encryption = "LZMA2+AES256"
    }
}

# -- Write JSON -------------------------------------------------------
$outputPath = Join-Path $configDir "symbiose_config.json"
$config | ConvertTo-Json -Depth 5 | Out-File -FilePath $outputPath -Encoding UTF8 -Force

Write-Host "[CONFIG-009] ============================================" -ForegroundColor Green
Write-Host "[CONFIG-009] Configuration written: $outputPath" -ForegroundColor Green
Write-Host "[CONFIG-009]   GPUs surrendered: $($gpuArray.Count)" -ForegroundColor Green
Write-Host "[CONFIG-009]   CCD drives: $($nvmeArray.Count)" -ForegroundColor Green
Write-Host "[CONFIG-009]   RAM: $($wizard.ram_allocation_gb) GB" -ForegroundColor Green
Write-Host "[CONFIG-009]   vCPU: $($wizard.vcpu_count)" -ForegroundColor Green
Write-Host "[CONFIG-009]   Mode: $($wizard.execution_mode)" -ForegroundColor Green
Write-Host "[CONFIG-009]   MMIO: $($wizard.high_mmio_mb) MB" -ForegroundColor Green
Write-Host "[CONFIG-009] ============================================" -ForegroundColor Green
