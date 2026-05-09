# CMakeLists.txt — ChaosLoader.exe Build System
#
# Reference: Interactive_Plan.md §II·3 Job 2 (CI-002)
#
# Build:
#   mkdir build && cd build
#   cmake -G "MinGW Makefiles" ..   (or -G "Visual Studio 17 2022" -A x64)
#   cmake --build .
#
# Output: ChaosLoader.exe — Win32 x64 console application

cmake_minimum_required(VERSION 3.16)
project(ChaosLoader
    VERSION 3.0.0
    LANGUAGES C
    DESCRIPTION "SymbioseOS Ring-3 VM Boot Loader"
)

# ── Target architecture ──────────────────────────────────────────────────────
# ChaosLoader must be x64 to match the KMDF driver
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ChaosLoader must be built as a 64-bit application")
endif()

# ── Vendored dependencies ────────────────────────────────────────────────────
# cJSON is vendored as a single .c + .h pair
# Download from: https://github.com/DaveGamble/cJSON
# Place cJSON.c and cJSON.h in src/
add_library(cjson STATIC
    src/cJSON.c
)
target_include_directories(cjson PUBLIC src/)

# ── ChaosLoader executable ───────────────────────────────────────────────────
# NOTE: Multimodal pipeline files (multimodal.h, modality_router.c,
# vision_pipeline.c, tts_pipeline.c, video_temporal.c, moviola_delta.c,
# modality_hotswap.c, scout_modality.c, demhx_rdi.c, moviola_dibit.c,
# demhx_midi_grammar.c, moviola_dvs.c) are built as part of the GUEST LINUX
# hive_mind binary (CI-003 build-linux), NOT this Win32 target. They share
# this src/ directory for organizational convenience.
add_executable(ChaosLoader
    src/main.c
    src/kernel_ioctls.c
    src/boot_params.c
)

target_include_directories(ChaosLoader PRIVATE
    src/
    ../02_Symbiose_Bridge/inc/    # symbiose_ioctl.h (shared IOCTL header)
)

target_link_libraries(ChaosLoader PRIVATE
    cjson
    setupapi     # SetupDiGetClassDevs, SetupDiEnumDeviceInterfaces
)

# ── Compiler settings ────────────────────────────────────────────────────────
target_compile_definitions(ChaosLoader PRIVATE
    _WIN32_WINNT=0x0A00    # Windows 10+
    UNICODE
    _UNICODE
)

# Enable warnings
if(MSVC)
    target_compile_options(ChaosLoader PRIVATE /W4 /WX-)
else()
    # MinGW / GCC
    target_compile_options(ChaosLoader PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ── Install target ───────────────────────────────────────────────────────────
# Output goes to 04_APBX_Transmigration/playbook/Executables/
install(TARGETS ChaosLoader
    RUNTIME DESTINATION "${CMAKE_SOURCE_DIR}/../04_APBX_Transmigration/playbook/Executables"
)
