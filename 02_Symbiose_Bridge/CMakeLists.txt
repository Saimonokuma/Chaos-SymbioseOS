# 02_Symbiose_Bridge/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(SymbioseBridge)

# Enable ASM language for SwitchToChaos.asm
enable_language(ASM_NASM)

# In a real WDF driver build, we would use the WDK. Here we just compile the objects
# to verify syntax and ensure they exist for the APBX packaging.

add_library(symbiose_bridge STATIC
    src/symbiose_bridge.c
    src/SwitchToChaos.asm
)
