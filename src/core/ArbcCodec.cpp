#include "aero/core/ArbcCodec.hpp"

#include <cstring>
#include <future>

#include <realscript/bytecode/Bytecode.h>
#include <realscript/diagnostics/Diagnostic.h>

namespace aero {
namespace {

void AppendU32Le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
}

} // namespace

std::vector<std::uint8_t> EncodeArbcBundle(
    const std::vector<rs_bc::Module>& modules) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(4096u);
    bytes.push_back('A');
    bytes.push_back('R');
    bytes.push_back('B');
    bytes.push_back('C');
    AppendU32Le(bytes, static_cast<std::uint32_t>(modules.size()));
    for (const auto& mod : modules) {
        const auto modBytes = rs_bc::encodeModule(mod);
        AppendU32Le(bytes, static_cast<std::uint32_t>(modBytes.size()));
        bytes.insert(bytes.end(), modBytes.begin(), modBytes.end());
    }
    return bytes;
}

bool DecodeArbcBundle(
    const std::uint8_t* data,
    std::size_t size,
    std::vector<rs_bc::Module>& outModules,
    bool allowSingleModuleFallback,
    std::string* outError) {
    outModules.clear();
    if (!data || size == 0u) {
        if (outError) {
            *outError = "DecodeArbcBundle: empty bytecode buffer";
        }
        return false;
    }

    if (size >= 8u && std::memcmp(data, "ARBC", 4u) == 0) {
        std::size_t pos = 4u;
        auto readU32 = [&](std::uint32_t& out) -> bool {
            if (pos + 4u > size) {
                return false;
            }
            out = static_cast<std::uint32_t>(data[pos]) |
                  (static_cast<std::uint32_t>(data[pos + 1u]) << 8u) |
                  (static_cast<std::uint32_t>(data[pos + 2u]) << 16u) |
                  (static_cast<std::uint32_t>(data[pos + 3u]) << 24u);
            pos += 4u;
            return true;
        };

        std::uint32_t modCount = 0u;
        if (!readU32(modCount) || modCount > 1024u || (allowSingleModuleFallback && modCount == 0u)) {
            if (outError) {
                *outError = "DecodeArbcBundle: invalid ARBC module count";
            }
            return false;
        }

        struct ModuleSlice final {
            std::size_t offset = 0u;
            std::size_t length = 0u;
        };
        std::vector<ModuleSlice> slices;
        slices.reserve(modCount);
        for (std::uint32_t i = 0u; i < modCount; ++i) {
            std::uint32_t modSize = 0u;
            if (!readU32(modSize) || pos + modSize > size) {
                if (outError) {
                    *outError = "DecodeArbcBundle: truncated ARBC module payload";
                }
                return false;
            }
            slices.push_back(ModuleSlice{pos, modSize});
            pos += modSize;
        }
        if (pos != size) {
            if (outError) {
                *outError = "DecodeArbcBundle: trailing bytes after ARBC payload";
            }
            return false;
        }

        outModules.resize(modCount);
        if (modCount >= 2u && size >= 131072u) {
            std::vector<std::future<bool>> futures;
            futures.reserve(modCount);
            for (std::uint32_t i = 0u; i < modCount; ++i) {
                const ModuleSlice sl = slices[i];
                futures.push_back(std::async(std::launch::async, [&, i, sl]() -> bool {
                    rs_diag::DiagnosticBag diag;
                    return rs_bc::decodeModule(data + sl.offset, sl.length, outModules[i], diag) && !diag.hasErrors();
                }));
            }
            bool allOk = true;
            for (auto& fut : futures) {
                if (!fut.get()) {
                    allOk = false;
                }
            }
            if (!allOk) {
                outModules.clear();
                if (outError) {
                    *outError = "DecodeArbcBundle: failed decoding ARBC module";
                }
                return false;
            }
            return true;
        }

        for (std::uint32_t i = 0u; i < modCount; ++i) {
            const ModuleSlice sl = slices[i];
            rs_diag::DiagnosticBag diag;
            if (!rs_bc::decodeModule(data + sl.offset, sl.length, outModules[i], diag) || diag.hasErrors()) {
                outModules.clear();
                if (outError) {
                    *outError = "DecodeArbcBundle: failed decoding ARBC module";
                }
                return false;
            }
        }
        return true;
    }

    if (!allowSingleModuleFallback) {
        if (outError) {
            *outError = "DecodeArbcBundle: missing ARBC header";
        }
        return false;
    }

    rs_diag::DiagnosticBag diag;
    rs_bc::Module singleMod;
    if (!rs_bc::decodeModule(data, size, singleMod, diag) || diag.hasErrors()) {
        if (outError) {
            *outError = "DecodeArbcBundle: failed decoding single bytecode module";
        }
        return false;
    }
    outModules.push_back(std::move(singleMod));
    return true;
}

} // namespace aero
