#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "aero/binding/DualTrackBindingEngine.hpp"
#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/host/HostDrivers.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

#define AERO_P03_CHECK(cond, msg)                                                  \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " -> "    \
                      << (msg) << " (" #cond ")\n";                                \
            std::exit(1);                                                          \
        }                                                                          \
    } while (false)

namespace aero::tests {

namespace {

std::filesystem::path ResolveSamplesDir() {
    std::error_code ec;
    if (const char* envDir = std::getenv("AERO_SAMPLES_DIR"); envDir && envDir[0] != '\0') {
        const std::filesystem::path p(envDir);
        if (std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
#ifdef AERO_SAMPLES_DIR
    {
        const std::filesystem::path p(AERO_SAMPLES_DIR);
        if (!p.empty() && std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
#endif
    constexpr const char* kCandidates[] = {
        "Samples",
        "samples",
        "../Samples",
        "../samples",
        "../../Samples",
        "../../samples",
        "../../../Samples",
        "../../../samples",
    };
    for (const char* cand : kCandidates) {
        const std::filesystem::path p(cand);
        if (std::filesystem::is_directory(p, ec)) {
            return p;
        }
    }
    return std::filesystem::path("Samples");
}

std::string RobotoFontPath() {
    return (ResolveSamplesDir() / "DataBinding" / "Data" / "Roboto-Regular.ttf").string();
}

std::string AeroMaticsFontPath() {
    return (ResolveSamplesDir() / "HelloWorld" / "Data" / "Aero Matics Regular.ttf").string();
}

std::string PtSansFontPath() {
    return (ResolveSamplesDir() / "Localization" / "Data" / "PTSans-Regular.ttf").string();
}

void PushBE16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
    b.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void PushBE32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
    b.push_back(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
    b.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
    b.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

// Synthesize a minimal valid TrueType font containing a Format 12 cmap mapping U+20000 (SIP CJK) -> glyph 1
std::vector<std::uint8_t> BuildSyntheticFormat12SupplementaryTtf() {
    // 7 mandatory tables: cmap, glyf, head, hhea, hmtx, loca, maxp
    std::vector<std::uint8_t> head(54u, 0u);
    head[0] = 0x00; head[1] = 0x01; // version 1.0
    head[18] = 0x03; head[19] = 0xE8; // unitsPerEm = 1000
    head[50] = 0x00; head[51] = 0x01; // indexToLocFormat = 1 (long)

    std::vector<std::uint8_t> hhea(36u, 0u);
    hhea[0] = 0x00; hhea[1] = 0x01;
    hhea[4] = 0x03; hhea[5] = 0x20; // ascent = 800
    hhea[6] = 0xFF; hhea[7] = 0x38; // descent = -200
    hhea[34] = 0x00; hhea[35] = 0x02; // numberOfHMetrics = 2

    std::vector<std::uint8_t> maxp(6u, 0u);
    maxp[0] = 0x00; maxp[1] = 0x01;
    maxp[4] = 0x00; maxp[5] = 0x02; // numGlyphs = 2

    std::vector<std::uint8_t> hmtx;
    PushBE16(hmtx, 500u); PushBE16(hmtx, 0u);   // gid 0 (.notdef)
    PushBE16(hmtx, 920u); PushBE16(hmtx, 60u);  // gid 1 (U+20000)

    // Simple 1-contour rectangular glyph for gid 1: [60, 100] .. [860, 760]
    std::vector<std::uint8_t> glyf;
    PushBE16(glyf, 1u); // numberOfContours = 1
    PushBE16(glyf, 60u); PushBE16(glyf, 100u); PushBE16(glyf, 860u); PushBE16(glyf, 760u);
    PushBE16(glyf, 3u); // endPtsOfContours[0] = 3 (4 points)
    PushBE16(glyf, 0u); // instructionLength = 0
    // 4 on-curve points using 16-bit signed deltas (flag = 0x01)
    for (int i = 0; i < 4; ++i) glyf.push_back(0x01u);
    // X deltas: +60, +800, 0, -800
    PushBE16(glyf, 60u); PushBE16(glyf, 800u); PushBE16(glyf, 0u); PushBE16(glyf, static_cast<std::uint16_t>(-800));
    // Y deltas: +100, 0, +660, -660
    PushBE16(glyf, 100u); PushBE16(glyf, 0u); PushBE16(glyf, 660u); PushBE16(glyf, static_cast<std::uint16_t>(-660));
    while ((glyf.size() % 4u) != 0u) glyf.push_back(0u);

    std::vector<std::uint8_t> loca;
    PushBE32(loca, 0u); // gid 0 empty [0..0]
    PushBE32(loca, 0u); // gid 1 [0..glyf.size()]
    PushBE32(loca, static_cast<std::uint32_t>(glyf.size()));

    std::vector<std::uint8_t> cmap;
    PushBE16(cmap, 0u); PushBE16(cmap, 1u); // version 0, 1 subtable
    PushBE16(cmap, 3u); PushBE16(cmap, 10u); PushBE32(cmap, 12u); // platform 3, encoding 10, offset 12
    PushBE16(cmap, 12u); PushBE16(cmap, 0u); // format 12, reserved 0
    PushBE32(cmap, 28u); // length = 16 + 1*12 = 28
    PushBE32(cmap, 0u);  // language = 0
    PushBE32(cmap, 1u);  // numGroups = 1
    PushBE32(cmap, 0x20000u); PushBE32(cmap, 0x20000u); PushBE32(cmap, 1u);

    struct Sec { const char* tag; const std::vector<std::uint8_t>* bytes; };
    const Sec secs[7] = {
        {"cmap", &cmap}, {"glyf", &glyf}, {"head", &head}, {"hhea", &hhea},
        {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}
    };

    std::vector<std::uint8_t> ttf;
    PushBE32(ttf, 0x00010000u);
    PushBE16(ttf, 7u);
    PushBE16(ttf, 64u);
    PushBE16(ttf, 2u);
    PushBE16(ttf, 48u);

    std::uint32_t curOff = 12u + 7u * 16u;
    for (const auto& s : secs) {
        ttf.push_back(static_cast<std::uint8_t>(s.tag[0]));
        ttf.push_back(static_cast<std::uint8_t>(s.tag[1]));
        ttf.push_back(static_cast<std::uint8_t>(s.tag[2]));
        ttf.push_back(static_cast<std::uint8_t>(s.tag[3]));
        PushBE32(ttf, 0u);
        PushBE32(ttf, curOff);
        PushBE32(ttf, static_cast<std::uint32_t>(s.bytes->size()));
        curOff += static_cast<std::uint32_t>((s.bytes->size() + 3u) & ~3u);
    }
    for (const auto& s : secs) {
        ttf.insert(ttf.end(), s.bytes->begin(), s.bytes->end());
        while ((ttf.size() % 4u) != 0u) ttf.push_back(0u);
    }
    return ttf;
}

} // namespace

// ============================================================================
// Test 1: Built-in Zero-File ASCII SDF Fallback & Collision-Free Unicode Lookup
// ============================================================================
void TestBuiltInFallbackAsciiSdfAndCollisionFreeLookup() {
    FontAtlas::Instance().ClearCustomFonts();

    // 1. Verify exact metric compatibility when no .ttf is loaded
    const Size asciiMetrics = MeasureUtf8TextRun("ABCD", 20.0f);
    AERO_P03_CHECK(
        std::fabs(asciiMetrics.width - 48.0f) < 1e-4f &&
            std::fabs(asciiMetrics.height - 25.0f) < 1e-4f,
        "Built-in fallback preserves 48x25 size for 'ABCD' at FontSize=20");

    const Size cjkMetrics = MeasureUtf8TextRun("开始星际战役", 20.0f);
    AERO_P03_CHECK(
        std::fabs(cjkMetrics.width - 114.0f) < 1e-4f,
        "Built-in fallback preserves 114.0f width for 6 CJK codepoints at FontSize=20");

    // 2. Verify zero UV hash collisions across 80 distinct non-ASCII / CJK codepoints
    PagedRenderTree tree;
    std::unordered_set<std::uint64_t> seenUvKeys;
    for (std::uint32_t i = 0u; i < 80u; ++i) {
        const std::uint32_t cp = 0x4E00u + i * 37u;
        const GlyphUvRect uv = tree.LookupCodePointGlyph(cp);
        AERO_P03_CHECK(uv.valid && uv.u0 > 0.0f && uv.v1 <= 0.5f,
                       "Fallback CJK glyph UV stays strictly in top half [0.0..0.5) with u0 > 0");
        const auto qU = static_cast<std::uint64_t>(std::llround(uv.u0 * 100000.0f));
        const auto qV = static_cast<std::uint64_t>(std::llround(uv.v0 * 100000.0f));
        const std::uint64_t uvKey = (qU << 32u) | qV;
        AERO_P03_CHECK(seenUvKeys.insert(uvKey).second,
                       "LookupCodePointGlyph assigns unique non-colliding Skyline UVs per codePoint");
    }

    // 3. Render readable ASCII text WITHOUT loading any .ttf file and verify real glyph strokes (not solid boxes!)
    auto canvas = MakeIntrusive<Canvas>();
    canvas->SetWidth(260.0f);
    canvas->SetHeight(60.0f);
    canvas->SetValue(PropId_Background, value::FromColor(Color{0.05f, 0.06f, 0.09f, 1.0f}));

    auto tb = MakeIntrusive<TextBlock>();
    tb->SetText("Hello 2.0!");
    tb->SetFontSize(24.0f);
    tb->SetForeground(Color{1.0f, 1.0f, 1.0f, 1.0f});
    Canvas::SetLeft(tb.Get(), 10.0f);
    Canvas::SetTop(tb.Get(), 12.0f);
    canvas->AddChild(tb.Get());

    canvas->Measure(Size{260.0f, 60.0f});
    canvas->Arrange(Rect{0.0f, 0.0f, 260.0f, 60.0f});
    (void)tree.BuildFromVisualTree(canvas.Get());

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 260u, 60u, rgba);

    std::size_t litPixels = 0u;
    std::size_t bgPixelsInTextBand = 0u;
    for (std::uint32_t y = 12u; y < 42u; ++y) {
        for (std::uint32_t x = 10u; x < 160u; ++x) {
            const std::uint8_t r = rgba[(y * 260u + x) * 4u];
            if (r > 140u) ++litPixels;
            else if (r < 30u) ++bgPixelsInTextBand;
        }
    }
    AERO_P03_CHECK(litPixels > 120u && bgPixelsInTextBand > litPixels,
                   "Zero-TTF fallback renders crisp readable ASCII strokes with open counter spaces rather than solid blocks");

    std::cout << "[PASS] TestBuiltInFallbackAsciiSdfAndCollisionFreeLookup (litPixels="
              << litPixels << ", openBg=" << bgPixelsInTextBand << ")\n";
}

// ============================================================================
// Test 2: Real TrueType (.ttf) Binary Parsing (Roboto, Aero Matics, PTSans & Format 12)
// ============================================================================
void TestTrueTypeBinaryParsingContoursCompoundAndKerning() {
    TrueTypeFontFace roboto;
    std::string err;
    AERO_P03_CHECK(roboto.LoadFromFile(RobotoFontPath(), &err), "Loaded Roboto-Regular.ttf: " + err);
    AERO_P03_CHECK(roboto.UnitsPerEm() == 2048u && roboto.AscentFUnits() > 1500 && roboto.DescentFUnits() < -200,
                   "Roboto head/hhea metrics parsed accurately");

    TrueTypeFontFace aeroMatics;
    AERO_P03_CHECK(aeroMatics.LoadFromFile(AeroMaticsFontPath(), &err), "Loaded Aero Matics Regular.ttf: " + err);
    AERO_P03_CHECK(aeroMatics.UnitsPerEm() == 2048u && aeroMatics.GlyphCount() > 150u,
                   "Aero Matics Regular head/maxp parsed accurately");

    TrueTypeFontFace ptSans;
    AERO_P03_CHECK(ptSans.LoadFromFile(PtSansFontPath(), &err), "Loaded PTSans-Regular.ttf (long loca=1): " + err);
    AERO_P03_CHECK(ptSans.UnitsPerEm() == 1000u && ptSans.GlyphCount() > 500u,
                   "PTSans long-loca format=1 parsed accurately");

    // Verify proportional non-monospace advances ('W' vs 'i', 'M' vs 'l')
    const std::uint32_t robotoW = roboto.FindGlyphIndex(U'W');
    const std::uint32_t robotoI = roboto.FindGlyphIndex(U'i');
    AERO_P03_CHECK(robotoW != 0u && robotoI != 0u, "Roboto cmap Format 4 resolved 'W' and 'i'");
    float advW = 0.0f, lsbW = 0.0f, advI = 0.0f, lsbI = 0.0f;
    roboto.GetGlyphHorizontalMetrics(robotoW, advW, lsbI);
    roboto.GetGlyphHorizontalMetrics(robotoI, advI, lsbW);
    AERO_P03_CHECK(advW > advI * 2.5f,
                   "Roboto hmtx proportional advanceWidth: 'W' is > 2.5x wider than 'i'");

    const std::uint32_t amM = aeroMatics.FindGlyphIndex(U'M');
    const std::uint32_t amI = aeroMatics.FindGlyphIndex(U'i');
    float amAdvM = 0.0f, amAdvI = 0.0f, dummyLsb = 0.0f;
    aeroMatics.GetGlyphHorizontalMetrics(amM, amAdvM, dummyLsb);
    aeroMatics.GetGlyphHorizontalMetrics(amI, amAdvI, dummyLsb);
    AERO_P03_CHECK(amAdvM > amAdvI * 2.0f,
                   "Aero Matics hmtx proportional advanceWidth: 'M' is > 2.0x wider than 'i'");

    // Verify glyf quadratic Bezier outline extraction on curved glyph 'O' and 'g'
    TtfGlyphOutline outlineO{};
    AERO_P03_CHECK(roboto.ExtractGlyphOutline(roboto.FindGlyphIndex(U'O'), outlineO),
                   "Extracted Roboto 'O' glyph outline");
    bool hasQuadBezier = false;
    for (const auto& seg : outlineO.segments) {
        if (seg.isQuadratic) {
            hasQuadBezier = true;
            break;
        }
    }
    AERO_P03_CHECK(!outlineO.isEmptyOutline && outlineO.segments.size() >= 8u && hasQuadBezier,
                   "Roboto 'O' outline contains closed inner/outer quadratic Bezier segments");

    // Verify descender yMin < 0 on 'g'
    TtfGlyphOutline outlineG{};
    AERO_P03_CHECK(roboto.ExtractGlyphOutline(roboto.FindGlyphIndex(U'g'), outlineG) &&
                       outlineG.yMin < -100.0f,
                   "Roboto 'g' outline extends below baseline (yMin < 0)");

    // Verify Compound Glyph recursive assembly (e.g. U+00C9 'É' = 'E' + acute accent)
    TtfGlyphOutline outlineE{}, outlineEAcute{};
    AERO_P03_CHECK(ptSans.ExtractGlyphOutline(ptSans.FindGlyphIndex(U'E'), outlineE) &&
                       ptSans.ExtractGlyphOutline(ptSans.FindGlyphIndex(0x00C9u), outlineEAcute),
                   "Extracted PTSans 'E' and compound 'É' (U+00C9)");
    AERO_P03_CHECK(outlineEAcute.segments.size() > outlineE.segments.size() &&
                       outlineEAcute.yMax > outlineE.yMax + 50.0f,
                   "Compound glyph 'É' recursively combined base 'E' + transformed acute accent above cap height");

    // Verify Format 12 full-Unicode supplementary plane (U+20000) cmap parsing & SDF baking
    const std::vector<std::uint8_t> synthFmt12 = BuildSyntheticFormat12SupplementaryTtf();
    TrueTypeFontFace fmt12Face;
    AERO_P03_CHECK(fmt12Face.LoadFromMemory(synthFmt12.data(), synthFmt12.size(), &err),
                   "Loaded synthetic Format 12 supplementary Unicode TTF");
    const std::uint32_t sipGid = fmt12Face.FindGlyphIndex(0x20000u);
    AERO_P03_CHECK(sipGid == 1u, "cmap Format 12 mapped U+20000 beyond BMP to glyphId=1");
    TtfGlyphOutline sipOutline{};
    AERO_P03_CHECK(fmt12Face.ExtractGlyphOutline(sipGid, sipOutline) && !sipOutline.isEmptyOutline,
                   "Extracted U+20000 outline from synthetic Format 12 TTF");

    // Verify optional kern table parsing on Roboto
    const std::uint32_t gidA = roboto.FindGlyphIndex(U'A');
    const std::uint32_t gidV = roboto.FindGlyphIndex(U'V');
    const std::uint32_t gidT = roboto.FindGlyphIndex(U'T');
    const std::uint32_t gidO = roboto.FindGlyphIndex(U'o');
    const float kernAV = roboto.GetKerningFUnits(gidA, gidV);
    const float kernTo = roboto.GetKerningFUnits(gidT, gidO);
    AERO_P03_CHECK(kernAV < 0.0f || kernTo < 0.0f,
                   "Roboto kern table parsed negative kerning adjustment for 'AV' / 'To'");

    std::cout << "[PASS] TestTrueTypeBinaryParsingContoursCompoundAndKerning (Roboto advW="
              << advW << " vs advI=" << advI << ", kernAV=" << kernAV << ")\n";
}

// ============================================================================
// Test 3: MeasureTextLayout, TextBlock FontFamily & TextWrapping="Wrap"
// ============================================================================
void TestMeasureTextLayoutAndTextBlockWrapping() {
    FontAtlas& atlas = FontAtlas::Instance();
    atlas.ClearCustomFonts();

    std::string err;
    AERO_P03_CHECK(atlas.LoadTrueTypeFontFromFile(RobotoFontPath(), "Roboto", false, &err),
                   "Registered Roboto in FontAtlas: " + err);
    AERO_P03_CHECK(atlas.LoadTrueTypeFontFromFile(AeroMaticsFontPath(), "Aero Matics", false, &err),
                   "Registered Aero Matics in FontAtlas: " + err);

    // 1. Direct MeasureTextLayout proportional advanceX & wrap verification
    const TextLayoutResult narrowRun = MeasureTextLayout("iiiiii", "Roboto", 24.0f);
    const TextLayoutResult wideRun = MeasureTextLayout("WWWWWW", "Roboto", 24.0f);
    AERO_P03_CHECK(narrowRun.usedTrueTypeMetrics && wideRun.usedTrueTypeMetrics,
                   "MeasureTextLayout used loaded Roboto TrueType metrics");
    AERO_P03_CHECK(wideRun.totalWidth > narrowRun.totalWidth * 2.4f,
                   "Proportional layout width of 'WWWWWW' is > 2.4x wider than 'iiiiii'");
    AERO_P03_CHECK(narrowRun.ascent > 0.0f && narrowRun.descent > 0.0f && narrowRun.lines.size() == 1u,
                   "MeasureTextLayout populated Ascent, Descent, and single-line baseline");

    const std::string paragraph =
        "Hello AeroGUI 2.0 dynamic TrueType SDF glyph atlas with automatic word wrapping!";
    const TextLayoutResult singleLine = MeasureTextLayout(
        paragraph, "Roboto", 18.0f, 10000.0f, TextWrapping::NoWrap);
    const TextLayoutResult wrapped = MeasureTextLayout(
        paragraph, "Roboto", 18.0f, 160.0f, TextWrapping::Wrap);

    AERO_P03_CHECK(singleLine.lines.size() == 1u && singleLine.totalWidth > 400.0f,
                   "NoWrap keeps paragraph on 1 wide line");
    AERO_P03_CHECK(wrapped.lines.size() >= 4u &&
                       wrapped.totalWidth <= 160.5f &&
                       wrapped.totalHeight > singleLine.totalHeight * 3.5f,
                   "Wrap breaks paragraph into >= 4 lines within maxWidth=160 and scales totalHeight");
    for (std::size_t ln = 1u; ln < wrapped.lines.size(); ++ln) {
        AERO_P03_CHECK(wrapped.lines[ln].baselineY > wrapped.lines[ln - 1u].baselineY + 10.0f,
                       "Wrapped lines have strictly increasing baselineY coordinates");
    }

    // 2. XAML Compiler + TextBlock MeasureCore + PagedRenderTree multi-line emission
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlPath = "WrapTestView.xaml";
    input.xamlContent = R"(
<StackPanel Width="180" Spacing="6" Padding="10">
    <TextBlock x:Name="NoWrapHeader" FontFamily="./#Aero Matics" FontSize="20"
               TextWrapping="NoWrap" Text="Hello AeroGUI 2.0!" Foreground="#FFFFFF" />
    <TextBlock x:Name="WrappedBody" FontFamily="./#Roboto" FontSize="16"
               TextWrapping="Wrap"
               Text="TrueType quadratic Bezier SDF atlas wraps multi-line text cleanly inside constrained panels."
               Foreground="#88DDFF" />
</StackPanel>
)";
    const AeroCompileResult compiled = compiler.Compile(input);
    AERO_P03_CHECK(compiled.succeeded, "AeroCompiler compiled FontFamily & TextWrapping XAML");

    auto root = CreateElementByTypeName(compiled.axbPackage.rootTypeName);
    AERO_P03_CHECK(MountAxbTemplate(root.Get(), compiled.axbPackage), "Mounted WrapTestView");

    auto* headerTb = root->FindName("NoWrapHeader")->DynamicCast<TextBlock>();
    auto* bodyTb = root->FindName("WrappedBody")->DynamicCast<TextBlock>();
    AERO_P03_CHECK(headerTb != nullptr && bodyTb != nullptr, "Found NoWrapHeader and WrappedBody");
    AERO_P03_CHECK(headerTb->GetFontFamily() == "./#Aero Matics" &&
                       headerTb->GetTextWrapping() == TextWrapping::NoWrap,
                   "NoWrapHeader preserved FontFamily='./#Aero Matics' and NoWrap");
    AERO_P03_CHECK(bodyTb->GetFontFamily() == "./#Roboto" &&
                       bodyTb->GetTextWrapping() == TextWrapping::Wrap,
                   "WrappedBody preserved FontFamily='./#Roboto' and TextWrapping::Wrap");

    root->Measure(Size{180.0f, 600.0f});
    root->Arrange(Rect{0.0f, 0.0f, 180.0f, root->DesiredSize().height});
    const float wrappedHeight = bodyTb->DesiredSize().height;

    // Toggle WrappedBody to NoWrap and verify DesiredSize().height drops to 1 line!
    bodyTb->SetTextWrapping(TextWrapping::NoWrap);
    root->Measure(Size{180.0f, 600.0f});
    const float noWrapHeight = bodyTb->DesiredSize().height;
    AERO_P03_CHECK(wrappedHeight > noWrapHeight * 3.0f,
                   "TextBlock::MeasureCore height increases > 3x under TextWrapping='Wrap' vs 'NoWrap'");

    // Switch back to Wrap, build PagedRenderTree, and verify multi-line glyph Y spread
    bodyTb->SetTextWrapping(TextWrapping::Wrap);
    root->Measure(Size{180.0f, 600.0f});
    root->Arrange(Rect{0.0f, 0.0f, 180.0f, root->DesiredSize().height});

    PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());
    const std::uint32_t bodyFirstNode = bodyTb->RenderNodeId();
    const std::uint16_t bodySpan = bodyTb->RenderNodeSpan();
    AERO_P03_CHECK(bodySpan >= 2u, "80+ glyph wrapped TextBlock spans multiple 4KB CommandPage glyph nodes");

    float minGlyphY = 1e30f;
    float maxGlyphY = -1e30f;
    for (std::uint16_t k = 0u; k < bodySpan; ++k) {
        const RenderNodeDescriptor* nd = tree.GetNode(bodyFirstNode + k);
        if (!nd || nd->primType != AeroPrimType::SdfTextGlyph) continue;
        const CommandPage& pg = tree.GetPage(nd->pageIndex);
        for (std::size_t q = 0u; q < nd->vertexCount / 4u; ++q) {
            const AeroVertex& v0 = pg.vertices[nd->vertexOffsetInPage + q * 4u];
            if (((v0.rgba >> 24u) & 0xFFu) > 0u) {
                minGlyphY = std::min(minGlyphY, v0.y);
                maxGlyphY = std::max(maxGlyphY, v0.y);
            }
        }
    }
    AERO_P03_CHECK((maxGlyphY - minGlyphY) > 45.0f,
                   "PagedRenderTree placed wrapped TextBlock glyphs across multiple vertical rows");

    std::cout << "[PASS] TestMeasureTextLayoutAndTextBlockWrapping (1-line H="
              << noWrapHeight << " -> wrapped H=" << wrappedHeight << ")\n";
}

// ============================================================================
// Test 4: WebFontStreamProvider Binary Shard Export & OnShardDataArrived Unpacking
// ============================================================================
void TestWebFontShardExportUnpackAndDirtyPageInvalidation() {
    FontAtlas& atlas = FontAtlas::Instance();
    atlas.ClearCustomFonts();

    // Step 1: Offline/server-side: load PTSans-Regular.ttf (contains Cyrillic U+0400..U+04FF = shard 0x04)
    // and export compact binary "AGFS" glyph shard for shardId = 0x04!
    std::string err;
    AERO_P03_CHECK(atlas.LoadTrueTypeFontFromFile(PtSansFontPath(), "PTSans", false, &err),
                   "Loaded PTSans-Regular.ttf for shard export: " + err);

    WebFontStreamProvider exporterStream;
    std::vector<std::uint8_t> cyrillicShardBytes;
    AERO_P03_CHECK(exporterStream.ExportShardPayloadForId(0x04u, "PTSans", cyrillicShardBytes),
                   "Exported Cyrillic shard 0x04 (U+0400..U+04FF) into compact AGFS binary payload");
    AERO_P03_CHECK(cyrillicShardBytes.size() > 2000u,
                   "Exported AGFS binary shard contains glyph metric directory + R8 SDF pixels");

    // Step 2: Simulate fresh WebAssembly client with ZERO local .ttf files loaded!
    atlas.ClearCustomFonts();
    AERO_P03_CHECK(!atlas.HasFontFamily("PTSans"), "Cleared local TTFs to simulate Web client startup");

    auto root = MakeIntrusive<Canvas>();
    root->SetWidth(320.0f);
    root->SetHeight(80.0f);
    root->SetValue(PropId_Background, value::FromColor(Color{0.06f, 0.07f, 0.10f, 1.0f}));

    // "Привет" = U+041F, U+0440, U+0438, U+0432, U+0435, U+0442 (all in shard 0x04!)
    auto cyrTb = MakeIntrusive<TextBlock>();
    cyrTb->SetFontFamily("PTSans");
    cyrTb->SetFontSize(26.0f);
    cyrTb->SetForeground(Color{1.0f, 0.95f, 0.4f, 1.0f});
    cyrTb->SetText("Привет");
    Canvas::SetLeft(cyrTb.Get(), 16.0f);
    Canvas::SetTop(cyrTb.Get(), 18.0f);
    root->AddChild(cyrTb.Get());

    root->Measure(Size{320.0f, 80.0f});
    root->Arrange(Rect{0.0f, 0.0f, 320.0f, 80.0f});

    PagedRenderTree renderTree;
    (void)renderTree.BuildFromVisualTree(root.Get());
    const std::uint32_t tbNodeId = cyrTb->RenderNodeId();
    const float uBeforeShard = renderTree.GetPage(renderTree.GetNode(tbNodeId)->pageIndex)
                                   .vertices[renderTree.GetNode(tbNodeId)->vertexOffsetInPage]
                                   .u;

    WebFontStreamProvider clientStream;
    const std::size_t queued = clientStream.InspectTextAndQueueMissingShards("Привет", tbNodeId);
    AERO_P03_CHECK(queued == 1u &&
                       clientStream.GetCodePointShardState(0x041Fu) == ShardLoadState::FetchingAsync,
                   "Queued missing shard 0x04 for Cyrillic text");

    // Verify corrupted AGFS header is safely rejected
    std::vector<std::uint8_t> corruptedShard = cyrillicShardBytes;
    corruptedShard.resize(20u); // Truncated inside 32-byte AGFS header
    AERO_P03_CHECK(!clientStream.OnShardDataArrived(0x04u, corruptedShard, &renderTree),
                   "Truncated AGFS shard safely rejected without state corruption");
    AERO_P03_CHECK(clientStream.GetCodePointShardState(0x041Fu) == ShardLoadState::FetchingAsync,
                   "Shard remains FetchingAsync after rejected payload");

    // Clear dirty pages so we can verify exact page invalidation when valid shard arrives
    renderTree.ClearDirtyPages();
    AERO_P03_CHECK(renderTree.DirtyPageCount() == 0u, "All pages clean prior to shard arrival");

    // Step 3: Deliver real AGFS shard bytes -> unpacks SDF glyphs into FontAtlas & refreshes PagedRenderTree!
    AERO_P03_CHECK(clientStream.OnShardDataArrived(0x04u, cyrillicShardBytes, &renderTree),
                   "OnShardDataArrived unpacked AGFS shard 0x04 into FontAtlas");
    AERO_P03_CHECK(clientStream.GetCodePointShardState(0x041Fu) == ShardLoadState::Loaded &&
                       clientStream.TotalUnpackedGlyphs() > 50u,
                   "Shard 0x04 marked Loaded and unpacked > 50 Cyrillic SDF glyphs");
    AERO_P03_CHECK(renderTree.DirtyPageCount() == 1u &&
                       renderTree.IsPageDirty(renderTree.GetNode(tbNodeId)->pageIndex),
                   "OnShardDataArrived marked the exact owning 4KB CommandPage dirty");

    const float uAfterShard = renderTree.GetPage(renderTree.GetNode(tbNodeId)->pageIndex)
                                  .vertices[renderTree.GetNode(tbNodeId)->vertexOffsetInPage]
                                  .u;
    AERO_P03_CHECK(std::fabs(uAfterShard - uBeforeShard) > 1e-4f,
                   "TextBlock quad UVs hot-updated in-place to newly unpacked shard SDF glyphs");

    // Rasterize unpacked Cyrillic shard text and verify crisp yellow pixels!
    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 320u, 80u, rgba);
    std::size_t yellowPixels = 0u;
    for (std::size_t i = 0u; i < 320u * 80u; ++i) {
        if (rgba[i * 4u + 0u] > 180u && rgba[i * 4u + 1u] > 160u && rgba[i * 4u + 2u] < 130u) {
            ++yellowPixels;
        }
    }
    AERO_P03_CHECK(yellowPixels > 120u,
                   "RasterizeTreeToRgba8 rendered unpacked WebFont shard Cyrillic glyphs with real SDF coverage");

    std::cout << "[PASS] TestWebFontShardExportUnpackAndDirtyPageInvalidation (shardBytes="
              << cyrillicShardBytes.size() << ", unpackedGlyphs=" << clientStream.TotalUnpackedGlyphs()
              << ", litPixels=" << yellowPixels << ")\n";
}

// ============================================================================
// Test 5: RasterizeTreeToRgba8 "Hello AeroGUI 2.0!" Golden Pixel Verification
// ============================================================================
void TestRasterizeHelloAeroGui2WithRealTrueTypeFonts() {
    FontAtlas& atlas = FontAtlas::Instance();
    atlas.ClearCustomFonts();

    std::string err;
    AERO_P03_CHECK(atlas.LoadTrueTypeFontFromFile(AeroMaticsFontPath(), "Aero Matics", true, &err),
                   "Loaded Aero Matics Regular.ttf as default font: " + err);
    AERO_P03_CHECK(atlas.LoadTrueTypeFontFromFile(RobotoFontPath(), "Roboto", false, &err),
                   "Loaded Roboto-Regular.ttf: " + err);

    constexpr std::uint32_t kFbW = 460u;
    constexpr std::uint32_t kFbH = 130u;

    auto root = MakeIntrusive<StackPanel>();
    root->SetWidth(static_cast<float>(kFbW));
    root->SetHeight(static_cast<float>(kFbH));
    root->SetPadding(Thickness{16.0f, 14.0f, 16.0f, 14.0f});
    root->SetSpacing(10.0f);
    root->SetValue(PropId_Background, value::FromColor(Color{0.06f, 0.08f, 0.12f, 1.0f}));

    auto titleTb = MakeIntrusive<TextBlock>();
    titleTb->SetFontFamily("./#Aero Matics");
    titleTb->SetFontSize(30.0f);
    titleTb->SetForeground(Color{0.96f, 0.98f, 1.0f, 1.0f});
    titleTb->SetText("Hello AeroGUI 2.0!");
    root->AddChild(titleTb.Get());

    auto subTb = MakeIntrusive<TextBlock>();
    subTb->SetFontFamily("Roboto");
    subTb->SetFontSize(22.0f);
    subTb->SetForeground(Color{0.30f, 0.85f, 1.0f, 1.0f});
    subTb->SetText("Hello AeroGUI 2.0!");
    root->AddChild(subTb.Get());

    ExecutionDomainContext domains;
    DualTrackBindingEngine bindingEngine(domains);
    DepthBucketLayoutQueue layoutQueue;
    PagedRenderTree renderTree;
    ZeroCopyGpuStream gpuStream(ShaderBackend::DesktopGL33, 64);

    EmbeddedGameHost host(root.Get(), &bindingEngine, &layoutQueue, &renderTree);
    host.SetViewportSize(Size{static_cast<float>(kFbW), static_cast<float>(kFbH)});
    host.TickUi(0.016f, UiPhase_All);

    const auto gpuStats = host.RecordGpuCommands(renderTree, gpuStream);
    AERO_P03_CHECK(gpuStats.drawCallCount == 1u, "Single UberShader draw call for multi-font UI view");

    std::vector<std::uint8_t> rgba;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, kFbW, kFbH, rgba);

    // Count crisp interior pixels & smooth anti-aliased edge pixels in both Aero Matics and Roboto bands
    std::size_t aeroMaticsCorePixels = 0u;
    std::size_t aeroMaticsEdgeAaPixels = 0u;
    for (std::uint32_t y = 10u; y < 56u; ++y) {
        for (std::uint32_t x = 12u; x < kFbW - 12u; ++x) {
            const std::uint8_t r = rgba[(y * kFbW + x) * 4u + 0u];
            if (r > 210u) ++aeroMaticsCorePixels;
            else if (r > 40u && r <= 210u) ++aeroMaticsEdgeAaPixels;
        }
    }

    std::size_t robotoCorePixels = 0u;
    std::size_t robotoEdgeAaPixels = 0u;
    for (std::uint32_t y = 56u; y < 115u; ++y) {
        for (std::uint32_t x = 12u; x < kFbW - 12u; ++x) {
            const std::uint8_t b = rgba[(y * kFbW + x) * 4u + 2u];
            if (b > 200u) ++robotoCorePixels;
            else if (b > 50u && b <= 200u) ++robotoEdgeAaPixels;
        }
    }

    AERO_P03_CHECK(aeroMaticsCorePixels > 500u && aeroMaticsEdgeAaPixels > 100u,
                   "Aero Matics 'Hello AeroGUI 2.0!' rendered crisp core strokes + smooth SDF AA edges");
    AERO_P03_CHECK(robotoCorePixels > 300u && robotoEdgeAaPixels > 80u,
                   "Roboto 'Hello AeroGUI 2.0!' rendered crisp core strokes + smooth SDF AA edges");

    // Verify counter-wound hole inside '0' of "2.0!" in Roboto:
    // Measure exact layout of Roboto "Hello AeroGUI 2.0!" to locate the '0' glyph center!
    const TextLayoutResult robotoLayout = MeasureTextLayout("Hello AeroGUI 2.0!", "Roboto", 22.0f);
    bool verifiedZeroHole = false;
    const Rect subRect = subTb->FinalRect();
    for (const auto& pg : robotoLayout.glyphs) {
        if (pg.codePoint == U'0') {
            const int cx = static_cast<int>( std::lround(subRect.x + 0.5f * (pg.x0 + pg.x1)) );
            const int cy = static_cast<int>( std::lround(subRect.y + 0.5f * (pg.y0 + pg.y1)) );
            if (cx > 5 && cx + 5 < static_cast<int>(kFbW) && cy > 2 && cy + 2 < static_cast<int>(kFbH)) {
                const std::uint8_t centerBlue = rgba[(static_cast<std::size_t>(cy) * kFbW + cx) * 4u + 2u];
                std::uint8_t maxRingBlue = 0u;
                for (int dx = -8; dx <= 8; ++dx) {
                    const std::uint8_t b = rgba[(static_cast<std::size_t>(cy) * kFbW + (cx + dx)) * 4u + 2u];
                    maxRingBlue = std::max(maxRingBlue, b);
                }
                AERO_P03_CHECK(maxRingBlue > 190u && centerBlue < 80u,
                               "Counter-wound inner contour of '0' stays hollow while left/right strokes are lit");
                verifiedZeroHole = true;
            }
            break;
        }
    }
    AERO_P03_CHECK(verifiedZeroHole, "Verified hollow inner counter of glyph '0' in 'Hello AeroGUI 2.0!'");

    // Print compact ASCII preview of the top "Hello" region from Aero Matics RGBA8 framebuffer
    std::cout << "  [Framebuffer Preview: 'Hello' in Aero Matics Regular 30px]\n";
    for (std::uint32_t y = 14u; y <= 44u; y += 2u) {
        std::cout << "  ";
        for (std::uint32_t x = 15u; x < 115u; ++x) {
            const std::uint8_t lum = rgba[(y * kFbW + x) * 4u + 1u];
            const char ch = (lum > 190u) ? '#' : ((lum > 95u) ? '+' : ((lum > 35u) ? '.' : ' '));
            std::cout << ch;
        }
        std::cout << "\n";
    }

    // Reset default font so any subsequent fallback tests start clean
    atlas.ClearCustomFonts();

    std::cout << "[PASS] TestRasterizeHelloAeroGui2WithRealTrueTypeFonts (AeroMatics core="
              << aeroMaticsCorePixels << "/aa=" << aeroMaticsEdgeAaPixels
              << ", Roboto core=" << robotoCorePixels << "/aa=" << robotoEdgeAaPixels << ")\n";
}

} // namespace aero::tests

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    aero::tests::TestBuiltInFallbackAsciiSdfAndCollisionFreeLookup();
    aero::tests::TestTrueTypeBinaryParsingContoursCompoundAndKerning();
    aero::tests::TestMeasureTextLayoutAndTextBlockWrapping();
    aero::tests::TestWebFontShardExportUnpackAndDirtyPageInvalidation();
    aero::tests::TestRasterizeHelloAeroGui2WithRealTrueTypeFonts();

    std::cout << "\nAll AeroGUI 2.0 P0-3 TrueType/SDF FontAtlas, TextWrapping & WebFont Shard tests PASSED 100%!\n";
    return 0;
}
