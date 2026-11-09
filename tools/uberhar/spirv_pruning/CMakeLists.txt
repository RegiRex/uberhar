# CodexAstraLocal: Build only the pinned SPIR-V emitter for the production
# pruning fixture; do not configure the emulator or fetch a different dependency.
cmake_minimum_required(VERSION 3.22)
project(UberharSpirvPruning LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(UBERHAR_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(UBERHAR_DEPENDENCY_ROOT "${UBERHAR_SOURCE_ROOT}" CACHE PATH "Pinned checkout dependencies")
add_library(SPIRV-Headers INTERFACE)
target_include_directories(SPIRV-Headers INTERFACE "${UBERHAR_DEPENDENCY_ROOT}/externals/spirv-headers/include")
add_library(SPIRV-Headers::SPIRV-Headers ALIAS SPIRV-Headers)
add_subdirectory("${UBERHAR_DEPENDENCY_ROOT}/externals/sirit/sirit/src" sirit)
