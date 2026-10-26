cmake_minimum_required(VERSION 3.24)
project(unikern CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Core library: KML / HCS / solver / backends / runtime
add_library(unikern_core STATIC
  src/kml.cpp
  src/hcs.cpp
  src/solver.cpp
  src/backends.cpp
  src/codegen_hip.cpp
  src/codegen_other.cpp
  src/runner.cpp
  src/runtime.cpp
)
target_include_directories(unikern_core PUBLIC include)

# CLI entry
add_executable(unikern src/main.cpp)
target_link_libraries(unikern PRIVATE unikern_core)

# Smoke test
enable_testing()
add_executable(unikern_smoke tests/smoke.cpp)
target_link_libraries(unikern_smoke PRIVATE unikern_core)
add_test(NAME smoke COMMAND unikern_smoke)

# Root-cause fix: host has multiple MinGW runtimes (ProgramData g++ 13.2 vs
# Git-bundled /mingw64 DLLs). Two libwinpthread copies in one process crashed
# ifstream paths at -O2/-O3. Link the C++ runtime statically.
if(MINGW)
  target_link_options(unikern PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_options(unikern_smoke PRIVATE -static -static-libgcc -static-libstdc++)
endif()
