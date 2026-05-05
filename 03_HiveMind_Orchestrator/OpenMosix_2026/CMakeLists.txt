cmake_minimum_required(VERSION 3.28)
project(openmosix_modern VERSION 0.1.0 LANGUAGES C)

# For testing only
add_library(openmosix_modern INTERFACE src/openmosix_tensor.h)
