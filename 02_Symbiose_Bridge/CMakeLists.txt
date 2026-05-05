cmake_minimum_required(VERSION 3.28)
project(symbiose_bridge LANGUAGES C)

# Drop assembly on Linux test compilation
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  enable_language(ASM_NASM)
  add_library(symbiose_bridge MODULE src/driver_entry.c src/acpi_handler.c src/nvme_isolation.c src/SwitchToChaos.asm)
else()
  add_library(symbiose_bridge MODULE src/driver_entry.c src/acpi_handler.c src/nvme_isolation.c)
endif()

set_target_properties(symbiose_bridge PROPERTIES PREFIX "" SUFFIX ".sys")
