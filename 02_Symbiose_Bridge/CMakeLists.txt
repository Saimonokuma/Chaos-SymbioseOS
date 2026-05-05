cmake_minimum_required(VERSION 3.28)
project(symbiose_bridge VERSION 0.1.0 LANGUAGES C)

# For testing only
add_executable(symbiose_bridge src/driver_entry.c src/acpi_handler.c src/nvme_isolation.c src/symbiose_bridge.h )
