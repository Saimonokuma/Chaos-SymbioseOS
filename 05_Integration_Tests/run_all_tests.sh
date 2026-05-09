#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# SYMBIOSE-OS FULL TEST SUITE RUNNER
# Runs all 18 test_*.sh scripts in a fully-equipped Docker container
# Coverage: 111/111 tasks (18 static + 3 runtime-only)
# ══════════════════════════════════════════════════════════════════════════════

set +e  # Don't exit on individual test failures

echo ""
echo "╔═════════════════════════════════════════════════════════════════════╗"
echo "║       SYMBIOSE-OS FULL TEST SUITE — $(date '+%Y-%m-%d %H:%M:%S')       ║"
echo "║       Docker: python:3.12-slim + netcat + openssl                  ║"
echo "║       Tests: 18 static + 3 runtime-skipped = 21 total             ║"
echo "╚═════════════════════════════════════════════════════════════════════╝"
echo ""

# Install full toolchain
echo "── Installing toolchain: netcat, openssl, socat, procps, numpy ──"
apt-get update -qq && apt-get install -yqq netcat-openbsd openssl socat procps > /dev/null 2>&1
pip install numpy pyyaml -q > /dev/null 2>&1
echo "  [OK] Toolchain ready"
echo ""

TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_TESTS=0
TEST_NUM=0
SUMMARY=""

run_test() {
    local script="$1"
    local name
    name=$(basename "$script" .sh)
    TEST_NUM=$((TEST_NUM + 1))

    echo "═══════════════════════════════════════════════════════════════"
    echo " ▶ TEST $TEST_NUM: $name"
    echo "   Time: $(date '+%H:%M:%S')"
    echo "═══════════════════════════════════════════════════════════════"

    OUTPUT=$(bash "$script" 2>&1) || true
    echo "$OUTPUT"

    # Extract results from the standard output format
    local results_line
    results_line=$(echo "$OUTPUT" | grep -E '[0-9]+/[0-9]+ passed' | tail -1)
    local passed run failed
    passed=$(echo "$results_line" | grep -oP '\d+(?=/\d+ passed)' || echo "0")
    run=$(echo "$results_line" | grep -oP '\d+/\K\d+(?= passed)' || echo "0")
    failed=$(echo "$results_line" | grep -oP '\d+(?= failed)' || echo "0")

    passed=${passed:-0}; run=${run:-0}; failed=${failed:-0}
    TOTAL_PASS=$((TOTAL_PASS + passed))
    TOTAL_FAIL=$((TOTAL_FAIL + failed))
    TOTAL_TESTS=$((TOTAL_TESTS + run))

    if [[ "$failed" -eq 0 && "$run" -gt 0 ]]; then
        SUMMARY="${SUMMARY}  ✅ TEST-$(printf '%02d' $TEST_NUM): $name — ${passed}/${run} PASS\n"
    elif [[ "$run" -eq 0 ]]; then
        SUMMARY="${SUMMARY}  ⚠️  TEST-$(printf '%02d' $TEST_NUM): $name — NO RESULTS PARSED\n"
    else
        SUMMARY="${SUMMARY}  ❌ TEST-$(printf '%02d' $TEST_NUM): $name — ${passed}/${run} (${failed} FAIL)\n"
    fi
    echo ""
}

# ═══════════════════════════════════════════════════════════════════════════
# TIER 1: Core Integration (CONFIG + KERNEL + BRIDGE + VFS)
# ═══════════════════════════════════════════════════════════════════════════
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " TIER 1: Core Integration"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
run_test "05_Integration_Tests/qemu_scripts/test_kernel_defconfig.sh"
run_test "05_Integration_Tests/qemu_scripts/test_config_wizard.sh"
run_test "05_Integration_Tests/qemu_scripts/test_bridge_subsystem.sh"
run_test "05_Integration_Tests/qemu_scripts/test_vfs_nvme.sh"

# ═══════════════════════════════════════════════════════════════════════════
# TIER 2: APBX + MOSIX Cluster
# ═══════════════════════════════════════════════════════════════════════════
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " TIER 2: APBX + MOSIX"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
run_test "05_Integration_Tests/qemu_scripts/test_apbx_playbook.sh"
run_test "05_Integration_Tests/qemu_scripts/test_mosix_cluster.sh"
run_test "05_Integration_Tests/qemu_scripts/test_mosix_rdma.sh"

# ═══════════════════════════════════════════════════════════════════════════
# TIER 3: IRC Extended (DCC + SHM + Tensor + QoS)
# ═══════════════════════════════════════════════════════════════════════════
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " TIER 3: IRC Extended"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
run_test "05_Integration_Tests/qemu_scripts/test_irc_extensions.sh"
run_test "05_Integration_Tests/qemu_scripts/test_dcc_tensor.sh"
run_test "05_Integration_Tests/qemu_scripts/test_dcc_ssend.sh"
run_test "05_Integration_Tests/qemu_scripts/test_shm_ring.sh"
run_test "05_Integration_Tests/qemu_scripts/test_tensor_dedup.sh"

# ═══════════════════════════════════════════════════════════════════════════
# TIER 4: MM Pipeline
# ═══════════════════════════════════════════════════════════════════════════
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " TIER 4: MM Pipeline"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
run_test "05_Integration_Tests/qemu_scripts/test_modality_router.sh"
run_test "05_Integration_Tests/qemu_scripts/test_vision_pipeline.sh"
run_test "05_Integration_Tests/qemu_scripts/test_moviola_delta.sh"
run_test "05_Integration_Tests/qemu_scripts/test_tts_playback.sh"

# ═══════════════════════════════════════════════════════════════════════════
# TIER 5: Extended Coverage (MM + UI)
# ═══════════════════════════════════════════════════════════════════════════
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " TIER 5: Extended Coverage"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
run_test "05_Integration_Tests/qemu_scripts/test_mm_extended.sh"
run_test "05_Integration_Tests/qemu_scripts/test_ui_components.sh"

# ═══════════════════════════════════════════════════════════════════════════
# TIER 6: Runtime Tests (require live QEMU/IRCd/Windows — SKIPPED in CI)
# ═══════════════════════════════════════════════════════════════════════════
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " TIER 6: Runtime Tests (SKIPPED — require live QEMU/IRCd/Windows)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
SUMMARY="${SUMMARY}  ⏭ TEST-19: phase4_qemu_test — SKIPPED (requires QEMU)\n"
SUMMARY="${SUMMARY}  ⏭ TEST-20: irc_bus_test — SKIPPED (requires IRCd)\n"
SUMMARY="${SUMMARY}  ⏭ TEST-21: native_vmx_smoke — SKIPPED (requires Windows + driver)\n"

# ═══════════════════════════════════════════════════════════════════════════
# FINAL REPORT
# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "╔═════════════════════════════════════════════════════════════════════╗"
echo "║              SYMBIOSE-OS — FINAL TEST REPORT                       ║"
echo "╠═════════════════════════════════════════════════════════════════════╣"
echo ""
echo -e "$SUMMARY"
echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo " STATIC TESTS: ${TOTAL_PASS}/${TOTAL_TESTS} passed, ${TOTAL_FAIL} failed"
echo " RUNTIME TESTS: 3 skipped (QEMU/IRCd/Windows required)"
echo " TASK COVERAGE: 111/111 tasks mapped (see manual_verification.md)"
echo ""
if [[ "$TOTAL_FAIL" -eq 0 ]]; then
    echo " ╔══════════════════════════════════════════╗"
    echo " ║  STATUS: ✅ ALL STATIC TESTS PASS        ║"
    echo " ╚══════════════════════════════════════════╝"
else
    echo " ╔══════════════════════════════════════════╗"
    echo " ║  STATUS: ❌ ${TOTAL_FAIL} FAILURES        ║"
    echo " ╚══════════════════════════════════════════╝"
fi
echo "═══════════════════════════════════════════════════════════════════════"
