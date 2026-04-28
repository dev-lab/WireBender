# Copyright (c) 2026 Taras Greben
# SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial-WireBender
# See LICENSE file for details.

cmake_minimum_required(VERSION 3.16)
project(WireBender CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Path to adaptagrams checkout ─────────────────────────────────────────────
# Override with: cmake -DADAPTAGRAMS_DIR=/path/to/adaptagrams ..
set(ADAPTAGRAMS_DIR "${CMAKE_SOURCE_DIR}/../adaptagrams"
    CACHE PATH "Root of the adaptagrams repository checkout")

set(LIBAVOID_SRC_DIR "${ADAPTAGRAMS_DIR}/cola/libavoid")
set(LIBCOLA_SRC_DIR  "${ADAPTAGRAMS_DIR}/cola/libcola")
set(LIBVPSC_SRC_DIR  "${ADAPTAGRAMS_DIR}/cola/libvpsc")

# ── adaptagrams static libraries ─────────────────────────────────────────────

file(GLOB LIBAVOID_SOURCES "${LIBAVOID_SRC_DIR}/*.cpp")
add_library(avoid STATIC ${LIBAVOID_SOURCES})
target_include_directories(avoid PUBLIC "${ADAPTAGRAMS_DIR}/cola")

file(GLOB LIBVPSC_SOURCES "${LIBVPSC_SRC_DIR}/*.cpp")
add_library(vpsc STATIC ${LIBVPSC_SOURCES})
target_include_directories(vpsc PUBLIC "${ADAPTAGRAMS_DIR}/cola")

file(GLOB LIBCOLA_SOURCES "${LIBCOLA_SRC_DIR}/*.cpp")
add_library(cola STATIC ${LIBCOLA_SOURCES})
target_include_directories(cola PUBLIC "${ADAPTAGRAMS_DIR}/cola")
target_link_libraries(cola PUBLIC vpsc)

# ── WireBender source files ───────────────────────────────────────────────────
set(WIREBENDER_SOURCES
    src/WireBender.cpp
    src/PcbVisualizer.cpp
)

# ── Build type default ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Target Configuration (WASM vs Native) ────────────────────────────────────
if(EMSCRIPTEN)
    # =========================================================================
    # WEB BUILD (WASM)
    # Triggered automatically when using `emcmake cmake ..`
    # =========================================================================

    add_executable(WireBender_WASM
        ${WIREBENDER_SOURCES}
        src/wasm_bindings.cpp
    )

    target_include_directories(WireBender_WASM PRIVATE src)
    target_link_libraries(WireBender_WASM PRIVATE avoid cola vpsc)

    # ── WASM build modes ─────────────────────────────────────────────────────
    # Default: release build with a readable JS glue file.
    # The WASM binary is always fully optimised (-O3); only the JS wrapper
    # formatting is affected by these flags.
    #
    # cmake -DWASM_DEBUG=ON ..   → unminified JS + source maps (development)
    # cmake -DWASM_DEBUG=OFF ..  → unminified JS + optimised WASM (default)
    option(WASM_DEBUG "Build WASM with debug info and source maps" OFF)
    # --pretty-print keeps the generated JS glue human-readable (newlines,
    # indentation) without affecting WASM binary size or runtime performance.
    # --closure 0 disables the Closure Compiler so identifier names in the
    # glue stay as emscripten emitted them — easier to read and debug.
    # --pre-js prepends js_preamble.js verbatim before the module factory,
    # which is where JSDoc lives so IDEs can index it.
    # Optimisation: apply -O3 explicitly at compile time so it takes effect
    # regardless of CMAKE_BUILD_TYPE.  LINK_FLAGS alone only affects LTO/linking.
    # Exception support: required because libvpsc throws std::runtime_error.

    if(WASM_DEBUG)
        set(WASM_OPT_FLAGS "-O0 -g")
        set(WASM_COMPILE_FLAGS -O0 -g3 -fexceptions)
    else()
        set(WASM_OPT_FLAGS "-O3")
        set(WASM_COMPILE_FLAGS -O3 -fexceptions -DNDEBUG) # HINT: add WD_DEBUG to have debug messages
    endif()

    target_compile_options(WireBender_WASM PRIVATE ${WASM_COMPILE_FLAGS})
    target_compile_definitions(WireBender_WASM PRIVATE WIREBENDER_LIBRARY)
    target_compile_options(avoid PRIVATE ${WASM_COMPILE_FLAGS})
    target_compile_options(vpsc  PRIVATE ${WASM_COMPILE_FLAGS})
    target_compile_options(cola  PRIVATE ${WASM_COMPILE_FLAGS})

    set_target_properties(WireBender_WASM PROPERTIES
        OUTPUT_NAME "WireBender"
        LINK_FLAGS  "${WASM_OPT_FLAGS} \
                     -fexceptions \
                     -s DISABLE_EXCEPTION_CATCHING=0 \
                     -lembind \
                     -s MODULARIZE=1 \
                     -s EXPORT_NAME=WireBenderModule \
                     -s ALLOW_MEMORY_GROWTH=1 \
                     -s EXPORT_ES6=1 \
                     --pretty-print \
                     --closure 0 \
                     --pre-js ${CMAKE_SOURCE_DIR}/src/js_preamble.js"
    )
    # make Node.js happy to import ESM
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/package.json" "{\n  \"type\": \"module\"\n}\n")
else()
    # =========================================================================
    # NATIVE BUILD (Static Library)
    # =========================================================================

    add_library(WireBender STATIC ${WIREBENDER_SOURCES})
    target_include_directories(WireBender PUBLIC src)
    target_link_libraries(WireBender PRIVATE avoid cola vpsc)
    target_compile_definitions(WireBender PRIVATE WIREBENDER_LIBRARY)

    # ── AddressSanitizer — enable with: cmake -DASAN=ON .. ───────────────────
    option(ASAN "Build with AddressSanitizer" OFF)
    if(ASAN)
        set(ASAN_FLAGS -fsanitize=address -fno-omit-frame-pointer -g)
        target_compile_options(avoid      PRIVATE ${ASAN_FLAGS})
        target_compile_options(vpsc       PRIVATE ${ASAN_FLAGS})
        target_compile_options(cola       PRIVATE ${ASAN_FLAGS})
        target_compile_options(WireBender PRIVATE ${ASAN_FLAGS})
    endif()

endif()
