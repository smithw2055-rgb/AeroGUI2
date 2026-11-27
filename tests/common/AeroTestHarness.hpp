#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "aero/drawing/Geometry.hpp"

#if defined(AERO_TEST_WITH_APP_HOST)
#include "aero/app/AeroAppHost.hpp"
#endif

namespace aero::test {

struct ImageMetrics {
    std::size_t distinctColors = 0u;
    double nonBlankPercent = 0.0;
    std::uint64_t fnv1a64 = 0u;
};

inline void ReportFailure(
    int& failureCounter,
    std::string_view message,
    const char* file = nullptr,
    int line = 0) {
    if (file != nullptr && line > 0) {
        std::fprintf(
            stderr,
            "[FAIL] %s:%d: %.*s\n",
            file,
            line,
            static_cast<int>(message.size()),
            message.data());
    } else {
        std::fprintf(
            stderr,
            "[FAIL] %.*s\n",
            static_cast<int>(message.size()),
            message.data());
    }
    ++failureCounter;
}

inline void CheckNear(
    int& failureCounter,
    float actual,
    float expected,
    float eps,
    const char* expr,
    const char* file,
    int line) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > eps) {
        std::fprintf(
            stderr,
            "[FAIL] %s:%d: %s (actual=%.5f expected=%.5f eps=%.5f)\n",
            file,
            line,
            expr,
            static_cast<double>(actual),
            static_cast<double>(expected),
            static_cast<double>(eps));
        ++failureCounter;
    }
}

[[nodiscard]] inline std::string ReadFileText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

[[nodiscard]] inline bool WriteFileText(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

[[nodiscard]] inline std::string NormalizeSlash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

[[nodiscard]] constexpr std::uint32_t PackRgbaBigEndian(
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b,
    std::uint8_t a) noexcept {
    return (static_cast<std::uint32_t>(r) << 24u) |
           (static_cast<std::uint32_t>(g) << 16u) |
           (static_cast<std::uint32_t>(b) << 8u) |
           static_cast<std::uint32_t>(a);
}

[[nodiscard]] inline std::uint32_t QuantizedClearColorRgba(const Color& c) noexcept {
    auto clampU8 = [](float x) noexcept -> std::uint8_t {
        if (x <= 0.0f) return 0u;
        if (x >= 1.0f) return 255u;
        return static_cast<std::uint8_t>(x * 255.0f + 0.5f);
    };
    return PackRgbaBigEndian(clampU8(c.r), clampU8(c.g), clampU8(c.b), clampU8(c.a));
}

[[nodiscard]] inline ImageMetrics ComputeExactImageMetrics(
    const std::vector<std::uint8_t>& rgba,
    std::uint32_t clearRgba) {
    ImageMetrics m{};
    const std::size_t pixelCount = rgba.size() / 4u;
    if (pixelCount == 0u) {
        return m;
    }

    std::unordered_set<std::uint32_t> uniqueColors;
    uniqueColors.reserve(1024u);
    std::size_t nonBlank = 0u;

    std::uint64_t hash = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    for (std::size_t i = 0u; i < pixelCount; ++i) {
        const std::size_t off = i * 4u;
        const std::uint8_t r = rgba[off];
        const std::uint8_t g = rgba[off + 1u];
        const std::uint8_t b = rgba[off + 2u];
        const std::uint8_t a = rgba[off + 3u];
        const std::uint32_t packed = PackRgbaBigEndian(r, g, b, a);
        uniqueColors.insert(packed);
        if (packed != clearRgba) {
            ++nonBlank;
        }
        hash ^= r; hash *= kPrime;
        hash ^= g; hash *= kPrime;
        hash ^= b; hash *= kPrime;
        hash ^= a; hash *= kPrime;
    }

    m.distinctColors = uniqueColors.size();
    m.nonBlankPercent = (100.0 * static_cast<double>(nonBlank)) / static_cast<double>(pixelCount);
    m.fnv1a64 = hash;
    return m;
}

[[nodiscard]] inline std::size_t CountDistinctColorsSampled(
    const std::vector<std::uint8_t>& rgba,
    std::size_t maxSamples = 12000u,
    std::size_t earlyStopAt = 64u) {
    std::unordered_set<std::uint32_t> colors;
    const std::size_t pixelCount = rgba.size() / 4u;
    if (pixelCount == 0u) {
        return 0u;
    }
    const std::size_t step = (maxSamples > 0u)
        ? std::max<std::size_t>(1u, pixelCount / maxSamples)
        : 1u;
    for (std::size_t i = 0u; i < pixelCount; i += step) {
        const std::size_t off = i * 4u;
        colors.insert(PackRgbaBigEndian(rgba[off], rgba[off + 1u], rgba[off + 2u], rgba[off + 3u]));
        if (earlyStopAt > 0u && colors.size() >= earlyStopAt) {
            break;
        }
    }
    return colors.size();
}

[[nodiscard]] inline std::size_t CountChangedPixels(
    const std::vector<std::uint8_t>& a,
    const std::vector<std::uint8_t>& b,
    int channelSumThreshold = 6) noexcept {
    if (a.size() != b.size()) {
        return a.size() / 4u;
    }
    std::size_t diffPixels = 0u;
    for (std::size_t i = 0u; i + 3u < a.size(); i += 4u) {
        const int dr = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        const int dg = std::abs(static_cast<int>(a[i + 1u]) - static_cast<int>(b[i + 1u]));
        const int db = std::abs(static_cast<int>(a[i + 2u]) - static_cast<int>(b[i + 2u]));
        const int da = std::abs(static_cast<int>(a[i + 3u]) - static_cast<int>(b[i + 3u]));
        if (dr + dg + db + da > channelSumThreshold) {
            ++diffPixels;
        }
    }
    return diffPixels;
}

#if defined(AERO_TEST_WITH_APP_HOST)

inline void StepFrames(AeroAppHost& host, int frameCount, float dtSeconds = 1.0f / 60.0f) {
    for (int f = 0; f < frameCount; ++f) {
        host.StepFrame(dtSeconds, f == frameCount - 1);
    }
}

inline void TapKey(
    AeroAppHost& host,
    KeyCode key,
    std::uint8_t modifiers = KeyMod_None,
    int settleFrames = 1,
    float dtSeconds = 1.0f / 60.0f) {
    host.InjectKey(key, true, modifiers);
    host.InjectKey(key, false, modifiers);
    if (settleFrames > 0) {
        StepFrames(host, settleFrames, dtSeconds);
    }
}

[[nodiscard]] inline Rect NodeViewportBounds(const VisualNode& node) noexcept {
    Rect r = node.layout.layoutRect;
    if (node.layout.hasPerspective3D) {
        return node.WorldMatrix3D().TransformRectBounds(r);
    }
    if (!node.layout.worldTransform.IsIdentity()) {
        return node.layout.worldTransform.TransformRectBounds(r);
    }
    return r;
}

[[nodiscard]] inline bool ResolveNodeCenter(
    const AeroAppHost& host,
    std::string_view name,
    float& outCx,
    float& outCy,
    VisualNodeId* outId = nullptr,
    Rect* outRect = nullptr,
    std::string* outFailureReason = nullptr) {
    const VisualNodeId id = host.FindVisualByOwnerName(name);
    if (!id.IsValid()) {
        if (outFailureReason) {
            *outFailureReason = "Expected named VisualNode '" + std::string(name) + "' not found";
        }
        return false;
    }
    const VisualNode* node = host.World().GetNode(id);
    if (!node) {
        if (outFailureReason) {
            *outFailureReason = "Named VisualNode '" + std::string(name) + "' resolved to null node";
        }
        return false;
    }
    const Rect r = node->layout.layoutRect;
    if (r.width <= 1.0f || r.height <= 1.0f) {
        if (outFailureReason) {
            *outFailureReason = "Named VisualNode '" + std::string(name) + "' has degenerate layoutRect (" +
                std::to_string(r.width) + "x" + std::to_string(r.height) + ")";
        }
        return false;
    }
    if (outId) {
        *outId = id;
    }
    if (outRect) {
        *outRect = r;
    }
    const Point localCenter{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const Point vpCenter = node->layout.hasPerspective3D
        ? node->WorldMatrix3D().ProjectPoint2D(localCenter)
        : node->layout.worldTransform.TransformPoint(localCenter);
    outCx = vpCenter.x;
    outCy = vpCenter.y;
    return true;
}

[[nodiscard]] inline bool ReadIntMember(
    const AeroAppHost& host,
    ObjectRef obj,
    std::string_view path,
    std::int64_t& outVal) {
    UnifiedValue v{};
    return host.ReadScriptMember(obj, path, v) && v.tryGetInt64(outVal);
}

[[nodiscard]] inline bool ReadBoolMember(
    const AeroAppHost& host,
    ObjectRef obj,
    std::string_view path,
    bool& outVal) {
    UnifiedValue v{};
    return host.ReadScriptMember(obj, path, v) && v.tryGetBool(outVal);
}

[[nodiscard]] inline bool ReadFloatMember(
    const AeroAppHost& host,
    ObjectRef obj,
    std::string_view path,
    float& outVal) {
    UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    if (v.tryGetFloat(outVal)) {
        return true;
    }
    double dv = 0.0;
    if (v.tryGetDouble(dv)) {
        outVal = static_cast<float>(dv);
        return true;
    }
    std::int64_t iv = 0;
    if (v.tryGetInt64(iv)) {
        outVal = static_cast<float>(iv);
        return true;
    }
    return false;
}

[[nodiscard]] inline bool ReadStringMember(
    const AeroAppHost& host,
    ObjectRef obj,
    std::string_view path,
    std::string& outVal) {
    UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&v)) {
        outVal = sv->str();
        return true;
    }
    if (const auto* ref = std::get_if<rs_rt::ObjectRef>(&v);
        ref && ref->valid() && ref->kind == rs_rt::ObjectKind::String) {
        if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
            outVal.assign(opt->data(), opt->size());
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool ReadObjectMember(
    const AeroAppHost& host,
    ObjectRef obj,
    std::string_view path,
    ObjectRef& outRef) {
    UnifiedValue v{};
    if (!host.ReadScriptMember(obj, path, v)) {
        return false;
    }
    if (const auto* ref = std::get_if<rs_rt::ObjectRef>(&v); ref && ref->valid()) {
        outRef = *ref;
        return true;
    }
    return false;
}

[[nodiscard]] inline bool ReadListCountMember(
    AeroAppHost& host,
    ObjectRef obj,
    std::string_view listPath,
    std::int64_t& outCount) {
    ObjectRef listRef{};
    if (!ReadObjectMember(host, obj, listPath, listRef)) {
        return false;
    }
    if (listRef.kind == rs_rt::ObjectKind::Array) {
        if (const auto len = host.Domains().uiHeap().arrayLength(listRef)) {
            outCount = static_cast<std::int64_t>(*len);
            return true;
        }
    }
    UnifiedValue arrVal{};
    if (host.ReadScriptMember(listRef, "_items", arrVal)) {
        if (const auto* arrRef = std::get_if<rs_rt::ObjectRef>(&arrVal);
            arrRef && arrRef->valid() && arrRef->kind == rs_rt::ObjectKind::Array) {
            if (const auto len = host.Domains().uiHeap().arrayLength(*arrRef)) {
                outCount = static_cast<std::int64_t>(*len);
                return true;
            }
        }
    }
    UnifiedValue res{};
    if (host.InvokeScriptMethod(listRef, "Count", {}, &res)) {
        return res.tryGetInt64(outCount);
    }
    return false;
}

[[nodiscard]] inline bool TryReadNodeTypeName(
    AeroAppHost& host,
    const VisualNode* n,
    std::string& outTypeName) {
    if (!n || !n->hasScriptOwner || !n->scriptOwner.valid()) {
        return false;
    }
    UnifiedValue res{};
    if (!host.InvokeScriptMethod(n->scriptOwner, "GetTypeName", {}, &res)) {
        return false;
    }
    if (const auto* sv = std::get_if<rs_rt::StringValue>(&res)) {
        outTypeName = sv->str();
        return true;
    }
    if (const auto* ref = std::get_if<rs_rt::ObjectRef>(&res);
        ref && ref->valid() && ref->kind == rs_rt::ObjectKind::String) {
        if (const auto opt = host.Domains().stringViewAcrossDomains(*ref)) {
            outTypeName.assign(opt->data(), opt->size());
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline VisualNodeId FindDescendantByOwnerName(
    const AeroAppHost& host,
    VisualNodeId rootId,
    std::string_view ownerName) {
    const VisualWorld& world = host.World();
    std::vector<VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const VisualNode* n = world.GetNode(q[i]);
        if (!n) {
            continue;
        }
        std::string name;
        if (q[i] != rootId &&
            n->hasScriptOwner &&
            n->scriptOwner.valid() &&
            ReadStringMember(host, n->scriptOwner, "Name", name) &&
            name == ownerName) {
            return q[i];
        }
        for (VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
    return VisualNodeId{};
}

[[nodiscard]] inline VisualNodeId FindDescendantByTypeName(
    AeroAppHost& host,
    VisualNodeId rootId,
    std::string_view typeName) {
    const VisualWorld& world = host.World();
    std::vector<VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const VisualNode* n = world.GetNode(q[i]);
        if (!n) {
            continue;
        }
        std::string tn;
        if (q[i] != rootId && TryReadNodeTypeName(host, n, tn) && tn == typeName) {
            return q[i];
        }
        for (VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
    return VisualNodeId{};
}

inline void CollectVisualNodesByOwnerName(
    const AeroAppHost& host,
    std::string_view ownerName,
    std::vector<VisualNodeId>& outIds) {
    outIds.clear();
    const VisualWorld& world = host.World();
    const std::size_t slots = world.ArenaSlotCount();
    for (std::size_t s = 0u; s < slots; ++s) {
        const VisualNode* n = world.NodeAtSlot(s);
        if (!n || !n->hasScriptOwner || !n->scriptOwner.valid()) {
            continue;
        }
        std::string name;
        if (ReadStringMember(host, n->scriptOwner, "Name", name) && name == ownerName) {
            outIds.push_back(n->id);
        }
    }
}

inline void CollectDescendantsByTypeName(
    AeroAppHost& host,
    VisualNodeId rootId,
    std::string_view typeName,
    std::vector<VisualNodeId>& outIds) {
    outIds.clear();
    const VisualWorld& world = host.World();
    std::vector<VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const VisualNode* n = world.GetNode(q[i]);
        if (!n) {
            continue;
        }
        std::string tn;
        if (q[i] != rootId && TryReadNodeTypeName(host, n, tn) && tn == typeName) {
            outIds.push_back(q[i]);
        }
        for (VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
}

inline void CollectSubtreeText(
    const VisualWorld& world,
    VisualNodeId rootId,
    std::string& outJoined) {
    outJoined.clear();
    std::vector<VisualNodeId> q{rootId};
    for (std::size_t i = 0u; i < q.size(); ++i) {
        const VisualNode* n = world.GetNode(q[i]);
        if (!n) {
            continue;
        }
        if (n->kind == VisualKind::Text && !n->Text().text.empty()) {
            if (!outJoined.empty()) {
                outJoined.push_back(' ');
            }
            outJoined.append(n->Text().text);
        }
        for (VisualNodeId cid : n->children) {
            q.push_back(cid);
        }
    }
}

#endif // AERO_TEST_WITH_APP_HOST

} // namespace aero::test
