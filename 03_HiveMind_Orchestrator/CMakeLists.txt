# 03_HiveMind_Orchestrator/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(HiveMindOrchestrator)

add_executable(ChaosLoader ChaosLoader/whpx_boot.c)
add_executable(symbiose_ircd IRCd_Neural_Bus/jumbo_payload.c)
add_executable(vfs_manager VFS_Storage_Manager/vfs_manager.c)

add_executable(openmosix_nx_migrate openmosix_nx/migrate.c)
add_executable(criugpu_daemon openmosix_nx/criugpu_daemon.c)

# We do not compile the .bpf.c file directly with GCC/MinGW here, as eBPF requires clang/LLVM.
# In a full CI, it would be compiled via clang -target bpf ...
