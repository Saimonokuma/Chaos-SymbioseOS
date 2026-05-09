# TEST-003: ChaosLoader Native VMX Smoke Test
# File: 05_Integration_Tests/qemu_scripts/native_vmx_smoke.ps1
#
# Cross-References:
#   XI  Line 5098 -- ChaosLoader smoke test monitoring serial output for
#                     kernel init sequence. Fail on 30s timeout.
#   XIII.4      -- HIVE-LOADER verification: Serial output contains
#                  'Linux version' within 30s; exits 0 on success, 1 on timeout
#   XIII.9      -- TEST gate: Passes on clean host; fails predictably on
#                  EPT misconfig
#   X.17        -- ChaosLoader keeps 3 pending IOCTLs in flight simultaneously:
#                  WAIT_VMEXIT, SERIAL_READ, SHUTDOWN_ACK
#   XIII.11     -- Error contract: VMLAUNCH fail -> Check VT-x BIOS setting
#                  SHM fail -> Is symbiose_bridge.sys loaded?

param(
    [string]$ChaosLoaderPath = "",
    [string]$ConfigPath = "",
    [int]$TimeoutSeconds = 30,
    [switch]$DryRunOnly
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

if (-not $ChaosLoaderPath) {
    $ChaosLoaderPath = Join-Path $ProjectRoot "03_HiveMind_Orchestrator\ChaosLoader\build\ChaosLoader.exe"
}
if (-not $ConfigPath) {
    $ConfigPath = Join-Path $ProjectRoot "04_APBX_Transmigration\playbook\config\symbiose_config.json"
}

$TestsRun = 0
$TestsPassed = 0
$TestsFailed = 0

function Pass($msg) {
    $script:TestsRun++
    $script:TestsPassed++
    Write-Host "  [PASS] $msg" -ForegroundColor Green
}

function Fail($msg, $detail = "") {
    $script:TestsRun++
    $script:TestsFailed++
    Write-Host "  [FAIL] $msg" -ForegroundColor Red
    if ($detail) {
        Write-Host "         Detail: $detail" -ForegroundColor Yellow
    }
}

function Info($msg) {
    Write-Host "  [INFO] $msg" -ForegroundColor Cyan
}

# --- Header ---
Write-Host ""
Write-Host "======================================================================="
Write-Host " TEST-003: ChaosLoader Native VMX Smoke Test"
Write-Host " Ref: Interactive_Plan.md XI(5098), XIII.4/9, X.17"
Write-Host "======================================================================="
Write-Host ""

# --- Pre-flight Check 1: Driver ---
Write-Host "-- Pre-flight Checks --"

try {
    $driverStatus = sc.exe query SymbioseBridge 2>&1
    if ($driverStatus -match "RUNNING") {
        Pass "TEST-003-P1: symbiose_bridge.sys driver is RUNNING"
    } elseif ($driverStatus -match "STOPPED") {
        Fail "TEST-003-P1: symbiose_bridge.sys driver status" "Driver is STOPPED"
    } else {
        Fail "TEST-003-P1: symbiose_bridge.sys driver status" "Driver not found - install via pnputil first"
    }
} catch {
    Fail "TEST-003-P1: symbiose_bridge.sys driver query" "sc query failed: $_"
}

# --- Pre-flight Check 2: ChaosLoader Binary ---
if (Test-Path $ChaosLoaderPath) {
    $exeInfo = Get-Item $ChaosLoaderPath
    Pass "TEST-003-P2: ChaosLoader.exe found ($($exeInfo.Length) bytes)"
} else {
    Fail "TEST-003-P2: ChaosLoader.exe not found" "Expected: $ChaosLoaderPath"
    Write-Host ""
    Write-Host "FATAL: Cannot proceed without ChaosLoader.exe" -ForegroundColor Red
    Write-Host ""
    Write-Host "======================================================================="
    Write-Host " TEST-003 Results: $TestsPassed/$TestsRun passed, $TestsFailed failed"
    Write-Host " Status: FAILED" -ForegroundColor Red
    Write-Host "======================================================================="
    exit 2
}

# --- Pre-flight Check 3: Config JSON ---
if (Test-Path $ConfigPath) {
    try {
        $config = Get-Content $ConfigPath -Raw | ConvertFrom-Json
        $requiredFields = @("ram_gb", "vcpu_count", "execution_mode")
        $missingFields = @()
        foreach ($field in $requiredFields) {
            if (-not ($config.PSObject.Properties.Name -contains $field)) {
                $missingFields += $field
            }
        }
        if ($missingFields.Count -eq 0) {
            $ramVal = $config.ram_gb
            $vcpuVal = $config.vcpu_count
            $modeVal = $config.execution_mode
            Pass "TEST-003-P3: symbiose_config.json valid (RAM=${ramVal}GB, vCPU=${vcpuVal}, mode=${modeVal})"
        } else {
            Fail "TEST-003-P3: symbiose_config.json incomplete" "Missing fields: $($missingFields -join ', ')"
        }
    } catch {
        Fail "TEST-003-P3: symbiose_config.json parse" "JSON parse error: $_"
    }
} else {
    Fail "TEST-003-P3: symbiose_config.json not found" "Expected: $ConfigPath"
}

Write-Host ""

# --- Test: Dry Run ---
Write-Host "-- Dry Run Test (XIII.4: JSON read) --"

if (Test-Path $ChaosLoaderPath) {
    try {
        $dryRunOutput = & $ChaosLoaderPath --dry-run --config "$ConfigPath" 2>&1
        $dryRunString = $dryRunOutput | Out-String

        if ($dryRunString -match "RAM|ram|vCPU|vcpu|mode") {
            Pass "TEST-003-D1: --dry-run prints parsed config values"
            $truncLen = [Math]::Min(200, $dryRunString.Trim().Length)
            Info "Output: $($dryRunString.Trim().Substring(0, $truncLen))"
        } elseif ($LASTEXITCODE -eq 0) {
            Pass "TEST-003-D1: --dry-run exited successfully (exit code 0)"
        } else {
            Fail "TEST-003-D1: --dry-run" "Exit code: $LASTEXITCODE"
        }
    } catch {
        Fail "TEST-003-D1: --dry-run execution" "Error: $_"
    }
}

if ($DryRunOnly) {
    Write-Host ""
    Write-Host "-- Dry-run only mode - skipping VMX launch --"
    Write-Host ""
    Write-Host "======================================================================="
    Write-Host " TEST-003 Results: $TestsPassed/$TestsRun passed, $TestsFailed failed"
    if ($TestsFailed -eq 0) { Write-Host " Status: ALL PASS" -ForegroundColor Green }
    else { Write-Host " Status: FAILED" -ForegroundColor Red }
    Write-Host "======================================================================="
    Write-Host ""
    exit $TestsFailed
}

Write-Host ""

# --- Test: Serial Output Monitoring ---
$timeoutMsg = "VMX Launch Test (Serial output -> Linux version in " + $TimeoutSeconds.ToString() + "s)"
Write-Host "-- $timeoutMsg --"

$serialLog = Join-Path $env:TEMP "test003_serial_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"
$chaosProcess = $null

try {
    $launchMsg = "Launching ChaosLoader.exe (timeout: " + $TimeoutSeconds.ToString() + "s)"
    Info $launchMsg
    Info "Serial log: $serialLog"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ChaosLoaderPath
    $psi.Arguments = "--config `"$ConfigPath`" --serial-log `"$serialLog`""
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $chaosProcess = [System.Diagnostics.Process]::Start($psi)
    Info "ChaosLoader PID: $($chaosProcess.Id)"

    $startTime = Get-Date
    $found = $false
    $elapsed = 0

    while ($elapsed -lt $TimeoutSeconds) {
        Start-Sleep -Milliseconds 500
        $elapsed = ((Get-Date) - $startTime).TotalSeconds

        if ($chaosProcess.HasExited) {
            $exitCode = $chaosProcess.ExitCode
            $stderr = $chaosProcess.StandardError.ReadToEnd()

            if ($exitCode -eq 1) {
                Fail "TEST-003-V1: ChaosLoader exited with code 1" "Likely VMLAUNCH failure. VT-x enabled in BIOS?"
                if ($stderr) { Info "stderr: $($stderr.Trim())" }
            } elseif ($exitCode -eq 2) {
                Fail "TEST-003-V1: ChaosLoader exited with code 2" "SHM mapping failed - driver not loaded?"
            } else {
                Fail "TEST-003-V1: ChaosLoader exited unexpectedly" "Exit code: $exitCode"
            }
            break
        }

        if (Test-Path $serialLog) {
            $serialContent = Get-Content $serialLog -Raw -ErrorAction SilentlyContinue
            if ($serialContent -and ($serialContent -match "Linux version")) {
                $found = $true
                $bootTime = [Math]::Round($elapsed, 1)
                $passMsg = "TEST-003-V1: Linux version detected in serial output (" + $bootTime.ToString() + " sec)"
                Pass $passMsg
                $lines = $serialContent -split [Environment]::NewLine
                foreach ($ln in $lines) {
                    if ($ln -match "Linux version") {
                        Info "Kernel: $($ln.Trim())"
                        break
                    }
                }
                break
            }
        }

        if ($chaosProcess.StandardOutput.Peek() -ge 0) {
            $stdoutLine = $chaosProcess.StandardOutput.ReadLine()
            if ($stdoutLine -match "Linux version") {
                $found = $true
                $bootTime = [Math]::Round($elapsed, 1)
                $passMsg = "TEST-003-V1: Linux version in stdout (" + $bootTime.ToString() + " sec)"
                Pass $passMsg
                Info "Output: $stdoutLine"
                break
            }
        }
    }

    if (-not $found -and -not $chaosProcess.HasExited) {
        $failMsg = "No Linux version within " + $TimeoutSeconds.ToString() + " seconds"
        Fail "TEST-003-V1: Serial output timeout" $failMsg
    }

} catch {
    Fail "TEST-003-V1: ChaosLoader launch" "Exception: $_"
} finally {
    if ($chaosProcess -and -not $chaosProcess.HasExited) {
        Info "Terminating ChaosLoader (PID: $($chaosProcess.Id))"
        $chaosProcess.Kill()
        $chaosProcess.WaitForExit(5000)
    }
    if ($chaosProcess) { $chaosProcess.Dispose() }

    if (Test-Path $serialLog) {
        $logContent = Get-Content $serialLog -ErrorAction SilentlyContinue
        if ($logContent) {
            Write-Host ""
            Write-Host "-- Serial Output (last 20 lines) --"
            $logContent | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
        }
        Remove-Item $serialLog -ErrorAction SilentlyContinue
    }
}

# --- Summary ---
Write-Host ""
Write-Host "======================================================================="
Write-Host " TEST-003 Results: $TestsPassed/$TestsRun passed, $TestsFailed failed"
if ($TestsFailed -eq 0) {
    Write-Host " Status: ALL PASS" -ForegroundColor Green
} else {
    Write-Host " Status: FAILED" -ForegroundColor Red
}
Write-Host " Gate: XIII.9 - Serial output contains Linux version within 30s"
Write-Host "======================================================================="
Write-Host ""

exit $TestsFailed
