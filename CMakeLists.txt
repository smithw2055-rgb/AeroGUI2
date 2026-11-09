cmake_minimum_required(VERSION 3.20)

project(AeroGUI2 VERSION 2.0.0 LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include RealScript from third-party/RealScript
add_subdirectory(third-party/RealScript)

add_library(aerogui_core STATIC
    src/core/UIElement.cpp
    src/bridge/ScriptUiBridge.cpp
    src/layout/DepthBucketLayoutQueue.cpp
    src/binding/DualTrackBindingEngine.cpp
    src/controls/Controls.cpp
    src/compiler/AeroCompiler.cpp
    src/render/PagedRenderTree.cpp
    src/render/ZeroCopyGpuStream.cpp
    src/host/HostDrivers.cpp
)

if(MSVC)
    target_compile_options(aerogui_core PUBLIC /utf-8)
endif()

target_include_directories(aerogui_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(aerogui_core
    PUBLIC
        realscript_frontend
)

add_executable(aero-compiler
    tools/aero_compiler/main.cpp
)

target_link_libraries(aero-compiler
    PRIVATE
        aerogui_core
)

add_executable(aerogui_phase1_tests
    tests/phase1_foundation_tests.cpp
)

target_link_libraries(aerogui_phase1_tests
    PRIVATE
        aerogui_core
)

add_test(NAME aerogui_phase1_tests COMMAND aerogui_phase1_tests)

add_executable(aerogui_phase2_tests
    tests/phase2_binding_compiler_layout_tests.cpp
)

target_link_libraries(aerogui_phase2_tests
    PRIVATE
        aerogui_core
)

add_test(NAME aerogui_phase2_tests COMMAND aerogui_phase2_tests)

add_executable(aerogui_phase3_tests
    tests/phase3_render_host_aot_wasm_tests.cpp
)

target_link_libraries(aerogui_phase3_tests
    PRIVATE
        aerogui_core
)

add_test(NAME aerogui_phase3_tests COMMAND aerogui_phase3_tests)
