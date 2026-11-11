#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aero/compiler/AeroCompiler.hpp"
#include "aero/compiler/AxbFormat.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/input/InputEventRouter.hpp"
#include "aero/render/GpuBackend.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

namespace {

int gTotalChecks = 0;
int gFailedChecks = 0;

void CheckImpl(bool cond, const char* expr, const char* msg, int line) {
    ++gTotalChecks;
    if (!cond) {
        ++gFailedChecks;
        std::fprintf(stderr, "[FAIL] line %d: %s (%s)\n", line, msg, expr);
    }
}

#define AERO_P13_CHECK(cond, msg) CheckImpl((cond), #cond, (msg), __LINE__)

struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

Rgba8 SamplePixel(
    const std::vector<std::uint8_t>& buf,
    std::uint32_t width,
    std::uint32_t x,
    std::uint32_t y) {
    const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4u;
    if (idx + 3u >= buf.size()) {
        return {};
    }
    return Rgba8{buf[idx + 0u], buf[idx + 1u], buf[idx + 2u], buf[idx + 3u]};
}

std::string ReadFileText(const char* path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good()) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// ============================================================================
// (a) 3-Stop LinearGradientBrush & RadialGradientBrush + UberShaders + Atlas LUT
// ============================================================================
void TestGradientBrushesAndShaders() {
    using namespace aero;

    // Verify reserved enum ranges strictly match p1-3 spec
    AERO_P13_CHECK(PropId_RadiusX >= 100 && PropId_ContentTemplate <= 119,
                   "BuiltinPropertyId strictly within reserved 100..119 range");
    AERO_P13_CHECK(static_cast<std::uint16_t>(AeroPrimType::SdfEllipse) == 7u &&
                       static_cast<std::uint16_t>(AeroPrimType::GradientRoundedRect) == 8u,
                   "AeroPrimType uses SdfEllipse=7 and GradientRoundedRect=8");

    // Verify all 3 UberShader backends preserve P0-1 bitmasks and implement primType 7 & 8
    for (ShaderBackend backend : {ShaderBackend::DesktopGL33, ShaderBackend::WebGL2, ShaderBackend::WebGPU_WGSL}) {
        const std::string_view vs = ZeroCopyGpuStream::UberVertexShaderSource(backend);
        const std::string_view ps = ZeroCopyGpuStream::UberPixelShaderSource(backend);
        AERO_P13_CHECK(vs.find("0x0007u") != std::string_view::npos &&
                           vs.find("0xFFF8u") != std::string_view::npos &&
                           vs.find("0x0008u") != std::string_view::npos,
                       "UberVertexShader decodes SdfRoundedRect modes + SdfEllipse(7) + GradientRoundedRect(8)");
        AERO_P13_CHECK(ps.find("sdEllipseApprox") != std::string_view::npos &&
                           ps.find("unpackAtlasFloat32") != std::string_view::npos &&
                           ps.find("applyGradientSpread") != std::string_view::npos,
                       "UberPixelShader implements analytical ellipse SDF and multi-stop LUT gradient sampling");
    }

    // Compile XAML with 3-stop LinearGradientBrush & 3-stop RadialGradientBrush
    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<Canvas Width="260" Height="220">
    <Rectangle x:Name="LinearGradRect" Canvas.Left="10" Canvas.Top="10" Width="200" Height="40" RadiusX="6" RadiusY="6">
        <Rectangle.Fill>
            <LinearGradientBrush StartPoint="0,0" EndPoint="1,0">
                <GradientStop Offset="0.0" Color="#FF0000"/>
                <GradientStop Offset="0.5" Color="#00FF00"/>
                <GradientStop Offset="1.0" Color="#0000FF"/>
            </LinearGradientBrush>
        </Rectangle.Fill>
    </Rectangle>
    <Rectangle x:Name="RadialGradRect" Canvas.Left="10" Canvas.Top="70" Width="120" Height="120">
        <Rectangle.Fill>
            <RadialGradientBrush Center="0.5,0.5" GradientOrigin="0.5,0.5" RadiusX="0.5" RadiusY="0.5">
                <GradientStop Offset="0.0" Color="#FFFFFF"/>
                <GradientStop Offset="0.5" Color="#FFFF00"/>
                <GradientStop Offset="1.0" Color="#0000FF"/>
            </RadialGradientBrush>
        </Rectangle.Fill>
    </Rectangle>
</Canvas>
)";
    const AeroCompileResult compiled = compiler.Compile(input);
    AERO_P13_CHECK(compiled.succeeded, "Compiled XAML with 3-stop Linear & Radial GradientBrushes");

    // Round-trip through .axb4 binary format
    const std::vector<std::uint8_t> axbBytes = compiled.axbPackage.Serialize();
    AxbPackage decodedPkg{};
    std::string decodeErr;
    AERO_P13_CHECK(AxbPackage::Deserialize(axbBytes, decodedPkg, &decodeErr),
                   ".axb4 binary serialization round-trips gradient brush specs cleanly");

    auto root = CreateElementByTypeName(decodedPkg.rootTypeName);
    AERO_P13_CHECK(MountAxbTemplate(root.Get(), decodedPkg), "Mounted decoded gradient XAML package");
    root->Measure(Size{260.0f, 220.0f});
    root->Arrange(Rect{0.0f, 0.0f, 260.0f, 220.0f});

    auto* linRect = root->FindName("LinearGradRect")->DynamicCast<Rectangle>();
    auto* radRect = root->FindName("RadialGradRect")->DynamicCast<Rectangle>();
    AERO_P13_CHECK(linRect != nullptr && linRect->GetFillGradientBrushId() > 0u,
                   "LinearGradientBrush bound non-zero gradientBrushId on Rectangle");
    AERO_P13_CHECK(radRect != nullptr && radRect->GetFillGradientBrushId() > 0u,
                   "RadialGradientBrush bound non-zero gradientBrushId on Rectangle");

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());

    // Verify GPU default UI atlas bakes 1D LUT rows for registered brushes
    std::vector<std::uint8_t> uiAtlas;
    BuildDefaultUiAtlasRgba8(tree, 512u, uiAtlas);
    const std::uint32_t linBrushId = linRect->GetFillGradientBrushId();
    const std::uint32_t linRampRow = 512u - 2u - (linBrushId - 1u) * 2u;
    const Rgba8 lutStart = SamplePixel(uiAtlas, 512u, 0u, linRampRow);
    const Rgba8 lutMid = SamplePixel(uiAtlas, 512u, 128u, linRampRow);
    const Rgba8 lutEnd = SamplePixel(uiAtlas, 512u, 255u, linRampRow);
    AERO_P13_CHECK(lutStart.r > 245 && lutStart.g < 10 && lutStart.b < 10 &&
                       lutMid.g > 240 && lutMid.r < 15 && lutMid.b < 15 &&
                       lutEnd.b > 245 && lutEnd.r < 10 && lutEnd.g < 10,
                   "UI Atlas 1D LUT row baked 3-stop Red -> Green -> Blue ramp");

    // Rasterize via ZeroCopyGpuStream::RasterizeTreeToRgba8 and verify smooth pixel transitions
    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 260u, 220u, fb, Color{0.0f, 0.0f, 0.0f, 1.0f});

    // LinearGradientBrush spans X=[10..210], centerline Y=30
    const Rgba8 linP0 = SamplePixel(fb, 260u, 12u, 30u);    // near start (t ~ 0.01) -> Red
    const Rgba8 linP25 = SamplePixel(fb, 260u, 60u, 30u);   // quarter (t = 0.25) -> Red+Green
    const Rgba8 linP50 = SamplePixel(fb, 260u, 110u, 30u);  // midpoint (t = 0.50) -> Green
    const Rgba8 linP75 = SamplePixel(fb, 260u, 160u, 30u);  // three-quarter (t = 0.75) -> Green+Blue
    const Rgba8 linP100 = SamplePixel(fb, 260u, 208u, 30u); // near end (t ~ 0.99) -> Blue

    AERO_P13_CHECK(linP0.r > 235 && linP0.g < 25 && linP0.b < 10,
                   "LinearGradientBrush start pixel is pure Red");
    AERO_P13_CHECK(linP25.r > 105 && linP25.r < 150 && linP25.g > 105 && linP25.g < 150 && linP25.b < 10,
                   "LinearGradientBrush 25% pixel smoothly interpolates Red -> Green");
    AERO_P13_CHECK(linP50.g > 235 && linP50.r < 20 && linP50.b < 20,
                   "LinearGradientBrush midpoint pixel is pure Green");
    AERO_P13_CHECK(linP75.g > 105 && linP75.g < 150 && linP75.b > 105 && linP75.b < 150 && linP75.r < 10,
                   "LinearGradientBrush 75% pixel smoothly interpolates Green -> Blue");
    AERO_P13_CHECK(linP100.b > 235 && linP100.r < 10 && linP100.g < 25,
                   "LinearGradientBrush end pixel is pure Blue");

    // RadialGradientBrush spans X=[10..130], Y=[70..190], center=(70, 130), radius=60
    const Rgba8 radCenter = SamplePixel(fb, 260u, 70u, 130u); // r=0 -> White (#FFFFFF)
    const Rgba8 radMid = SamplePixel(fb, 260u, 100u, 130u);   // r=30 (t=0.5) -> Yellow (#FFFF00)
    const Rgba8 radOuter = SamplePixel(fb, 260u, 128u, 130u); // r=58 (t~0.97) -> Blue (#0000FF)
    const Rgba8 radCorner = SamplePixel(fb, 260u, 15u, 75u);  // r>60 Pad clamped -> Blue (#0000FF)

    AERO_P13_CHECK(radCenter.r > 240 && radCenter.g > 240 && radCenter.b > 240,
                   "RadialGradientBrush center pixel is White");
    AERO_P13_CHECK(radMid.r > 235 && radMid.g > 235 && radMid.b < 30,
                   "RadialGradientBrush mid-radius pixel is Yellow");
    AERO_P13_CHECK(radOuter.b > 215 && radOuter.r < 35 && radOuter.g < 35,
                   "RadialGradientBrush outer rim pixel transitions smoothly to Blue");
    AERO_P13_CHECK(radCorner.b > 240 && radCorner.r < 10 && radCorner.g < 10,
                   "RadialGradientBrush corner beyond radius pads cleanly to final stop Blue");
}

// ============================================================================
// (b) Geometric Primitives: Ellipse (Circle + Asymmetric + AA + Stroke),
//     Rectangle, Line, Polygon & Polyline
// ============================================================================
void TestGeometricPrimitives() {
    using namespace aero;

    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<Canvas Width="320" Height="220">
    <Ellipse x:Name="PureCircle" Canvas.Left="10" Canvas.Top="10" Width="80" Height="80" Fill="#FF2060"/>
    <Ellipse x:Name="StrokedOval" Canvas.Left="110" Canvas.Top="20" Width="120" Height="60"
             Fill="#2080FF" Stroke="#00FFAA" StrokeThickness="4"/>
    <Line x:Name="DiagLine" Canvas.Left="10" Canvas.Top="110" X1="0" Y1="0" X2="80" Y2="40"
          Stroke="#FFCC00" StrokeThickness="4"/>
    <Polygon x:Name="TriPoly" Canvas.Left="120" Canvas.Top="100"
             Points="40,5 75,75 5,75" Fill="#44DD66" Stroke="#FFFFFF" StrokeThickness="2"/>
    <Polyline x:Name="OpenZig" Canvas.Left="220" Canvas.Top="100"
              Points="5,60 40,10 75,60" Stroke="#FF66CC" StrokeThickness="4"/>
</Canvas>
)";
    const AeroCompileResult compiled = compiler.Compile(input);
    AERO_P13_CHECK(compiled.succeeded, "Compiled XAML with Ellipse, Line, Polygon, and Polyline");
    AERO_P13_CHECK(compiled.axbPackage.mtsdfPaths.size() >= 2u,
                   "Polygon and Polyline Points were pre-baked into MTSDF paths at compile time");

    auto root = CreateElementByTypeName(compiled.axbPackage.rootTypeName);
    AERO_P13_CHECK(MountAxbTemplate(root.Get(), compiled.axbPackage), "Mounted geometric primitives package");
    root->Measure(Size{320.0f, 220.0f});
    root->Arrange(Rect{0.0f, 0.0f, 320.0f, 220.0f});

    // Verify dedicated runtime classes (no longer degraded to Border!)
    AERO_P13_CHECK(root->FindName("PureCircle")->IsA<Ellipse>(), "Ellipse instantiates Aero.UI.Ellipse");
    AERO_P13_CHECK(root->FindName("DiagLine")->IsA<Line>(), "Line instantiates Aero.UI.Line");
    AERO_P13_CHECK(root->FindName("TriPoly")->IsA<Polygon>(), "Polygon instantiates Aero.UI.Polygon");
    AERO_P13_CHECK(root->FindName("OpenZig")->IsA<Polyline>(), "Polyline instantiates Aero.UI.Polyline");

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());

    // Rasterize onto transparent clearColor (0,0,0,0) to inspect exact alpha coverage!
    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 320u, 220u, fb, Color{0.0f, 0.0f, 0.0f, 0.0f});

    // 1. PureCircle: box [10..90] x [10..90], center (50, 50), radius 40
    const Rgba8 circleCornerOutside = SamplePixel(fb, 320u, 14u, 14u); // dist = hypot(-35.5,-35.5)-40 = +10.2 -> outside!
    const Rgba8 circleCenterInside = SamplePixel(fb, 320u, 50u, 50u);  // center -> opaque #FF2060
    AERO_P13_CHECK(circleCornerOutside.a == 0u, "Ellipse circle exterior corner is completely transparent");
    AERO_P13_CHECK(circleCenterInside.a == 255u && circleCenterInside.r > 245 && circleCenterInside.b > 80,
                   "Ellipse circle interior center is opaque filled");

    // Check smooth AA coverage along circle right boundary near x = 90, y = 50:
    // At y = 50 (py = 49, sy = 49.5, dy = -0.5), scan px = 88..91 to find smooth AA feather pixel (0 < cov < 1)
    const float covEdge = EvaluateSdfEllipseCoverage(40.0f, 0.0f, 40.0f, 40.0f, 0.0f, false, 1.0f);
    AERO_P13_CHECK(covEdge > 0.45f && covEdge < 0.55f,
                   "EvaluateSdfEllipseCoverage yields smooth ~0.5 AA coverage on exact perimeter");

    bool foundSmoothEdgePixel = false;
    for (std::uint32_t yScan = 20u; yScan <= 80u; ++yScan) {
        for (std::uint32_t xScan = 10u; xScan <= 90u; ++xScan) {
            const Rgba8 p = SamplePixel(fb, 320u, xScan, yScan);
            // BlendPixelOver sets dst[3]=255 when a>0.001 over transparent dst, with dst.r = round(srcR * cov)
            if (p.r > 20u && p.r < 235u) {
                foundSmoothEdgePixel = true;
                break;
            }
        }
        if (foundSmoothEdgePixel) break;
    }
    AERO_P13_CHECK(foundSmoothEdgePixel, "Rasterized Ellipse exhibits smooth anti-aliased edge transition pixels");

    // 2. StrokedOval: box [110..230] x [20..80], center (170, 50), rx=60, ry=30, stroke=4 (#00FFAA)
    const Rgba8 ovalOutside = SamplePixel(fb, 320u, 114u, 24u); // corner of bounding box -> outside ellipse!
    const Rgba8 ovalCenter = SamplePixel(fb, 320u, 170u, 50u);  // interior -> #2080FF
    const Rgba8 ovalTopStroke = SamplePixel(fb, 320u, 170u, 22u); // top stroke ring -> #00FFAA
    AERO_P13_CHECK(ovalOutside.a == 0u, "Asymmetric Ellipse exterior bounding-box corner is transparent");
    AERO_P13_CHECK(ovalCenter.b > 240 && ovalCenter.r < 50, "Asymmetric Ellipse interior filled with #2080FF");
    AERO_P13_CHECK(ovalTopStroke.g > 220 && ovalTopStroke.b > 140 && ovalTopStroke.r < 30,
                   "Asymmetric Ellipse perimeter renders analytical SDF stroke #00FFAA");

    // 3. DiagLine: (10,110) -> (90,150), midpoint (50, 130)
    const Rgba8 lineMid = SamplePixel(fb, 320u, 50u, 130u);
    const Rgba8 lineOff = SamplePixel(fb, 320u, 20u, 148u);
    AERO_P13_CHECK(lineMid.r > 230 && lineMid.g > 180 && lineOff.a == 0u,
                   "Line renders crisp segment along (X1,Y1)->(X2,Y2) and leaves off-segment transparent");

    // 4. Polygon (closed filled triangle centered around (120+40, 100+50) = (160, 150))
    const Rgba8 polyInside = SamplePixel(fb, 320u, 160u, 155u);
    AERO_P13_CHECK(polyInside.g > 180 && polyInside.a == 255u,
                   "Polygon Points rendered closed filled triangle via MTSDF");

    // 5. Polyline (open stroked chevron: apex at (220+40, 100+10)=(260, 110), base center (260, 155) is hollow!)
    const Rgba8 zigApex = SamplePixel(fb, 320u, 260u, 111u);
    const Rgba8 zigHollowInterior = SamplePixel(fb, 320u, 260u, 150u);
    AERO_P13_CHECK(zigApex.r > 200 && zigHollowInterior.a == 0u,
                   "Polyline Points rendered open stroked path without closing or filling interior");
}

// ============================================================================
// (c) UniformGrid 2x3 Exact Coordinates + FirstColumn + Auto Row/Col Deduction
// ============================================================================
void TestUniformGridLayout() {
    using namespace aero;

    auto ug = MakeIntrusive<UniformGrid>();
    ug->SetRows(2);
    ug->SetColumns(3);

    std::vector<IntrusivePtr<Rectangle>> cells;
    for (int i = 0; i < 6; ++i) {
        auto cell = MakeIntrusive<Rectangle>();
        cell->SetFill(Color{0.2f, 0.7f, 0.9f, 1.0f});
        ug->AddChild(cell.Get());
        cells.push_back(cell);
    }

    ug->Measure(Size{300.0f, 200.0f});
    ug->Arrange(Rect{0.0f, 0.0f, 300.0f, 200.0f});

    const Rect expected[6] = {
        Rect{0.0f, 0.0f, 100.0f, 100.0f},
        Rect{100.0f, 0.0f, 100.0f, 100.0f},
        Rect{200.0f, 0.0f, 100.0f, 100.0f},
        Rect{0.0f, 100.0f, 100.0f, 100.0f},
        Rect{100.0f, 100.0f, 100.0f, 100.0f},
        Rect{200.0f, 100.0f, 100.0f, 100.0f},
    };
    for (int i = 0; i < 6; ++i) {
        const Rect actual = cells[static_cast<std::size_t>(i)]->FinalRect();
        AERO_P13_CHECK(std::fabs(actual.x - expected[i].x) < 1e-4f &&
                           std::fabs(actual.y - expected[i].y) < 1e-4f &&
                           std::fabs(actual.width - expected[i].width) < 1e-4f &&
                           std::fabs(actual.height - expected[i].height) < 1e-4f,
                       "UniformGrid 2x3 cell arranged at exact equal-division coordinates");
    }

    // Test FirstColumn=1 with Columns=3 and 5 items -> auto Rows=2, item 0 starts at column 1 (x=100, y=0)
    auto ugFirstCol = MakeIntrusive<UniformGrid>();
    ugFirstCol->SetColumns(3);
    ugFirstCol->SetFirstColumn(1);
    std::vector<IntrusivePtr<Rectangle>> fcCells;
    for (int i = 0; i < 5; ++i) {
        auto c = MakeIntrusive<Rectangle>();
        ugFirstCol->AddChild(c.Get());
        fcCells.push_back(c);
    }
    ugFirstCol->Measure(Size{300.0f, 200.0f});
    ugFirstCol->Arrange(Rect{0.0f, 0.0f, 300.0f, 200.0f});
    AERO_P13_CHECK(std::fabs(fcCells[0]->FinalRect().x - 100.0f) < 1e-4f &&
                       std::fabs(fcCells[0]->FinalRect().y - 0.0f) < 1e-4f &&
                       std::fabs(fcCells[2]->FinalRect().x - 0.0f) < 1e-4f &&
                       std::fabs(fcCells[2]->FinalRect().y - 100.0f) < 1e-4f,
                   "UniformGrid FirstColumn=1 offsets row 0 start column and wraps cleanly to row 1");
}

// ============================================================================
// (d) ProgressBar Exact 50% Proportional Fill & Orientation
// ============================================================================
void TestProgressBar() {
    using namespace aero;

    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<Canvas Width="240" Height="100">
    <ProgressBar x:Name="HalfBar" Canvas.Left="20" Canvas.Top="20"
                 Width="200" Height="24" Minimum="0" Maximum="100" Value="50"
                 Background="#202838" Foreground="#00E676" CornerRadius="6"/>
</Canvas>
)";
    const AeroCompileResult compiled = compiler.Compile(input);
    AERO_P13_CHECK(compiled.succeeded, "Compiled ProgressBar XAML");

    auto root = CreateElementByTypeName(compiled.axbPackage.rootTypeName);
    AERO_P13_CHECK(MountAxbTemplate(root.Get(), compiled.axbPackage), "Mounted ProgressBar XAML");
    root->Measure(Size{240.0f, 100.0f});
    root->Arrange(Rect{0.0f, 0.0f, 240.0f, 100.0f});

    auto* bar = root->FindName("HalfBar")->DynamicCast<ProgressBar>();
    AERO_P13_CHECK(bar != nullptr, "ProgressBar instantiates Aero.UI.ProgressBar");
    const Rect fillRect = bar->ComputeFillBarLocalRect();
    AERO_P13_CHECK(std::fabs(fillRect.width - 100.0f) < 1e-4f &&
                       std::fabs(fillRect.height - 24.0f) < 1e-4f,
                   "ProgressBar at Value=50 (Min=0, Max=100, Width=200) has exact 100px (50%) fill width");

    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());
    AERO_P13_CHECK(bar->RenderNodeSpan() == 2u, "ProgressBar emits track trough + fill bar nodes");
    const RenderNodeDescriptor* fillNode = tree.GetNode(bar->RenderNodeId() + 1u);
    AERO_P13_CHECK(fillNode != nullptr && std::fabs(fillNode->width - 100.0f) < 1e-4f,
                   "ProgressBar PagedRenderTree fill node width is exact 100.0f (50%)");

    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 240u, 100u, fb, Color{0.0f, 0.0f, 0.0f, 1.0f});

    // Bar spans X=[20..220], Y=[20..44]; fill covers X=[20..120), trough covers X=[120..220)
    const Rgba8 leftFilled = SamplePixel(fb, 240u, 118u, 32u);  // x=118 (inside 50% fill) -> bright green
    const Rgba8 rightTrough = SamplePixel(fb, 240u, 122u, 32u); // x=122 (just past 50% fill) -> dark trough
    AERO_P13_CHECK(leftFilled.g > 210 && leftFilled.r < 25,
                   "ProgressBar pixel at x=98px inside bar is filled with Foreground green");
    AERO_P13_CHECK(rightTrough.g < 60 && rightTrough.b < 75,
                   "ProgressBar pixel at x=102px inside bar remains dark track trough");

    // Non-zero Minimum=20, Maximum=120, Value=45 -> ratio = 25/100 = 0.25 -> width = 50px
    bar->SetMinimum(20.0f);
    bar->SetMaximum(120.0f);
    bar->SetProgressValue(45.0f);
    AERO_P13_CHECK(std::fabs(bar->ComputeFillBarLocalRect().width - 50.0f) < 1e-4f,
                   "ProgressBar (Value - Minimum) / (Maximum - Minimum) formula exact for non-zero Minimum");
}

// ============================================================================
// (e) Expander Header Click Collapse/Expand, TabControl Click Page Switching,
//     GroupBox / HeaderedContentControl & Official Gallery XAML Samples
// ============================================================================
void TestExpanderTabControlAndGallerySamples() {
    using namespace aero;

    AeroCompiler compiler;
    AeroCompileInput input{};
    input.xamlContent = R"(
<StackPanel Width="360" Spacing="8">
    <Expander x:Name="DemoExpander" Header="Graphics Settings" Width="320" IsExpanded="False">
        <Rectangle x:Name="ExpanderBodyRect" Height="80" Fill="#FF4466"/>
    </Expander>
    <GroupBox x:Name="DemoGroup" Header="Audio Group" Width="320">
        <Rectangle Height="36" Fill="#33AAFF"/>
    </GroupBox>
    <TabControl x:Name="DemoTabs" Width="320" Height="150">
        <TabItem Header="General">
            <Rectangle x:Name="TabPage0Rect" Fill="#EE2233"/>
        </TabItem>
        <TabItem Header="Network">
            <Rectangle x:Name="TabPage1Rect" Fill="#22DD55"/>
        </TabItem>
        <TabItem Header="Security">
            <Rectangle x:Name="TabPage2Rect" Fill="#3366FF"/>
        </TabItem>
    </TabControl>
</StackPanel>
)";
    const AeroCompileResult compiled = compiler.Compile(input);
    AERO_P13_CHECK(compiled.succeeded, "Compiled composite XAML with Expander, GroupBox, and TabControl");

    auto root = CreateElementByTypeName(compiled.axbPackage.rootTypeName);
    AERO_P13_CHECK(MountAxbTemplate(root.Get(), compiled.axbPackage), "Mounted composite controls XAML");
    root->Measure(Size{360.0f, 500.0f});
    root->Arrange(Rect{0.0f, 0.0f, 360.0f, 500.0f});

    auto* expander = root->FindName("DemoExpander")->DynamicCast<Expander>();
    auto* expBody = root->FindName("ExpanderBodyRect");
    auto* groupBox = root->FindName("DemoGroup")->DynamicCast<GroupBox>();
    auto* tabCtrl = root->FindName("DemoTabs")->DynamicCast<TabControl>();
    auto* tabPage0 = root->FindName("TabPage0Rect");
    auto* tabPage1 = root->FindName("TabPage1Rect");
    auto* tabPage2 = root->FindName("TabPage2Rect");

    AERO_P13_CHECK(expander != nullptr && groupBox != nullptr && tabCtrl != nullptr,
                   "Expander, GroupBox, and TabControl instantiated dedicated C++ classes");

    // GroupBox header + border verification + Title fallback compatibility
    AERO_P13_CHECK(groupBox->GetHeader() == "Audio Group" &&
                       groupBox->BodyBorderLocalRect().height > 36.0f,
                   "GroupBox measures top Header bar and wraps body content inside framed border");
    const UnifiedValue legacyTitleVal = groupBox->GetValue(PropId_Title);
    const auto* legacyTitleStr = std::get_if<StringValue>(&legacyTitleVal);
    AERO_P13_CHECK(legacyTitleStr != nullptr && legacyTitleStr->view() == "Audio Group",
                   "GroupBox Header preserves backward-compatible GetValue(PropId_Title) lookup");

    // 1. Verify Expander initial collapsed state
    const float collapsedHeight = expander->FinalRect().height;
    AERO_P13_CHECK(!expander->IsExpanded() && collapsedHeight >= 26.0f && collapsedHeight <= 40.0f,
                   "Collapsed Expander measures/arranges header height only");
    AERO_P13_CHECK(expBody->FinalRect().height == 0.0f,
                   "Collapsed Expander hides body content rect (height == 0)");

    InputEventRouter router(root.Get());
    PagedRenderTree tree;
    (void)tree.SyncVisualTree(root.Get());

    // Click Expander header bar via InputEventRouter -> expands!
    const Rect expHdr = expander->HeaderLocalRect();
    const Point expClickPt{expHdr.x + 40.0f, expHdr.y + expHdr.height * 0.5f};
    AERO_P13_CHECK(router.DispatchPointerDown(expClickPt, PointerButton::Left, 0),
                   "Clicking Expander header bar handled by Expander");
    (void)router.DispatchPointerUp(expClickPt, PointerButton::Left, 0);

    AERO_P13_CHECK(expander->IsExpanded(), "Clicking Expander header toggled IsExpanded to true");
    root->Measure(Size{360.0f, 500.0f});
    root->Arrange(Rect{0.0f, 0.0f, 360.0f, 500.0f});
    (void)tree.SyncVisualTree(root.Get());

    const float expandedHeight = expander->FinalRect().height;
    AERO_P13_CHECK(expandedHeight >= collapsedHeight + 80.0f && expBody->FinalRect().height >= 80.0f,
                   "Expanded Expander height grows by content height and arranges body content");

    // Click Expander header again -> collapses back!
    const Rect expHdr2 = expander->HeaderLocalRect();
    (void)router.DispatchPointerDown(Point{expHdr2.x + 40.0f, expHdr2.y + 10.0f}, PointerButton::Left, 0);
    (void)router.DispatchPointerUp(Point{expHdr2.x + 40.0f, expHdr2.y + 10.0f}, PointerButton::Left, 0);
    root->Measure(Size{360.0f, 500.0f});
    root->Arrange(Rect{0.0f, 0.0f, 360.0f, 500.0f});
    (void)tree.SyncVisualTree(root.Get());
    AERO_P13_CHECK(!expander->IsExpanded() && std::fabs(expander->FinalRect().height - collapsedHeight) < 1e-3f,
                   "Clicking Expander header again collapses height back to header-only");

    // 2. Verify TabControl initial page 0 and click switching to page 1 & page 2
    AERO_P13_CHECK(tabCtrl->TabCount() == 3u && tabCtrl->GetSelectedIndex() == 0,
                   "TabControl initializes with 3 tabs and SelectedIndex == 0");
    AERO_P13_CHECK(tabPage0->FinalRect().width > 200.0f && tabPage1->FinalRect().width == 0.0f,
                   "TabControl initially arranges Page 0 and collapses inactive Page 1");

    std::vector<std::uint8_t> fb;
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 360u, 500u, fb, Color{0.05f, 0.05f, 0.08f, 1.0f});
    const Rect pageArea = tabCtrl->ContentPanelLocalRect();
    const auto sampleX = static_cast<std::uint32_t>(pageArea.x + pageArea.width * 0.5f);
    const auto sampleY = static_cast<std::uint32_t>(pageArea.y + pageArea.height * 0.5f);
    const Rgba8 page0Pixel = SamplePixel(fb, 360u, sampleX, sampleY);
    AERO_P13_CHECK(page0Pixel.r > 210 && page0Pixel.g < 60,
                   "TabControl renders Page 0 (#EE2233 Red) when SelectedIndex == 0");

    // Click Tab 1 ("Network") header button in top tab strip
    const Rect tab1Hdr = tabCtrl->GetTabHeaderLocalRect(1u);
    const Point tab1Click{tab1Hdr.x + tab1Hdr.width * 0.5f, tab1Hdr.y + tab1Hdr.height * 0.5f};
    AERO_P13_CHECK(router.DispatchPointerDown(tab1Click, PointerButton::Left, 0),
                   "Clicking Tab 1 header in TabControl handled");
    (void)router.DispatchPointerUp(tab1Click, PointerButton::Left, 0);

    AERO_P13_CHECK(tabCtrl->GetSelectedIndex() == 1,
                   "Clicking Tab 1 header switched SelectedIndex to 1");
    root->Measure(Size{360.0f, 500.0f});
    root->Arrange(Rect{0.0f, 0.0f, 360.0f, 500.0f});
    (void)tree.SyncVisualTree(root.Get());

    AERO_P13_CHECK(tabPage0->FinalRect().width == 0.0f && tabPage1->FinalRect().width > 200.0f,
                   "TabControl switched arranged layout from Page 0 to Page 1");
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 360u, 500u, fb, Color{0.05f, 0.05f, 0.08f, 1.0f});
    const Rgba8 page1Pixel = SamplePixel(fb, 360u, sampleX, sampleY);
    AERO_P13_CHECK(page1Pixel.g > 195 && page1Pixel.r < 60,
                   "TabControl renders Page 1 (#22DD55 Green) after tab click");

    // Click Tab 2 ("Security") header button
    const Rect tab2Hdr = tabCtrl->GetTabHeaderLocalRect(2u);
    const Point tab2Click{tab2Hdr.x + tab2Hdr.width * 0.5f, tab2Hdr.y + tab2Hdr.height * 0.5f};
    (void)router.DispatchPointerDown(tab2Click, PointerButton::Left, 0);
    (void)router.DispatchPointerUp(tab2Click, PointerButton::Left, 0);
    root->Measure(Size{360.0f, 500.0f});
    root->Arrange(Rect{0.0f, 0.0f, 360.0f, 500.0f});
    (void)tree.SyncVisualTree(root.Get());
    ZeroCopyGpuStream::RasterizeTreeToRgba8(tree, 360u, 500u, fb, Color{0.05f, 0.05f, 0.08f, 1.0f});
    const Rgba8 page2Pixel = SamplePixel(fb, 360u, sampleX, sampleY);
    AERO_P13_CHECK(tabCtrl->GetSelectedIndex() == 2 && tabPage2->FinalRect().width > 200.0f && page2Pixel.b > 220 && page2Pixel.g < 120,
                   "TabControl renders Page 2 (#3366FF Blue) after clicking Tab 2");

    // 3. Official Gallery Sample XAMLs end-to-end compile + mount + layout + render smoke check
    const char* kSampleFiles[] = {
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/Brushes.xaml",
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/UniformGrid.xaml",
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/ProgressBar.xaml",
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/Expander.xaml",
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/TabControl.xaml",
        "/working_dir/c_1a21a30fb41e1c84/scratch/samples/Gallery/Data/Samples/GroupBox.xaml",
    };
    for (const char* samplePath : kSampleFiles) {
        const std::string xaml = ReadFileText(samplePath);
        if (xaml.empty()) {
            continue;
        }
        AeroCompileInput sIn{};
        sIn.xamlPath = samplePath;
        sIn.xamlContent = xaml;
        const AeroCompileResult sRes = compiler.Compile(sIn);
        AERO_P13_CHECK(sRes.succeeded, samplePath);
        auto sampleRoot = CreateElementByTypeName(sRes.axbPackage.rootTypeName);
        AERO_P13_CHECK(sampleRoot.Get() != nullptr && MountAxbTemplate(sampleRoot.Get(), sRes.axbPackage),
                       "Mounted official Gallery sample cleanly");
        sampleRoot->Measure(Size{1280.0f, 960.0f});
        sampleRoot->Arrange(Rect{0.0f, 0.0f, 1280.0f, 960.0f});
        PagedRenderTree sampleTree;
        const std::size_t nodeCount = sampleTree.BuildFromVisualTree(sampleRoot.Get());
        AERO_P13_CHECK(nodeCount > 0u, "Built non-empty PagedRenderTree for official Gallery sample");
    }
}

} // namespace

int main() {
    TestGradientBrushesAndShaders();
    TestGeometricPrimitives();
    TestUniformGridLayout();
    TestProgressBar();
    TestExpanderTabControlAndGallerySamples();

    std::printf("[p1_3_tests] Passed %d / %d checks.\n",
                gTotalChecks - gFailedChecks,
                gTotalChecks);
    return (gFailedChecks == 0) ? 0 : 1;
}
