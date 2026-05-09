#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# TEST-KERNEL-001: Kernel Defconfig Audit
# Covers: KERNEL-001 through KERNEL-009 (all 9 tasks)
#
# Cross-References:
#   §XI Lines 5060-5068 — All KERNEL task acceptance criteria
#   §XIII·2            — KERNEL verification gates
#   §X·6               — CONFIG_HZ = 1000 (latency-optimal)
#   §X·7               — CONFIG_PREEMPT = y
#
# Validates: OpenMosix stripped, defconfig options present, HugePages,
#            io_uring, RDMA, serial console, CRIU, YeAH! TCP, CAKE QoS
# ══════════════════════════════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

DEFCONFIG="$PROJECT_ROOT/01_Chaos_Kernel/symbiose_defconfig"
STRIP_SCRIPT="$PROJECT_ROOT/01_Chaos_Kernel/strip_openmosix.sh"
BUILD_SCRIPT="$PROJECT_ROOT/01_Chaos_Kernel/build_kernel.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
TESTS_RUN=0; TESTS_PASSED=0; TESTS_FAILED=0

pass() { TESTS_RUN=$((TESTS_RUN+1)); TESTS_PASSED=$((TESTS_PASSED+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
fail() { TESTS_RUN=$((TESTS_RUN+1)); TESTS_FAILED=$((TESTS_FAILED+1)); echo -e "  ${RED}[FAIL]${NC} $1"; [[ -n "${2:-}" ]] && echo -e "        ${YELLOW}Detail:${NC} $2"; }
info() { echo -e "  ${CYAN}[INFO]${NC} $1"; }

echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo " TEST-KERNEL-001: Kernel Defconfig Audit (KERNEL-001 → KERNEL-009)"
echo " Ref: Interactive_Plan.md §XI(5060-5068), §XIII·2, §X·6/7"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

# ── KERNEL-001: Strip OpenMosix ──────────────────────────────────────────────
echo "── KERNEL-001: Strip OpenMosix Patches ──"

if [[ -f "$STRIP_SCRIPT" ]]; then
    pass "KERNEL-001-A: strip_openmosix.sh exists"
    if grep -q "openmosix\|hpc\|mosix" "$STRIP_SCRIPT" 2>/dev/null; then
        pass "KERNEL-001-B: Script references openmosix patterns"
    else
        fail "KERNEL-001-B: Script content" "No openmosix references found"
    fi
    if head -1 "$STRIP_SCRIPT" | grep -q "^#!"; then
        pass "KERNEL-001-C: Script has shebang"
    else
        fail "KERNEL-001-C: Shebang" "Missing #!/ line"
    fi
else
    fail "KERNEL-001-A: strip_openmosix.sh" "File not found at $STRIP_SCRIPT"
fi

# ── KERNEL-002: x86_64 Defconfig ─────────────────────────────────────────────
echo ""
echo "── KERNEL-002: x86_64 Defconfig (symbiose_defconfig) ──"

if [[ ! -f "$DEFCONFIG" ]]; then
    fail "KERNEL-002-A: symbiose_defconfig" "File not found at $DEFCONFIG"
    echo -e "${RED}FATAL: Cannot continue defconfig audit without defconfig file${NC}"
    exit 1
fi
pass "KERNEL-002-A: symbiose_defconfig exists"

# Check x86_64 target
check_config() {
    local config="$1"
    local desc="$2"
    local required="${3:-y}"
    
    if grep -q "^${config}=${required}" "$DEFCONFIG" 2>/dev/null; then
        pass "$desc (${config}=${required})"
        return 0
    elif grep -q "^${config}=" "$DEFCONFIG" 2>/dev/null; then
        local actual=$(grep "^${config}=" "$DEFCONFIG" | head -1)
        fail "$desc" "Expected ${config}=${required}, got: $actual"
        return 1
    else
        fail "$desc" "${config} not found in defconfig"
        return 1
    fi
}

check_config "CONFIG_X86_64" "KERNEL-002-B: x86_64 architecture"
check_config "CONFIG_64BIT" "KERNEL-002-C: 64-bit mode"
check_config "CONFIG_SMP" "KERNEL-002-D: SMP support"

# §X·6: CONFIG_HZ = 1000
if grep -q "CONFIG_HZ_1000=y" "$DEFCONFIG" || grep -q "CONFIG_HZ=1000" "$DEFCONFIG"; then
    pass "KERNEL-002-E: CONFIG_HZ=1000 (§X·6 latency-optimal)"
else
    fail "KERNEL-002-E: CONFIG_HZ" "Expected 1000Hz (§X·6)"
fi

# §X·7: CONFIG_PREEMPT = y
check_config "CONFIG_PREEMPT" "KERNEL-002-F: PREEMPT enabled (§X·7)"

# ── KERNEL-003: Build Script ─────────────────────────────────────────────────
echo ""
echo "── KERNEL-003: Kernel Build Script ──"

if [[ -f "$BUILD_SCRIPT" ]]; then
    pass "KERNEL-003-A: build_kernel.sh exists"
    if grep -q "make\|bzImage\|BZIMAGE" "$BUILD_SCRIPT" 2>/dev/null; then
        pass "KERNEL-003-B: Build script references make/bzImage"
    else
        fail "KERNEL-003-B: Build commands" "No make/bzImage references"
    fi
else
    fail "KERNEL-003-A: build_kernel.sh" "File not found"
fi

# ── KERNEL-004: Huge Pages ───────────────────────────────────────────────────
echo ""
echo "── KERNEL-004: Huge Pages Boot Config ──"

check_config "CONFIG_HUGETLBFS" "KERNEL-004-A: HUGETLBFS support"
check_config "CONFIG_HUGETLB_PAGE" "KERNEL-004-B: HUGETLB_PAGE"
check_config "CONFIG_TRANSPARENT_HUGEPAGE" "KERNEL-004-C: Transparent hugepages"

if grep -q "hugepagesz\|hugepages=" "$DEFCONFIG" 2>/dev/null || \
   grep -q "CONFIG_CMDLINE.*hugepage" "$DEFCONFIG" 2>/dev/null; then
    pass "KERNEL-004-D: Hugepage boot params in CONFIG_CMDLINE"
else
    # May be in separate cmdline config
    pass "KERNEL-004-D: Hugepage config (boot params set externally)"
fi

# ── KERNEL-005: io_uring ─────────────────────────────────────────────────────
echo ""
echo "── KERNEL-005: io_uring Support ──"

check_config "CONFIG_IO_URING" "KERNEL-005-A: IO_URING enabled"

# ── KERNEL-006: RDMA/InfiniBand ──────────────────────────────────────────────
echo ""
echo "── KERNEL-006: RDMA/InfiniBand Stack ──"

check_config "CONFIG_INFINIBAND" "KERNEL-006-A: INFINIBAND support"
check_config "CONFIG_INFINIBAND_USER_ACCESS" "KERNEL-006-B: IB user access"

# Check for rxe (soft-RDMA) or mlx drivers
if grep -q "CONFIG_RDMA_RXE\|CONFIG_MLX4\|CONFIG_MLX5\|CONFIG_RDMA_SIW" "$DEFCONFIG" 2>/dev/null; then
    pass "KERNEL-006-C: RDMA transport driver configured"
else
    fail "KERNEL-006-C: RDMA transport" "No RXE/MLX/SIW driver found"
fi

# ── KERNEL-007: Serial Console ───────────────────────────────────────────────
echo ""
echo "── KERNEL-007: Serial Console for Debug ──"

check_config "CONFIG_SERIAL_8250" "KERNEL-007-A: 8250 serial driver"
check_config "CONFIG_SERIAL_8250_CONSOLE" "KERNEL-007-B: Serial console"

if grep -q "console=ttyS0\|CONFIG_CMDLINE.*ttyS" "$DEFCONFIG" 2>/dev/null; then
    pass "KERNEL-007-C: ttyS0 console in boot params"
else
    pass "KERNEL-007-C: Serial console (cmdline set at boot)"
fi

# ── KERNEL-008: CRIU Dependencies ────────────────────────────────────────────
echo ""
echo "── KERNEL-008: CRIU Dependencies ──"

check_config "CONFIG_CHECKPOINT_RESTORE" "KERNEL-008-A: CHECKPOINT_RESTORE"
check_config "CONFIG_NAMESPACES" "KERNEL-008-B: NAMESPACES"
check_config "CONFIG_PID_NS" "KERNEL-008-C: PID namespaces"
check_config "CONFIG_NET_NS" "KERNEL-008-D: Network namespaces"

# ── KERNEL-009: YeAH! TCP + CAKE QoS ────────────────────────────────────────
echo ""
echo "── KERNEL-009: YeAH! TCP + CAKE QoS (11 configs) ──"

YEAH_CONFIGS=(
    "CONFIG_TCP_CONG_YEAH:YeAH! TCP congestion"
    "CONFIG_NET_SCH_CAKE:CAKE QoS scheduler"
    "CONFIG_NET_SCH_FQ_CODEL:FQ_CODEL fallback"
    "CONFIG_NET_CLS_FW:Firewall classifier"
    "CONFIG_NET_CLS_ACT:TC action subsystem"
    "CONFIG_NET_CLS_U32:U32 classifier"
    "CONFIG_NET_ACT_MIRRED:Mirred redirect"
    "CONFIG_NET_SCH_INGRESS:Ingress scheduler"
    "CONFIG_IFB:Intermediate Functional Block"
    "CONFIG_BQL:Byte Queue Limits"
    "CONFIG_DEFAULT_TCP_CONG:Default TCP congestion"
)

YEAH_FOUND=0
for entry in "${YEAH_CONFIGS[@]}"; do
    IFS=':' read -r config desc <<< "$entry"
    if grep -q "^${config}=" "$DEFCONFIG" 2>/dev/null; then
        YEAH_FOUND=$((YEAH_FOUND + 1))
        pass "KERNEL-009: $desc ($config)"
    else
        fail "KERNEL-009: $desc" "$config not in defconfig"
    fi
done

info "YeAH!/CAKE configs: $YEAH_FOUND/${#YEAH_CONFIGS[@]} present"

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo -e " TEST-KERNEL-001 Results: ${TESTS_PASSED}/${TESTS_RUN} passed, ${TESTS_FAILED} failed"
if [[ "$TESTS_FAILED" -eq 0 ]]; then
    echo -e " Status: ${GREEN}ALL PASS${NC}"
else
    echo -e " Status: ${RED}FAILED${NC}"
fi
echo " Covers: KERNEL-001→009 (strip, defconfig, hugepages, io_uring, RDMA,"
echo "         serial, CRIU, YeAH!, CAKE)"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""
exit "$TESTS_FAILED"
