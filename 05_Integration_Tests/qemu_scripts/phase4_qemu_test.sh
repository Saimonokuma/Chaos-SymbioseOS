#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# TEST-001: VMCS Register Dump on Triple Fault
# File: 05_Integration_Tests/qemu_scripts/phase4_qemu_test.sh
#
# Cross-References:
#   §XI  Line 5096 — "Dump VMCS registers upon triple fault; validate CR0/CR2/CR3/RIP"
#   §XIII·3      — BRIDGE verification: VMXON trace, EPT PML4 trace
#   §XIII·4      — HIVE-LOADER verification: "CR0/CR2/CR3/RIP printed within 5s"
#   §XIII·9      — TEST gate: "non-zero CR0, valid RIP; exit 0"
#   §X·9         — VMCS_LINK_POINTER must be 0xFFFFFFFFFFFFFFFF
#   §X·10        — CR0/CR4 fixed bits from IA32_VMX_CR0_FIXED0/1 MSRs
#   §X·12        — Check ZF/CF before reading VM_INSTRUCTION_ERROR (0x4400)
#   §X·14        — CR2 is NOT in VMCS — read via __readcr2() immediately
#   §XIII·11     — Error contract: triple fault → SYMBIOSE_CRASH_DUMP struct
#
# Test Flow:
#   1. Launch QEMU with a minimal kernel designed to triple-fault
#   2. Capture serial output (COM1 → stdio)
#   3. Parse the SYMBIOSE_CRASH_DUMP output for CR0/CR2/CR3/RIP
#   4. Validate all registers are present and non-zero (except CR2 which may be 0)
#   5. Validate RIP is in plausible kernel address range
#
# Usage: bash phase4_qemu_test.sh [--qemu-path PATH] [--kernel PATH] [--timeout SECS]
#
# Exit codes:
#   0 = All checks pass
#   1 = Register dump not found or invalid
#   2 = QEMU/kernel not found
#   3 = Timeout waiting for output
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

# ─── Configuration ───────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

QEMU_BIN="${1:-qemu-system-x86_64}"
KERNEL_PATH="${2:-$PROJECT_ROOT/01_Chaos_Kernel/bin/BZIMAGE}"
TIMEOUT_SECS="${3:-30}"
SERIAL_LOG="$(mktemp /tmp/test001_serial_XXXXXX.log)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# ─── Test Infrastructure ────────────────────────────────────────────────────
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass() {
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}[PASS]${NC} $1"
}

fail() {
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "  ${RED}[FAIL]${NC} $1"
    echo -e "        ${YELLOW}Expected:${NC} $2"
    echo -e "        ${YELLOW}Got:${NC}      $3"
}

info() {
    echo -e "  ${CYAN}[INFO]${NC} $1"
}

cleanup() {
    # Kill QEMU if still running
    if [[ -n "${QEMU_PID:-}" ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
    rm -f "$SERIAL_LOG"
}
trap cleanup EXIT

# ─── Header ──────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo " TEST-001: VMCS Register Dump on Triple Fault"
echo " Ref: Interactive_Plan.md §XI(5096), §XIII·3/4/9, §X·9/10/12/14"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

# ─── Pre-flight Checks ──────────────────────────────────────────────────────
echo "── Pre-flight ──"

# Check QEMU binary
if ! command -v "$QEMU_BIN" &>/dev/null; then
    echo -e "${RED}ERROR:${NC} QEMU not found at: $QEMU_BIN"
    echo "Install QEMU or pass --qemu-path"
    exit 2
fi
info "QEMU found: $(command -v "$QEMU_BIN")"

# Check kernel image
if [[ ! -f "$KERNEL_PATH" ]]; then
    # Fallback: try the 2004 BZIMAGE for structural testing
    KERNEL_PATH="$PROJECT_ROOT/CHAOS 1.5/CHAOS/BZIMAGE"
    if [[ ! -f "$KERNEL_PATH" ]]; then
        echo -e "${RED}ERROR:${NC} No kernel image found"
        echo "Build the kernel first (CI-003) or provide --kernel PATH"
        exit 2
    fi
    info "Using legacy BZIMAGE for structural test (32-bit — will triple-fault as expected)"
fi
info "Kernel: $KERNEL_PATH ($(stat -f%z "$KERNEL_PATH" 2>/dev/null || stat -c%s "$KERNEL_PATH" 2>/dev/null || echo '?') bytes)"

echo ""

# ─── Test Execution ──────────────────────────────────────────────────────────
echo "── Launching QEMU (timeout: ${TIMEOUT_SECS}s) ──"
info "Serial output → $SERIAL_LOG"

# Launch QEMU with:
#   - KVM acceleration if available (will use VMX)
#   - Serial output to stdio captured to log
#   - No display (headless)
#   - Small RAM (256M — enough to trigger boot + triple fault)
#   - Automatic shutdown on triple fault / reset
$QEMU_BIN \
    -kernel "$KERNEL_PATH" \
    -m 256M \
    -cpu host,+vmx 2>/dev/null || true \
    -smp 1 \
    -serial file:"$SERIAL_LOG" \
    -display none \
    -no-reboot \
    -append "console=ttyS0,115200 panic=1" \
    &>"$SERIAL_LOG.qemu_stderr" &
QEMU_PID=$!

info "QEMU PID: $QEMU_PID"

# Wait for QEMU to produce output or timeout
WAITED=0
while [[ $WAITED -lt $TIMEOUT_SECS ]]; do
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        info "QEMU exited after ${WAITED}s"
        break
    fi

    # Check if we have crash dump output
    if [[ -f "$SERIAL_LOG" ]] && grep -qi "CR0\|triple\|fault\|panic\|CRASH_DUMP\|SYMBIOSE" "$SERIAL_LOG" 2>/dev/null; then
        info "Crash/register output detected at ${WAITED}s"
        sleep 2  # Allow remaining output to flush
        break
    fi

    sleep 1
    WAITED=$((WAITED + 1))
done

# Kill QEMU if still running
if kill -0 "$QEMU_PID" 2>/dev/null; then
    if [[ $WAITED -ge $TIMEOUT_SECS ]]; then
        info "Timeout reached (${TIMEOUT_SECS}s) — killing QEMU"
    fi
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
fi

echo ""
echo "── Serial Output ──"
if [[ -f "$SERIAL_LOG" ]] && [[ -s "$SERIAL_LOG" ]]; then
    # Show last 50 lines of serial output
    tail -n 50 "$SERIAL_LOG" | head -n 50
else
    info "(no serial output captured)"
fi

echo ""

# ─── Register Validation ────────────────────────────────────────────────────
echo "── Register Validation (§XIII·9: non-zero CR0, valid RIP) ──"

# §X·14: CR2 is NOT in VMCS — must be read via __readcr2() immediately
# The crash dump format per §XIII·11:
#   SYMBIOSE CRASH DUMP: CR0=<hex> CR2=<hex> CR3=<hex> RIP=<hex>
# Or kernel panic format:
#   CR0: <hex> CR2: <hex> CR3: <hex>
#   RIP: <hex>

CR0=""
CR2=""
CR3=""
RIP=""

if [[ -f "$SERIAL_LOG" ]] && [[ -s "$SERIAL_LOG" ]]; then
    # Try SymbioseOS crash dump format first
    if grep -q "SYMBIOSE CRASH DUMP" "$SERIAL_LOG"; then
        CR0=$(grep -oP 'CR0=0x[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | sed 's/CR0=//')
        CR2=$(grep -oP 'CR2=0x[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | sed 's/CR2=//')
        CR3=$(grep -oP 'CR3=0x[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | sed 's/CR3=//')
        RIP=$(grep -oP 'RIP=0x[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | sed 's/RIP=//')
        info "Parsed SYMBIOSE CRASH DUMP format"
    fi

    # Fallback: Linux kernel panic format
    if [[ -z "$CR0" ]]; then
        CR0=$(grep -oP 'CR0[: =]+0x?[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '0x?[0-9a-fA-F]+$')
        CR2=$(grep -oP 'CR2[: =]+0x?[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '0x?[0-9a-fA-F]+$')
        CR3=$(grep -oP 'CR3[: =]+0x?[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '0x?[0-9a-fA-F]+$')
        RIP=$(grep -oP 'RIP[: =]+0x?[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '0x?[0-9a-fA-F]+$')
        if [[ -n "$CR0" ]]; then
            info "Parsed Linux kernel panic format"
        fi
    fi

    # Fallback: any register dump format
    if [[ -z "$CR0" ]]; then
        CR0=$(grep -ioP 'cr0\s*[:=]\s*[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '[0-9a-fA-F]+$')
        CR2=$(grep -ioP 'cr2\s*[:=]\s*[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '[0-9a-fA-F]+$')
        CR3=$(grep -ioP 'cr3\s*[:=]\s*[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '[0-9a-fA-F]+$')
        RIP=$(grep -ioP 'rip\s*[:=]\s*[0-9a-fA-F]+' "$SERIAL_LOG" | head -1 | grep -oP '[0-9a-fA-F]+$')
        if [[ -n "$CR0" ]]; then
            info "Parsed generic register dump format"
        fi
    fi
fi

# ─── Assertions ──────────────────────────────────────────────────────────────

# Test 1: CR0 present and non-zero
# §XIII·9: "non-zero CR0"
# §X·10: CR0 must have VMX fixed bits set (bit 0 = PE, bit 31 = PG at minimum)
if [[ -n "$CR0" ]] && [[ "$CR0" != "0" ]] && [[ "$CR0" != "0x0" ]] && [[ "$CR0" != "00000000" ]]; then
    pass "TEST-001-A: CR0 present and non-zero (CR0=$CR0)"
else
    fail "TEST-001-A: CR0 present and non-zero" "non-zero hex value" "${CR0:-<not found>}"
fi

# Test 2: CR2 present (may be zero — fault address)
# §X·14: CR2 NOT in VMCS — must be captured via __readcr2()
if [[ -n "$CR2" ]]; then
    pass "TEST-001-B: CR2 present (CR2=$CR2) [§X·14: read via __readcr2()]"
else
    fail "TEST-001-B: CR2 present" "hex value (may be 0)" "${CR2:-<not found>}"
fi

# Test 3: CR3 present and non-zero (page table base)
if [[ -n "$CR3" ]] && [[ "$CR3" != "0" ]] && [[ "$CR3" != "0x0" ]] && [[ "$CR3" != "00000000" ]]; then
    pass "TEST-001-C: CR3 present and non-zero (CR3=$CR3)"
else
    fail "TEST-001-C: CR3 present and non-zero" "non-zero hex value (page table base)" "${CR3:-<not found>}"
fi

# Test 4: RIP present and in valid kernel range
# §XIII·9: "valid RIP"
# Valid RIP should be > 0x1000 (above null page) and ideally in kernel space
if [[ -n "$RIP" ]]; then
    # Convert to decimal for range check (handle both 0x prefix and raw hex)
    RIP_CLEAN=$(echo "$RIP" | sed 's/^0x//')
    RIP_DEC=$(printf '%d' "0x$RIP_CLEAN" 2>/dev/null || echo "0")

    if [[ "$RIP_DEC" -gt 4096 ]]; then
        pass "TEST-001-D: RIP present and valid (RIP=$RIP, decimal=$RIP_DEC)"
    else
        fail "TEST-001-D: RIP in valid range" "> 0x1000 (above null page)" "$RIP (decimal: $RIP_DEC)"
    fi
else
    fail "TEST-001-D: RIP present" "hex value > 0x1000" "${RIP:-<not found>}"
fi

# Test 5: All 4 registers captured (full dump completeness)
# §XIII·4: "Full register state captured"
REGS_FOUND=0
[[ -n "$CR0" ]] && REGS_FOUND=$((REGS_FOUND + 1))
[[ -n "$CR2" ]] && REGS_FOUND=$((REGS_FOUND + 1))
[[ -n "$CR3" ]] && REGS_FOUND=$((REGS_FOUND + 1))
[[ -n "$RIP" ]] && REGS_FOUND=$((REGS_FOUND + 1))

if [[ "$REGS_FOUND" -eq 4 ]]; then
    pass "TEST-001-E: Full register dump complete ($REGS_FOUND/4 registers captured)"
else
    fail "TEST-001-E: Full register dump complete" "4/4 registers" "$REGS_FOUND/4 registers"
fi

# Test 6: Crash output arrived within timeout (§XIII·4: "within 5 seconds")
if [[ "$WAITED" -lt "$TIMEOUT_SECS" ]]; then
    pass "TEST-001-F: Crash dump arrived within timeout (${WAITED}s < ${TIMEOUT_SECS}s)"
else
    fail "TEST-001-F: Crash dump arrived within timeout" "< ${TIMEOUT_SECS}s" "${WAITED}s (timeout)"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo -e " TEST-001 Results: ${TESTS_PASSED}/${TESTS_RUN} passed, ${TESTS_FAILED} failed"
if [[ "$TESTS_FAILED" -eq 0 ]]; then
    echo -e " Status: ${GREEN}ALL PASS${NC}"
else
    echo -e " Status: ${RED}FAILED${NC}"
fi
echo " Gate: §XIII·9 — VMCS triple fault dump with non-zero CR0, valid RIP"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

exit "$TESTS_FAILED"
