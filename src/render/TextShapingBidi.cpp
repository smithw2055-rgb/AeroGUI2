#include "aero/render/FontAtlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "aero/core/StringUtils.hpp"

namespace aero {

namespace {

[[nodiscard]] bool IsWordBreakAfter(std::uint32_t cp) noexcept {
    return cp == ' ' || cp == '\t' || cp == '-' || IsWideCjkCodePoint(cp);
}

} // namespace

TextLayoutResult FontAtlas::MeasureLayout(
    std::string_view utf8Text,
    std::string_view fontFamily,
    float fontSize,
    float maxWidth,
    TextWrapping wrapMode,
    float charSpacing,
    int fontWeight,
    bool fontItalic,
    float explicitLineHeight,
    bool baseRtl,
    TextTrimming trimMode,
    float maxHeight) {
    RichTextRunInput singleRun{};
    singleRun.text = utf8Text;
    singleRun.fontFamily = fontFamily;
    singleRun.fontSize = fontSize;
    singleRun.fontWeight = (fontWeight > 0) ? fontWeight : 400;
    singleRun.fontItalic = fontItalic ? 1 : 0;
    return MeasureRichLayoutSpan(
        &singleRun,
        utf8Text.empty() ? 0u : 1u,
        fontFamily,
        fontSize,
        (fontWeight > 0) ? fontWeight : 400,
        fontItalic,
        maxWidth,
        wrapMode,
        charSpacing,
        explicitLineHeight,
        baseRtl,
        trimMode,
        maxHeight);
}

namespace {

enum class ArabicJoinType : std::uint8_t {
    NonJoining = 0,
    RightJoining = 1,
    DualJoining = 2,
    JoinCausing = 3,
    Transparent = 4,
};

ArabicJoinType ClassifyArabicJoining(std::uint32_t cp) noexcept {
    if ((cp >= 0x064Bu && cp <= 0x065Fu) || cp == 0x0670u || (cp >= 0x06D6u && cp <= 0x06EDu)) {
        return ArabicJoinType::Transparent;
    }
    if (cp == 0x0640u || cp == 0x200Du) return ArabicJoinType::JoinCausing;
    switch (cp) {
        case 0x0622u: case 0x0623u: case 0x0624u: case 0x0625u: case 0x0627u:
        case 0x0629u: case 0x062Fu: case 0x0630u: case 0x0631u: case 0x0632u:
        case 0x0648u: case 0x0671u:
            return ArabicJoinType::RightJoining;
        default:
            break;
    }
    if ((cp >= 0x0626u && cp <= 0x063Au) || (cp >= 0x0641u && cp <= 0x064Au) ||
        cp == 0x067Eu || cp == 0x0686u || cp == 0x0698u || cp == 0x06A9u ||
        cp == 0x06AFu || cp == 0x06CCu) {
        if (cp == 0x0621u) return ArabicJoinType::NonJoining;
        // Note: U+0649 (Alef Maksura) is DualJoining in Unicode/GSUB or RightJoining when final; in Arabic it only occurs final unless Kashmiri, but check:
        if (cp == 0x0649u) return ArabicJoinType::RightJoining;
        return ArabicJoinType::DualJoining;
    }
    return ArabicJoinType::NonJoining;
}

bool JoinsToLeft(ArabicJoinType jt) noexcept {
    return jt == ArabicJoinType::DualJoining || jt == ArabicJoinType::JoinCausing;
}
bool JoinsToRight(ArabicJoinType jt) noexcept {
    return jt == ArabicJoinType::RightJoining || jt == ArabicJoinType::DualJoining || jt == ArabicJoinType::JoinCausing;
}

std::uint32_t FallbackArabicPresentationFormB(std::uint32_t cp, int form /*0=isol,1=init,2=medi,3=fina*/) noexcept {
    struct Row { std::uint32_t base, isol, fina, init, medi; };
    static constexpr Row kRows[] = {
        {0x0621, 0xFE80, 0xFE80, 0xFE80, 0xFE80},
        {0x0622, 0xFE81, 0xFE82, 0xFE81, 0xFE82},
        {0x0623, 0xFE83, 0xFE84, 0xFE83, 0xFE84},
        {0x0624, 0xFE85, 0xFE86, 0xFE85, 0xFE86},
        {0x0625, 0xFE87, 0xFE88, 0xFE87, 0xFE88},
        {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},
        {0x0627, 0xFE8D, 0xFE8E, 0xFE8D, 0xFE8E},
        {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92},
        {0x0629, 0xFE93, 0xFE94, 0xFE93, 0xFE94},
        {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98},
        {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},
        {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},
        {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},
        {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8},
        {0x062F, 0xFEA9, 0xFEAA, 0xFEA9, 0xFEAA},
        {0x0630, 0xFEAB, 0xFEAC, 0xFEAB, 0xFEAC},
        {0x0631, 0xFEAD, 0xFEAE, 0xFEAD, 0xFEAE},
        {0x0632, 0xFEAF, 0xFEB0, 0xFEAF, 0xFEB0},
        {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},
        {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},
        {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},
        {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},
        {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},
        {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},
        {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC},
        {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0},
        {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4},
        {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},
        {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},
        {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},
        {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},
        {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},
        {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},
        {0x0648, 0xFEED, 0xFEEE, 0xFEED, 0xFEEE},
        {0x0649, 0xFEEF, 0xFEF0, 0xFEEF, 0xFEF0},
        {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},
    };
    for (const auto& r : kRows) {
        if (r.base == cp) {
            if (form == 1) return r.init;
            if (form == 2) return r.medi;
            if (form == 3) return r.fina;
            return r.isol;
        }
    }
    return cp;
}

enum class BidiClass : std::uint8_t {
    L = 0, R = 1, AL = 2, EN = 3, AN = 4, ES = 5, ET = 6, CS = 7, NSM = 8, WS = 9, ON = 10
};

BidiClass ClassifyBidi(std::uint32_t cp) noexcept {
    if ((cp >= 0x0600u && cp <= 0x06FFu) || (cp >= 0x0750u && cp <= 0x077Fu) ||
        (cp >= 0x08A0u && cp <= 0x08FFu) || (cp >= 0xFB50u && cp <= 0xFDFFu) ||
        (cp >= 0xFE70u && cp <= 0xFEFFu)) {
        if (cp >= 0x0660u && cp <= 0x0669u) return BidiClass::AN;
        if ((cp >= 0x064Bu && cp <= 0x065Fu) || cp == 0x0670u) return BidiClass::NSM;
        if (cp == 0x060Cu) return BidiClass::CS;
        return BidiClass::AL;
    }
    if (cp >= 0x0590u && cp <= 0x05FFu) return BidiClass::R;
    if (cp >= '0' && cp <= '9') return BidiClass::EN;
    if (cp == '+' || cp == '-') return BidiClass::ES;
    if (cp == '%' || cp == '#' || cp == '$') return BidiClass::ET;
    if (cp == ',' || cp == '.' || cp == ':' || cp == '/') return BidiClass::CS;
    if (cp == ' ' || cp == '\t') return BidiClass::WS;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= 0x00C0u && cp <= 0x02AFu) || (cp >= 0x3040u && cp <= 0x9FFFu)) {
        return BidiClass::L;
    }
    return BidiClass::ON;
}

std::uint32_t MirrorBracketCodePoint(std::uint32_t cp) noexcept {
    switch (cp) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        default: return cp;
    }
}

void ComputeUax9LineVisualOrder(
    const std::vector<std::uint32_t>& lineCps,
    bool paragraphRtl,
    std::vector<std::size_t>& outOrder,
    std::vector<std::uint8_t>& outLevels) {
    const std::size_t n = lineCps.size();
    outOrder.resize(n);
    for (std::size_t i = 0u; i < n; ++i) outOrder[i] = i;
    const std::uint8_t baseLevel = paragraphRtl ? 1u : 0u;
    outLevels.assign(n, baseLevel);
    if (n == 0u) return;

    std::vector<BidiClass> cls(n);
    std::vector<bool> isNsm(n, false);
    bool hasRtlOrNum = paragraphRtl;
    for (std::size_t i = 0u; i < n; ++i) {
        cls[i] = ClassifyBidi(lineCps[i]);
        isNsm[i] = (cls[i] == BidiClass::NSM);
        if (cls[i] == BidiClass::R || cls[i] == BidiClass::AL) hasRtlOrNum = true;
    }
    if (!hasRtlOrNum) return;

    const BidiClass sos = paragraphRtl ? BidiClass::R : BidiClass::L;
    const BidiClass eos = sos;

    // W1: NSM -> preceding class
    BidiClass prev = sos;
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::NSM) cls[i] = prev;
        else prev = cls[i];
    }
    // W2: EN -> AN if last strong was AL
    BidiClass lastStrong = sos;
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::L || cls[i] == BidiClass::R || cls[i] == BidiClass::AL) lastStrong = cls[i];
        else if (cls[i] == BidiClass::EN && lastStrong == BidiClass::AL) cls[i] = BidiClass::AN;
    }
    // W3: AL -> R
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::AL) cls[i] = BidiClass::R;
    }
    // W4: ES/CS between numbers
    for (std::size_t i = 1u; i + 1u < n; ++i) {
        if (cls[i] == BidiClass::ES && cls[i - 1u] == BidiClass::EN && cls[i + 1u] == BidiClass::EN) {
            cls[i] = BidiClass::EN;
        } else if (cls[i] == BidiClass::CS) {
            if (cls[i - 1u] == BidiClass::EN && cls[i + 1u] == BidiClass::EN) cls[i] = BidiClass::EN;
            else if (cls[i - 1u] == BidiClass::AN && cls[i + 1u] == BidiClass::AN) cls[i] = BidiClass::AN;
        }
    }
    // W5: ET adjacent to EN -> EN
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] != BidiClass::ET) continue;
        std::size_t j = i;
        while (j + 1u < n && cls[j + 1u] == BidiClass::ET) ++j;
        const bool adjEn = (i > 0u && cls[i - 1u] == BidiClass::EN) || (j + 1u < n && cls[j + 1u] == BidiClass::EN);
        if (adjEn) {
            for (std::size_t k = i; k <= j; ++k) cls[k] = BidiClass::EN;
        }
        i = j;
    }
    // W6: remaining ES, ET, CS -> ON
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::ES || cls[i] == BidiClass::ET || cls[i] == BidiClass::CS) cls[i] = BidiClass::ON;
    }
    // W7: EN -> L if last strong was L
    lastStrong = sos;
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] == BidiClass::L || cls[i] == BidiClass::R) lastStrong = cls[i];
        else if (cls[i] == BidiClass::EN && lastStrong == BidiClass::L) cls[i] = BidiClass::L;
    }
    // N1 & N2: resolve neutral/whitespace sequences
    auto strongForNeutral = [](BidiClass c) noexcept -> BidiClass {
        if (c == BidiClass::EN || c == BidiClass::AN) return BidiClass::R;
        return c;
    };
    for (std::size_t i = 0u; i < n; ++i) {
        if (cls[i] != BidiClass::ON && cls[i] != BidiClass::WS) continue;
        std::size_t j = i;
        while (j + 1u < n && (cls[j + 1u] == BidiClass::ON || cls[j + 1u] == BidiClass::WS)) ++j;
        const BidiClass leftDir = (i > 0u) ? strongForNeutral(cls[i - 1u]) : sos;
        const BidiClass rightDir = (j + 1u < n) ? strongForNeutral(cls[j + 1u]) : eos;
        const BidiClass resolved = (leftDir == rightDir) ? leftDir : (paragraphRtl ? BidiClass::R : BidiClass::L);
        for (std::size_t k = i; k <= j; ++k) cls[k] = resolved;
        i = j;
    }
    // I1 / I2: implicit levels
    for (std::size_t i = 0u; i < n; ++i) {
        if ((baseLevel & 1u) == 0u) {
            if (cls[i] == BidiClass::R) outLevels[i] = baseLevel + 1u;
            else if (cls[i] == BidiClass::EN || cls[i] == BidiClass::AN) outLevels[i] = baseLevel + 2u;
        } else {
            if (cls[i] == BidiClass::L || cls[i] == BidiClass::EN || cls[i] == BidiClass::AN) {
                outLevels[i] = baseLevel + 1u;
            }
        }
    }
    // L1: trailing whitespace on line resets to paragraph embedding level
    for (std::size_t k = n; k > 0u; --k) {
        const std::uint32_t cp = lineCps[k - 1u];
        if (cp == ' ' || cp == '\t') outLevels[k - 1u] = baseLevel;
        else break;
    }
    // Preserve base-before-NSM cluster ordering inside odd-level runs so L2 reversal leaves
    // combining marks logically after (to the right of / attached to) their base character.
    for (std::size_t i = 0u; i < n; ) {
        if ((outLevels[outOrder[i]] & 1u) != 0u && !isNsm[outOrder[i]]) {
            std::size_t j = i;
            while (j + 1u < n && isNsm[outOrder[j + 1u]] &&
                   outLevels[outOrder[j + 1u]] == outLevels[outOrder[i]]) {
                ++j;
            }
            if (j > i) {
                std::reverse(outOrder.begin() + static_cast<std::ptrdiff_t>(i),
                             outOrder.begin() + static_cast<std::ptrdiff_t>(j + 1u));
            }
            i = j + 1u;
        } else {
            ++i;
        }
    }
    // L2: reverse contiguous sequences from maxLevel down to minOddLevel
    std::uint8_t maxLev = 0u;
    std::uint8_t minOdd = 255u;
    for (std::uint8_t lv : outLevels) {
        maxLev = std::max(maxLev, lv);
        if ((lv & 1u) != 0u) minOdd = std::min(minOdd, lv);
    }
    if (minOdd <= maxLev) {
        for (int lv = static_cast<int>(maxLev); lv >= static_cast<int>(minOdd); --lv) {
            std::size_t i = 0u;
            while (i < n) {
                if (outLevels[outOrder[i]] < static_cast<std::uint8_t>(lv)) {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j + 1u < n && outLevels[outOrder[j + 1u]] >= static_cast<std::uint8_t>(lv)) ++j;
                std::reverse(outOrder.begin() + static_cast<std::ptrdiff_t>(i),
                             outOrder.begin() + static_cast<std::ptrdiff_t>(j + 1u));
                i = j + 1u;
            }
        }
    }
}

} // namespace

TextLayoutResult FontAtlas::MeasureRichLayout(
    const std::vector<RichTextRunInput>& runs,
    std::string_view defaultFontFamily,
    float defaultFontSize,
    int defaultFontWeight,
    bool defaultItalic,
    float maxWidth,
    TextWrapping wrapMode,
    float charSpacing,
    float explicitLineHeight,
    bool baseRtl,
    TextTrimming trimMode,
    float maxHeight) {
    return MeasureRichLayoutSpan(
        runs.data(),
        runs.size(),
        defaultFontFamily,
        defaultFontSize,
        defaultFontWeight,
        defaultItalic,
        maxWidth,
        wrapMode,
        charSpacing,
        explicitLineHeight,
        baseRtl,
        trimMode,
        maxHeight);
}

TextLayoutResult FontAtlas::MeasureRichLayoutSpan(
    const RichTextRunInput* runsData,
    std::size_t runCount,
    std::string_view defaultFontFamily,
    float defaultFontSize,
    int defaultFontWeight,
    bool defaultItalic,
    float maxWidth,
    TextWrapping wrapMode,
    float charSpacing,
    float explicitLineHeight,
    bool baseRtl,
    TextTrimming trimMode,
    float maxHeight) {
    TextLayoutResult result{};
    result.baseRtl = baseRtl;
    const float baseFont = std::max(1.0f, defaultFontSize);
    const int baseWeight = (defaultFontWeight > 0) ? defaultFontWeight : 400;
    const float validExplicitLineH =
        (std::isfinite(explicitLineHeight) && explicitLineHeight > 0.0f)
            ? explicitLineHeight
            : 0.0f;

    std::unique_lock<std::mutex> lock(mutex_);
    std::vector<const RegisteredFontEntry*> defaultChain;
    ResolveFontChainLocked(defaultFontFamily, baseWeight, defaultItalic, defaultChain);

    const RegisteredFontEntry* primaryEntry = defaultChain.empty() ? nullptr : defaultChain.front();
    std::shared_ptr<TrueTypeFontFace> primaryFace =
        (primaryEntry && primaryEntry->face && primaryEntry->face->IsValid())
            ? primaryEntry->face
            : nullptr;

    const bool hasDefaultCustomFont = (primaryEntry != nullptr);
    result.usedTrueTypeMetrics = hasDefaultCustomFont;

    if (primaryFace) {
        result.ascent = primaryFace->AscentEm() * baseFont;
        result.descent = primaryFace->DescentEm() * baseFont;
        result.lineGap = primaryFace->LineGapEm() * baseFont;
    } else if (primaryEntry && primaryEntry->isShardOnly) {
        result.ascent = primaryEntry->shardAscentEm * baseFont;
        result.descent = primaryEntry->shardDescentEm * baseFont;
        result.lineGap = primaryEntry->shardLineGapEm * baseFont;
    } else {
        // Built-in fallback vertical metrics preserving 100% existing unit-test layout invariants
        result.ascent = baseFont * 0.95f;
        result.descent = baseFont * 0.25f;
        result.lineGap = 0.0f;
    }
    const float natBaseLineH = std::max(1.0f, result.ascent + result.descent + result.lineGap);
    result.lineHeight = (validExplicitLineH > 0.0f)
        ? std::max(natBaseLineH, validExplicitLineH)
        : natBaseLineH;

    if (!runsData || runCount == 0u) {
        return result;
    }

    struct TokenGlyph {
        std::uint32_t cp = 0u;
        std::uint32_t sourceCodePointIndex = 0u;
        FontAtlasGlyph meta{};
        FontAtlasGlyph mirroredMeta{};
        const TrueTypeFontFace* kernFace = nullptr;
        std::uint32_t fontId = 0u;
        float fontSize = 14.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineGap = 0.0f;
        float upemInvScale = 0.0f;
        std::uint32_t customRgba = 0u;
        bool hasCustomRgba = false;
        bool hasCustomFont = false;
        bool hasMirroredMeta = false;
        bool syntheticBold = false;
        bool syntheticItalic = false;
        bool underline = false;
        bool strikethrough = false;
        bool explicitNewline = false;
    };

    std::vector<TokenGlyph> decoded;
    std::vector<std::shared_ptr<TrueTypeFontFace>> keepAliveFaces;
    if (primaryFace) {
        keepAliveFaces.push_back(primaryFace);
    }

    auto retainFace = [&](const std::shared_ptr<TrueTypeFontFace>& sp) {
        if (!sp) return;
        for (const auto& existing : keepAliveFaces) {
            if (existing.get() == sp.get()) return;
        }
        keepAliveFaces.push_back(sp);
    };

    std::vector<const RegisteredFontEntry*> scratchRunChain;
    std::uint32_t rawCodePointIdx = 0u;
    for (std::size_t runIdx = 0u; runIdx < runCount; ++runIdx) {
        const RichTextRunInput& run = runsData[runIdx];
        if (run.text.empty()) {
            continue;
        }
        const float runFontSize = (run.fontSize > 0.0f && std::isfinite(run.fontSize))
            ? std::max(1.0f, run.fontSize)
            : baseFont;
        const int runWeight = (run.fontWeight > 0) ? run.fontWeight : baseWeight;
        const bool runItalic = (run.fontItalic >= 0) ? (run.fontItalic != 0) : defaultItalic;
        const std::string_view runFamily = !run.fontFamily.empty() ? run.fontFamily : defaultFontFamily;

        const bool matchesDefault =
            (runFamily == defaultFontFamily) &&
            (runWeight == baseWeight) &&
            (runItalic == defaultItalic);
        const std::vector<const RegisteredFontEntry*>& runChain =
            matchesDefault ? defaultChain : (ResolveFontChainLocked(runFamily, runWeight, runItalic, scratchRunChain), scratchRunChain);
        const bool wantBold = (runWeight >= 600);
        const bool runHasCustom = !runChain.empty();
        if (runHasCustom) {
            result.usedTrueTypeMetrics = true;
        }

        const RegisteredFontEntry* runPrimary = runChain.empty() ? nullptr : runChain.front();
        float runAscent = result.ascent;
        float runDescent = result.descent;
        float runLineGap = result.lineGap;
        if (runPrimary && runPrimary->face && runPrimary->face->IsValid()) {
            runAscent = runPrimary->face->AscentEm() * runFontSize;
            runDescent = runPrimary->face->DescentEm() * runFontSize;
            runLineGap = runPrimary->face->LineGapEm() * runFontSize;
            retainFace(runPrimary->face);
        } else if (runPrimary && runPrimary->isShardOnly) {
            runAscent = runPrimary->shardAscentEm * runFontSize;
            runDescent = runPrimary->shardDescentEm * runFontSize;
            runLineGap = runPrimary->shardLineGapEm * runFontSize;
        } else if (!hasDefaultCustomFont) {
            runAscent = runFontSize * 0.95f;
            runDescent = runFontSize * 0.25f;
            runLineGap = 0.0f;
        }

        std::size_t cursor = 0u;
        std::uint32_t cp = 0u;
        while (DecodeNextUtf8CodePoint(run.text, cursor, cp)) {
            const std::uint32_t curCpIdx = rawCodePointIdx++;
            if (cp == '\r') {
                continue;
            }
            if (cp == '\n') {
                TokenGlyph nl{};
                nl.cp = '\n';
                nl.sourceCodePointIndex = curCpIdx;
                nl.fontSize = runFontSize;
                nl.ascent = runAscent;
                nl.descent = runDescent;
                nl.lineGap = runLineGap;
                nl.explicitNewline = true;
                decoded.push_back(nl);
                continue;
            }
            const RegisteredFontEntry* chosen = nullptr;
            TokenGlyph tg{};
            tg.cp = cp;
            tg.sourceCodePointIndex = curCpIdx;
            tg.meta = EnsureGlyphFromChainLocked(runChain, cp, &chosen);
            const std::uint32_t mcp = MirrorBracketCodePoint(cp);
            if (mcp != cp) {
                tg.mirroredMeta = EnsureGlyphFromChainLocked(runChain, mcp, nullptr);
                tg.hasMirroredMeta = tg.mirroredMeta.valid;
            }
            tg.fontSize = runFontSize;
            tg.ascent = runAscent;
            tg.descent = runDescent;
            tg.lineGap = runLineGap;
            tg.customRgba = run.fgRgba;
            tg.hasCustomRgba = run.hasFg;
            tg.underline = (run.decorations & 1) != 0;
            tg.strikethrough = (run.decorations & 2) != 0;
            tg.hasCustomFont = (chosen != nullptr);
            if (chosen) {
                tg.fontId = chosen->fontId;
                tg.syntheticBold = wantBold && (chosen->weightClass < 600);
                tg.syntheticItalic = runItalic && !chosen->isItalic;
                if (chosen->face && chosen->face->IsValid()) {
                    retainFace(chosen->face);
                    tg.kernFace = chosen->face.get();
                    tg.upemInvScale = runFontSize / static_cast<float>(chosen->face->UnitsPerEm());
                }
            } else {
                tg.syntheticBold = wantBold;
                tg.syntheticItalic = runItalic;
            }
            decoded.push_back(tg);
        }
    }

    // Arabic contextual joining + GSUB / Presentation Forms-B shaping across decoded tokens
    bool anyArabic = false;
    for (const TokenGlyph& tg : decoded) {
        if (!tg.explicitNewline && tg.cp >= 0x0600u && tg.cp <= 0x06FFu) {
            anyArabic = true;
            break;
        }
    }
    if (anyArabic) {
        const std::size_t dn = decoded.size();
        std::vector<ArabicJoinType> jtypes(dn, ArabicJoinType::NonJoining);
        for (std::size_t i = 0u; i < dn; ++i) {
            if (!decoded[i].explicitNewline) jtypes[i] = ClassifyArabicJoining(decoded[i].cp);
        }
        auto prevJoinType = [&](std::size_t idx) noexcept -> ArabicJoinType {
            for (std::size_t k = idx; k > 0u; --k) {
                if (decoded[k - 1u].explicitNewline) return ArabicJoinType::NonJoining;
                if (jtypes[k - 1u] != ArabicJoinType::Transparent) return jtypes[k - 1u];
            }
            return ArabicJoinType::NonJoining;
        };
        auto nextJoinType = [&](std::size_t idx) noexcept -> ArabicJoinType {
            for (std::size_t k = idx + 1u; k < dn; ++k) {
                if (decoded[k].explicitNewline) return ArabicJoinType::NonJoining;
                if (jtypes[k] != ArabicJoinType::Transparent) return jtypes[k];
            }
            return ArabicJoinType::NonJoining;
        };
        auto findFontById = [&](std::uint32_t fid) -> const RegisteredFontEntry* {
            for (const auto& f : fonts_) {
                if (f.fontId == fid) return &f;
            }
            return nullptr;
        };

        std::vector<std::uint32_t> shapedGids(dn, 0u);
        for (std::size_t i = 0u; i < dn; ++i) {
            TokenGlyph& tg = decoded[i];
            if (tg.explicitNewline || tg.cp < 0x0600u || tg.cp > 0x06FFu) continue;
            const ArabicJoinType curJ = jtypes[i];
            if (curJ == ArabicJoinType::NonJoining || curJ == ArabicJoinType::Transparent || curJ == ArabicJoinType::JoinCausing) {
                shapedGids[i] = tg.meta.glyphIndex;
                continue;
            }
            const bool joinPrev = JoinsToLeft(prevJoinType(i)) && JoinsToRight(curJ);
            const bool joinNext = JoinsToLeft(curJ) && JoinsToRight(nextJoinType(i));
            int form = 0; // 0=isol, 1=init, 2=medi, 3=fina
            if (joinPrev && joinNext) form = 2;
            else if (joinPrev) form = 3;
            else if (joinNext) form = 1;

            const RegisteredFontEntry* fe = findFontById(tg.fontId);
            if (fe && fe->face && fe->face->HasArabicGsub()) {
                static constexpr std::uint32_t kFeatTags[4] = {
                    0x69736F6Cu, // isol
                    0x696E6974u, // init
                    0x6D656469u, // medi
                    0x66696E61u  // fina
                };
                const std::uint32_t baseGid = fe->face->FindGlyphIndex(tg.cp);
                const std::uint32_t subGid = fe->face->ApplyArabicGsubFeature(kFeatTags[form], baseGid);
                shapedGids[i] = subGid;
                if (subGid != baseGid && subGid != 0u) {
                    tg.meta = EnsureGlyphByIndexLocked(*fe, subGid, tg.cp);
                }
            } else if (fe && fe->face) {
                const std::uint32_t presCp = FallbackArabicPresentationFormB(tg.cp, form);
                if (presCp != tg.cp && fe->face->FindGlyphIndex(presCp) != 0u) {
                    tg.meta = EnsureGlyphLocked(fe, presCp);
                    shapedGids[i] = tg.meta.glyphIndex;
                }
            }
        }

        // Apply Lam-Alef & GSUB 2-glyph ligatures ('liga' / 'rlig')
        std::vector<TokenGlyph> compacted;
        compacted.reserve(dn);
        for (std::size_t i = 0u; i < dn; ++i) {
            if (i + 1u < dn && !decoded[i].explicitNewline && !decoded[i + 1u].explicitNewline &&
                decoded[i].fontId == decoded[i + 1u].fontId && decoded[i].fontId != 0u) {
                const RegisteredFontEntry* fe = findFontById(decoded[i].fontId);
                if (fe && fe->face && fe->face->HasArabicGsub() && shapedGids[i] != 0u && shapedGids[i + 1u] != 0u) {
                    const std::uint32_t ligGid = fe->face->ApplyArabicGsubLigature(shapedGids[i], shapedGids[i + 1u]);
                    if (ligGid != 0u) {
                        TokenGlyph ligTg = decoded[i];
                        ligTg.meta = EnsureGlyphByIndexLocked(*fe, ligGid, decoded[i].cp);
                        compacted.push_back(ligTg);
                        ++i;
                        continue;
                    }
                }
                if (decoded[i].cp == 0x0644u &&
                    (decoded[i + 1u].cp == 0x0622u || decoded[i + 1u].cp == 0x0623u ||
                     decoded[i + 1u].cp == 0x0625u || decoded[i + 1u].cp == 0x0627u) &&
                    fe && fe->face) {
                    const bool joinPrev = JoinsToLeft(prevJoinType(i));
                    std::uint32_t lamAlefCp = 0u;
                    switch (decoded[i + 1u].cp) {
                        case 0x0622u: lamAlefCp = joinPrev ? 0xFEF6u : 0xFEF5u; break;
                        case 0x0623u: lamAlefCp = joinPrev ? 0xFEF8u : 0xFEF7u; break;
                        case 0x0625u: lamAlefCp = joinPrev ? 0xFEFAu : 0xFEF9u; break;
                        case 0x0627u: lamAlefCp = joinPrev ? 0xFEFCu : 0xFEFBu; break;
                        default: break;
                    }
                    if (lamAlefCp != 0u && fe->face->FindGlyphIndex(lamAlefCp) != 0u) {
                        TokenGlyph ligTg = decoded[i];
                        ligTg.meta = EnsureGlyphLocked(fe, lamAlefCp);
                        compacted.push_back(ligTg);
                        ++i;
                        continue;
                    }
                }
            }
            compacted.push_back(decoded[i]);
        }
        decoded = std::move(compacted);
    }

    lock.unlock();

    if (decoded.empty()) {
        return result;
    }

    const bool wrapEnabled = (wrapMode != TextWrapping::NoWrap) &&
        std::isfinite(maxWidth) && (maxWidth > 0.0f) && (maxWidth < 500000.0f);
    // Small float tolerance prevents re-wrapping an auto-sized TextBlock arranged to its own DesiredSize.width
    const float wrapLimit = maxWidth + 0.05f;

    result.glyphs.reserve(decoded.size());

    std::uint32_t curLineIdx = 0u;
    float curPenX = 0.0f;
    float curTopY = 0.0f;
    std::size_t lineStartGlyphIdx = 0u;
    std::uint32_t curLineFirstCp = 0u;
    std::size_t lastBreakDecodedIdx = static_cast<std::size_t>(-1);
    std::size_t lastBreakGlyphOutIdx = 0u;
    std::uint32_t prevGlyphIdOnLine = 0xFFFFFFFFu;
    std::uint32_t prevFontIdOnLine = 0u;
    std::vector<std::size_t> lineTokenIndices;
    lineTokenIndices.reserve(64u);

    auto finishLine = [&](std::size_t endGlyphIdx, bool endedByNewline, std::uint32_t nextLineFirstCp) {
        float lineAscent = result.ascent;
        float lineDescent = result.descent;
        float lineGap = result.lineGap;
        if (endGlyphIdx > lineStartGlyphIdx) {
            lineAscent = 0.0f;
            lineDescent = 0.0f;
            lineGap = 0.0f;
            for (std::size_t k = lineStartGlyphIdx; k < endGlyphIdx; ++k) {
                const TokenGlyph& ltg = decoded[lineTokenIndices[k]];
                lineAscent = std::max(lineAscent, ltg.ascent);
                lineDescent = std::max(lineDescent, ltg.descent);
                lineGap = std::max(lineGap, ltg.lineGap);
            }
        }
        const float natLineH = std::max(1.0f, lineAscent + lineDescent + lineGap);
        const float effectiveLineH = (validExplicitLineH > 0.0f)
            ? std::max(natLineH, validExplicitLineH)
            : natLineH;
        const float extraLeading = std::max(0.0f, effectiveLineH - natLineH);
        const float lineBaselineY = curTopY + lineAscent + extraLeading * 0.5f;

        if ((baseRtl || anyArabic) && endGlyphIdx > lineStartGlyphIdx) {
            // Trim trailing wrap spaces before BiDi visual reordering so right edge stays flush
            std::size_t visEnd = endGlyphIdx;
            if (wrapEnabled) {
                while (visEnd > lineStartGlyphIdx && result.glyphs[visEnd - 1u].isWhitespace) {
                    --visEnd;
                }
            }
            const std::size_t visCnt = visEnd - lineStartGlyphIdx;
            if (visCnt > 0u) {
                std::vector<std::uint32_t> lineCps(visCnt);
                for (std::size_t m = 0u; m < visCnt; ++m) {
                    lineCps[m] = decoded[lineTokenIndices[lineStartGlyphIdx + m]].cp;
                }
                std::vector<std::size_t> visOrder;
                std::vector<std::uint8_t> visLevels;
                ComputeUax9LineVisualOrder(lineCps, baseRtl, visOrder, visLevels);

                std::vector<PositionedGlyph> origG(
                    result.glyphs.begin() + static_cast<std::ptrdiff_t>(lineStartGlyphIdx),
                    result.glyphs.begin() + static_cast<std::ptrdiff_t>(visEnd));
                std::vector<std::size_t> origTok(
                    lineTokenIndices.begin() + static_cast<std::ptrdiff_t>(lineStartGlyphIdx),
                    lineTokenIndices.begin() + static_cast<std::ptrdiff_t>(visEnd));

                float rePenX = 0.0f;
                for (std::size_t m = 0u; m < visCnt; ++m) {
                    const std::size_t srcOff = visOrder[m];
                    PositionedGlyph pg = origG[srcOff];
                    const std::size_t tokIdx = origTok[srcOff];
                    const TokenGlyph& ltg = decoded[tokIdx];
                    FontAtlasGlyph gmeta = ltg.meta;
                    if ((visLevels[srcOff] & 1u) != 0u) {
                        const std::uint32_t mcp = MirrorBracketCodePoint(ltg.cp);
                        if (mcp != ltg.cp) {
                            pg.codePoint = mcp;
                            if (ltg.hasMirroredMeta) {
                                gmeta = ltg.mirroredMeta;
                                pg.glyphIndex = gmeta.glyphIndex;
                                pg.u0 = gmeta.u0; pg.v0 = gmeta.v0;
                                pg.u1 = gmeta.u1; pg.v1 = gmeta.v1;
                                pg.cellWidth = gmeta.cellWidth;
                                pg.cellHeight = gmeta.cellHeight;
                                pg.pxRange = gmeta.pxRange;
                                pg.advanceX = gmeta.advanceEm * ltg.fontSize;
                            }
                        }
                    }
                    float trackPx = 0.0f;
                    if (m > 0u && std::isfinite(charSpacing) && charSpacing != 0.0f &&
                        ClassifyBidi(ltg.cp) != BidiClass::NSM) {
                        trackPx = charSpacing * 0.001f * ltg.fontSize;
                    }
                    const float pStart = rePenX + trackPx;
                    pg.penX = pStart;
                    if (ltg.hasCustomFont) {
                        pg.x0 = pStart + gmeta.bearingXEm * ltg.fontSize;
                        pg.x1 = pg.x0 + gmeta.quadWidthEm * ltg.fontSize;
                    } else {
                        pg.x0 = pStart;
                        pg.x1 = pStart + pg.advanceX;
                    }
                    result.glyphs[lineStartGlyphIdx + m] = pg;
                    lineTokenIndices[lineStartGlyphIdx + m] = tokIdx;
                    rePenX = pStart + pg.advanceX;
                }
                for (std::size_t k = visEnd; k < endGlyphIdx; ++k) {
                    result.glyphs[k].penX = rePenX;
                    result.glyphs[k].x0 = rePenX;
                    result.glyphs[k].x1 = rePenX;
                }
            }
        }

        float trimmedW = 0.0f;
        for (std::size_t k = lineStartGlyphIdx; k < endGlyphIdx; ++k) {
            PositionedGlyph& gpg = result.glyphs[k];
            const TokenGlyph& ltg = decoded[lineTokenIndices[k]];
            gpg.baselineY = lineBaselineY;
            if (ltg.hasCustomFont) {
                gpg.y0 = lineBaselineY - ltg.meta.bearingYEm * ltg.fontSize;
                gpg.y1 = gpg.y0 + ltg.meta.quadHeightEm * ltg.fontSize;
            } else {
                gpg.y0 = curTopY;
                gpg.y1 = curTopY + effectiveLineH;
            }
            if (!gpg.isWhitespace || !wrapEnabled) {
                trimmedW = std::max(trimmedW, gpg.penX + gpg.advanceX);
            }
        }
        if (!wrapEnabled && endGlyphIdx > lineStartGlyphIdx) {
            trimmedW = curPenX;
        }
        TextLineMetrics lm{};
        lm.lineIndex = curLineIdx;
        lm.width = trimmedW;
        lm.topY = curTopY;
        lm.baselineY = lineBaselineY;
        lm.ascent = lineAscent;
        lm.descent = lineDescent;
        lm.lineGap = lineGap;
        lm.lineHeight = effectiveLineH;
        lm.firstGlyphIndex = lineStartGlyphIdx;
        lm.glyphCount = endGlyphIdx - lineStartGlyphIdx;
        if (endGlyphIdx > lineStartGlyphIdx) {
            std::uint32_t minCp = result.glyphs[lineStartGlyphIdx].sourceCodePointIndex;
            for (std::size_t k = lineStartGlyphIdx + 1u; k < endGlyphIdx; ++k) {
                minCp = std::min(minCp, result.glyphs[k].sourceCodePointIndex);
            }
            lm.firstCodePointIndex = minCp;
        } else {
            lm.firstCodePointIndex = curLineFirstCp;
        }
        lm.endedByNewline = endedByNewline;
        result.lines.push_back(lm);
        result.totalWidth = std::max(result.totalWidth, lm.width);

        ++curLineIdx;
        curTopY += effectiveLineH;
        curPenX = 0.0f;
        lineStartGlyphIdx = endGlyphIdx;
        curLineFirstCp = nextLineFirstCp;
        lastBreakDecodedIdx = static_cast<std::size_t>(-1);
        prevGlyphIdOnLine = 0xFFFFFFFFu;
        prevFontIdOnLine = 0u;
    };

    for (std::size_t i = 0u; i < decoded.size(); ++i) {
        const TokenGlyph& tg = decoded[i];
        if (tg.explicitNewline) {
            finishLine(result.glyphs.size(), true, tg.sourceCodePointIndex + 1u);
            continue;
        }

        const bool isSpace = (tg.cp == ' ' || tg.cp == '\t');
        float kernPx = 0.0f;
        float trackingPx = 0.0f;
        if (prevGlyphIdOnLine != 0xFFFFFFFFu) {
            if (tg.kernFace && tg.fontId == prevFontIdOnLine) {
                kernPx = tg.kernFace->GetKerningFUnits(prevGlyphIdOnLine, tg.meta.glyphIndex) * tg.upemInvScale;
            }
            if (std::isfinite(charSpacing) && charSpacing != 0.0f) {
                trackingPx = charSpacing * 0.001f * tg.fontSize;
            }
        }
        const float advPx = tg.meta.advanceEm * tg.fontSize;
        const float nextPenX = curPenX + kernPx + trackingPx + advPx;

        // CJK characters can also break BEFORE the ideograph itself
        if (wrapEnabled && IsWideCjkCodePoint(tg.cp) && result.glyphs.size() > lineStartGlyphIdx) {
            lastBreakDecodedIdx = i;
            lastBreakGlyphOutIdx = result.glyphs.size();
        }

        if (wrapEnabled && !isSpace &&
            (result.glyphs.size() > lineStartGlyphIdx) &&
            (nextPenX > wrapLimit)) {
            if (lastBreakDecodedIdx != static_cast<std::size_t>(-1) &&
                lastBreakGlyphOutIdx > lineStartGlyphIdx) {
                // Rewind back to last word/CJK break boundary (save break index before finishLine resets it)
                const std::size_t savedBreakDecodedIdx = lastBreakDecodedIdx;
                result.glyphs.resize(lastBreakGlyphOutIdx);
                lineTokenIndices.resize(lastBreakGlyphOutIdx);
                // Skip leading spaces at the start of the wrapped line
                std::size_t resumeIdx = savedBreakDecodedIdx;
                while (resumeIdx < decoded.size() &&
                       !decoded[resumeIdx].explicitNewline &&
                       (decoded[resumeIdx].cp == ' ' || decoded[resumeIdx].cp == '\t')) {
                    ++resumeIdx;
                }
                const std::uint32_t nextCp = (resumeIdx < decoded.size())
                    ? decoded[resumeIdx].sourceCodePointIndex
                    : rawCodePointIdx;
                finishLine(result.glyphs.size(), false, nextCp);
                i = resumeIdx - 1u;
                continue;
            }
            if (wrapMode == TextWrapping::Wrap) {
                // Emergency character-level break for unbroken words exceeding maxWidth
                finishLine(result.glyphs.size(), false, tg.sourceCodePointIndex);
                kernPx = 0.0f;
                trackingPx = 0.0f;
            }
        }

        const float penStart = curPenX + kernPx + trackingPx;
        const float baselineY = curTopY + tg.ascent;
        PositionedGlyph pg{};
        pg.codePoint = tg.cp;
        pg.glyphIndex = tg.meta.glyphIndex;
        pg.lineIndex = curLineIdx;
        pg.sourceCodePointIndex = tg.sourceCodePointIndex;
        pg.penX = penStart;
        pg.baselineY = baselineY;
        pg.advanceX = advPx;
        pg.pxRange = tg.meta.pxRange;
        pg.cellWidth = tg.meta.cellWidth;
        pg.cellHeight = tg.meta.cellHeight;
        pg.u0 = tg.meta.u0;
        pg.v0 = tg.meta.v0;
        pg.u1 = tg.meta.u1;
        pg.v1 = tg.meta.v1;
        pg.isWhitespace = tg.meta.isWhitespace || isSpace;
        pg.customRgba = tg.customRgba;
        pg.hasCustomRgba = tg.hasCustomRgba;
        pg.syntheticBold = tg.syntheticBold;
        pg.syntheticItalic = tg.syntheticItalic;
        pg.underline = tg.underline;
        pg.strikethrough = tg.strikethrough;

        if (tg.hasCustomFont) {
            pg.x0 = penStart + tg.meta.bearingXEm * tg.fontSize;
            pg.y0 = baselineY - tg.meta.bearingYEm * tg.fontSize;
            pg.x1 = pg.x0 + tg.meta.quadWidthEm * tg.fontSize;
            pg.y1 = pg.y0 + tg.meta.quadHeightEm * tg.fontSize;
        } else {
            pg.x0 = penStart;
            pg.y0 = curTopY;
            pg.x1 = penStart + advPx;
            pg.y1 = curTopY + result.lineHeight;
        }

        result.glyphs.push_back(pg);
        lineTokenIndices.push_back(i);
        curPenX = penStart + advPx;
        prevGlyphIdOnLine = tg.meta.glyphIndex;
        prevFontIdOnLine = tg.fontId;

        if (wrapEnabled && IsWordBreakAfter(tg.cp)) {
            lastBreakDecodedIdx = i + 1u;
            lastBreakGlyphOutIdx = result.glyphs.size();
        }
    }

    finishLine(result.glyphs.size(), true, rawCodePointIdx);

    if (!result.usedTrueTypeMetrics && result.lines.size() == 1u && validExplicitLineH <= 0.0f) {
        // Exact single-line height compatibility with MeasureUtf8TextRun (1.25f * fontSize)
        result.totalHeight = baseFont * 1.25f;
    } else {
        result.totalHeight = curTopY;
    }
    if (trimMode != TextTrimming::None && !result.lines.empty()) {
        const bool trimWActive = std::isfinite(maxWidth) && (maxWidth > 0.0f) && (maxWidth < 500000.0f);
        const bool trimHActive = std::isfinite(maxHeight) && (maxHeight > 0.0f) && (maxHeight < 500000.0f);
        bool verticalOverflow = false;
        std::size_t visibleLineCount = result.lines.size();
        if (trimHActive && result.lines.size() > 1u && result.totalHeight > maxHeight + 0.5f) {
            std::size_t lastVis = 0u;
            for (std::size_t li = 1u; li < result.lines.size(); ++li) {
                if (result.lines[li].topY + result.lines[li].lineHeight <= maxHeight + 0.5f) {
                    lastVis = li;
                } else {
                    break;
                }
            }
            if (lastVis + 1u < result.lines.size()) {
                verticalOverflow = true;
                visibleLineCount = lastVis + 1u;
            }
        }

        bool anyLineNeedsTrim = verticalOverflow;
        if (!anyLineNeedsTrim && trimWActive) {
            for (std::size_t li = 0u; li < visibleLineCount; ++li) {
                if (result.lines[li].width > maxWidth + 0.25f) {
                    anyLineNeedsTrim = true;
                    break;
                }
            }
        }

        if (anyLineNeedsTrim) {
            FontAtlasGlyph defaultDotMeta{};
            {
                std::lock_guard<std::mutex> trimLock(mutex_);
                std::vector<const RegisteredFontEntry*> dotChain;
                ResolveFontChainLocked(defaultFontFamily, baseWeight, defaultItalic, dotChain);
                defaultDotMeta = EnsureGlyphFromChainLocked(dotChain, static_cast<std::uint32_t>('.'), nullptr);
            }

            std::vector<PositionedGlyph> trimmedGlyphs;
            trimmedGlyphs.reserve(result.glyphs.size() + 6u);
            float maxLineW = 0.0f;

            for (std::size_t li = 0u; li < visibleLineCount; ++li) {
                TextLineMetrics& line = result.lines[li];
                const std::size_t gStart = line.firstGlyphIndex;
                const std::size_t gEnd = std::min(gStart + line.glyphCount, result.glyphs.size());
                const std::size_t origCount = (gEnd >= gStart) ? (gEnd - gStart) : 0u;
                const bool forceLastLineEllipsis = verticalOverflow && (li + 1u == visibleLineCount);
                const bool widthOverflow = trimWActive && (line.width > maxWidth + 0.25f);

                if (!forceLastLineEllipsis && !widthOverflow) {
                    line.firstGlyphIndex = trimmedGlyphs.size();
                    for (std::size_t gi = gStart; gi < gEnd; ++gi) {
                        PositionedGlyph g = result.glyphs[gi];
                        g.lineIndex = static_cast<std::uint32_t>(li);
                        trimmedGlyphs.push_back(g);
                    }
                    line.glyphCount = origCount;
                    maxLineW = std::max(maxLineW, line.width);
                    continue;
                }

                float dotFontSize = baseFont;
                bool dotCustomFont = hasDefaultCustomFont;
                std::uint32_t dotCustomRgba = 0u;
                bool dotHasCustomRgba = false;
                bool dotBold = (baseWeight >= 600);
                bool dotItalic = defaultItalic;
                bool dotUnderline = false;
                bool dotStrike = false;
                std::uint32_t dotSrcCp = line.firstCodePointIndex;

                std::size_t rtlOrigEnd = origCount;
                if (baseRtl && wrapEnabled) {
                    while (rtlOrigEnd > 0u && result.glyphs[gStart + rtlOrigEnd - 1u].isWhitespace) {
                        --rtlOrigEnd;
                    }
                }
                std::size_t rtlLeftWs = 0u;
                if (baseRtl) {
                    while (rtlLeftWs < rtlOrigEnd && result.glyphs[gStart + rtlLeftWs].isWhitespace) {
                        ++rtlLeftWs;
                    }
                }

                if (origCount > 0u) {
                    const std::size_t styleIdx = baseRtl
                        ? (rtlLeftWs < rtlOrigEnd ? (gStart + rtlLeftWs) : gStart)
                        : (gEnd - 1u);
                    const PositionedGlyph& tailG = result.glyphs[styleIdx];
                    dotCustomRgba = tailG.customRgba;
                    dotHasCustomRgba = tailG.hasCustomRgba;
                    dotBold = tailG.syntheticBold;
                    dotItalic = tailG.syntheticItalic;
                    dotUnderline = tailG.underline;
                    dotStrike = tailG.strikethrough;
                    dotSrcCp = tailG.sourceCodePointIndex;
                    if (styleIdx < lineTokenIndices.size() && lineTokenIndices[styleIdx] < decoded.size()) {
                        const TokenGlyph& tailTok = decoded[lineTokenIndices[styleIdx]];
                        dotFontSize = tailTok.fontSize;
                        dotCustomFont = tailTok.hasCustomFont;
                    }
                }

                const float trackPx = (std::isfinite(charSpacing) && charSpacing != 0.0f)
                    ? (charSpacing * 0.001f * dotFontSize)
                    : 0.0f;
                const float dotAdv = defaultDotMeta.advanceEm * dotFontSize;
                const float ellipsisW = 3.0f * dotAdv + 2.0f * std::max(0.0f, trackPx);
                const float effLimitW = trimWActive ? maxWidth : (line.width + ellipsisW + 1.0f);
                const float availBeforeEllipsis = std::max(0.0f, effLimitW - ellipsisW);

                auto emitEllipsisDot = [&](float pStart) {
                    PositionedGlyph dg{};
                    dg.codePoint = static_cast<std::uint32_t>('.');
                    dg.glyphIndex = defaultDotMeta.glyphIndex;
                    dg.lineIndex = static_cast<std::uint32_t>(li);
                    dg.sourceCodePointIndex = dotSrcCp;
                    dg.penX = pStart;
                    dg.baselineY = line.baselineY;
                    dg.advanceX = dotAdv;
                    dg.pxRange = defaultDotMeta.pxRange;
                    dg.cellWidth = defaultDotMeta.cellWidth;
                    dg.cellHeight = defaultDotMeta.cellHeight;
                    dg.u0 = defaultDotMeta.u0;
                    dg.v0 = defaultDotMeta.v0;
                    dg.u1 = defaultDotMeta.u1;
                    dg.v1 = defaultDotMeta.v1;
                    dg.isWhitespace = false;
                    dg.customRgba = dotCustomRgba;
                    dg.hasCustomRgba = dotHasCustomRgba;
                    dg.syntheticBold = dotBold;
                    dg.syntheticItalic = dotItalic;
                    dg.underline = dotUnderline;
                    dg.strikethrough = dotStrike;
                    if (dotCustomFont) {
                        dg.x0 = pStart + defaultDotMeta.bearingXEm * dotFontSize;
                        dg.x1 = dg.x0 + defaultDotMeta.quadWidthEm * dotFontSize;
                        dg.y0 = line.baselineY - defaultDotMeta.bearingYEm * dotFontSize;
                        dg.y1 = dg.y0 + defaultDotMeta.quadHeightEm * dotFontSize;
                    } else {
                        dg.x0 = pStart;
                        dg.x1 = pStart + dotAdv;
                        dg.y0 = line.topY;
                        dg.y1 = line.topY + line.lineHeight;
                    }
                    trimmedGlyphs.push_back(dg);
                };

                if (baseRtl && rtlOrigEnd > 0u) {
                    bool endedAtWordBoundary = line.endedByNewline;
                    if (!endedAtWordBoundary) {
                        std::size_t maxTokIdx = 0u;
                        bool hasTok = false;
                        for (std::size_t k = rtlLeftWs; k < rtlOrigEnd; ++k) {
                            if (gStart + k < lineTokenIndices.size() && lineTokenIndices[gStart + k] < decoded.size()) {
                                if (!hasTok || lineTokenIndices[gStart + k] > maxTokIdx) {
                                    maxTokIdx = lineTokenIndices[gStart + k];
                                    hasTok = true;
                                }
                            }
                        }
                        if (hasTok) {
                            if (maxTokIdx + 1u >= decoded.size()) {
                                endedAtWordBoundary = true;
                            } else {
                                const TokenGlyph& nextTok = decoded[maxTokIdx + 1u];
                                endedAtWordBoundary = nextTok.explicitNewline ||
                                    nextTok.cp == ' ' || nextTok.cp == '\t' || IsWideCjkCodePoint(nextTok.cp);
                            }
                        }
                    }

                    std::size_t rtlKeepStart = rtlOrigEnd;
                    if (rtlLeftWs < rtlOrigEnd) {
                        const float rightEdge =
                            result.glyphs[gStart + rtlOrigEnd - 1u].penX +
                            result.glyphs[gStart + rtlOrigEnd - 1u].advanceX;
                        for (std::size_t k = rtlOrigEnd; k > rtlLeftWs; --k) {
                            const std::size_t idx = k - 1u;
                            const float span = rightEdge - result.glyphs[gStart + idx].penX;
                            if (span <= availBeforeEllipsis + 0.05f) {
                                rtlKeepStart = idx;
                            } else {
                                break;
                            }
                        }
                    }

                    if (trimMode == TextTrimming::WordEllipsis) {
                        if (!(rtlKeepStart == rtlLeftWs && endedAtWordBoundary)) {
                            if (rtlKeepStart > rtlLeftWs &&
                                result.glyphs[gStart + rtlKeepStart - 1u].isWhitespace) {
                                // Already aligned on a word boundary to the visual left
                            } else {
                                bool foundWs = false;
                                for (std::size_t k = rtlKeepStart; k < rtlOrigEnd; ++k) {
                                    if (result.glyphs[gStart + k].isWhitespace) {
                                        rtlKeepStart = k + 1u;
                                        foundWs = true;
                                        break;
                                    }
                                }
                                if (!foundWs) {
                                    rtlKeepStart = rtlOrigEnd;
                                }
                            }
                        }
                    }
                    while (rtlKeepStart < rtlOrigEnd && result.glyphs[gStart + rtlKeepStart].isWhitespace) {
                        ++rtlKeepStart;
                    }

                    line.firstGlyphIndex = trimmedGlyphs.size();
                    for (int d = 0; d < 3; ++d) {
                        const float pStart = static_cast<float>(d) * (dotAdv + std::max(0.0f, trackPx));
                        emitEllipsisDot(pStart);
                    }
                    float endRightX = ellipsisW;
                    if (rtlKeepStart < rtlOrigEnd) {
                        const float keepBaseX = ellipsisW + (trackPx != 0.0f ? trackPx : 0.0f);
                        const float shiftX = keepBaseX - result.glyphs[gStart + rtlKeepStart].penX;
                        for (std::size_t k = rtlKeepStart; k < rtlOrigEnd; ++k) {
                            PositionedGlyph g = result.glyphs[gStart + k];
                            g.lineIndex = static_cast<std::uint32_t>(li);
                            g.penX += shiftX;
                            g.x0 += shiftX;
                            g.x1 += shiftX;
                            endRightX = std::max(endRightX, g.penX + g.advanceX);
                            trimmedGlyphs.push_back(g);
                        }
                    }
                    line.glyphCount = (rtlOrigEnd - rtlKeepStart) + 3u;
                    line.width = endRightX;
                    maxLineW = std::max(maxLineW, line.width);
                    continue;
                }

                std::size_t strippedCount = origCount;
                while (strippedCount > 0u && result.glyphs[gStart + strippedCount - 1u].isWhitespace) {
                    --strippedCount;
                }

                bool endedAtWordBoundary = line.endedByNewline;
                if (!endedAtWordBoundary && origCount > 0u && (gStart + strippedCount - 1u) < lineTokenIndices.size()) {
                    const std::size_t lastTokIdx = lineTokenIndices[gStart + strippedCount - 1u];
                    if (lastTokIdx + 1u >= decoded.size()) {
                        endedAtWordBoundary = true;
                    } else {
                        const TokenGlyph& nextTok = decoded[lastTokIdx + 1u];
                        endedAtWordBoundary = nextTok.explicitNewline ||
                            nextTok.cp == ' ' || nextTok.cp == '\t' || IsWideCjkCodePoint(nextTok.cp);
                    }
                }

                std::size_t charKeep = 0u;
                for (std::size_t k = 0u; k < strippedCount; ++k) {
                    const PositionedGlyph& g = result.glyphs[gStart + k];
                    if (g.penX + g.advanceX <= availBeforeEllipsis + 0.05f) {
                        charKeep = k + 1u;
                    } else {
                        break;
                    }
                }

                std::size_t keepCount = charKeep;
                if (trimMode == TextTrimming::WordEllipsis) {
                    if (charKeep == strippedCount && endedAtWordBoundary) {
                        keepCount = charKeep;
                    } else {
                        std::size_t wordKeep = 0u;
                        bool foundWs = false;
                        for (std::size_t k = charKeep; k > 0u; --k) {
                            if (result.glyphs[gStart + k - 1u].isWhitespace) {
                                wordKeep = k - 1u;
                                foundWs = true;
                                break;
                            }
                        }
                        if (!foundWs && charKeep < strippedCount && result.glyphs[gStart + charKeep].isWhitespace) {
                            wordKeep = charKeep;
                            foundWs = true;
                        }
                        if (foundWs) {
                            while (wordKeep > 0u && result.glyphs[gStart + wordKeep - 1u].isWhitespace) {
                                --wordKeep;
                            }
                            keepCount = wordKeep;
                        } else {
                            keepCount = 0u;
                        }
                    }
                } else {
                    while (keepCount > 0u && result.glyphs[gStart + keepCount - 1u].isWhitespace) {
                        --keepCount;
                    }
                }

                line.firstGlyphIndex = trimmedGlyphs.size();
                float ellPenX = 0.0f;
                for (std::size_t k = 0u; k < keepCount; ++k) {
                    PositionedGlyph g = result.glyphs[gStart + k];
                    g.lineIndex = static_cast<std::uint32_t>(li);
                    ellPenX = std::max(ellPenX, g.penX + g.advanceX);
                    trimmedGlyphs.push_back(g);
                }
                if (keepCount > 0u && trackPx != 0.0f) {
                    ellPenX += trackPx;
                }

                for (int d = 0; d < 3; ++d) {
                    const float pStart = ellPenX + static_cast<float>(d) * (dotAdv + std::max(0.0f, trackPx));
                    emitEllipsisDot(pStart);
                }

                line.glyphCount = keepCount + 3u;
                line.width = ellPenX + ellipsisW;
                maxLineW = std::max(maxLineW, line.width);
            }

            result.lines.resize(visibleLineCount);
            result.glyphs = std::move(trimmedGlyphs);
            result.totalWidth = maxLineW;
            result.totalHeight = result.lines.empty()
                ? 0.0f
                : (result.lines.back().topY + result.lines.back().lineHeight);
        }
    }

    return result;
}

void FontAtlas::CopyGlyphRegionRgba8(
    std::uint32_t maxRows,
    std::vector<std::uint8_t>& outRgba8,
    std::uint32_t& outWidth,
    std::uint32_t& outRows,
    std::uint64_t* outRevision) const {
    std::lock_guard<std::mutex> lock(mutex_);
    outWidth = atlasWidth_;
    const std::uint32_t limitRows = std::min<std::uint32_t>(
        maxRows,
        std::min<std::uint32_t>(glyphRegionHeight_, atlasHeight_));
    outRows = limitRows;
    if (outRevision) {
        *outRevision = revision_.load(std::memory_order_relaxed);
    }
    const std::size_t pixelCount = static_cast<std::size_t>(outWidth) * outRows;
    outRgba8.resize(pixelCount * 4u);
    const std::size_t avail = std::min(pixelCount, sdfPixels_.size());
    std::uint8_t* __restrict dst = outRgba8.data();
    const std::uint8_t* __restrict src = sdfPixels_.data();
    for (std::size_t i = 0u; i < avail; ++i) {
        const std::uint8_t sd = src[i];
        const std::size_t base = i * 4u;
        dst[base + 0u] = sd;
        dst[base + 1u] = sd;
        dst[base + 2u] = sd;
        dst[base + 3u] = 255u;
    }
    for (std::size_t i = avail; i < pixelCount; ++i) {
        const std::size_t base = i * 4u;
        dst[base + 0u] = 0u;
        dst[base + 1u] = 0u;
        dst[base + 2u] = 0u;
        dst[base + 3u] = 255u;
    }
}

void FontAtlas::CopyGlyphDirtyBandRgba8(
    std::uint32_t maxRows,
    std::uint64_t& ioKnownRevision,
    std::vector<std::uint8_t>& outRgba8,
    std::uint32_t& outWidth,
    std::uint32_t& outYOffset,
    std::uint32_t& outRows,
    bool& outFullBand) const {
    std::lock_guard<std::mutex> lock(mutex_);
    outWidth = atlasWidth_;
    const std::uint32_t limitRows = std::min<std::uint32_t>(
        maxRows,
        std::min<std::uint32_t>(glyphRegionHeight_, atlasHeight_));
    const std::uint64_t curRev = revision_.load(std::memory_order_relaxed);
    if (limitRows == 0u || outWidth == 0u) {
        outYOffset = 0u;
        outRows = 0u;
        outFullBand = false;
        outRgba8.clear();
        ioKnownRevision = curRev;
        return;
    }

    const bool canPartial =
        (ioKnownRevision != 0u) &&
        hasDirtyRowSpan_ &&
        (ioKnownRevision == dirtyBaseRevision_) &&
        (dirtyRowMax_ > dirtyRowMin_) &&
        (dirtyRowMin_ < limitRows);

    const std::uint32_t y0 = canPartial ? dirtyRowMin_ : 0u;
    const std::uint32_t y1 = canPartial ? std::min<std::uint32_t>(dirtyRowMax_, limitRows) : limitRows;
    outYOffset = y0;
    outRows = y1 - y0;
    outFullBand = !canPartial;

    const std::size_t startTexel = static_cast<std::size_t>(y0) * outWidth;
    const std::size_t pixelCount = static_cast<std::size_t>(outWidth) * outRows;
    outRgba8.resize(pixelCount * 4u);
    std::uint8_t* __restrict dst = outRgba8.data();
    const std::size_t availEnd = std::min(startTexel + pixelCount, sdfPixels_.size());
    const std::size_t copyCount = (availEnd > startTexel) ? (availEnd - startTexel) : 0u;
    const std::uint8_t* __restrict src = sdfPixels_.data() + startTexel;
    for (std::size_t i = 0u; i < copyCount; ++i) {
        const std::uint8_t sd = src[i];
        const std::size_t base = i * 4u;
        dst[base + 0u] = sd;
        dst[base + 1u] = sd;
        dst[base + 2u] = sd;
        dst[base + 3u] = 255u;
    }
    for (std::size_t i = copyCount; i < pixelCount; ++i) {
        const std::size_t base = i * 4u;
        dst[base + 0u] = 0u;
        dst[base + 1u] = 0u;
        dst[base + 2u] = 0u;
        dst[base + 3u] = 255u;
    }

    dirtyBaseRevision_ = curRev;
    hasDirtyRowSpan_ = false;
    dirtyRowMin_ = 0xFFFFFFFFu;
    dirtyRowMax_ = 0u;
    ioKnownRevision = curRev;
}

TextLayoutResult MeasureTextLayout(
    std::string_view utf8Text,
    std::string_view fontFamily,
    float fontSize,
    float maxWidth,
    TextWrapping wrapMode,
    float charSpacing) {
    return FontAtlas::Instance().MeasureLayout(
        utf8Text, fontFamily, fontSize, maxWidth, wrapMode, charSpacing);
}

} // namespace aero
