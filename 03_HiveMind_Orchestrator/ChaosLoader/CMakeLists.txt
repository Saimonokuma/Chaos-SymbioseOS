cmake_minimum_required(VERSION 3.28)
project(ChaosLoader VERSION 0.1.0 LANGUAGES C)

# For testing only
add_executable(ChaosLoader src/main.c src/boot_params.c src/boot_params.h src/symbiose_ioctls.h)
