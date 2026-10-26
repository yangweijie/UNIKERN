cmake_minimum_required(VERSION 3.24)
project(unikern C CXX)  # C is needed for the SQLite amalgamation

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# R7 (Phase 6): real SQLite from the amalgamation (3.47.2), compiled as a
# static library. Extension loading is omitted — the ledger never dlopens.
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION)
# Third-party code: silence the global -Wall/-Wextra inside it.
target_compile_options(sqlite3 PRIVATE -w)

# Core library: KML / HCS / solver / backends / runner / ledgers
add_library(unikern_core STATIC
  src/kml.cpp
  src/hcs.cpp
  src/solver.cpp
  src/backends.cpp
  src/codegen_hip.cpp
  src/codegen_other.cpp
  src/runner.cpp
  src/runtime.cpp
  src/sqlite_ledger.cpp
)
target_include_directories(unikern_core PUBLIC include)
target_link_libraries(unikern_core PRIVATE sqlite3)

# CLI entry
add_executable(unikern src/main.cpp)
target_link_libraries(unikern PRIVATE unikern_core)

# Smoke test
enable_testing()
add_executable(unikern_smoke tests/smoke.cpp)
target_link_libraries(unikern_smoke PRIVATE unikern_core)
# The smoke test's crash-tx child (R7.4 interrupt simulation) opens SQLite
# directly, so it needs the amalgamation header.
target_include_directories(unikern_smoke PRIVATE third_party/sqlite)
add_test(NAME smoke COMMAND unikern_smoke)

# Root-cause fix: host has multiple MinGW runtimes (ProgramData g++ 13.2 vs
# Git-bundled /mingw64 DLLs). Two libwinpthread copies in one process crashed
# ifstream paths at -O2/-O3. Link the C++ runtime statically.
if(MINGW)
  target_link_options(unikern PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_options(unikern_smoke PRIVATE -static -static-libgcc -static-libstdc++)
endif()
