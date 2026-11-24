#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(AERO_STDLIB_DLL_BUILD)
#    define AERO_STDLIB_API __declspec(dllexport)
#  elif defined(AERO_STDLIB_DLL_IMPORT)
#    define AERO_STDLIB_API __declspec(dllimport)
#  else
#    define AERO_STDLIB_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define AERO_STDLIB_API __attribute__((visibility("default")))
#else
#  define AERO_STDLIB_API
#endif

#define AERO_STDLIB_DLL_MAGIC 0x42445341u /* "ASDB" little-endian */
#define AERO_STDLIB_DLL_ABI_VERSION 2u

#define AERO_STDLIB_FLAG_OPTIMIZED_O2 (1u << 0)
#define AERO_STDLIB_FLAG_REACTIVE_MACROS (1u << 1)

typedef struct AeroStdlibDllHeader {
    uint32_t magic;
    uint32_t abiVersion;
    uint32_t structSize;
    uint32_t flags;
    uint32_t realscriptVersionMajor;
    uint32_t realscriptVersionMinor;
    uint32_t realscriptVersionPatch;
    uint32_t realscriptSdkCompatVersion;
    uint64_t stdlibSourceHash;
    uint64_t frontendPackChecksum;
    uint64_t lazyBytecodeChecksum;
    const uint8_t* frontendPackBytes;
    uint64_t frontendPackSize;
    const uint8_t* lazyBytecodeBytes;
    uint64_t lazyBytecodeSize;
    const char* moduleListCsv;
} AeroStdlibDllHeader;

typedef const AeroStdlibDllHeader* (*AeroStdlibGetHeaderFn)(void);

AERO_STDLIB_API const AeroStdlibDllHeader* aero_stdlib_get_header(void);

#ifdef __cplusplus
}
#endif
