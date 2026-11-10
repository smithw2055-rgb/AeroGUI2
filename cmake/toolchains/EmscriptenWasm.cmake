# AeroGUI 2.0 + RealScript Emscripten WebAssembly (Wasm + WebGL2 + SIMD128) Toolchain
# Strictly forbids ASYNCIFY (-sASYNCIFY=0), C++ exceptions (-fno-exceptions), and C++ RTTI (-fno-rtti)
# while enabling REALSCRIPT_SLIM_RUNTIME so aerogui_runtime ships well below the 700 KB Wasm budget.

set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REALSCRIPT_SLIM_RUNTIME ON CACHE BOOL "Build slim RealScript runtime for Wasm" FORCE)
set(AERO_BUILD_COMPILER_TOOLS OFF CACHE BOOL "Omit host XAML/RealScript compiler in Wasm runtime build" FORCE)
set(AERO_BUILD_TESTS OFF CACHE BOOL "Disable desktop tests during Wasm cross-build" FORCE)
set(REALSCRIPT_BUILD_TESTS OFF CACHE BOOL "Disable desktop tests during Wasm cross-build" FORCE)
set(REALSCRIPT_ENABLE_TOOLCHAIN_JIT_TESTS OFF CACHE BOOL "Disable external JIT in Wasm" FORCE)

set(AERO_WASM_COMPILE_FLAGS "-Oz -flto -fno-exceptions -fno-rtti -msimd128")
set(AERO_WASM_LINK_FLAGS "-Oz -flto -fno-exceptions -fno-rtti -sWASM=1 -sUSE_WEBGL2=1 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY=0 -sFILESYSTEM=0")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${AERO_WASM_COMPILE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${AERO_WASM_LINK_FLAGS}")
