# Phase 1.2: Root CMakeLists.txt
# Crucible: PATTERN-012 (pinned dependencies)

cmake_minimum_required(VERSION 3.28)
project(Chaos-Symbiose-OS VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

# Pinned toolchain versions
set(WDK_VERSION "10.0.26100" CACHE STRING "Windows Driver Kit version")
set(GCC_MIN_VERSION "13.2")
set(NASM_MIN_VERSION "2.16.01")

# Sub-projects
add_subdirectory(02_Symbiose_Bridge)
add_subdirectory(03_HiveMind_Orchestrator/ChaosLoader)
add_subdirectory(03_HiveMind_Orchestrator/IRCd_Neural_Bus)
add_subdirectory(03_HiveMind_Orchestrator/VFS_Storage_Manager)

# Testing
enable_testing()
add_subdirectory(05_Integration_Tests)