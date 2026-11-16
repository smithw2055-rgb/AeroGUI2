cmake_minimum_required(VERSION 3.20)

project(AeroGUI2 VERSION 2.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AERO_BUILD_COMPILER_TOOLS "Build host XAML+RealScript compiler & desktop compiler test suites" ON)
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

if(DEFINED REALSCRIPT_SOURCE_DIR AND NOT "${REALSCRIPT_SOURCE_DIR}" STREQUAL "")
    message(WARNING "REALSCRIPT_SOURCE_DIR is deprecated and ignored; AeroGUI2 consumes prebuilt RealScript outputs via RealScript_DIR / REALSCRIPT_BUILD_DIR / CMAKE_PREFIX_PATH.")
endif()

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

if(DEFINED RealScript_SLIM_RUNTIME AND RealScript_SLIM_RUNTIME)
    set(REALSCRIPT_SLIM_RUNTIME ON)
endif()

if(REALSCRIPT_SLIM_RUNTIME OR NOT TARGET RealScript::Frontend)
    set(AERO_BUILD_COMPILER_TOOLS OFF CACHE BOOL "Compiler tools require full RealScript frontend" FORCE)
endif()

# ============================================================================
# 1. Modular Pure Runtime Libraries (NoesisGUI-Style Layered Targets, Wasm-Ready)
# ============================================================================
set(AERO_KERNEL_SOURCES
    src/runtime/AeroObservableList.cpp
    src/runtime/AeroExecutionDomains.cpp
    src/core/AeroObject.cpp
    src/core/TypeClass.cpp
    src/core/DependencyProperty.cpp
    src/core/UIViewContext.cpp
    src/core/UIElement.cpp
    src/core/UIResourceContext.cpp
    src/asset/AxbFormat.cpp
    src/asset/AxbTemplateRegistry.cpp
    src/asset/GradientBrush.cpp
    src/asset/MtsdfPathBaker.cpp
    src/asset/RiveDecoder.cpp
    src/asset/ValueConverters.cpp
    src/asset/XamlLiteralParser.cpp
    src/layout/DepthBucketLayoutQueue.cpp
    src/binding/DualTrackBindingEngine.cpp
    src/controls/ControlContainers.cpp
    src/controls/ControlPanelsShapes.cpp
    src/controls/ControlButtonsRanges.cpp
    src/controls/ControlTextMedia.cpp
    src/controls/ControlItems.cpp
    src/controls/AxbTemplateMounter.cpp
    src/controls/ControlRenderEmitters.cpp
    src/input/InputEventRouter.cpp
    src/animation/AnimationEngine.cpp
    src/style/StyleTriggerEngine.cpp
    src/render/PagedRenderTree.cpp
    src/render/FontAtlas.cpp
    src/render/ImageAtlas.cpp
)

add_library(aerogui_kernel STATIC
    ${AERO_KERNEL_SOURCES}
)

if(MSVC)
    target_compile_options(aerogui_kernel PUBLIC /utf-8)
endif()

set_target_properties(aerogui_kernel PROPERTIES EXPORT_NAME Kernel)

target_include_directories(aerogui_kernel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aerogui_kernel
    PUBLIC
        RealScript::SlimRuntime
)

if(TARGET Threads::Threads)
    target_link_libraries(aerogui_kernel PUBLIC Threads::Threads)
endif()

add_library(AeroGUI::Kernel ALIAS aerogui_kernel)

set(AERO_RENDER_SOURCES
    src/render/ZeroCopyGpuStream.cpp
    src/render/GpuBackend.cpp
)

add_library(aerogui_render STATIC
    ${AERO_RENDER_SOURCES}
)

if(MSVC)
    target_compile_options(aerogui_render PUBLIC /utf-8)
endif()

set_target_properties(aerogui_render PROPERTIES EXPORT_NAME Render)

target_include_directories(aerogui_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aerogui_render
    PUBLIC
        aerogui_kernel
)

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(aerogui_render PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library(AeroGUI::Render ALIAS aerogui_render)

set(AERO_APP_HOST_SOURCES
    src/bridge/ScriptUIBridge.cpp
    src/host/HostDrivers.cpp
    src/host/NativeWindowHost.cpp
)

add_library(aerogui_app_host STATIC
    ${AERO_APP_HOST_SOURCES}
)

if(MSVC)
    target_compile_options(aerogui_app_host PUBLIC /utf-8)
    add_link_options(/STACK:8388608)
endif()

set_target_properties(aerogui_app_host PROPERTIES EXPORT_NAME AppHost)

target_include_directories(aerogui_app_host
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aerogui_app_host
    PUBLIC
        aerogui_render
)

if(WIN32)
    target_link_libraries(aerogui_app_host PUBLIC user32 gdi32 imm32)
endif()

add_library(AeroGUI::AppHost ALIAS aerogui_app_host)

add_library(aerogui_runtime INTERFACE)
set_target_properties(aerogui_runtime PROPERTIES EXPORT_NAME Runtime)
target_link_libraries(aerogui_runtime INTERFACE aerogui_app_host)
add_library(AeroGUI::Runtime ALIAS aerogui_runtime)

set(AERO_RUNTIME_SOURCES
    ${AERO_KERNEL_SOURCES}
    ${AERO_RENDER_SOURCES}
    ${AERO_APP_HOST_SOURCES}
)

# ============================================================================
# 2. Host Compiler Library & CLI (Only built when full RealScript frontend is active)
# ============================================================================
if(AERO_BUILD_COMPILER_TOOLS)
    add_library(aerogui_compiler_lib STATIC
        src/compiler/AeroCompiler.cpp
        src/compiler/FrontendImporters.cpp
        src/compiler/AeroAotCppEmitter.cpp
        src/compiler/CSharpCompatLowerer.cpp
        src/compiler/XamlApplication.cpp
    )

    if(MSVC)
        target_compile_options(aerogui_compiler_lib PUBLIC /utf-8)
    endif()

    set_target_properties(aerogui_compiler_lib PROPERTIES EXPORT_NAME Compiler)

    target_include_directories(aerogui_compiler_lib
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    target_link_libraries(aerogui_compiler_lib
        PUBLIC
            aerogui_runtime
            RealScript::Frontend
    )

    add_library(AeroGUI::Compiler ALIAS aerogui_compiler_lib)

    # Composite targets (aerocore & aerogui_core) preserving 100% downstream compatibility
    add_library(aerocore INTERFACE)
    set_target_properties(aerocore PROPERTIES EXPORT_NAME Core)
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
    set_target_properties(aero-compiler PROPERTIES EXPORT_NAME aero-compiler)

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

        # Active canonical WPF/script & 23-sample regression/completeness suites
        aero_register_core_test(wpf_controls_and_script_tests   tests/wpf_controls_and_script_tests.cpp)
        aero_register_core_test(samples_mainwindow_tests        tests/samples_mainwindow_tests.cpp)
        aero_register_core_test(samples_completeness_tests      tests/samples_completeness_tests.cpp)
        aero_register_core_test(samples_golden_regression_tests tests/samples_golden_regression_tests.cpp)
    endif()
else()
    # In Wasm / REALSCRIPT_SLIM_RUNTIME mode, aerocore & aerogui_core alias the pure runtime
    add_library(aerocore INTERFACE)
    set_target_properties(aerocore PROPERTIES EXPORT_NAME Core)
    target_link_libraries(aerocore INTERFACE aerogui_runtime)

    add_library(aerogui_core INTERFACE)
    target_link_libraries(aerogui_core INTERFACE aerocore)
    add_library(AeroGUI::Core ALIAS aerocore)
endif()

if(AERO_BUILD_TESTS)
    # Pure runtime smoke test compiling all runtime TUs + test with RTTI disabled
    # (avoids -frtti/-fno-rtti cross-TU ODR mixing and verifies 0 dynamic_cast/typeid & 0 AeroCompiler.cpp dependency)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/slim_runtime_smoke_tests.cpp")
        add_executable(aerogui_slim_runtime_tests
            ${AERO_RUNTIME_SOURCES}
            tests/slim_runtime_smoke_tests.cpp
        )
        target_include_directories(aerogui_slim_runtime_tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(aerogui_slim_runtime_tests
            PRIVATE
                RealScript::SlimRuntime
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
        message(FATAL_ERROR "Missing test source: ${CMAKE_CURRENT_SOURCE_DIR}/tests/slim_runtime_smoke_tests.cpp")
    endif()
endif()

set(aerogui_export_targets aerogui_kernel aerogui_render aerogui_app_host aerogui_runtime aerocore)
if(AERO_BUILD_COMPILER_TOOLS)
    list(APPEND aerogui_export_targets aerogui_compiler_lib aero-compiler)
    set(AERO_HAS_COMPILER_TOOLS_CMAKE TRUE)
else()
    set(AERO_HAS_COMPILER_TOOLS_CMAKE FALSE)
endif()

file(TO_CMAKE_PATH "${RealScript_DIR}" REALSCRIPT_CONFIG_HINT_DIR)
export(
    TARGETS ${aerogui_export_targets}
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
        TARGETS ${aerogui_export_targets}
        EXPORT AeroGUI2Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
