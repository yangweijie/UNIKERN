cmake_minimum_required(VERSION 3.24)
project(unikern C CXX)  # C: SQLite. OBJCXX (Metal runner) is Apple-only and is
# enabled via enable_language() in the APPLE branch below — MinGW has no
# Objective-C++ compiler, and an unconditional OBJCXX in project() breaks
# Windows configuration outright (E17).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# GCC-style warning flags are invalid for cl.exe — translate per compiler.
# /utf-8: sources carry UTF-8 comments (C4819 under the default CP936
# console codepage); _CRT_SECURE_NO_WARNINGS: MSVC's fopen/getenv deprecation
# noise for standard C APIs used throughout.
if(MSVC)
  add_compile_options(/W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

# R7 (Phase 6): real SQLite from the amalgamation (3.47.2), compiled as a
# static library. Extension loading is omitted — the ledger never dlopens.
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION)
# Third-party code: silence the global warnings inside it (compiler-aware:
# -w is GCC syntax, /w is MSVC).
if(MSVC)
  target_compile_options(sqlite3 PRIVATE /w)
else()
  target_compile_options(sqlite3 PRIVATE -w)
endif()

# Phase 10 (G4 Apple half): Metal runner. The Metal.framework runtime
# provides its own JIT compiler (newLibraryWithSource), so no Xcode or
# metal-cpp headers are needed — just the SDK's Metal.framework. On
# non-macOS platforms UK_HAS_METAL stays undefined and the runner
# compiles to an honest stub.
if(APPLE)
  enable_language(OBJCXX)  # Metal runner is Objective-C++; MinGW cannot build it
  find_library(METAL_FRAMEWORK Metal)
  if(METAL_FRAMEWORK)
    message(STATUS "Metal framework found: ${METAL_FRAMEWORK}")
    set(UK_HAS_METAL TRUE)
  else()
    message(STATUS "Metal framework NOT found: apple:metal runner compiles to honest stub")
    set(UK_HAS_METAL FALSE)
  endif()
else()
  set(UK_HAS_METAL FALSE)
endif()

# Phase 9 (G4 Intel half): OpenCL runner. The Khronos CL headers are vendored
# in third_party/CL (see its README); the import library comes from an Intel
# oneAPI install or a system OpenCL dev package. When no import library is
# found, src/runner_ocl.cpp compiles to an honest stub (UK_HAS_OCL undefined)
# reporting the gap — the build never silently degrades (R10).
# Only enable on Windows (Intel oneAPI); macOS has its own OpenCL.framework
# but the vendored headers target the Windows/Intel toolchain.
set(UK_HAS_OCL FALSE)
if(WIN32)
  find_path(OPENCL_INCLUDE_DIR CL/cl.h PATHS third_party NO_DEFAULT_PATH)
  find_library(OPENCL_LIBRARY
  NAMES OpenCL
  PATHS
    "C:/Program Files (x86)/Intel/oneAPI/2026.1/lib"
    "C:/Program Files (x86)/Intel/oneAPI/2025.3/lib"
    "C:/Program Files/Intel/oneAPI/2026.1/lib"
    "C:/Program Files/Intel/oneAPI/2025.3/lib"
  NO_DEFAULT_PATH)
if(NOT OPENCL_LIBRARY)
  find_library(OPENCL_LIBRARY NAMES OpenCL)
endif()
if(OPENCL_INCLUDE_DIR AND OPENCL_LIBRARY)
  message(STATUS "OpenCL found: ${OPENCL_LIBRARY} (include: ${OPENCL_INCLUDE_DIR})")
  set(UK_HAS_OCL TRUE)
else()
  message(STATUS "OpenCL NOT found: intel:ocl runner compiles to honest stub")
  set(UK_HAS_OCL FALSE)
endif()
endif()  # WIN32

# Core library: KML / HCS / solver / backends / runner / ledgers
set(UK_CORE_SRCS
  src/json_flat.cpp
  src/kml.cpp
  src/kml_front.cpp
  src/reference.cpp
  src/hcs.cpp
  src/solver.cpp
  src/backends.cpp
  src/codegen_hip.cpp
  src/codegen_other.cpp
  src/runner.cpp
  src/runner_ocl.cpp
  src/runtime.cpp
  src/sqlite_ledger.cpp
)
if(APPLE)
  list(APPEND UK_CORE_SRCS src/runner_metal.mm)  # Objective-C++: Apple only
else()
  list(APPEND UK_CORE_SRCS src/runner_metal_stub.cpp)  # honest gap (E17)
endif()
add_library(unikern_core STATIC ${UK_CORE_SRCS})
target_include_directories(unikern_core PUBLIC include)
target_link_libraries(unikern_core PRIVATE sqlite3)
if(UK_HAS_OCL)
  target_include_directories(unikern_core PRIVATE ${OPENCL_INCLUDE_DIR})
  target_compile_definitions(unikern_core PRIVATE UK_HAS_OCL)
  target_link_libraries(unikern_core PRIVATE ${OPENCL_LIBRARY})
endif()
if(UK_HAS_METAL)
  target_compile_definitions(unikern_core PRIVATE UK_HAS_METAL)
  find_library(FOUNDATION_FRAMEWORK Foundation)
  target_link_libraries(unikern_core PUBLIC ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
  target_compile_options(unikern_core PRIVATE -fobjc-arc)
endif()

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

# Phase 8 acceptance test: the R1-R10 CPU-testable matrix in one binary
# (real-device items live in docs/acceptance-mvp.md, not here).
add_executable(unikern_acceptance tests/acceptance.cpp)
target_link_libraries(unikern_acceptance PRIVATE unikern_core)
# The R7.3 gate arms shell out to the real CLI, so it must exist by test time.
add_dependencies(unikern_acceptance unikern)
# Same as smoke: the crash-tx child opens SQLite directly.
target_include_directories(unikern_acceptance PRIVATE third_party/sqlite)
# R10 mirror: the acceptance test includes the vendored CL headers (declarations
# only — it resolves the ICD loader via Win32 GetProcAddress, never links it).
target_include_directories(unikern_acceptance PRIVATE third_party)
add_test(NAME acceptance COMMAND unikern_acceptance)
# [R6.8-dtype] reads the two runner sources it cannot compile on this host (the OpenCL
# runner is an #ifdef stub without an Intel SDK, runner_metal.mm is Apple-only) to prove
# they hold one tolerance table, not a second one. Same env the anchors gate uses.
set_tests_properties(acceptance PROPERTIES ENVIRONMENT "SOURCE_DIR=${CMAKE_SOURCE_DIR}")

# Doc->code anchor check (better-harness finding doc-code-anchors-drift-unvalidated):
# every `path:line` citation in docs/, AGENTS.md and the planning files must still
# name code that lives there. Line anchors otherwise rot silently — three of four
# spot-checked ones had already drifted when this gate was added, and one had
# drifted twice in the same day because the files were being edited around it.
# Skips honestly when no interpreter is found (the test is then absent, not green).
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME anchors
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/probe/anchors_check.py
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(anchors PROPERTIES ENVIRONMENT "SOURCE_DIR=${CMAKE_SOURCE_DIR}")

  # Probe hygiene gate (better-harness finding #5): every probe that rewrites src/ must restore
  # under a guard, because a crash between patch and restore leaves the working tree sabotaged
  # and the next build quietly wrong. Registered next to `anchors` for the same reason: the rule
  # was prose in AGENTS.md, and prose does not fail a build.
  add_test(NAME probe_hygiene
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/probe/probe_hygiene_check.py
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Record-integrity gate (harness finding #4): every file path cited by the planning docs or an
  # evidence README must exist, and a declared workload input must not live inside a git-ignored
  # tree that a clone cannot carry. A rotting citation turns a "reproduce" command into a lie.
  add_test(NAME record_audit
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/probe/record_audit.py
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Excluded-TU syntax gate (Phase 45, D11): src/runner_ocl.cpp is #ifdef'd out of this host's
  # build when no Intel SDK is found, so edits there compile as an empty file and ctest stays
  # green — that is where a second, hard-coded tolerance table had been living. The mirrored
  # CL headers are enough for -fsyntax-only, so the body gets a real compile check without an
  # Intel toolchain, a device or a link step. It carries its own control (a broken TU must fail).
  add_test(NAME runner_tu
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/probe/runner_tu_check.py
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # Generated-program syntax gate (Phase 50, widened in Phase 52): the acceptance arms read generated
  # source as TEXT and
  # gate #6 covers the runner TUs, so until now nothing ever asked hipcc to parse an emitted
  # program. Phase 49's control P3 proved the hole is real — an artifact that cannot compile while
  # every text arm stays green. This gate emits gemm/conv/reduce through the CLI and runs
  # `hipcc -fsyntax-only` with a device-reported --offload-arch; it carries its own broken-source
  # control and exits 77 (ctest Skipped, not Passed) on a host without hipcc.
  add_test(NAME generated_programs
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/probe/generated_program_check.py
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(generated_programs PROPERTIES SKIP_RETURN_CODE 77)
else()
  message(STATUS "anchors_check: no Python3 interpreter found, gate not registered")
endif()

# Root-cause fix: host has multiple MinGW runtimes (ProgramData g++ 13.2 vs
# Git-bundled /mingw64 DLLs). Two libwinpthread copies in one process crashed
# ifstream paths at -O2/-O3. Link the C++ runtime statically.
if(MINGW)
  target_link_options(unikern PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_options(unikern_smoke PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_options(unikern_acceptance PRIVATE -static -static-libgcc -static-libstdc++)
endif()
