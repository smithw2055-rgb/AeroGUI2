cmake_minimum_required(VERSION 3.20)

project(AeroGUI2 VERSION 2.0.0 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AERO_BUILD_COMPILER_TOOLS "Build host XAML+RealScript compiler & desktop compiler test suites" ON)
option(AERO_BUILD_TESTS "Build AeroGUI conformance and slim-runtime test suites" ON)

# Include RealScript from third-party/RealScript
add_subdirectory(third-party/RealScript)

if(REALSCRIPT_SLIM_RUNTIME)
    set(AERO_BUILD_COMPILER_TOOLS OFF CACHE BOOL "Compiler tools require full RealScript frontend" FORCE)
endif()

# ============================================================================
# 1. Pure Runtime Library (Zero XML Parser, Zero Compiler Frontend, Wasm-Ready)
# ============================================================================
set(AERO_RUNTIME_SOURCES
    src/core/UIElement.cpp
    src/core/AxbFormat.cpp
    src/bridge/ScriptUiBridge.cpp
    src/layout/DepthBucketLayoutQueue.cpp
    src/binding/DualTrackBindingEngine.cpp
    src/controls/Controls.cpp
    src/render/PagedRenderTree.cpp
    src/render/ZeroCopyGpuStream.cpp
    src/host/HostDrivers.cpp
)

add_library(aerogui_runtime STATIC
    ${AERO_RUNTIME_SOURCES}
)

if(MSVC)
    target_compile_options(aerogui_runtime PUBLIC /utf-8)
endif()

target_include_directories(aerogui_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(aerogui_runtime
    PUBLIC
        realscript_slim_runtime
)

add_library(AeroGUI::Runtime ALIAS aerogui_runtime)

# ============================================================================
# 2. Host Compiler Library & CLI (Only built when full RealScript frontend is active)
# ============================================================================
if(AERO_BUILD_COMPILER_TOOLS)
    add_library(aerogui_compiler_lib STATIC
        src/compiler/AeroCompiler.cpp
    )

    if(MSVC)
        target_compile_options(aerogui_compiler_lib PUBLIC /utf-8)
    endif()

    target_include_directories(aerogui_compiler_lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(aerogui_compiler_lib
        PUBLIC
            aerogui_runtime
            realscript_frontend
    )

    add_library(AeroGUI::Compiler ALIAS aerogui_compiler_lib)

    # Composite target preserving 100% compatibility with existing downstream consumers
    add_library(aerogui_core INTERFACE)
    target_link_libraries(aerogui_core
        INTERFACE
            aerogui_runtime
            aerogui_compiler_lib
    )

    add_executable(aero-compiler
        tools/aero_compiler/main.cpp
    )

    target_link_libraries(aero-compiler
        PRIVATE
            aerogui_compiler_lib
    )

    if(AERO_BUILD_TESTS)
        add_executable(aerogui_phase1_tests
            tests/phase1_foundation_tests.cpp
        )
        target_link_libraries(aerogui_phase1_tests PRIVATE aerogui_core)
        add_test(NAME aerogui_phase1_tests COMMAND aerogui_phase1_tests)

        add_executable(aerogui_phase2_tests
            tests/phase2_binding_compiler_layout_tests.cpp
        )
        target_link_libraries(aerogui_phase2_tests PRIVATE aerogui_core)
        add_test(NAME aerogui_phase2_tests COMMAND aerogui_phase2_tests)

        add_executable(aerogui_phase3_tests
            tests/phase3_render_host_aot_wasm_tests.cpp
        )
        target_link_libraries(aerogui_phase3_tests PRIVATE aerogui_core)
        add_test(NAME aerogui_phase3_tests COMMAND aerogui_phase3_tests)

        add_executable(aerogui_phase4_tests
            tests/phase4_runtime_render_mtsdf_tests.cpp
        )
        target_link_libraries(aerogui_phase4_tests PRIVATE aerogui_core)
        add_test(NAME aerogui_phase4_tests COMMAND aerogui_phase4_tests)

        add_executable(aerogui_phase5_tests
            tests/phase5_layout_transforms_tests.cpp
        )
        target_link_libraries(aerogui_phase5_tests PRIVATE aerogui_core)
        add_test(NAME aerogui_phase5_tests COMMAND aerogui_phase5_tests)
    endif()
else()
    # In Wasm / REALSCRIPT_SLIM_RUNTIME mode, aerogui_core aliases the pure runtime
    add_library(aerogui_core INTERFACE)
    target_link_libraries(aerogui_core INTERFACE aerogui_runtime)
endif()

if(AERO_BUILD_TESTS)
    # Pure runtime smoke test compiling all runtime TUs + test with RTTI disabled
    # (avoids -frtti/-fno-rtti cross-TU ODR mixing and verifies 0 dynamic_cast/typeid & 0 AeroCompiler.cpp dependency)
    add_executable(aerogui_slim_runtime_tests
        ${AERO_RUNTIME_SOURCES}
        tests/phase4_slim_runtime_smoke_tests.cpp
    )
    target_include_directories(aerogui_slim_runtime_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(aerogui_slim_runtime_tests
        PRIVATE
            realscript_slim_runtime
    )
    if(MSVC)
        target_compile_options(aerogui_slim_runtime_tests PRIVATE /utf-8 /GR-)
    else()
        target_compile_options(aerogui_slim_runtime_tests PRIVATE -fno-rtti)
    endif()
    add_test(NAME aerogui_slim_runtime_tests COMMAND aerogui_slim_runtime_tests)
endif()
