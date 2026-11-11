cmake_minimum_required(VERSION 3.20)

project(AeroGUI2 VERSION 2.0.0 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AERO_BUILD_COMPILER_TOOLS "Build host XAML+RealScript compiler & desktop compiler test suites" ON)
option(AERO_BUILD_TESTS "Build AeroGUI conformance and slim-runtime test suites" ON)

if(NOT EMSCRIPTEN)
    find_package(Threads QUIET)
endif()

if(REALSCRIPT_SLIM_RUNTIME OR EMSCRIPTEN)
    set(REALSCRIPT_INSTALL_SDK OFF CACHE BOOL "" FORCE)
    set(REALSCRIPT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

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
    src/core/UiResourceContext.cpp
    src/bridge/ScriptUiBridge.cpp
    src/layout/DepthBucketLayoutQueue.cpp
    src/binding/DualTrackBindingEngine.cpp
    src/controls/Controls.cpp
    src/input/InputEventRouter.cpp
    src/animation/AnimationEngine.cpp
    src/style/StyleTriggerEngine.cpp
    src/render/PagedRenderTree.cpp
    src/render/ZeroCopyGpuStream.cpp
    src/render/FontAtlas.cpp
    src/render/ImageAtlas.cpp
    src/render/GpuBackend.cpp
    src/host/HostDrivers.cpp
    src/host/NativeWindowHost.cpp
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

if(TARGET Threads::Threads)
    target_link_libraries(aerogui_runtime PUBLIC Threads::Threads)
endif()

if(WIN32)
    target_link_libraries(aerogui_runtime PUBLIC user32 gdi32 imm32)
endif()

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(aerogui_runtime PUBLIC ${CMAKE_DL_LIBS})
endif()

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

    # Composite targets (aerocore & aerogui_core) preserving 100% downstream compatibility
    add_library(aerocore INTERFACE)
    target_link_libraries(aerocore
        INTERFACE
            aerogui_runtime
            aerogui_compiler_lib
    )

    add_library(aerogui_core INTERFACE)
    target_link_libraries(aerogui_core INTERFACE aerocore)
    add_library(AeroGUI::Core ALIAS aerocore)

    add_executable(aero-compiler
        tools/aero_compiler/main.cpp
    )

    target_link_libraries(aero-compiler
        PRIVATE
            aerogui_compiler_lib
    )

    add_executable(aero-viewer
        tools/aero_viewer/main.cpp
    )

    target_link_libraries(aero-viewer
        PRIVATE
            aerocore
    )

    target_compile_definitions(aero-viewer
        PRIVATE
            AERO_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
    )

    if(AERO_BUILD_TESTS)
        # Helper function to register test targets with strict source existence verification
        function(aero_register_core_test target_name source_rel_path)
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_rel_path}")
                add_executable(${target_name}
                    "${source_rel_path}"
                )
                target_link_libraries(${target_name} PRIVATE aerocore)
                target_compile_definitions(${target_name}
                    PRIVATE
                        AERO_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
                        AERO_TEST_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_out"
                )
                add_test(
                    NAME ${target_name}
                    COMMAND ${target_name}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                )
            else()
                message(FATAL_ERROR "Missing test source: ${CMAKE_CURRENT_SOURCE_DIR}/${source_rel_path}")
            endif()
        endfunction()

        # Phase 1 - Phase 6 architectural foundation & motion suites
        aero_register_core_test(aerogui_phase1_tests  tests/phase1_foundation_tests.cpp)
        aero_register_core_test(aerogui_phase2_tests  tests/phase2_binding_compiler_layout_tests.cpp)
        aero_register_core_test(aerogui_phase3_tests  tests/phase3_render_host_aot_wasm_tests.cpp)
        aero_register_core_test(aerogui_phase4_tests  tests/phase4_runtime_render_mtsdf_tests.cpp)
        aero_register_core_test(aerogui_phase5_tests  tests/phase5_layout_transforms_tests.cpp)
        aero_register_core_test(aerogui_phase5b_tests tests/phase5b_input_controls_tests.cpp)
        aero_register_core_test(aerogui_phase5c_tests tests/phase5c_style_animation_tests.cpp)
        aero_register_core_test(aerogui_phase5d_tests tests/phase5d_datatrigger_template_tests.cpp)
        aero_register_core_test(aerogui_phase6_tests  tests/phase6_abi_motion_importers_tests.cpp)

        # P0 milestone suites (rendering fidelity, GPU/window host, TrueType SDF, image atlas)
        aero_register_core_test(p0_1_tests tests/p0_1_tests.cpp)
        aero_register_core_test(p0_2_tests tests/p0_2_tests.cpp)
        aero_register_core_test(p0_3_tests tests/p0_3_tests.cpp)
        aero_register_core_test(p0_4_tests tests/p0_4_tests.cpp)

        # P1 milestone suites (ControlTemplate, MergedDictionaries/Loc, Gradients/Shapes, MultiBinding/Behaviors, 3D/Effects/Rive)
        aero_register_core_test(p1_1_tests tests/p1_1_tests.cpp)
        aero_register_core_test(p1_2_tests tests/p1_2_tests.cpp)
        aero_register_core_test(p1_3_tests tests/p1_3_tests.cpp)
        aero_register_core_test(p1_4_tests tests/p1_4_tests.cpp)
        aero_register_core_test(p1_5_tests tests/p1_5_tests.cpp)

        # P2 milestone suites (demos, Wasm/slim packaging, Samples.zip full golden regression)
        aero_register_core_test(p2_1_tests tests/p2_1_tests.cpp)
        aero_register_core_test(p2_2_tests tests/p2_2_tests.cpp)
        aero_register_core_test(p2_3_tests tests/p2_3_tests.cpp)
    endif()
else()
    # In Wasm / REALSCRIPT_SLIM_RUNTIME mode, aerocore & aerogui_core alias the pure runtime
    add_library(aerocore INTERFACE)
    target_link_libraries(aerocore INTERFACE aerogui_runtime)

    add_library(aerogui_core INTERFACE)
    target_link_libraries(aerogui_core INTERFACE aerocore)
    add_library(AeroGUI::Core ALIAS aerocore)
endif()

if(AERO_BUILD_TESTS)
    # Pure runtime smoke test compiling all runtime TUs + test with RTTI disabled
    # (avoids -frtti/-fno-rtti cross-TU ODR mixing and verifies 0 dynamic_cast/typeid & 0 AeroCompiler.cpp dependency)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/phase4_slim_runtime_smoke_tests.cpp")
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
        target_compile_definitions(aerogui_slim_runtime_tests
            PRIVATE
                AERO_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
                AERO_TEST_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_out"
        )
        if(WIN32)
            target_link_libraries(aerogui_slim_runtime_tests PRIVATE user32 gdi32 imm32)
        endif()
        if(TARGET Threads::Threads)
            target_link_libraries(aerogui_slim_runtime_tests PRIVATE Threads::Threads)
        endif()
        if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
            target_link_libraries(aerogui_slim_runtime_tests PRIVATE ${CMAKE_DL_LIBS})
        endif()
        if(MSVC)
            target_compile_options(aerogui_slim_runtime_tests PRIVATE /utf-8 /GR-)
        else()
            target_compile_options(aerogui_slim_runtime_tests PRIVATE -fno-rtti)
        endif()
        add_test(
            NAME aerogui_slim_runtime_tests
            COMMAND aerogui_slim_runtime_tests
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    else()
        message(FATAL_ERROR "Missing test source: ${CMAKE_CURRENT_SOURCE_DIR}/tests/phase4_slim_runtime_smoke_tests.cpp")
    endif()
endif()
