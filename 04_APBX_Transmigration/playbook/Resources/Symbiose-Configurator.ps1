<#
.SYNOPSIS
    Symbiose-Configurator.ps1 - Dark-themed WinForms Hardware Configuration GUI
    Handles CONFIG-003/004/005/006/008/011/012/013/014

.DESCRIPTION
    Launched by AME Wizard during Phase 0 via !powerShell.
    Reads detection JSON files from $env:TEMP, presents a multi-tab
    dark-themed wizard, and writes selections to symbiose_wizard.json.
    Write-SymbioseConfig.ps1 later merges this with AME Wizard options.

.NOTES
    Reference: Interactive_Plan.md SIX.1 (symbiose_config.json schema)
#>

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = "Stop"

# ======================================================================
# Load detection data
# ======================================================================

$gpuData = @()
$driveData = @()
$sysData = @{ ram = @{ total_gb = 32; default_gb = 24; min_allocation_gb = 8; max_allocation_gb = 28 }
              cpu = @{ logical_processors = 16; default_vcpu = 12; max_vcpu = 14; name = "Unknown" }
              numa = @{ is_multi_numa = $false; node_count = 1; nodes = @() } }

$gpuFile = Join-Path $env:TEMP "symbiose_gpus.json"
$driveFile = Join-Path $env:TEMP "symbiose_drives.json"
$sysFile = Join-Path $env:TEMP "symbiose_system.json"

if (Test-Path $gpuFile)   { $gpuData   = Get-Content $gpuFile   -Raw | ConvertFrom-Json }
if (Test-Path $driveFile) { $driveData = Get-Content $driveFile  -Raw | ConvertFrom-Json }
if (Test-Path $sysFile)   { $sysData   = Get-Content $sysFile    -Raw | ConvertFrom-Json }

# ======================================================================
# Theme colors
# ======================================================================

$bgDark      = [System.Drawing.Color]::FromArgb(18, 18, 24)
$bgPanel     = [System.Drawing.Color]::FromArgb(28, 28, 38)
$bgInput     = [System.Drawing.Color]::FromArgb(38, 38, 52)
$fgPrimary   = [System.Drawing.Color]::FromArgb(220, 220, 240)
$fgSecondary = [System.Drawing.Color]::FromArgb(140, 140, 170)
$accent      = [System.Drawing.Color]::FromArgb(120, 80, 255)
$accentHover = [System.Drawing.Color]::FromArgb(140, 100, 255)
$warnColor   = [System.Drawing.Color]::FromArgb(255, 180, 60)
$okColor     = [System.Drawing.Color]::FromArgb(80, 220, 120)

$fontTitle   = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$fontNormal  = New-Object System.Drawing.Font("Segoe UI", 10)
$fontSmall   = New-Object System.Drawing.Font("Segoe UI", 8.5)
$fontMono    = New-Object System.Drawing.Font("Cascadia Code", 9)

# ======================================================================
# Main form
# ======================================================================

$form = New-Object System.Windows.Forms.Form
$form.Text = "SymbioseOS V3 - Hardware Configurator"
$form.Size = New-Object System.Drawing.Size(720, 620)
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox = $false
$form.BackColor = $bgDark
$form.ForeColor = $fgPrimary
$form.Font = $fontNormal

# -- Title bar --------------------------------------------------------
$lblTitle = New-Object System.Windows.Forms.Label
$lblTitle.Text = "[*]  SymbioseOS V3 - Hardware Configuration"
$lblTitle.Font = $fontTitle
$lblTitle.ForeColor = $fgPrimary
$lblTitle.Location = New-Object System.Drawing.Point(20, 12)
$lblTitle.AutoSize = $true
$form.Controls.Add($lblTitle)

$lblSub = New-Object System.Windows.Forms.Label
$lblSub.Text = "Configure resources for the autonomous AI node"
$lblSub.Font = $fontSmall
$lblSub.ForeColor = $fgSecondary
$lblSub.Location = New-Object System.Drawing.Point(20, 40)
$lblSub.AutoSize = $true
$form.Controls.Add($lblSub)

# -- Tab control ------------------------------------------------------
$tabs = New-Object System.Windows.Forms.TabControl
$tabs.Location = New-Object System.Drawing.Point(15, 65)
$tabs.Size = New-Object System.Drawing.Size(680, 460)
$tabs.Font = $fontNormal

# ======================================================================
# TAB 1: GPU + NVMe Selection (CONFIG-001/002)
# ======================================================================

$tab1 = New-Object System.Windows.Forms.TabPage
$tab1.Text = "[PC] Hardware"
$tab1.BackColor = $bgPanel

$y = 15
$lblGpu = New-Object System.Windows.Forms.Label
$lblGpu.Text = "GPU Selection - Select GPU(s) to surrender to SymbioseOS:"
$lblGpu.Location = New-Object System.Drawing.Point(15, $y)
$lblGpu.Size = New-Object System.Drawing.Size(640, 22)
$lblGpu.ForeColor = $fgPrimary
$tab1.Controls.Add($lblGpu)
$y += 28

$gpuChecklist = New-Object System.Windows.Forms.CheckedListBox
$gpuChecklist.Location = New-Object System.Drawing.Point(15, $y)
$gpuChecklist.Size = New-Object System.Drawing.Size(640, 90)
$gpuChecklist.BackColor = $bgInput
$gpuChecklist.ForeColor = $fgPrimary
$gpuChecklist.Font = $fontMono
$gpuChecklist.BorderStyle = "None"

foreach ($gpu in $gpuData) {
    $label = "$($gpu.friendly_name) - VRAM: $($gpu.vram_gb)GB, BAR1: $($gpu.bar1_gb)GB"
    $gpuChecklist.Items.Add($label, $true) | Out-Null
}
$tab1.Controls.Add($gpuChecklist)
$y += 100

$lblDrive = New-Object System.Windows.Forms.Label
$lblDrive.Text = "NVMe/Storage - Select CCD (Continuous Context Drive):"
$lblDrive.Location = New-Object System.Drawing.Point(15, $y)
$lblDrive.Size = New-Object System.Drawing.Size(640, 22)
$lblDrive.ForeColor = $fgPrimary
$tab1.Controls.Add($lblDrive)
$y += 28

$driveChecklist = New-Object System.Windows.Forms.CheckedListBox
$driveChecklist.Location = New-Object System.Drawing.Point(15, $y)
$driveChecklist.Size = New-Object System.Drawing.Size(640, 90)
$driveChecklist.BackColor = $bgInput
$driveChecklist.ForeColor = $fgPrimary
$driveChecklist.Font = $fontMono
$driveChecklist.BorderStyle = "None"

foreach ($drv in $driveData) {
    $warn = if ($drv.has_windows_partitions) { " [!] WINDOWS" } else { "" }
    $label = "$($drv.friendly_name) - $($drv.size_gb)GB $($drv.bus_type)$warn"
    $checked = -not $drv.has_windows_partitions
    $driveChecklist.Items.Add($label, $checked) | Out-Null
}
$tab1.Controls.Add($driveChecklist)
$y += 100

# Override checkbox for drives with Windows partitions
$chkAllowWinDrives = New-Object System.Windows.Forms.CheckBox
$chkAllowWinDrives.Text = "Allow drives with Windows partitions (SymbioseNull does NOT format — filter only)"
$chkAllowWinDrives.Location = New-Object System.Drawing.Point(15, $y)
$chkAllowWinDrives.Size = New-Object System.Drawing.Size(640, 20)
$chkAllowWinDrives.ForeColor = $warnColor
$chkAllowWinDrives.Font = $fontSmall
$chkAllowWinDrives.Checked = $false
$tab1.Controls.Add($chkAllowWinDrives)
$y += 24

$lblWinWarn = New-Object System.Windows.Forms.Label
$lblWinWarn.Text = "Safe: the filter controls I/O access only. Your data stays intact."
$lblWinWarn.Location = New-Object System.Drawing.Point(35, $y)
$lblWinWarn.Size = New-Object System.Drawing.Size(620, 16)
$lblWinWarn.ForeColor = $fgSecondary
$lblWinWarn.Font = $fontSmall
$tab1.Controls.Add($lblWinWarn)
$y += 22

$lblMmio = New-Object System.Windows.Forms.Label
$lblMmio.Text = "MMIO Auto-Calculation: (computed from selected GPUs)"
$lblMmio.Location = New-Object System.Drawing.Point(15, $y)
$lblMmio.Size = New-Object System.Drawing.Size(640, 22)
$lblMmio.ForeColor = $fgSecondary
$lblMmio.Font = $fontSmall
$tab1.Controls.Add($lblMmio)

$tabs.TabPages.Add($tab1)

# ======================================================================
# TAB 2: RAM + vCPU + NUMA (CONFIG-003/004/005)
# ======================================================================

$tab2 = New-Object System.Windows.Forms.TabPage
$tab2.Text = "[MEM] Resources"
$tab2.BackColor = $bgPanel

$y = 15
$lblRam = New-Object System.Windows.Forms.Label
$lblRam.Text = "RAM Allocation (GB) - Reserve 4GB for Windows host:"
$lblRam.Location = New-Object System.Drawing.Point(15, $y)
$lblRam.AutoSize = $true
$lblRam.ForeColor = $fgPrimary
$tab2.Controls.Add($lblRam)
$y += 28

$ramSlider = New-Object System.Windows.Forms.TrackBar
$ramSlider.Location = New-Object System.Drawing.Point(15, $y)
$ramSlider.Size = New-Object System.Drawing.Size(540, 45)
$ramSlider.Minimum = $sysData.ram.min_allocation_gb
$ramSlider.Maximum = $sysData.ram.max_allocation_gb
$ramSlider.Value = $sysData.ram.default_gb
$ramSlider.TickFrequency = 4
$ramSlider.BackColor = $bgPanel
$tab2.Controls.Add($ramSlider)

$lblRamVal = New-Object System.Windows.Forms.Label
$lblRamVal.Text = "$($sysData.ram.default_gb) GB"
$lblRamVal.Location = New-Object System.Drawing.Point(570, ($y + 8))
$lblRamVal.Size = New-Object System.Drawing.Size(80, 25)
$lblRamVal.ForeColor = $okColor
$lblRamVal.Font = $fontTitle
$tab2.Controls.Add($lblRamVal)

$ramSlider.Add_ValueChanged({ $lblRamVal.Text = "$($ramSlider.Value) GB" })
$y += 60

$lblCpu = New-Object System.Windows.Forms.Label
$lblCpu.Text = "vCPU Allocation - Reserve 2 cores for host ($($sysData.cpu.name)):"
$lblCpu.Location = New-Object System.Drawing.Point(15, $y)
$lblCpu.AutoSize = $true
$lblCpu.ForeColor = $fgPrimary
$tab2.Controls.Add($lblCpu)
$y += 28

$cpuSlider = New-Object System.Windows.Forms.TrackBar
$cpuSlider.Location = New-Object System.Drawing.Point(15, $y)
$cpuSlider.Size = New-Object System.Drawing.Size(540, 45)
$cpuSlider.Minimum = 1
$cpuSlider.Maximum = $sysData.cpu.max_vcpu
$cpuSlider.Value = $sysData.cpu.default_vcpu
$cpuSlider.TickFrequency = 2
$cpuSlider.BackColor = $bgPanel
$tab2.Controls.Add($cpuSlider)

$lblCpuVal = New-Object System.Windows.Forms.Label
$lblCpuVal.Text = "$($sysData.cpu.default_vcpu) vCPU"
$lblCpuVal.Location = New-Object System.Drawing.Point(570, ($y + 8))
$lblCpuVal.Size = New-Object System.Drawing.Size(90, 25)
$lblCpuVal.ForeColor = $okColor
$lblCpuVal.Font = $fontTitle
$tab2.Controls.Add($lblCpuVal)

$cpuSlider.Add_ValueChanged({ $lblCpuVal.Text = "$($cpuSlider.Value) vCPU" })
$y += 60

# NUMA toggle
$chkNuma = New-Object System.Windows.Forms.CheckBox
$chkNuma.Text = "Enable NUMA-aware allocation (pin vCPU + RAM to GPU NUMA node)"
$chkNuma.Location = New-Object System.Drawing.Point(15, $y)
$chkNuma.Size = New-Object System.Drawing.Size(500, 24)
$chkNuma.ForeColor = $fgPrimary
$chkNuma.Checked = $sysData.numa.is_multi_numa
$chkNuma.Enabled = $sysData.numa.is_multi_numa
$tab2.Controls.Add($chkNuma)
$y += 30

if (-not $sysData.numa.is_multi_numa) {
    $lblNumaNote = New-Object System.Windows.Forms.Label
    $lblNumaNote.Text = "(Single NUMA node detected - NUMA pinning not applicable)"
    $lblNumaNote.Location = New-Object System.Drawing.Point(35, $y)
    $lblNumaNote.AutoSize = $true
    $lblNumaNote.ForeColor = $fgSecondary
    $lblNumaNote.Font = $fontSmall
    $tab2.Controls.Add($lblNumaNote)
}
$y += 35

# Execution mode
$lblMode = New-Object System.Windows.Forms.Label
$lblMode.Text = "Execution Mode (CONFIG-006):"
$lblMode.Location = New-Object System.Drawing.Point(15, $y)
$lblMode.AutoSize = $true
$lblMode.ForeColor = $fgPrimary
$tab2.Controls.Add($lblMode)
$y += 28

$rbDisk = New-Object System.Windows.Forms.RadioButton
$rbDisk.Text = "Disk-backed (persistent rootfs - survives reboot)"
$rbDisk.Location = New-Object System.Drawing.Point(30, $y)
$rbDisk.Size = New-Object System.Drawing.Size(500, 22)
$rbDisk.ForeColor = $fgPrimary
$rbDisk.Checked = $true
$tab2.Controls.Add($rbDisk)
$y += 26

$rbRam = New-Object System.Windows.Forms.RadioButton
$rbRam.Text = "Ramdisk (volatile - faster, but lost on reboot)"
$rbRam.Location = New-Object System.Drawing.Point(30, $y)
$rbRam.Size = New-Object System.Drawing.Size(500, 22)
$rbRam.ForeColor = $fgPrimary
$tab2.Controls.Add($rbRam)

$tabs.TabPages.Add($tab2)

# ======================================================================
# TAB 3: Multimodal (CONFIG-011/012/013/014)
# ======================================================================

$tab3 = New-Object System.Windows.Forms.TabPage
$tab3.Text = "[AI] Multimodal"
$tab3.BackColor = $bgPanel

$y = 15
$lblMm = New-Object System.Windows.Forms.Label
$lblMm.Text = "Multimodal Senses - Your AI will have:"
$lblMm.Location = New-Object System.Drawing.Point(15, $y)
$lblMm.AutoSize = $true
$lblMm.ForeColor = $fgPrimary
$tab3.Controls.Add($lblMm)
$y += 30

$chkVision = New-Object System.Windows.Forms.CheckBox
$chkVision.Text = "[EYE] Vision (CLIP normalization - requires mmproj)"
$chkVision.Location = New-Object System.Drawing.Point(30, $y)
$chkVision.Size = New-Object System.Drawing.Size(500, 24)
$chkVision.ForeColor = $fgPrimary
$chkVision.Checked = $true
$tab3.Controls.Add($chkVision)
$y += 30

$chkSTT = New-Object System.Windows.Forms.CheckBox
$chkSTT.Text = "[EAR] Hearing / STT (Whisper - requires .bin model)"
$chkSTT.Location = New-Object System.Drawing.Point(30, $y)
$chkSTT.Size = New-Object System.Drawing.Size(500, 24)
$chkSTT.ForeColor = $fgPrimary
$chkSTT.Checked = $true
$tab3.Controls.Add($chkSTT)
$y += 30

$chkTTS = New-Object System.Windows.Forms.CheckBox
$chkTTS.Text = "[VOICE] Speech / TTS (Piper - requires .onnx model)"
$chkTTS.Location = New-Object System.Drawing.Point(30, $y)
$chkTTS.Size = New-Object System.Drawing.Size(500, 24)
$chkTTS.ForeColor = $fgPrimary
$chkTTS.Checked = $true
$tab3.Controls.Add($chkTTS)
$y += 30

$chkMoviola = New-Object System.Windows.Forms.CheckBox
$chkMoviola.Text = "[BOLT] Moviola Delta-Motion (software frame differencing)"
$chkMoviola.Location = New-Object System.Drawing.Point(30, $y)
$chkMoviola.Size = New-Object System.Drawing.Size(500, 24)
$chkMoviola.ForeColor = $fgPrimary
$chkMoviola.Checked = $true
$tab3.Controls.Add($chkMoviola)
$y += 30

$chkDVS = New-Object System.Windows.Forms.CheckBox
$chkDVS.Text = "[CAM] DVS Hardware (Dynamic Vision Sensor via libcaer)"
$chkDVS.Location = New-Object System.Drawing.Point(30, $y)
$chkDVS.Size = New-Object System.Drawing.Size(500, 24)
$chkDVS.ForeColor = $fgSecondary
$chkDVS.Checked = $false
$chkDVS.Enabled = $false  # Greyed out until DVS hardware detected
$tab3.Controls.Add($chkDVS)
$y += 35

# Moviola sensitivity slider (CONFIG-013)
$lblSens = New-Object System.Windows.Forms.Label
$lblSens.Text = "Moviola Delta Threshold (0=sensitive, 255=coarse, default=15):"
$lblSens.Location = New-Object System.Drawing.Point(15, $y)
$lblSens.AutoSize = $true
$lblSens.ForeColor = $fgPrimary
$tab3.Controls.Add($lblSens)
$y += 28

$sensSlider = New-Object System.Windows.Forms.TrackBar
$sensSlider.Location = New-Object System.Drawing.Point(15, $y)
$sensSlider.Size = New-Object System.Drawing.Size(540, 45)
$sensSlider.Minimum = 0
$sensSlider.Maximum = 255
$sensSlider.Value = 15
$sensSlider.TickFrequency = 16
$sensSlider.BackColor = $bgPanel
$tab3.Controls.Add($sensSlider)

$lblSensVal = New-Object System.Windows.Forms.Label
$lblSensVal.Text = "15"
$lblSensVal.Location = New-Object System.Drawing.Point(570, ($y + 8))
$lblSensVal.Size = New-Object System.Drawing.Size(60, 25)
$lblSensVal.ForeColor = $okColor
$lblSensVal.Font = $fontTitle
$tab3.Controls.Add($lblSensVal)

$sensSlider.Add_ValueChanged({ $lblSensVal.Text = "$($sensSlider.Value)" })

$tabs.TabPages.Add($tab3)

# ======================================================================
# Confirm button
# ======================================================================

$form.Controls.Add($tabs)

$btnConfirm = New-Object System.Windows.Forms.Button
$btnConfirm.Text = "[OK]  Confirm Configuration"
$btnConfirm.Location = New-Object System.Drawing.Point(240, 540)
$btnConfirm.Size = New-Object System.Drawing.Size(240, 40)
$btnConfirm.FlatStyle = "Flat"
$btnConfirm.BackColor = $accent
$btnConfirm.ForeColor = [System.Drawing.Color]::White
$btnConfirm.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$btnConfirm.FlatAppearance.BorderSize = 0

$btnConfirm.Add_Click({
    # -- Collect all selections ------------------------------------
    $selectedGpus = @()
    for ($i = 0; $i -lt $gpuChecklist.Items.Count; $i++) {
        if ($gpuChecklist.GetItemChecked($i) -and $i -lt $gpuData.Count) {
            $gpuData[$i].surrendered = $true
            $selectedGpus += $gpuData[$i]
        }
    }

    $selectedDrives = @()
    for ($i = 0; $i -lt $driveChecklist.Items.Count; $i++) {
        if ($driveChecklist.GetItemChecked($i) -and $i -lt $driveData.Count) {
            $driveData[$i].is_ccd = $true
            $selectedDrives += $driveData[$i]
        }
    }

    # MMIO auto-calculation: 2 * BAR1 * GPU_count * 1024 (CONFIG-008)
    $totalBar1 = ($selectedGpus | Measure-Object -Property bar1_gb -Sum).Sum
    $gpuCount = $selectedGpus.Count
    $highMmioMb = 2 * $totalBar1 * $gpuCount * 1024
    if ($highMmioMb -lt 4096) { $highMmioMb = 4096 }

    $wizardResult = @{
        gpu_selections    = $selectedGpus
        drive_selections  = $selectedDrives
        allow_windows_ccd = $chkAllowWinDrives.Checked
        ram_allocation_gb = $ramSlider.Value
        vcpu_count        = $cpuSlider.Value
        numa_pinned       = $chkNuma.Checked
        numa_node         = 0
        execution_mode    = if ($rbDisk.Checked) { "disk-backed" } else { "ramdisk" }
        high_mmio_mb      = $highMmioMb
        multimodal = @{
            vision_enabled    = $chkVision.Checked
            stt_enabled       = $chkSTT.Checked
            tts_enabled       = $chkTTS.Checked
            moviola_enabled   = $chkMoviola.Checked
            dvs_enabled       = $chkDVS.Checked
            delta_threshold   = $sensSlider.Value
        }
    }

    $outPath = Join-Path $env:TEMP "symbiose_wizard.json"
    $wizardResult | ConvertTo-Json -Depth 5 | Out-File -FilePath $outPath -Encoding UTF8 -Force

    Write-Host "[CONFIGURATOR] Saved to $outPath" -ForegroundColor Green
    $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.Close()
})

$form.Controls.Add($btnConfirm)

# -- Show form --------------------------------------------------------
$result = $form.ShowDialog()

if ($result -ne [System.Windows.Forms.DialogResult]::OK) {
    Write-Error "[CONFIGURATOR] User cancelled configuration"
    exit 1
}

Write-Host "[CONFIGURATOR] Configuration complete" -ForegroundColor Green
