# Phase 5: Physical Hardware Validation Checklist

## Pre-Deployment Verification

- [ ] BIOS: Secure Boot disabled
- [ ] BIOS: VT-x / AMD-V enabled
- [ ] BIOS: IOMMU / VT-d enabled
- [ ] Windows: VBS disabled (verified via `Get-ComputerInfo`)
- [ ] Windows: HVCI disabled (verified via Registry)
- [ ] Windows: Memory Integrity disabled (verified via Security Center)
- [ ] Windows: Test Signing enabled (verified via `bcdedit`)

## Hardware Airlock Verification

- [ ] Target NVMe drive visible in BIOS
- [ ] Target NVMe drive NOT visible in Windows Disk Management
- [ ] SymbioseNull driver loaded for target NVMe (verified via `pnputil`)
- [ ] Target GPU visible in BIOS
- [ ] Target GPU NOT visible in Windows Device Manager
- [ ] SymbioseNull driver loaded for target GPU (verified via `pnputil`)

## Driver Verification

- [ ] symbiose_bridge.sys loaded (verified via `sc query`)
- [ ] Driver start type = boot (verified via `sc qc`)
- [ ] No BSOD on boot with driver loaded

## Payload Verification

- [ ] BZIMAGE present at C:\Symbiose_Core\BZIMAGE
- [ ] CHAOS.RDZ present at C:\Symbiose_Core\CHAOS.RDZ
- [ ] ChaosLoader.exe present at C:\Symbiose_Core\ChaosLoader.exe
- [ ] SHA256 hashes match build artifacts

## Chaos-OS Boot Verification

- [ ] ChaosLoader.exe runs without error
- [ ] ACPI shutdown hook triggers on Windows shutdown
- [ ] LLM ACK_READY_TO_DIE signal received within 30 seconds
- [ ] NVMe context dump completes before power-off
- [ ] System reboots successfully after context dump

## Rollback Verification

- [ ] Rollback script tested: VBS re-enabled
- [ ] Rollback script tested: NVMe driver restored
- [ ] Rollback script tested: GPU driver restored
- [ ] Rollback script tested: symbiose_bridge.sys unregistered
- [ ] Rollback script tested: Windows boots normally after rollback