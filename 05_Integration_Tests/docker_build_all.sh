#!/bin/bash
# ══════════════════════════════════════════════════════════════════════════
# docker_build_all.sh — Build ALL SymbioseOS binaries in Docker
#
# Builds:
#   1. x86_64 Linux kernel (BZIMAGE) from kernel v6.12 + symbiose_defconfig
#   2. hive_mind (guest PID 1, musl static)
#   3. symbiose_ircd (guest, musl static)
#   4. ChaosLoader.exe (Win32 cross-compile, MinGW-w64)
#   5. symbiose_ircd.exe (Win32 cross-compile, MinGW-w64)
#   6. initrd.img (cpio+gzip)
#
# Usage (from PowerShell on Windows):
#   docker build -t symbiose-builder -f 05_Integration_Tests/Dockerfile.build .
#   docker run --name symbiose-build symbiose-builder
#   docker cp symbiose-build:/output/. ./build/
#   docker rm symbiose-build
# ══════════════════════════════════════════════════════════════════════════
echo "This script documents the build process."
echo "Use the Dockerfile.build instead."
