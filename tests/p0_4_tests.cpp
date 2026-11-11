#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "aero/compiler/AeroCompiler.hpp"
#include "aero/controls/Controls.hpp"
#include "aero/layout/DepthBucketLayoutQueue.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"

namespace {

int gChecksRun = 0;
int gChecksFailed = 0;

void CheckImpl(bool cond, const char* expr, int line, const char* msg) {
    ++gChecksRun;
    if (!cond) {
        ++gChecksFailed;
        std::cerr << "[FAIL] Line " << line << ": " << msg << " (" << expr << ")\n";
    }
}

#define AERO_CHECK(cond, msg) CheckImpl((cond), #cond, __LINE__, (msg))

[[nodiscard]] bool ApproxEq(float a, float b, float eps = 1e-3f) noexcept {
    return std::fabs(a - b) <= eps;
}

constexpr const char* kSamplesDir = "/working_dir/c_1a21a30fb41e1c84/scratch/samples";

// Build a minimal valid uncompressed 24-bit BMP in memory (4x4 quadrant pattern)
std::vector<std::uint8_t> BuildSyntheticBmp24() {
    constexpr std::uint32_t w = 4u;
    constexpr std::uint32_t h = 4u;
    constexpr std::uint32_t rowStride = 12u; // 4 * 3 = 12 (multiple of 4)
    constexpr std::uint32_t pixelBytes = rowStride * h;
    constexpr std::uint32_t fileSize = 54u + pixelBytes;

    std::vector<std::uint8_t> bmp(fileSize, 0u);
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[2] = static_cast<std::uint8_t>(fileSize & 0xFFu);
    bmp[10] = 54u; // pixel data offset
    bmp[14] = 40u; // BITMAPINFOHEADER size
    bmp[18] = static_cast<std::uint8_t>(w);
    bmp[22] = static_cast<std::uint8_t>(h); // positive = bottom-up
    bmp[26] = 1u;  // planes
    bmp[28] = 24u; // 24-bit BGR

    // Bottom-up rows: top row (y=0 in decoded top-down) is stored at srcY = 3
    for (std::uint32_t y = 0u; y < h; ++y) {
        const std::uint32_t srcY = h - 1u - y;
        std::uint8_t* row = bmp.data() + 54u + srcY * rowStride;
        for (std::uint32_t x = 0u; x < w; ++x) {
            const bool right = (x >= 2u);
            const bool bottom = (y >= 2u);
            const std::uint8_t r = right ? 240u : 20u;
            const std::uint8_t g = bottom ? 220u : 30u;
            const std::uint8_t b = (!right && !bottom) ? 250u : 40u;
            row[x * 3u + 0u] = b;
            row[x * 3u + 1u] = g;
            row[x * 3u + 2u] = r;
        }
    }
    return bmp;
}

// Build a minimal valid RLE 32-bit TGA in memory (4x2 top-origin)
std::vector<std::uint8_t> BuildSyntheticRleTga32() {
    std::vector<std::uint8_t> tga(18u, 0u);
    tga[2] = 10u;  // RLE true-color
    tga[12] = 4u;  // width = 4
    tga[14] = 2u;  // height = 2
    tga[16] = 32u; // 32-bit BGRA
    tga[17] = 0x28u; // top-left origin + 8 alpha bits

    // RLE packet 1: 4 pixels of pure red (BGRA = 0, 0, 255, 255)
    tga.push_back(0x83u); // run of 4
    tga.push_back(0u);
    tga.push_back(0u);
    tga.push_back(255u);
    tga.push_back(255u);

    // RLE packet 2: 4 pixels of semi-transparent green (BGRA = 0, 200, 0, 128)
    tga.push_back(0x83u);
    tga.push_back(0u);
    tga.push_back(200u);
    tga.push_back(0u);
    tga.push_back(128u);
    return tga;
}

void TestZeroDependencyImageDecoders() {
    aero::DecodedImage earth{};
    std::string err;
    const std::string earthPath = std::string(kSamplesDir) + "/DataBinding/Data/Images/Earth.png";
    AERO_CHECK(aero::DecodeImageFromFile(earthPath, earth, &err), "Decode Earth.png via pure C++17 PNG+Inflate");
    AERO_CHECK(earth.Valid() && earth.format == aero::ImageFormat::Png, "Earth.png DecodedImage valid PNG");
    AERO_CHECK(earth.width == 256u && earth.height == 256u, "Earth.png dimensions == 256x256");

    // Verify Earth.png outer corner (0,0) is transparent and center (128,128) is opaque non-zero color
    const std::uint8_t cornerAlpha = earth.rgbaPixels[3u];
    const std::size_t centerIdx = (128u * 256u + 128u) * 4u;
    const std::uint8_t centerR = earth.rgbaPixels[centerIdx + 0u];
    const std::uint8_t centerG = earth.rgbaPixels[centerIdx + 1u];
    const std::uint8_t centerB = earth.rgbaPixels[centerIdx + 2u];
    const std::uint8_t centerA = earth.rgbaPixels[centerIdx + 3u];
    AERO_CHECK(cornerAlpha == 0u, "Earth.png corner pixel (0,0) is transparent (alpha == 0)");
    AERO_CHECK(centerA == 255u && (centerR > 0u || centerG > 0u || centerB > 0u),
               "Earth.png center pixel (128,128) is opaque with non-zero RGB");

    std::size_t nonZeroPixels = 0u;
    for (std::size_t i = 0u; i < static_cast<std::size_t>(earth.width) * earth.height; ++i) {
        const std::uint8_t* p = earth.rgbaPixels.data() + i * 4u;
        if ((p[0] | p[1] | p[2] | p[3]) != 0u) {
            ++nonZeroPixels;
        }
    }
    AERO_CHECK(nonZeroPixels > 20000u, "Earth.png contains >20,000 non-zero RGBA pixels");

    // Test 8-bit RGB PNG (QuestLog/Data/Images/Image0.png: 558x225)
    aero::DecodedImage questImg{};
    const std::string questPath = std::string(kSamplesDir) + "/QuestLog/Data/Images/Image0.png";
    AERO_CHECK(aero::DecodeImageFromFile(questPath, questImg, &err), "Decode QuestLog Image0.png (RGB8)");
    AERO_CHECK(questImg.width == 558u && questImg.height == 225u, "Image0.png dimensions == 558x225");
    AERO_CHECK(questImg.rgbaPixels[3u] == 255u, "RGB8 PNG expands alpha to 255");

    // Test 8-bit Palette Colormap PNG (DataBinding/Data/Images/Space.png: 1920x1920)
    aero::DecodedImage spaceImg{};
    const std::string spacePath = std::string(kSamplesDir) + "/DataBinding/Data/Images/Space.png";
    AERO_CHECK(aero::DecodeImageFromFile(spacePath, spaceImg, &err), "Decode Space.png (8-bit PLTE colormap)");
    AERO_CHECK(spaceImg.width == 1920u && spaceImg.height == 1920u, "Space.png dimensions == 1920x1920");

    // Test real Baseline Exif JPEG (BackgroundBlur/Data/Background.jpg: 2508x1252)
    aero::DecodedImage bgJpg{};
    const std::string bgJpgPath = std::string(kSamplesDir) + "/BackgroundBlur/Data/Background.jpg";
    AERO_CHECK(aero::DecodeImageFromFile(bgJpgPath, bgJpg, &err), "Decode BackgroundBlur/Data/Background.jpg");
    AERO_CHECK(bgJpg.Valid() && bgJpg.format == aero::ImageFormat::Jpeg, "Background.jpg DecodedImage valid JPEG");
    AERO_CHECK(bgJpg.width == 2508u && bgJpg.height == 1252u, "Background.jpg dimensions == 2508x1252");

    std::uint64_t lumaSum = 0u;
    std::uint8_t minR = 255u;
    std::uint8_t maxR = 0u;
    const std::size_t jpgTotalPx = static_cast<std::size_t>(bgJpg.width) * bgJpg.height;
    for (std::size_t i = 0u; i < jpgTotalPx; i += 17u) {
        const std::uint8_t r = bgJpg.rgbaPixels[i * 4u + 0u];
        const std::uint8_t g = bgJpg.rgbaPixels[i * 4u + 1u];
        const std::uint8_t b = bgJpg.rgbaPixels[i * 4u + 2u];
        const std::uint8_t a = bgJpg.rgbaPixels[i * 4u + 3u];
        if (a != 255u) {
            AERO_CHECK(false, "JPEG alpha must be 255");
            break;
        }
        minR = std::min(minR, r);
        maxR = std::max(maxR, r);
        lumaSum += static_cast<std::uint64_t>(r) + g + b;
    }
    AERO_CHECK(lumaSum > 100000u && (maxR > minR + 40u),
               "Background.jpg has rich dynamic range and non-zero decoded color content");

    // Test second real Exif JPEG (Inventory/Data/Images/Background.jpg: 1920x1080)
    aero::DecodedImage invBg{};
    const std::string invBgPath = std::string(kSamplesDir) + "/Inventory/Data/Images/Background.jpg";
    AERO_CHECK(aero::DecodeImageFromFile(invBgPath, invBg, &err), "Decode Inventory Background.jpg");
    AERO_CHECK(invBg.width == 1920u && invBg.height == 1080u, "Inventory Background.jpg == 1920x1080");

    // Test BMP & TGA pure C++17 decoders
    const std::vector<std::uint8_t> bmpBytes = BuildSyntheticBmp24();
    aero::DecodedImage bmpImg{};
    AERO_CHECK(aero::DecodeImageFromMemory(bmpBytes.data(), bmpBytes.size(), bmpImg, &err), "Decode 24-bit BMP");
    AERO_CHECK(bmpImg.width == 4u && bmpImg.height == 4u && bmpImg.format == aero::ImageFormat::Bmp,
               "BMP dimensions 4x4");
    AERO_CHECK(bmpImg.rgbaPixels[0] == 20u && bmpImg.rgbaPixels[1] == 30u && bmpImg.rgbaPixels[2] == 250u,
               "BMP bottom-up row flip & BGR->RGBA channel order exact at (0,0)");
    const std::size_t brIdx = (3u * 4u + 3u) * 4u;
    AERO_CHECK(bmpImg.rgbaPixels[brIdx + 0u] == 240u && bmpImg.rgbaPixels[brIdx + 1u] == 220u,
               "BMP bottom-right quadrant color exact at (3,3)");

    const std::vector<std::uint8_t> tgaBytes = BuildSyntheticRleTga32();
    aero::DecodedImage tgaImg{};
    AERO_CHECK(aero::DecodeImageFromMemory(tgaBytes.data(), tgaBytes.size(), tgaImg, &err), "Decode 32-bit RLE TGA");
    AERO_CHECK(tgaImg.width == 4u && tgaImg.height == 2u && tgaImg.format == aero::ImageFormat::Tga,
               "TGA dimensions 4x2");
    AERO_CHECK(tgaImg.rgbaPixels[0] == 255u && tgaImg.rgbaPixels[3] == 255u &&
               tgaImg.rgbaPixels[4u * 4u + 1u] == 200u && tgaImg.rgbaPixels[4u * 4u + 3u] == 128u,
               "TGA RLE BGRA->RGBA & alpha preserved");

    std::cout << "[PASS] TestZeroDependencyImageDecoders\n";
}

void TestRelativeUriAndMeasureCoreNaturalSizing() {
    auto& atlas = aero::ImageAtlas::Instance();
    atlas.Clear();
    atlas.SetBaseDirectory(std::string(kSamplesDir) + "/DataBinding/Data");
    atlas.AddSearchDirectory(std::string(kSamplesDir) + "/QuestLog/Data");

    // Relative XAML URI resolution ("Images/Earth.png") + WPF pack:// URI resolution
    const aero::ImageAtlasEntry* earthEntry = atlas.LoadOrGet("Images/Earth.png");
    AERO_CHECK(earthEntry != nullptr && earthEntry->width == 256u && earthEntry->height == 256u,
               "Relative XAML URI 'Images/Earth.png' resolved against BaseDirectory");
    AERO_CHECK(!earthEntry->isStandalone && earthEntry->textureId == 0u,
               "256x256 Earth.png packed into shared Skyline UI atlas (textureId == 0)");

    const aero::ImageAtlasEntry* packEntry =
        atlas.LoadOrGet("pack://application:,,,/DataBinding;component/Images/Mars.png");
    AERO_CHECK(packEntry != nullptr && packEntry->width == 256u && packEntry->height == 256u,
               "WPF pack://application:,,,/Assembly;component/ URI resolved cleanly");

    // Natural MeasureCore without explicit Width / Height
    auto imgEarth = aero::MakeIntrusive<aero::Image>();
    imgEarth->SetSource("Images/Earth.png");
    imgEarth->Measure(aero::Size{1000000.0f, 1000000.0f});
    AERO_CHECK(ApproxEq(imgEarth->DesiredSize().width, 256.0f) &&
               ApproxEq(imgEarth->DesiredSize().height, 256.0f),
               "Unconstrained Image::MeasureCore reports natural 256x256 size of Earth.png");

    // Non-square Image0.png (558x225) via search directory
    auto imgQuest = aero::MakeIntrusive<aero::Image>();
    imgQuest->SetSource("Images/Image0.png");
    imgQuest->SetStretch(aero::Stretch::Uniform);
    imgQuest->Measure(aero::Size{1000000.0f, 1000000.0f});
    AERO_CHECK(ApproxEq(imgQuest->DesiredSize().width, 558.0f) &&
               ApproxEq(imgQuest->DesiredSize().height, 225.0f),
               "Unconstrained Image::MeasureCore reports natural 558x225 size of Image0.png");

    // Single-axis constrained Width=279 (half of 558) -> proportional Height=112.5 under Stretch::Uniform
    imgQuest->SetWidth(279.0f);
    imgQuest->Measure(aero::Size{800.0f, 600.0f});
    AERO_CHECK(ApproxEq(imgQuest->DesiredSize().width, 279.0f) &&
               ApproxEq(imgQuest->DesiredSize().height, 112.5f, 0.05f),
               "Image with explicit Width=279 and Auto Height computes aspect-preserving DesiredSize.height=112.5");

    // Constrained box {400, 300} with Stretch::None keeps natural 256x256 desired size
    auto imgNone = aero::MakeIntrusive<aero::Image>();
    imgNone->SetSource("Images/Earth.png");
    imgNone->SetStretch(aero::Stretch::None);
    imgNone->Measure(aero::Size{400.0f, 300.0f});
    AERO_CHECK(ApproxEq(imgNone->DesiredSize().width, 256.0f) &&
               ApproxEq(imgNone->DesiredSize().height, 256.0f),
               "Stretch::None in 400x300 constraint preserves natural 256x256 DesiredSize");

    std::cout << "[PASS] TestRelativeUriAndMeasureCoreNaturalSizing\n";
}

void TestStretchModesGeometryAndUvCropping() {
    auto& atlas = aero::ImageAtlas::Instance();
    atlas.Clear();
    atlas.SetBaseDirectory(std::string(kSamplesDir) + "/DataBinding/Data");

    const aero::ImageAtlasEntry* earth = atlas.LoadOrGet("Images/Earth.png");
    AERO_CHECK(earth != nullptr, "Loaded Earth.png into Skyline atlas");
    const float bu0 = earth->u0;
    const float bv0 = earth->v0;
    const float bu1 = earth->u1;
    const float bv1 = earth->v1;
    const float spanU = bu1 - bu0;
    const float spanV = bv1 - bv0;

    auto verifyModeInTree = [&](aero::Stretch mode, float boxW, float boxH) -> aero::RenderNodeDescriptor {
        auto root = aero::MakeIntrusive<aero::Canvas>();
        root->SetWidth(500.0f);
        root->SetHeight(400.0f);

        auto img = aero::MakeIntrusive<aero::Image>();
        img->SetSource("Images/Earth.png");
        img->SetStretch(mode);
        img->SetWidth(boxW);
        img->SetHeight(boxH);
        aero::Canvas::SetLeft(img.Get(), 20.0f);
        aero::Canvas::SetTop(img.Get(), 30.0f);
        root->AddChild(img.Get());

        root->Measure(aero::Size{500.0f, 400.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 500.0f, 400.0f});

        aero::PagedRenderTree tree;
        (void)tree.BuildFromVisualTree(root.Get());
        const std::uint32_t imgNid = img->RenderNodeId();
        const aero::RenderNodeDescriptor* nd = tree.GetNode(imgNid);
        AERO_CHECK(nd != nullptr && nd->primType == aero::AeroPrimType::TexturedQuad,
                   "Emitted TexturedQuad RenderNodeDescriptor for Image");
        return nd ? *nd : aero::RenderNodeDescriptor{};
    };

    // 1. Stretch::None inside larger 400x300 box -> centered 256x256 quad, uncropped atlas UV
    {
        const aero::RenderNodeDescriptor nd = verifyModeInTree(aero::Stretch::None, 400.0f, 300.0f);
        AERO_CHECK(ApproxEq(nd.x, 20.0f + (400.0f - 256.0f) * 0.5f) &&
                   ApproxEq(nd.y, 30.0f + (300.0f - 256.0f) * 0.5f) &&
                   ApproxEq(nd.width, 256.0f) && ApproxEq(nd.height, 256.0f),
                   "Stretch::None (400x300 box) centers 256x256 natural quad");
        AERO_CHECK(ApproxEq(nd.uv0X, bu0) && ApproxEq(nd.uv0Y, bv0) &&
                   ApproxEq(nd.uv1X, bu1) && ApproxEq(nd.uv1Y, bv1),
                   "Stretch::None (400x300 box) uses full sprite UV range");
    }

    // 2. Stretch::None inside smaller 128x64 box -> clamped 128x64 quad + centered UV crop (0 Scissor page!)
    {
        const aero::RenderNodeDescriptor nd = verifyModeInTree(aero::Stretch::None, 128.0f, 64.0f);
        AERO_CHECK(ApproxEq(nd.x, 20.0f) && ApproxEq(nd.y, 30.0f) &&
                   ApproxEq(nd.width, 128.0f) && ApproxEq(nd.height, 64.0f),
                   "Stretch::None (128x64 smaller box) clamps destRect to 128x64 without spilling");
        AERO_CHECK(ApproxEq(nd.uv0X, bu0 + 0.25f * spanU) &&
                   ApproxEq(nd.uv1X, bu0 + 0.75f * spanU) &&
                   ApproxEq(nd.uv0Y, bv0 + 0.375f * spanV) &&
                   ApproxEq(nd.uv1Y, bv0 + 0.625f * spanV),
                   "Stretch::None (128x64 smaller box) crops center 50%x25% UVs at 1:1 texel density");
    }

    // 3. Stretch::Fill inside 400x200 box -> full 400x200 quad, full atlas UV
    {
        const aero::RenderNodeDescriptor nd = verifyModeInTree(aero::Stretch::Fill, 400.0f, 200.0f);
        AERO_CHECK(ApproxEq(nd.x, 20.0f) && ApproxEq(nd.y, 30.0f) &&
                   ApproxEq(nd.width, 400.0f) && ApproxEq(nd.height, 200.0f),
                   "Stretch::Fill fills entire 400x200 box");
        AERO_CHECK(ApproxEq(nd.uv0X, bu0) && ApproxEq(nd.uv0Y, bv0) &&
                   ApproxEq(nd.uv1X, bu1) && ApproxEq(nd.uv1Y, bv1),
                   "Stretch::Fill maps full sprite UV range");
    }

    // 4. Stretch::Uniform inside 400x200 box -> pillarboxed 200x200 quad at x = 20 + 100 = 120, full UV
    {
        const aero::RenderNodeDescriptor nd = verifyModeInTree(aero::Stretch::Uniform, 400.0f, 200.0f);
        AERO_CHECK(ApproxEq(nd.x, 120.0f) && ApproxEq(nd.y, 30.0f) &&
                   ApproxEq(nd.width, 200.0f) && ApproxEq(nd.height, 200.0f),
                   "Stretch::Uniform letterboxes/pillarboxes 256x256 into centered 200x200 inside 400x200");
        AERO_CHECK(ApproxEq(nd.uv0X, bu0) && ApproxEq(nd.uv0Y, bv0) &&
                   ApproxEq(nd.uv1X, bu1) && ApproxEq(nd.uv1Y, bv1),
                   "Stretch::Uniform preserves full sprite UV range");
    }

    // 5. Stretch::UniformToFill inside 400x200 box -> full 400x200 quad, vertical UV cropped to [0.25, 0.75], 0 Scissor!
    {
        auto root = aero::MakeIntrusive<aero::Canvas>();
        root->SetWidth(500.0f);
        root->SetHeight(400.0f);

        auto img = aero::MakeIntrusive<aero::Image>();
        img->SetSource("Images/Earth.png");
        img->SetStretch(aero::Stretch::UniformToFill);
        img->SetWidth(400.0f);
        img->SetHeight(200.0f);
        aero::Canvas::SetLeft(img.Get(), 20.0f);
        aero::Canvas::SetTop(img.Get(), 30.0f);
        root->AddChild(img.Get());

        root->Measure(aero::Size{500.0f, 400.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 500.0f, 400.0f});

        aero::PagedRenderTree tree;
        (void)tree.BuildFromVisualTree(root.Get());
        AERO_CHECK(tree.PageCount() == 1u &&
                   (tree.GetPage(0).header.flags & aero::CommandPageFlag_ScissorClipActive) == 0u,
                   "Stretch::UniformToFill requires 0 extra GPU Scissor CommandPage splits");

        const aero::RenderNodeDescriptor* nd = tree.GetNode(img->RenderNodeId());
        AERO_CHECK(nd != nullptr &&
                   ApproxEq(nd->x, 20.0f) && ApproxEq(nd->y, 30.0f) &&
                   ApproxEq(nd->width, 400.0f) && ApproxEq(nd->height, 200.0f),
                   "Stretch::UniformToFill fills exact 400x200 target rect");
        AERO_CHECK(nd != nullptr &&
                   ApproxEq(nd->uv0X, bu0) && ApproxEq(nd->uv1X, bu1) &&
                   ApproxEq(nd->uv0Y, bv0 + 0.25f * spanV) &&
                   ApproxEq(nd->uv1Y, bv0 + 0.75f * spanV),
                   "Stretch::UniformToFill crops vertical UV to centered [0.25, 0.75] span");
    }

    std::cout << "[PASS] TestStretchModesGeometryAndUvCropping\n";
}

void TestAtlasBatchMergingAndMultiTextureSplitting() {
    auto& atlas = aero::ImageAtlas::Instance();
    atlas.Clear();
    atlas.AddSearchDirectory(std::string(kSamplesDir) + "/DataBinding/Data");
    atlas.AddSearchDirectory(std::string(kSamplesDir) + "/Inventory/Data");
    atlas.AddSearchDirectory(std::string(kSamplesDir) + "/BackgroundBlur/Data");

    // 1. Multiple small/medium sprites in shared Skyline UI atlas -> 1 single CommandPage & 1 DrawCall batch!
    {
        auto root = aero::MakeIntrusive<aero::StackPanel>();
        root->SetOrientation(aero::Orientation::Horizontal);
        root->SetWidth(800.0f);
        root->SetHeight(200.0f);

        const char* smallIcons[4] = {
            "Images/Items/Axe.png",
            "Images/Items/KnightHelmet.png",
            "Images/Earth.png",
            "Images/Jupiter.png"
        };
        for (const char* uri : smallIcons) {
            auto icon = aero::MakeIntrusive<aero::Image>();
            icon->SetSource(uri);
            icon->SetWidth(64.0f);
            icon->SetHeight(64.0f);
            root->AddChild(icon.Get());
        }
        auto label = aero::MakeIntrusive<aero::TextBlock>();
        label->SetText("Inventory & Planets");
        root->AddChild(label.Get());

        root->Measure(aero::Size{800.0f, 200.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 800.0f, 200.0f});

        aero::PagedRenderTree tree;
        (void)tree.BuildFromVisualTree(root.Get());

        aero::ZeroCopyGpuStream gpuStream(aero::ShaderBackend::DesktopGL33, 64);
        const aero::ZeroCopySubmissionStats stats = gpuStream.SubmitDirtyPages(tree);

        AERO_CHECK(atlas.StandaloneTextureCount() == 0u,
                   "All 4 small/medium sprites packed into shared Skyline UI atlas (0 standalone textures)");
        AERO_CHECK(tree.PageCount() == 1u && tree.GetPage(0).header.reserved[0] == 0u,
                   "Panel + 4 Skyline atlas Images + TextBlock share 1 single 4KB CommandPage (textureId == 0)");
        AERO_CHECK(stats.drawCallCount == 1u &&
                   gpuStream.LastClipDrawBatches().size() == 1u &&
                   gpuStream.LastClipDrawBatches()[0].textureId == 0u,
                   "Multiple small atlas images coalesce into 1 GpuClipDrawBatch (0 DrawCall break)");
    }

    // 2. Large standalone textures + trailing shared-atlas small icons -> automatic textureId page & batch splitting!
    {
        const aero::ImageAtlasEntry* bgEntry = atlas.LoadOrGet("Background.jpg"); // 2508x1252 standalone
        const aero::ImageAtlasEntry* charEntry = atlas.LoadOrGet("Images/Character.png"); // 531x1200 standalone
        const aero::ImageAtlasEntry* axeEntry = atlas.LoadOrGet("Images/Items/Axe.png"); // 62x62 atlas (0)
        const aero::ImageAtlasEntry* bootsEntry = atlas.LoadOrGet("Images/Items/Boots.png"); // 62x62 atlas (0)

        AERO_CHECK(bgEntry != nullptr && bgEntry->isStandalone && bgEntry->textureId > 0u,
                   "2508x1252 Background.jpg assigned standalone textureId > 0");
        AERO_CHECK(charEntry != nullptr && charEntry->isStandalone && charEntry->textureId > 0u &&
                   charEntry->textureId != bgEntry->textureId,
                   "531x1200 Character.png assigned distinct standalone textureId");
        AERO_CHECK(axeEntry != nullptr && !axeEntry->isStandalone && axeEntry->textureId == 0u &&
                   bootsEntry != nullptr && !bootsEntry->isStandalone && bootsEntry->textureId == 0u,
                   "62x62 Axe.png & Boots.png share textureId == 0");

        auto root = aero::MakeIntrusive<aero::Canvas>();
        root->SetWidth(1280.0f);
        root->SetHeight(720.0f);
        // Make root Canvas transparent/unrendered? Note Canvas emits 1 background quad on textureId 0 first!
        auto imgBg = aero::MakeIntrusive<aero::Image>();
        imgBg->SetSource("Background.jpg");
        imgBg->SetStretch(aero::Stretch::UniformToFill);
        imgBg->SetWidth(1280.0f);
        imgBg->SetHeight(720.0f);
        root->AddChild(imgBg.Get());

        auto imgChar = aero::MakeIntrusive<aero::Image>();
        imgChar->SetSource("Images/Character.png");
        imgChar->SetStretch(aero::Stretch::Uniform);
        imgChar->SetWidth(300.0f);
        imgChar->SetHeight(600.0f);
        root->AddChild(imgChar.Get());

        auto imgAxe = aero::MakeIntrusive<aero::Image>();
        imgAxe->SetSource("Images/Items/Axe.png");
        imgAxe->SetWidth(62.0f);
        imgAxe->SetHeight(62.0f);
        root->AddChild(imgAxe.Get());

        auto imgBoots = aero::MakeIntrusive<aero::Image>();
        imgBoots->SetSource("Images/Items/Boots.png");
        imgBoots->SetWidth(62.0f);
        imgBoots->SetHeight(62.0f);
        aero::Canvas::SetLeft(imgBoots.Get(), 70.0f);
        root->AddChild(imgBoots.Get());

        root->Measure(aero::Size{1280.0f, 720.0f});
        root->Arrange(aero::Rect{0.0f, 0.0f, 1280.0f, 720.0f});

        aero::PagedRenderTree tree;
        (void)tree.BuildFromVisualTree(root.Get());

        aero::ZeroCopyGpuStream gpuStream(aero::ShaderBackend::DesktopGL33, 64);
        const aero::ZeroCopySubmissionStats stats = gpuStream.SubmitDirtyPages(tree);
        const auto& batches = gpuStream.LastClipDrawBatches();

        // Expected batches:
        // Batch 0: Canvas root node (textureId = 0)
        // Batch 1: Background.jpg (textureId = bgEntry->textureId)
        // Batch 2: Character.png (textureId = charEntry->textureId)
        // Batch 3: Axe.png + Boots.png merged together! (textureId = 0, vertexCount = 8)
        AERO_CHECK(stats.drawCallCount == 4u && batches.size() == 4u,
                   "Distinct large textures split batches while trailing small atlas images merge");
        if (batches.size() == 4u) {
            AERO_CHECK(batches[0].textureId == 0u &&
                       batches[1].textureId == bgEntry->textureId &&
                       batches[2].textureId == charEntry->textureId &&
                       batches[3].textureId == 0u &&
                       batches[3].vertexCount == 8u,
                       "Batch textureIds match [0, bgTexId, charTexId, 0 (merged 2 icons = 8 verts)]");
        }

        // Also test incremental SyncVisualTree when switching imgChar from Character.png to Earth.png!
        imgChar->SetSource("Images/Earth.png");
        (void)tree.SyncVisualTree(root.Get());
        const aero::ZeroCopySubmissionStats syncStats = gpuStream.SubmitDirtyPages(tree);
        const auto& syncBatches = gpuStream.LastClipDrawBatches();
        // Now Character slot switched to Earth.png (textureId == 0), so Batch 2 & Batch 3 coalesce into textureId == 0!
        AERO_CHECK(syncStats.drawCallCount == 3u && syncBatches.size() == 3u &&
                   syncBatches[2].textureId == 0u && syncBatches[2].pageCount == 2u &&
                   syncBatches[2].vertexCount == static_cast<std::uint32_t>(aero::kVerticesPerCommandPage + 8u),
                   "Incremental SyncVisualTree updates single-node page textureId and coalesces trailing atlas batches");
    }

    std::cout << "[PASS] TestAtlasBatchMergingAndMultiTextureSplitting\n";
}

void TestSoftwareRasterizeTexturedVisualTree() {
    auto& atlas = aero::ImageAtlas::Instance();
    atlas.Clear();
    atlas.SetBaseDirectory(std::string(kSamplesDir) + "/DataBinding/Data");

    // Register a deterministic 2x2 crisp RGBA sprite to verify bilinear interpolation & vertex color tinting,
    // alongside a real 256x256 PNG (Earth.png) rendered inside the same visual tree!
    aero::DecodedImage pat{};
    pat.width = 2u;
    pat.height = 2u;
    pat.format = aero::ImageFormat::RawRgba8;
    pat.rgbaPixels = {
        255u,  40u,  20u, 255u,   20u, 255u,  40u, 255u,
         20u,  60u, 255u, 255u,  255u, 255u, 255u, 255u
    };
    const aero::ImageAtlasEntry* patEntry = atlas.RegisterDecodedImage("mem://quad_pattern.rgba", pat);
    AERO_CHECK(patEntry != nullptr && patEntry->textureId == 0u, "Registered 2x2 pattern into Skyline atlas");

    auto root = aero::MakeIntrusive<aero::Canvas>();
    root->SetWidth(320.0f);
    root->SetHeight(200.0f);
    root->SetValue(aero::PropId_Background, aero::value::FromColor(aero::Color{0.05f, 0.06f, 0.08f, 1.0f}));

    // Left: Real PNG (Earth.png, 256x256 scaled Uniform to 140x140 at (20, 30))
    auto earthImg = aero::MakeIntrusive<aero::Image>();
    earthImg->SetSource("Images/Earth.png");
    earthImg->SetStretch(aero::Stretch::Uniform);
    earthImg->SetWidth(140.0f);
    earthImg->SetHeight(140.0f);
    aero::Canvas::SetLeft(earthImg.Get(), 20.0f);
    aero::Canvas::SetTop(earthImg.Get(), 30.0f);
    root->AddChild(earthImg.Get());

    // Right: Tinted 2x2 texture stretched to 80x80 at (200, 60) with 50% green suppression tint (Foreground = {1, 0.5, 1, 1})
    auto tintedImg = aero::MakeIntrusive<aero::Image>();
    tintedImg->SetSource("mem://quad_pattern.rgba");
    tintedImg->SetStretch(aero::Stretch::Fill);
    tintedImg->SetWidth(80.0f);
    tintedImg->SetHeight(80.0f);
    tintedImg->SetValue(aero::PropId_Foreground, aero::value::FromColor(aero::Color{1.0f, 0.5f, 1.0f, 1.0f}));
    aero::Canvas::SetLeft(tintedImg.Get(), 200.0f);
    aero::Canvas::SetTop(tintedImg.Get(), 60.0f);
    root->AddChild(tintedImg.Get());

    root->Measure(aero::Size{320.0f, 200.0f});
    root->Arrange(aero::Rect{0.0f, 0.0f, 320.0f, 200.0f});

    aero::PagedRenderTree tree;
    (void)tree.BuildFromVisualTree(root.Get());

    std::vector<std::uint8_t> fb;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(
        tree,
        320u,
        200u,
        fb,
        aero::Color{0.05f, 0.06f, 0.08f, 1.0f});

    auto getPx = [&](std::uint32_t x, std::uint32_t y) -> const std::uint8_t* {
        return fb.data() + (static_cast<std::size_t>(y) * 320u + x) * 4u;
    };

    // 1. Transparent corner of Earth.png at (22, 32) must preserve dark Canvas background (~13, 15, 20)
    const std::uint8_t* bgCorner = getPx(22u, 32u);
    AERO_CHECK(bgCorner[0] < 25u && bgCorner[1] < 25u && bgCorner[2] < 30u,
               "Earth.png transparent corner at (22, 32) preserves underlying dark Canvas background");

    // 2. Center of Earth.png sphere at (90, 100) must render bright Earth ocean/land pixels above dark background
    std::size_t litEarthPixels = 0u;
    for (std::uint32_t y = 55u; y < 145u; ++y) {
        for (std::uint32_t x = 45u; x < 135u; ++x) {
            const std::uint8_t* p = getPx(x, y);
            if (p[0] > 35u || p[1] > 35u || p[2] > 45u) {
                ++litEarthPixels;
            }
        }
    }
    AERO_CHECK(litEarthPixels > 4500u,
               "RasterizeTreeToRgba8 rendered >4,500 lit Earth.png pixels inside sphere bounds");

    // 3. Verify bilinear sampling + vertex color multiplication on tintedImg:
    // Top-right texel of pat is (20, 255, 40, 255); multiplied by tint (1.0, 0.5, 1.0) -> ~(20, 128, 40) at (275, 65)
    const std::uint8_t* trTinted = getPx(275u, 65u);
    AERO_CHECK(trTinted[0] <= 30u && trTinted[1] >= 115u && trTinted[1] <= 140u && trTinted[2] <= 50u,
               "TexturedQuad multiplies sampled texel by vertex color (255 * 0.5 -> ~128 green)");

    // Center of 80x80 magnified 2x2 texture at (240, 100) must be smoothly bilinearly interpolated (not pure corner color)
    const std::uint8_t* bilerpCenter = getPx(240u, 100u);
    AERO_CHECK(bilerpCenter[0] > 90u && bilerpCenter[0] < 190u &&
               bilerpCenter[2] > 90u && bilerpCenter[2] < 190u,
               "TexturedQuad performs smooth bilinear interpolation across texel centers");

    // 4. End-to-end XAML <Image> compile + mount + render verification
    aero::AeroCompiler compiler;
    aero::AeroCompileInput xamlIn{};
    xamlIn.xamlPath = "EarthCard.xaml";
    xamlIn.xamlContent = R"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Width="200" Height="160" Background="#121824">
        <Image Source="Images/Earth.png" Stretch="Uniform" Width="120" Height="120"/>
    </Border>)";
    const aero::AeroCompileResult xamlRes = compiler.Compile(xamlIn);
    AERO_CHECK(xamlRes.succeeded && !xamlRes.axbBytes.empty(), "Compiled XAML <Image Source='Images/Earth.png'/>");

    aero::AxbPackage pkg{};
    AERO_CHECK(aero::AxbPackage::Deserialize(xamlRes.axbBytes, pkg), "Deserialized compiled Image AxbPackage");
    auto xamlRoot = aero::CreateElementByTypeName(pkg.rootTypeName);
    AERO_CHECK(aero::MountAxbTemplate(xamlRoot.Get(), pkg), "Mounted XAML Image visual tree");
    xamlRoot->Measure(aero::Size{200.0f, 160.0f});
    xamlRoot->Arrange(aero::Rect{0.0f, 0.0f, 200.0f, 160.0f});

    aero::PagedRenderTree xamlTree;
    (void)xamlTree.BuildFromVisualTree(xamlRoot.Get());
    std::vector<std::uint8_t> xamlFb;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(xamlTree, 200u, 160u, xamlFb);
    const std::uint8_t* xamlCenter = xamlFb.data() + (80u * 200u + 100u) * 4u;
    AERO_CHECK(xamlCenter[0] > 30u || xamlCenter[1] > 30u || xamlCenter[2] > 45u,
               "Compiled XAML <Image> rendered non-background Earth.png texels at viewport center");

    std::cout << "[PASS] TestSoftwareRasterizeTexturedVisualTree\n";
}

} // namespace

int main() {
    aero::EnsureBuiltinTypesRegistered();
    aero::EnsureControlTypesRegistered();

    TestZeroDependencyImageDecoders();
    TestRelativeUriAndMeasureCoreNaturalSizing();
    TestStretchModesGeometryAndUvCropping();
    TestAtlasBatchMergingAndMultiTextureSplitting();
    TestSoftwareRasterizeTexturedVisualTree();

    std::cout << "========================================\n";
    std::cout << "P0-4 Tests Summary: " << (gChecksRun - gChecksFailed) << "/" << gChecksRun << " checks passed.\n";
    std::cout << "========================================\n";
    return (gChecksFailed == 0) ? 0 : 1;
}
