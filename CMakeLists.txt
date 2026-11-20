cmake_minimum_required(VERSION 3.20)

project(AeroGUI2 VERSION 3.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(EMSCRIPTEN)
    option(AERO_BUILD_COMPILER_TOOLS "Build host XAML+RealScript compiler & desktop compiler test suites" OFF)
else()
    option(AERO_BUILD_COMPILER_TOOLS "Build host XAML+RealScript compiler & desktop compiler test suites" ON)
endif()
option(AERO_BUILD_TESTS "Build AeroGUI conformance and slim-runtime test suites" ON)
option(AERO_INSTALL_SDK "Generate installable AeroGUI2 SDK targets" ON)

if(NOT EMSCRIPTEN)
    find_package(Threads QUIET)
endif()

if(EMSCRIPTEN)
    get_filename_component(_aero_default_realscript_build_dir
        "${CMAKE_CURRENT_SOURCE_DIR}/../RealScript/build-wasm" ABSOLUTE)
else()
    get_filename_component(_aero_default_realscript_build_dir
        "${CMAKE_CURRENT_SOURCE_DIR}/../RealScript/build" ABSOLUTE)
endif()
set(REALSCRIPT_BUILD_DIR "${_aero_default_realscript_build_dir}"
    CACHE PATH "Path to a RealScript build tree or installed SDK prefix containing RealScriptConfig.cmake")

if(MSVC AND EXISTS "${REALSCRIPT_BUILD_DIR}/Release")
    if(NOT EXISTS "${REALSCRIPT_BUILD_DIR}/RelWithDebInfo" AND NOT DEFINED CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO)
        set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
    endif()
    if(NOT EXISTS "${REALSCRIPT_BUILD_DIR}/MinSizeRel" AND NOT DEFINED CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL)
        set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
    endif()
endif()

if(NOT TARGET RealScript::SlimRuntime)
    find_package(RealScript CONFIG REQUIRED
        HINTS
            "${REALSCRIPT_BUILD_DIR}"
            "${_aero_default_realscript_build_dir}"
            "C:/Projects/RealScript/build"
            "C:/Projects/RealScript/build/sdk"
            "${CMAKE_CURRENT_SOURCE_DIR}/../RealScript/build"
            "${CMAKE_CURRENT_SOURCE_DIR}/../RealScript/build/sdk"
    )
endif()

if(NOT TARGET RealScript::Frontend)
    set(AERO_BUILD_COMPILER_TOOLS OFF CACHE BOOL "Compiler tools require full RealScript frontend" FORCE)
endif()

function(aero_apply_no_rtti_options target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8 /GR-)
    else()
        target_compile_options(${target_name} PRIVATE -fno-rtti)
    endif()
endfunction()

function(aero_apply_default_rtti_options target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8)
    endif()
endfunction()

if(MSVC)
    add_link_options(/STACK:8388608)
endif()

# ============================================================================
# 1. Foundation (drawing + core + runtime + asset) [NO RTTI, SlimRuntime]
# ============================================================================
add_library(aerogui_foundation STATIC
    src/core/AeroObject.cpp
    src/core/TypeClass.cpp
    src/core/UIResourceStore.cpp
    src/runtime/AeroExecutionDomains.cpp
    src/runtime/AeroObservableList.cpp
    src/asset/GradientBrush.cpp
    src/asset/MtsdfPathBaker.cpp
)
add_library(AeroGUI::Foundation ALIAS aerogui_foundation)
target_include_directories(aerogui_foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(aerogui_foundation
    PUBLIC
        RealScript::SlimRuntime
        $<$<BOOL:${Threads_FOUND}>:Threads::Threads>
)
aero_apply_no_rtti_options(aerogui_foundation)

# ============================================================================
# 2. Render (atlases + PagedRenderTree + rasterizer + GPU) [NO RTTI]
# ============================================================================
add_library(aerogui_render STATIC
    src/render/FontAtlas.cpp
    src/render/ImageAtlas.cpp
    src/render/RiveRenderer.cpp
    src/render/PagedRenderTree.cpp
    src/render/SoftwareRasterizer.cpp
    src/render/ZeroCopyGpuStream.cpp
    src/render/GpuBackend.cpp
)
add_library(AeroGUI::Render ALIAS aerogui_render)
target_link_libraries(aerogui_render
    PUBLIC
        aerogui_foundation
)
if(UNIX AND NOT EMSCRIPTEN AND CMAKE_DL_LIBS)
    target_link_libraries(aerogui_render PRIVATE ${CMAKE_DL_LIBS})
endif()
aero_apply_no_rtti_options(aerogui_render)

# ============================================================================
# 3. Visual Microkernel (VisualWorld + Drawing + Anim + Input + HostBindings)
# ============================================================================
add_library(aerogui_visual STATIC
    src/visual/VisualTree.cpp
    src/visual/VisualLayoutSolver.cpp
    src/visual/DrawingCommandBuffer.cpp
    src/visual/VisualAnimationEngine.cpp
    src/visual/VisualInputRouter.cpp
    src/visual/VisualRenderAdapter.cpp
    src/visual/VisualHostBindings.cpp
)
add_library(AeroGUI::Visual ALIAS aerogui_visual)
target_link_libraries(aerogui_visual
    PUBLIC
        aerogui_render
)
aero_apply_no_rtti_options(aerogui_visual)

# ============================================================================
# 4. Platform Host (HostDrivers + NativeWindowHost) [NO RTTI]
# ============================================================================
add_library(aerogui_host STATIC
    src/host/HostDrivers.cpp
    src/host/NativeWindowHost.cpp
)
add_library(AeroGUI::Host ALIAS aerogui_host)
target_link_libraries(aerogui_host
    PUBLIC
        aerogui_visual
)
if(WIN32)
    target_link_libraries(aerogui_host PRIVATE user32 gdi32 winmm mfplat mfreadwrite mfuuid ole32)
endif()
aero_apply_no_rtti_options(aerogui_host)

add_library(aerogui_runtime INTERFACE)
add_library(AeroGUI::Runtime ALIAS aerogui_runtime)
target_link_libraries(aerogui_runtime INTERFACE aerogui_host)

# ============================================================================
# 5. Ahead-of-Time / XAML Compiler (ZERO GUI/render/host/Win32 deps)
# ============================================================================
if(AERO_BUILD_COMPILER_TOOLS AND TARGET RealScript::Frontend)
    add_library(aerogui_compiler STATIC
        src/compiler/StdlibModulePack.cpp
        src/compiler/XmlPullLexer.cpp
        src/compiler/MarkupExtensionParser.cpp
        src/compiler/XamlToRsEmitter.cpp
        src/compiler/CSharpCompatLowerer.cpp
        src/compiler/MirReachabilityPruner.cpp
        src/compiler/AeroCompilerV3.cpp
    )
    add_library(AeroGUI::Compiler ALIAS aerogui_compiler)
    target_link_libraries(aerogui_compiler
        PUBLIC
            aerogui_foundation
            RealScript::Frontend
    )
    target_compile_definitions(aerogui_compiler
        PUBLIC
            $<BUILD_INTERFACE:AERO_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib">
            $<INSTALL_INTERFACE:AERO_STDLIB_DIR="$<INSTALL_PREFIX>/${CMAKE_INSTALL_DATADIR}/aerogui2/stdlib">
    )
    aero_apply_default_rtti_options(aerogui_compiler)

    add_executable(aero-compiler
        tools/aero_compiler/main.cpp
    )
    target_link_libraries(aero-compiler PRIVATE aerogui_compiler)
    aero_apply_default_rtti_options(aero-compiler)
endif()

# ============================================================================
# 6. App Host & Viewer Tool
# ============================================================================
add_library(aerogui_app STATIC
    src/app/AeroAppHost.cpp
)
add_library(AeroGUI::App ALIAS aerogui_app)
target_link_libraries(aerogui_app
    PUBLIC
        aerogui_host
)
if(AERO_BUILD_COMPILER_TOOLS AND TARGET aerogui_compiler)
    target_link_libraries(aerogui_app PUBLIC aerogui_compiler)
    target_compile_definitions(aerogui_app PUBLIC AERO_HAS_COMPILER_V3=1)
    if(TARGET RealScript::StencilJit AND NOT EMSCRIPTEN)
        target_link_libraries(aerogui_app PRIVATE RealScript::StencilJit)
        target_compile_definitions(aerogui_app PRIVATE AERO_HAS_STENCIL_JIT=1)
    endif()
    aero_apply_default_rtti_options(aerogui_app)
else()
    aero_apply_no_rtti_options(aerogui_app)
endif()

add_library(aerocore INTERFACE)
add_library(AeroGUI::Core ALIAS aerocore)
target_link_libraries(aerocore INTERFACE aerogui_app)

if(NOT EMSCRIPTEN)
    add_executable(aero-viewer
        tools/aero_viewer/main.cpp
    )
    target_link_libraries(aero-viewer PRIVATE aerogui_app)
    if(AERO_BUILD_COMPILER_TOOLS AND TARGET aerogui_compiler)
        aero_apply_default_rtti_options(aero-viewer)
    else()
        aero_apply_no_rtti_options(aero-viewer)
    endif()
endif()

# ============================================================================
# Pre-Registered Future v3 CTest Suites
# ============================================================================
if(AERO_BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/microkernel_tests.cpp")
        add_executable(microkernel_tests tests/microkernel_tests.cpp)
        target_link_libraries(microkernel_tests PRIVATE aerogui_visual)
        aero_apply_no_rtti_options(microkernel_tests)
        add_test(NAME microkernel_tests COMMAND microkernel_tests)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/render_host_tests.cpp")
        add_executable(render_host_tests tests/render_host_tests.cpp)
        target_link_libraries(render_host_tests PRIVATE aerogui_host)
        target_compile_definitions(render_host_tests PRIVATE
            AERO_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
        )
        aero_apply_no_rtti_options(render_host_tests)
        add_test(NAME render_host_tests COMMAND render_host_tests)
    endif()

    if(AERO_BUILD_COMPILER_TOOLS AND TARGET aerogui_compiler AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/compiler_v3_tests.cpp")
        add_executable(compiler_v3_tests tests/compiler_v3_tests.cpp)
        target_link_libraries(compiler_v3_tests PRIVATE aerogui_compiler)
        target_compile_definitions(compiler_v3_tests PRIVATE
            AERO_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
            AERO_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
            AERO_TEST_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_out"
        )
        aero_apply_default_rtti_options(compiler_v3_tests)
        add_test(NAME compiler_v3_tests COMMAND compiler_v3_tests)
    endif()

    if(AERO_BUILD_COMPILER_TOOLS AND TARGET aerogui_app AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/v3_controls_and_samples_tests.cpp")
        add_executable(v3_controls_and_samples_tests tests/v3_controls_and_samples_tests.cpp)
        target_link_libraries(v3_controls_and_samples_tests PRIVATE aerogui_app)
        target_compile_definitions(v3_controls_and_samples_tests PRIVATE
            AERO_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
            AERO_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
            AERO_TEST_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_out"
        )
        aero_apply_default_rtti_options(v3_controls_and_samples_tests)
        add_test(NAME v3_controls_and_samples_tests COMMAND v3_controls_and_samples_tests)
    endif()

    if(AERO_BUILD_COMPILER_TOOLS AND TARGET aero-compiler AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/v3_slim_no_rtti_and_aot_tests.cpp")
        add_executable(v3_slim_no_rtti_and_aot_tests tests/v3_slim_no_rtti_and_aot_tests.cpp)
        target_link_libraries(v3_slim_no_rtti_and_aot_tests PRIVATE aerogui_host)
        add_dependencies(v3_slim_no_rtti_and_aot_tests aero-compiler)
        target_compile_definitions(v3_slim_no_rtti_and_aot_tests PRIVATE
            AERO_COMPILER_EXE="$<TARGET_FILE:aero-compiler>"
            AERO_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
            AERO_TEST_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_out"
        )
        aero_apply_no_rtti_options(v3_slim_no_rtti_and_aot_tests)
        add_test(NAME v3_slim_no_rtti_and_aot_tests COMMAND v3_slim_no_rtti_and_aot_tests)
    endif()

    if(AERO_BUILD_COMPILER_TOOLS AND TARGET aerogui_app AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/v3_legacy_samples_tests.cpp")
        add_executable(v3_legacy_samples_tests tests/v3_legacy_samples_tests.cpp)
        target_link_libraries(v3_legacy_samples_tests PRIVATE aerogui_app)
        target_compile_definitions(v3_legacy_samples_tests PRIVATE
            AERO_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
            AERO_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Samples"
            AERO_TEST_TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_out"
        )
        aero_apply_default_rtti_options(v3_legacy_samples_tests)
        add_test(NAME v3_legacy_samples_tests COMMAND v3_legacy_samples_tests)
    endif()
endif()

# ============================================================================
# Package Config / SDK Export
# ============================================================================
set(_aero_export_targets
    aerogui_foundation
    aerogui_render
    aerogui_visual
    aerogui_host
    aerogui_runtime
    aerogui_app
    aerocore
)
if(TARGET aerogui_compiler)
    list(APPEND _aero_export_targets aerogui_compiler aero-compiler)
    set(AERO_HAS_COMPILER_TOOLS_CMAKE TRUE)
else()
    set(AERO_HAS_COMPILER_TOOLS_CMAKE FALSE)
endif()

file(TO_CMAKE_PATH "${RealScript_DIR}" REALSCRIPT_CONFIG_HINT_DIR)
export(
    TARGETS ${_aero_export_targets}
    NAMESPACE AeroGUI::
    FILE "${CMAKE_CURRENT_BINARY_DIR}/AeroGUI2Targets.cmake"
)
configure_package_config_file(
    cmake/AeroGUI2Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/AeroGUI2Config.cmake"
    INSTALL_DESTINATION .
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/AeroGUI2ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

if(AERO_INSTALL_SDK)
    set(REALSCRIPT_CONFIG_HINT_DIR "")
    install(
        TARGETS ${_aero_export_targets}
        EXPORT AeroGUI2Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(
        DIRECTORY stdlib/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/aerogui2/stdlib
        FILES_MATCHING PATTERN "*.rs"
    )
    configure_package_config_file(
        cmake/AeroGUI2Config.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/InstallConfig/AeroGUI2Config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AeroGUI2
    )
    install(
        EXPORT AeroGUI2Targets
        FILE AeroGUI2Targets.cmake
        NAMESPACE AeroGUI::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AeroGUI2
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/InstallConfig/AeroGUI2Config.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/AeroGUI2ConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AeroGUI2
    )
endif()
