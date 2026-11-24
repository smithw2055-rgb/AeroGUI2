#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <realscript/aot_cpp/AotRuntime.h>
#include <realscript/bytecode/Bytecode.h>
#include <realscript/runtime/Runtime.h>
#include <realscript/semantic/Semantic.h>

#include "aero/asset/GradientBrush.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"
#include "aero/render/PagedRenderTree.hpp"
#include "aero/render/ZeroCopyGpuStream.hpp"
#include "aero/runtime/AeroExecutionDomains.hpp"
#include "aero/visual/DrawingCommandBuffer.hpp"
#include "aero/visual/VisualAnimationEngine.hpp"
#include "aero/visual/VisualHostBindings.hpp"
#include "aero/visual/VisualInputRouter.hpp"
#include "aero/visual/VisualRenderAdapter.hpp"
#include "aero/visual/VisualTree.hpp"

namespace {

int gFailureCount = 0;

void CheckImpl(bool cond, const char* expr, int line) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] Line %d: %s\n", line, expr);
        ++gFailureCount;
    }
}

void CheckNearImpl(float actual, float expected, float eps, const char* expr, int line) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > eps) {
        std::fprintf(
            stderr,
            "[FAIL] Line %d: %s (actual=%.5f expected=%.5f eps=%.5f)\n",
            line,
            expr,
            static_cast<double>(actual),
            static_cast<double>(expected),
            static_cast<double>(eps));
        ++gFailureCount;
    }
}

#define CHECK(expr) CheckImpl((expr), #expr, __LINE__)
#define CHECK_NEAR(actual, expected, eps) CheckNearImpl((actual), (expected), (eps), #actual, __LINE__)

[[nodiscard]] aero::rs_rt::Value MakeStrVal(std::string_view sv) {
    return aero::rs_rt::Value{aero::rs_rt::StringValue{sv}};
}

void TestGenerationalHandlesAndCycleGuard() {
    aero::rs_rt::NativeHandleRegistry handles;
    aero::UIResourceStore store;
    aero::VisualWorld world(&handles, &store);

    const aero::VisualNodeId n1 = world.CreateNode(aero::VisualKind::Box);
    const aero::VisualNodeId n2 = world.CreateNode(aero::VisualKind::Box);
    const aero::VisualNodeId n3 = world.CreateNode(aero::VisualKind::Box);
    CHECK(n1.IsValid() && n2.IsValid() && n3.IsValid());
    CHECK(world.LiveNodeCount() == 3u);

    const aero::rs_rt::NativeHandle h1 = world.HandleFor(n1);
    CHECK(h1.valid());
    CHECK(world.ResolveHandle(h1) == n1);

    // Cycle guards & re-parenting invariants
    world.SetRoot(n1);
    CHECK(!world.AddChild(n1, n1));
    CHECK(world.AddChild(n1, n2));
    CHECK(world.AddChild(n2, n3));
    CHECK(!world.AddChild(n3, n1));
    CHECK(!world.AddChild(n2, n1));
    CHECK(!world.InsertChild(n3, 0, n1));

    // Re-parent n3 under n1 -> detaches from n2 cleanly
    CHECK(world.InsertChild(n1, 0, n3));
    CHECK(world.GetNode(n2)->children.empty());
    CHECK(world.GetNode(n1)->children.size() == 2u);
    CHECK(world.GetNode(n1)->children[0] == n3);
    CHECK(world.GetNode(n1)->children[1] == n2);

    // Generational slot reuse invalidates old handle & VisualNodeId
    CHECK(world.DestroyNode(n2));
    CHECK(!world.IsAlive(n2));
    CHECK(!world.ResolveHandle(h1).IsValid() == false); // n1 still alive
    CHECK(!world.GetNode(n2));

    const aero::rs_rt::NativeHandle h3 = world.HandleFor(n3);
    CHECK(world.DestroyNode(n3));
    CHECK(!world.ResolveHandle(h3).IsValid());

    const aero::VisualNodeId recycled = world.CreateNode(aero::VisualKind::Text);
    CHECK(recycled.IsValid());
    CHECK(recycled != n3);
    CHECK(!world.ResolveHandle(h3).IsValid());
    CHECK(world.ResolveHandle(world.HandleFor(recycled)) == recycled);
}

void TestGcRootSafetyAndExternalPools() {
    auto heap = std::make_shared<aero::rs_rt::ManagedHeap>();
    aero::rs_rt::NativeHandleRegistry handles;
    aero::UIResourceStore store;
    aero::VisualWorld world(&handles, &store, heap.get());

    const aero::VisualNodeId node = world.CreateNode(aero::VisualKind::Box);
    auto objOpt = heap->allocateObject(101u, {aero::rs_rt::Value{42LL}}, {});
    CHECK(objOpt.has_value());
    const aero::rs_rt::ObjectRef owner = *objOpt;
    CHECK(heap->isAlive(owner));

    CHECK(world.BindOwner(node, owner, aero::VisualCallback_PointerDown));
    CHECK(world.GetOwner(node) == owner);

    // Trigger GC with no stack roots: VisualWorld PersistentRoot + external pool marker keep owner alive
    heap->collect();
    CHECK(heap->isAlive(owner));

    // Destroying node releases pin and allows subsequent GC to reclaim owner
    CHECK(world.DestroyNode(node));
    heap->collect();
    CHECK(!heap->isAlive(owner));
}

void TestAllNineLayoutModes() {
    aero::UIResourceStore store;

    // 1. Manual
    {
        aero::VisualWorld w(nullptr, &store);
        auto root = w.CreateNode(aero::VisualKind::Box);
        auto box = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.AddChild(root, box);
        w.SetSize(box, 100.0f, 40.0f);
        w.SetMargin(box, 10.0f, 20.0f, 0.0f, 0.0f);
        w.SetAlignment(box, aero::HorizontalAlignment::Center, aero::VerticalAlignment::Bottom);
        w.UpdateLayout(400.0f, 200.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(box, aero::VisualLayoutRectQuery::X), 155.0f, 0.5f);
        CHECK_NEAR(w.GetLayoutRectScalar(box, aero::VisualLayoutRectQuery::Y), 160.0f, 0.5f);
        CHECK_NEAR(w.GetLayoutRectScalar(box, aero::VisualLayoutRectQuery::Width), 100.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(box, aero::VisualLayoutRectQuery::Height), 40.0f, 0.1f);
    }

    // 2. Stack (Horizontal & Vertical, with Collapsed middle child)
    {
        aero::VisualWorld w(nullptr, &store);
        auto root = w.CreateNode(aero::VisualKind::Box);
        auto c0 = w.CreateNode(aero::VisualKind::Box);
        auto c1 = w.CreateNode(aero::VisualKind::Box);
        auto c2 = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.AddChild(root, c0);
        w.AddChild(root, c1);
        w.AddChild(root, c2);
        w.SetLayoutMode(root, aero::LayoutMode::Stack, 10.0f, 0.0f, 0, 0); // Horizontal, spacing=10
        w.SetSize(c0, 50.0f, 20.0f);
        w.SetSize(c1, 80.0f, 20.0f);
        w.SetSize(c2, 60.0f, 20.0f);
        w.SetVisibility(c1, aero::Visibility::Collapsed);
        w.UpdateLayout(400.0f, 200.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(c0, aero::VisualLayoutRectQuery::X), 0.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(c2, aero::VisualLayoutRectQuery::X), 60.0f, 0.1f);

        w.SetLayoutMode(root, aero::LayoutMode::Stack, 8.0f, 0.0f, 1, 0); // Vertical, spacing=8
        w.UpdateLayout(400.0f, 200.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(c0, aero::VisualLayoutRectQuery::Y), 0.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(c2, aero::VisualLayoutRectQuery::Y), 28.0f, 0.1f);
    }

    // 3. Dock (Left, Top, Right, LastChildFill)
    {
        aero::VisualWorld w(nullptr, &store);
        auto root = w.CreateNode(aero::VisualKind::Box);
        auto top = w.CreateNode(aero::VisualKind::Box);
        auto left = w.CreateNode(aero::VisualKind::Box);
        auto fill = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.AddChild(root, top);
        w.AddChild(root, left);
        w.AddChild(root, fill);
        w.SetLayoutMode(root, aero::LayoutMode::Dock, 0.0f, 0.0f, 1, 0);
        w.SetDock(top, aero::Dock::Top);
        w.SetSize(top, std::numeric_limits<float>::quiet_NaN(), 30.0f);
        w.SetDock(left, aero::Dock::Left);
        w.SetSize(left, 60.0f, std::numeric_limits<float>::quiet_NaN());
        w.UpdateLayout(300.0f, 200.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(top, aero::VisualLayoutRectQuery::Height), 30.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(left, aero::VisualLayoutRectQuery::Y), 30.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(left, aero::VisualLayoutRectQuery::Height), 170.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(fill, aero::VisualLayoutRectQuery::X), 60.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(fill, aero::VisualLayoutRectQuery::Y), 30.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(fill, aero::VisualLayoutRectQuery::Width), 240.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(fill, aero::VisualLayoutRectQuery::Height), 170.0f, 0.1f);
    }

    // 4. Wrap (Horizontal wrapping with ItemWidth=80, ItemHeight=30, Spacing=10 -> 10000)
    {
        aero::VisualWorld w(nullptr, &store);
        auto root = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.SetLayoutMode(root, aero::LayoutMode::Wrap, 80.0f, 30.0f, 0, 10000);
        std::vector<aero::VisualNodeId> items;
        for (int i = 0; i < 4; ++i) {
            auto c = w.CreateNode(aero::VisualKind::Box);
            w.AddChild(root, c);
            items.push_back(c);
        }
        // Width 200: row 0 fits 2 items (80 + 10 + 80 = 170 <= 200), 3rd item wraps to y = 30 + 10 = 40
        w.UpdateLayout(200.0f, 200.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(items[0], aero::VisualLayoutRectQuery::X), 0.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(items[1], aero::VisualLayoutRectQuery::X), 90.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(items[2], aero::VisualLayoutRectQuery::X), 0.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(items[2], aero::VisualLayoutRectQuery::Y), 40.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(items[3], aero::VisualLayoutRectQuery::X), 90.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(items[3], aero::VisualLayoutRectQuery::Y), 40.0f, 0.1f);
    }

    // 5. Grid (Pixel + Auto + Star + RowSpan/ColSpan)
    {
        aero::VisualWorld w(nullptr, &store);
        auto root = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.SetLayoutMode(root, aero::LayoutMode::Grid, 0.0f, 0.0f, 0, 0);
        w.GridClearDefs(root);
        w.GridAddColDef(root, aero::GridUnitType::Pixel, 50.0f);
        w.GridAddColDef(root, aero::GridUnitType::Auto, 0.0f);
        w.GridAddColDef(root, aero::GridUnitType::Star, 1.0f);
        w.GridAddColDef(root, aero::GridUnitType::Star, 2.0f);
        w.GridAddRowDef(root, aero::GridUnitType::Pixel, 40.0f);
        w.GridAddRowDef(root, aero::GridUnitType::Star, 1.0f);

        auto autoItem = w.CreateNode(aero::VisualKind::Box);
        auto spanItem = w.CreateNode(aero::VisualKind::Box);
        w.AddChild(root, autoItem);
        w.AddChild(root, spanItem);
        w.SetSize(autoItem, 30.0f, 20.0f);
        w.SetGridCell(autoItem, 0, 1, 1, 1);
        w.SetGridCell(spanItem, 0, 2, 2, 2); // spans Star(1) + Star(2), both rows

        // Viewport 380x240: Col0=50, Col1=30 -> remaining 300 -> Star(1)=100, Star(2)=200
        w.UpdateLayout(380.0f, 240.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(autoItem, aero::VisualLayoutRectQuery::X), 50.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(spanItem, aero::VisualLayoutRectQuery::X), 80.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(spanItem, aero::VisualLayoutRectQuery::Width), 300.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(spanItem, aero::VisualLayoutRectQuery::Height), 240.0f, 0.1f);
    }

    // 6. UniformGrid with firstCol=1
    {
        aero::VisualWorld w(nullptr, &store);
        auto root = w.CreateNode(aero::VisualKind::Box);
        auto a = w.CreateNode(aero::VisualKind::Box);
        auto b = w.CreateNode(aero::VisualKind::Box);
        auto c = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.AddChild(root, a);
        w.AddChild(root, b);
        w.AddChild(root, c);
        w.SetLayoutMode(root, aero::LayoutMode::UniformGrid, 1.0f, 0.0f, 2, 2); // 2x2, firstCol=1
        w.UpdateLayout(200.0f, 100.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(a, aero::VisualLayoutRectQuery::X), 100.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(a, aero::VisualLayoutRectQuery::Y), 0.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(b, aero::VisualLayoutRectQuery::X), 0.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(b, aero::VisualLayoutRectQuery::Y), 50.0f, 0.1f);
    }

    // 7. Canvas
    {
        aero::VisualWorld w(nullptr, &store);
        auto root = w.CreateNode(aero::VisualKind::Box);
        auto a = w.CreateNode(aero::VisualKind::Box);
        auto b = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.AddChild(root, a);
        w.AddChild(root, b);
        w.SetLayoutMode(root, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
        w.SetSize(a, 40.0f, 20.0f);
        w.SetCanvasOffsets(a, 15.0f, 25.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
        w.SetSize(b, 50.0f, 30.0f);
        w.SetCanvasOffsets(b, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(), 10.0f, 20.0f);
        w.UpdateLayout(300.0f, 200.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(a, aero::VisualLayoutRectQuery::X), 15.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(a, aero::VisualLayoutRectQuery::Y), 25.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(b, aero::VisualLayoutRectQuery::X), 240.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(b, aero::VisualLayoutRectQuery::Y), 150.0f, 0.1f);
    }

    // 8. ScrollViewport
    {
        aero::VisualWorld w(nullptr, &store);
        auto sv = w.CreateNode(aero::VisualKind::Box);
        auto content = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(sv);
        w.AddChild(sv, content);
        w.SetLayoutMode(sv, aero::LayoutMode::ScrollViewport, 0.0f, 0.0f, 0, 0);
        w.SetSize(content, 1000.0f, 800.0f);
        w.SetScrollOffsets(sv, 40.0f, 25.0f);
        w.UpdateLayout(300.0f, 200.0f);
        CHECK_NEAR(w.GetLayoutRectScalar(sv, aero::VisualLayoutRectQuery::ScrollExtentWidth), 1000.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(sv, aero::VisualLayoutRectQuery::ScrollExtentHeight), 800.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(content, aero::VisualLayoutRectQuery::X), -40.0f, 0.1f);
        CHECK_NEAR(w.GetLayoutRectScalar(content, aero::VisualLayoutRectQuery::Y), -25.0f, 0.1f);
    }

    // 9. Viewbox (Uniform LayoutScaler refW=400, refH=200 inside 800x600 -> scale 2.0, centered Y=(600-400)/2=100)
    {
        aero::VisualWorld w(nullptr, &store);
        auto vb = w.CreateNode(aero::VisualKind::Box);
        auto child = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(vb);
        w.AddChild(vb, child);
        w.SetLayoutMode(
            vb,
            aero::LayoutMode::Viewbox,
            400.0f,
            200.0f,
            static_cast<std::int32_t>(aero::Stretch::Uniform),
            static_cast<std::int32_t>(aero::StretchDirection::Both));
        w.UpdateLayout(800.0f, 600.0f);
        const aero::VisualNode* cNode = w.GetNode(child);
        CHECK(cNode != nullptr);
        CHECK_NEAR(cNode->layout.worldTransform.m00, 2.0f, 0.01f);
        CHECK_NEAR(cNode->layout.worldTransform.m11, 2.0f, 0.01f);
        const aero::Point loc = w.ScreenToLocal(child, aero::Point{400.0f, 300.0f});
        CHECK_NEAR(loc.x, 200.0f, 0.5f);
        CHECK_NEAR(loc.y, 100.0f, 0.5f);
    }
}

void TestIncrementalDepthBuckets() {
    aero::UIResourceStore store;
    aero::VisualWorld w(nullptr, &store);
    auto root = w.CreateNode(aero::VisualKind::Box);
    auto panelA = w.CreateNode(aero::VisualKind::Box);
    auto panelB = w.CreateNode(aero::VisualKind::Box);
    auto leafA = w.CreateNode(aero::VisualKind::Box);
    auto spyA = w.CreateNode(aero::VisualKind::Box);
    auto spyB = w.CreateNode(aero::VisualKind::Box);
    w.SetRoot(root);
    w.SetLayoutMode(
        root, aero::LayoutMode::Stack, 0.0f, 0.0f,
        static_cast<std::int32_t>(aero::Orientation::Vertical), 0);
    w.AddChild(root, panelA);
    w.AddChild(root, panelB);
    w.SetSize(panelA, 150.0f, 100.0f);
    w.SetSize(panelB, 150.0f, 100.0f);
    w.SetLayoutMode(
        panelA, aero::LayoutMode::Stack, 0.0f, 0.0f,
        static_cast<std::int32_t>(aero::Orientation::Vertical), 0);
    w.SetLayoutMode(
        panelB, aero::LayoutMode::Stack, 0.0f, 0.0f,
        static_cast<std::int32_t>(aero::Orientation::Vertical), 0);
    w.AddChild(panelA, leafA);
    w.AddChild(panelA, spyA);
    w.AddChild(panelB, spyB);
    w.SetSize(leafA, 40.0f, 40.0f);
    w.SetLayoutMode(spyA, aero::LayoutMode::CustomCallback, 0.0f, 0.0f, 0, 0);
    w.SetLayoutMode(spyB, aero::LayoutMode::CustomCallback, 0.0f, 0.0f, 0, 0);

    int measureA = 0, measureB = 0;
    int arrangeA = 0, arrangeB = 0;
    w.SetCustomLayoutCallbacks(
        [&](aero::VisualWorld&, aero::VisualNodeId nid, aero::Size) -> aero::Size {
            if (nid == spyA) ++measureA;
            if (nid == spyB) ++measureB;
            return aero::Size{30.0f, 20.0f};
        },
        [&](aero::VisualWorld&, aero::VisualNodeId nid, aero::Rect) {
            if (nid == spyA) ++arrangeA;
            if (nid == spyB) ++arrangeB;
        });

    w.UpdateLayout(800.0f, 600.0f);
    CHECK(measureA == 1 && measureB == 1);
    CHECK(arrangeA == 1 && arrangeB == 1);

    // Clean frame: 0 calls
    w.UpdateLayout(800.0f, 600.0f);
    CHECK(measureA == 1 && measureB == 1);
    CHECK(arrangeA == 1 && arrangeB == 1);

    // Dirty only leafA directly: leafA's desiredSize changes, bubbling ONLY to fixed-size panelA;
    // panelA's Stack re-arranges leafA (90x50) and shifts spyA (arrangeA -> 2),
    // while root, panelB, and spyB remain completely untouched.
    w.SetSize(leafA, 90.0f, 50.0f);
    w.UpdateLayout(800.0f, 600.0f);
    CHECK(measureA == 1 && arrangeA == 2);
    CHECK(measureB == 1 && arrangeB == 1);
    CHECK_NEAR(w.GetLayoutRectScalar(leafA, aero::VisualLayoutRectQuery::Width), 90.0f, 0.1f);
    CHECK_NEAR(w.GetLayoutRectScalar(spyA, aero::VisualLayoutRectQuery::Y), 50.0f, 0.1f);

    // Dirty spyA directly -> custom measure+arrange re-runs on spyA while spyB stays untouched
    w.MarkDirty(spyA, aero::VisualDirty_Layout);
    w.UpdateLayout(800.0f, 600.0f);
    CHECK(measureA == 2 && arrangeA == 3);
    CHECK(measureB == 1 && arrangeB == 1);
}

void TestDrawingCommandBuffer() {
    aero::DrawingCommandBuffer buf;
    const aero::VisualNodeId invalid{};
    buf.Begin(invalid);
    buf.FillRect(invalid, 0.0f, 0.0f, 10.0f, 10.0f, 0xFFFFFFFFu);
    buf.End(invalid);
    CHECK(!buf.HasCommands(invalid));

    aero::VisualNodeId n{0u, 1u};
    buf.Begin(n);
    buf.Clear(n, 0xFF101010u);
    buf.FillRect(n, 1.0f, 2.0f, 30.0f, 40.0f, 0xFFFF0000u);
    buf.DrawRect(n, 1.0f, 2.0f, 30.0f, 40.0f, 0xFFFFFFFFu, 2.0f);
    buf.FillRoundedRect(n, 4.0f, 4.0f, 20.0f, 20.0f, 6.0f, 6.0f, 0xFF00FF00u);
    buf.DrawRoundedRect(n, 4.0f, 4.0f, 20.0f, 20.0f, 6.0f, 6.0f, 0xFFFFFFFFu, 1.5f);
    buf.FillEllipse(n, 50.0f, 50.0f, 10.0f, 10.0f, 0xFF0000FFu);
    buf.DrawEllipse(n, 50.0f, 50.0f, 12.0f, 12.0f, 0xFFFFFFFFu, 1.0f);
    buf.DrawLine(n, 0.0f, 0.0f, 20.0f, 20.0f, 0xFFFFFFFFu, 2.0f);
    buf.DrawPolyline(n, {aero::Point{0.0f, 0.0f}, aero::Point{10.0f, 5.0f}}, 0xFFFFFFFFu, 1.0f);
    buf.DrawPath(n, "M 0,0 L 10,0 L 10,10 Z", 0.0f, 0.0f, 10.0f, 10.0f, 0xFFFFFFFFu, 0u, 0.0f);
    buf.DrawImage(n, "mem://img", 0.0f, 0.0f, 8.0f, 8.0f, 0xFFFFFFFFu);
    buf.DrawText(n, "Hello", 5.0f, 5.0f, 50.0f, 20.0f, "", 12.0f, 0xFFFFFFFFu,
        aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    buf.PushClip(n, 0.0f, 0.0f, 100.0f, 100.0f);
    buf.PushTransform(n, 5.0f, 5.0f, 1.0f, 1.0f, 15.0f);
    buf.PushOpacity(n, 0.5f);
    buf.PopOpacity(n);
    buf.PopTransform(n);
    buf.PopClip(n);
    buf.End(n);

    const aero::DrawingCommandList* list = buf.FindCommands(n);
    CHECK(list != nullptr);
    CHECK(!list->recording);
    CHECK(list->version == 1ull);
    CHECK(list->commands.size() == 18u);

    buf.Begin(n);
    buf.End(n);
    CHECK(buf.FindCommands(n)->version == 2ull);
    CHECK(buf.FindCommands(n)->commands.empty());
}

void TestAnimationEngine() {
    // 1. Verify all EasingFunctionKind x 3 modes + non-linear Bounce parabola
    for (int k = 0; k <= 11; ++k) {
        const auto kind = static_cast<aero::EasingFunctionKind>(k);
        for (int m = 0; m <= 2; ++m) {
            const auto mode = static_cast<aero::EasingMode>(m);
            CHECK_NEAR(aero::EvaluateEasing(kind, mode, 0.0f), 0.0f, 0.01f);
            CHECK_NEAR(aero::EvaluateEasing(kind, mode, 1.0f), 1.0f, 0.01f);
        }
    }
    const float bounceMid = aero::EvaluateEasing(aero::EasingFunctionKind::Bounce, aero::EasingMode::EaseOut, 0.5f);
    CHECK(bounceMid > 0.70f); // true parabolic Bounce hits >0.75 before settling, not linear 0.5
    const float expPos = aero::EvaluateEasing(aero::EasingFunctionKind::Exponential, aero::EasingMode::EaseOut, 0.5f, 2.0f, 0.0f, 1u);
    const float expNeg = aero::EvaluateEasing(aero::EasingFunctionKind::Exponential, aero::EasingMode::EaseOut, 0.5f, -2.0f, 0.0f, 1u);
    const float expZero = aero::EvaluateEasing(aero::EasingFunctionKind::Exponential, aero::EasingMode::EaseOut, 0.5f, 0.0f, 0.0f, 1u);
    CHECK(expPos > 0.70f);
    CHECK(expNeg < 0.45f);
    CHECK_NEAR(expZero, 0.5f, 1e-4f);
    const float backZero = aero::EvaluateEasing(aero::EasingFunctionKind::Back, aero::EasingMode::EaseIn, 0.5f, 0.0f, 0.0f, 1u);
    CHECK_NEAR(backZero, 0.125f, 1e-4f);
    const float bounceZero = aero::EvaluateEasing(aero::EasingFunctionKind::Bounce, aero::EasingMode::EaseOut, 0.5f, 0.0f, 0.0f, 1u);
    CHECK(bounceZero >= 0.0f && bounceZero <= 1.0f);
    CHECK(std::fabs(bounceZero - bounceMid) > 0.05f);

    aero::VisualWorld w;
    aero::VisualAnimationEngine anim;
    int wakeCount = 0;
    anim.SetWakeHostCallback([&]() { ++wakeCount; });
    auto box = w.CreateNode(aero::VisualKind::Box);
    w.SetRoot(box);

    // 2. FloatTween with delay remainder + autoReverse + repeatCount=1
    anim.StartFloat(
        box, aero::VisualAnimProp::TranslateX, 0.0f, 100.0f, 1.0f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.25f, true, 1);
    CHECK(wakeCount == 1);
    anim.Tick(0.75f, w); // 0.25 delay consumed, 0.5 active elapsed => half forward => x=50
    CHECK_NEAR(w.GetNode(box)->transform2D.tx, 50.0f, 0.5f);
    anim.Tick(0.50f, w); // hits 1.0 duration -> reverses to start of backward => x=100
    CHECK_NEAR(w.GetNode(box)->transform2D.tx, 100.0f, 0.5f);
    anim.Tick(0.50f, w); // halfway back => x=50
    CHECK_NEAR(w.GetNode(box)->transform2D.tx, 50.0f, 0.5f);
    anim.Tick(0.50f, w); // finished full autoReverse cycle => x=0 and stopped
    CHECK_NEAR(w.GetNode(box)->transform2D.tx, 0.0f, 0.1f);
    CHECK(!anim.HasActiveAnimations());

    // 3. RepeatCount <= 0 repeats indefinitely
    const std::int32_t infId = anim.StartFloat(
        box, aero::VisualAnimProp::Opacity, 0.2f, 0.8f, 0.5f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, false, 0);
    for (int s = 0; s < 10; ++s) {
        anim.Tick(0.3f, w);
        CHECK(anim.HasActiveAnimations());
    }
    anim.StopTrack(infId);
    CHECK(!anim.HasActiveAnimations());

    // 4. ColorTween RGBA lerp
    anim.StartColor(box, aero::VisualAnimProp::BgColor, 0xFF000000u, 0xFFFFFFFFu, 1.0f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut);
    anim.Tick(0.5f, w);
    const std::uint32_t midRgba = w.GetNode(box)->box.bgRgba;
    CHECK((midRgba & 0xFFu) >= 127u && (midRgba & 0xFFu) <= 129u);
    anim.StopAll(box);

    // 5. Analytical C1 SpringFloat stability on large dt & small steps
    anim.SpringFloat(box, aero::VisualAnimProp::TranslateY, 0.0f, 200.0f, 320.0f, 28.0f, 1.0f);
    anim.Tick(0.016f, w);
    CHECK(w.GetNode(box)->transform2D.ty > 0.0f && w.GetNode(box)->transform2D.ty < 200.0f);
    anim.Tick(5.0f, w);
    CHECK_NEAR(w.GetNode(box)->transform2D.ty, 200.0f, 0.05f);
    CHECK(!anim.HasActiveAnimations());

    // 6. KeyFrames (discrete + eased segment)
    std::vector<aero::VisualKeyFrame> kfs;
    kfs.push_back(aero::VisualKeyFrame{0.0f, 10.0f, 0u, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, false});
    kfs.push_back(aero::VisualKeyFrame{1.0f, 110.0f, 0u, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, false});
    kfs.push_back(aero::VisualKeyFrame{2.0f, 500.0f, 0u, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, true});
    anim.StartKeyFrames(box, aero::VisualAnimProp::CustomScalar0, std::move(kfs), false, 1);
    anim.Tick(0.5f, w);
    CHECK_NEAR(w.GetNode(box)->customScalars[0], 60.0f, 0.5f);
    anim.Tick(1.0f, w); // t=1.5 inside discrete segment [1,2] -> holds 110
    CHECK_NEAR(w.GetNode(box)->customScalars[0], 110.0f, 0.5f);
    anim.Tick(0.5f, w); // t=2.0 -> snaps to 500 and ends
    CHECK_NEAR(w.GetNode(box)->customScalars[0], 500.0f, 0.5f);
    CHECK(!anim.HasActiveAnimations());

    // 7. BeginKeyFramesTrack + AddKeyFrameFloat infinite repeat loop (wraps smoothly past t > durationSec)
    const std::int32_t loopKfId = anim.BeginKeyFramesTrack(
        box, aero::VisualAnimProp::CustomScalar1, 0.0f, false, -1);
    CHECK(loopKfId > 0);
    anim.AddKeyFrameFloat(loopKfId, 0.0f, 0.0f, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, 0.0f, 0u, false);
    anim.AddKeyFrameFloat(loopKfId, 0.5f, 50.0f, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, 0.0f, 0u, false);
    anim.AddKeyFrameFloat(loopKfId, 1.0f, 100.0f, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, 0.0f, 0u, false);
    anim.AddKeyFrameFloat(loopKfId, 1.5f, 50.0f, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, 0.0f, 0u, false);
    anim.AddKeyFrameFloat(loopKfId, 2.0f, 0.0f, aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, 0.0f, 0u, false);
    anim.Tick(2.25f, w);
    CHECK_NEAR(w.GetNode(box)->customScalars[1], 25.0f, 0.5f);
    anim.Tick(0.50f, w); // t = 2.75s -> local 0.75s -> 75.0f
    CHECK_NEAR(w.GetNode(box)->customScalars[1], 75.0f, 0.5f);
    CHECK(anim.HasActiveAnimations());
    anim.StopTrack(loopKfId);
    CHECK(!anim.HasActiveAnimations());
}

void TestHitTestTransformsAndInputRouter() {
    aero::VisualWorld w;
    auto root = w.CreateNode(aero::VisualKind::Box);
    auto rot2d = w.CreateNode(aero::VisualKind::Box);
    auto card3d = w.CreateNode(aero::VisualKind::Box);
    auto zLow = w.CreateNode(aero::VisualKind::Box);
    auto zHigh = w.CreateNode(aero::VisualKind::Box);
    auto clipBox = w.CreateNode(aero::VisualKind::Box);
    auto clippedChild = w.CreateNode(aero::VisualKind::Box);

    w.SetRoot(root);
    w.SetLayoutMode(root, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
    w.AddChild(root, rot2d);
    w.AddChild(root, card3d);
    w.AddChild(root, zHigh); // added BEFORE zLow, yet higher zIndex!
    w.AddChild(root, zLow);
    w.AddChild(root, clipBox);
    w.AddChild(clipBox, clippedChild);

    w.SetSize(rot2d, 100.0f, 40.0f);
    w.SetCanvasOffsets(rot2d, 50.0f, 50.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetTransform2D(rot2d, 0.0f, 0.0f, 1.0f, 1.0f, 90.0f, 0.0f, 0.0f, 0.5f, 0.5f);

    w.SetSize(card3d, 120.0f, 80.0f);
    w.SetCanvasOffsets(card3d, 220.0f, 60.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetTransform3D(card3d, 0.0f, 35.0f, 0.0f, 0.0f, 0.0f, 0.0f, 600.0f);

    w.SetSize(zHigh, 60.0f, 60.0f);
    w.SetCanvasOffsets(zHigh, 50.0f, 200.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetZIndex(zHigh, 10);

    w.SetSize(zLow, 60.0f, 60.0f);
    w.SetCanvasOffsets(zLow, 50.0f, 200.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetZIndex(zLow, 1);

    w.SetSize(clipBox, 40.0f, 40.0f);
    w.SetCanvasOffsets(clipBox, 200.0f, 200.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetClipToBounds(clipBox, true);
    w.SetLayoutMode(clipBox, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
    w.SetSize(clippedChild, 30.0f, 30.0f);
    w.SetCanvasOffsets(clippedChild, 60.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());

    w.SetInteractiveFlags(rot2d, true, true, true);
    w.SetInteractiveFlags(card3d, true, true, true);
    w.SetInteractiveFlags(zHigh, true, true, true);
    w.SetInteractiveFlags(zLow, true, true, true);
    w.SetInteractiveFlags(root, true, false, false);

    w.UpdateLayout(500.0f, 400.0f);

    std::string clipStorage = "clip";
    int wakeCalled = 0;
    aero::UIHostPlatformHooks hooks{};
    hooks.requestWake = [&]() { ++wakeCalled; };
    hooks.getClipboardText = [&]() { return clipStorage; };
    hooks.setClipboardText = [&](std::string_view s) { clipStorage.assign(s); };

    aero::ImeCompositionController ime;
    aero::VisualInputRouter router(&w, nullptr, &hooks);

    // 2D rotated box center is (100, 70); original size 100x40 rotated 90 deg spans x in [80..120], y in [20..120]
    CHECK(router.HitTest(aero::Point{100.0f, 30.0f}) == rot2d);
    CHECK(!router.HitTest(aero::Point{60.0f, 70.0f}).IsValid()); // outside vertical rotated strip

    // 3D planar hit test at projected off-center corner point inside card3d
    const aero::Point p3dScreen = w.GetNode(card3d)->WorldMatrix3D().ProjectPoint2D(aero::Point{250.0f, 90.0f});
    CHECK(router.HitTest(p3dScreen) == card3d);

    // zIndex ordering beats declaration order
    CHECK(router.HitTest(aero::Point{80.0f, 230.0f}) == zHigh);

    // clipToBounds prevents hitting clippedChild sticking outside clipBox
    CHECK(!router.HitTest(aero::Point{270.0f, 210.0f}).IsValid());

    // Script callback dispatch, hover enter/leave, capture, ring queue & focus navigation
    int downCount = 0;
    int enterCount = 0;
    int leaveCount = 0;
    router.SetScriptEventDispatcher([&](aero::VisualNodeId, aero::VisualCallbackFlags flag, const aero::NativeInputEvent&) -> bool {
        if (flag == aero::VisualCallback_PointerDown) ++downCount;
        if (flag == aero::VisualCallback_PointerEnter) ++enterCount;
        if (flag == aero::VisualCallback_PointerLeave) ++leaveCount;
        return true;
    });

    CHECK(router.QueuePointerMove(aero::Point{100.0f, 70.0f}));
    CHECK(router.QueuePointerButton(aero::Point{100.0f, 70.0f}, aero::PointerButton::Left, true));
    CHECK(router.QueuePointerMove(aero::Point{80.0f, 230.0f}));
    CHECK(wakeCalled == 3);
    CHECK(router.FlushQueuedEvents() == 3u);
    CHECK(enterCount == 2 && leaveCount == 1 && downCount == 1);
    CHECK(router.HoveredNode() == zHigh);
    CHECK(router.FocusedNode() == rot2d);

    // Directional focus navigation
    CHECK(router.MoveFocus(aero::FocusDirection::Right));
    CHECK(router.FocusedNode() == card3d);
    CHECK(router.MoveFocus(aero::FocusDirection::Down));
    CHECK(router.MoveFocus(aero::FocusDirection::Next));

    // IME + Clipboard
    ime.BeginFocus(rot2d);
    ime.BeginComposition();
    ime.UpdateComposition("ni", 2u);
    CHECK(ime.IsComposing() && ime.PreeditText() == "ni");
    ime.CommitComposition("你");
    CHECK(!ime.IsComposing() && ime.CommittedText() == "你");
    ime.EndBlur();
    CHECK(!ime.IsImeFocused());

    router.SetClipboardText("hello_aero");
    CHECK(router.GetClipboardText() == "hello_aero");
}

void TestVisualRenderAdapterAndRasterizer() {
    aero::UIResourceStore store;
    aero::VisualWorld w(nullptr, &store);
    aero::DrawingCommandBuffer drawBuf;

    // Register decoded 4x4 checkerboard image
    aero::DecodedImage chkImg{};
    chkImg.width = 4u;
    chkImg.height = 4u;
    chkImg.format = aero::ImageFormat::Unknown;
    chkImg.rgbaPixels.assign(4u * 4u * 4u, 255u);
    for (std::uint32_t y = 0u; y < 4u; ++y) {
        for (std::uint32_t x = 0u; x < 4u; ++x) {
            const std::size_t off = (y * 4u + x) * 4u;
            const bool dark = ((x + y) % 2u) == 0u;
            chkImg.rgbaPixels[off + 0u] = dark ? 20u : 240u;
            chkImg.rgbaPixels[off + 1u] = dark ? 180u : 80u;
            chkImg.rgbaPixels[off + 2u] = 250u;
            chkImg.rgbaPixels[off + 3u] = 255u;
        }
    }
    const auto* imgEntry = store.Images().RegisterDecodedImage("mem://chk", std::move(chkImg), true);
    CHECK(imgEntry != nullptr);

    aero::GradientBrushDesc grad{};
    grad.kind = aero::GradientBrushKind::Linear;
    grad.stops = {
        aero::GradientStop{0.0f, aero::Color{0.9f, 0.2f, 0.2f, 1.0f}},
        aero::GradientStop{1.0f, aero::Color{0.2f, 0.3f, 0.95f, 1.0f}}};
    const std::uint32_t gradId = aero::GradientBrushRegistry::Instance().RegisterBrush(grad);
    CHECK(gradId > 0u);

    auto root = w.CreateNode(aero::VisualKind::Box);
    auto box = w.CreateNode(aero::VisualKind::Box);
    auto textNode = w.CreateNode(aero::VisualKind::Text);
    auto imgNode = w.CreateNode(aero::VisualKind::Image);
    auto pathNode = w.CreateNode(aero::VisualKind::Vector);
    auto trimPath = w.CreateNode(aero::VisualKind::Vector);
    auto arcNode = w.CreateNode(aero::VisualKind::Vector);
    auto blurNode = w.CreateNode(aero::VisualKind::Box);
    auto blurChild = w.CreateNode(aero::VisualKind::Box);
    auto canvasNode = w.CreateNode(aero::VisualKind::CanvasDraw);

    w.SetRoot(root);
    w.SetLayoutMode(root, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
    w.SetBoxStyle(root, 0xFF181A22u, 0u, 0, 0, 0, 0, 0, 0, 0, 0);

    w.AddChild(root, box);
    w.SetCanvasOffsets(box, 20.0f, 20.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(box, 100.0f, 60.0f);
    w.SetBoxStyle(box, 0xFFFFFFFFu, 0xFFFFFFFFu, 2.0f, 2.0f, 2.0f, 2.0f, 12.0f, 4.0f, 16.0f, 8.0f);
    w.SetBoxGradients(box, static_cast<std::int32_t>(gradId), -1);
    w.SetEffect(box, aero::UIEffectKind::DropShadow, 4.0f, 6.0f, 45.0f, 0xAA000000u, 1.0f);

    w.AddChild(root, textNode);
    w.SetCanvasOffsets(textNode, 140.0f, 20.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(textNode, 120.0f, 32.0f);
    w.SetText(textNode, "Aero", "", 18.0f, 0xFFFFFFFFu, aero::TextWrapping::NoWrap, aero::TextAlignment::Left, aero::TextTrimming::None);
    w.SetTextSelection(textNode, 1, 2, 2, true, 0x883399FFu, 0xFFFFFFFFu);

    w.AddChild(root, imgNode);
    w.SetCanvasOffsets(imgNode, 280.0f, 20.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(imgNode, 64.0f, 64.0f);
    w.SetImage(imgNode, "mem://chk", aero::Stretch::Fill, aero::StretchDirection::Both, 0xFFFFFFFFu);

    w.AddChild(root, pathNode);
    w.SetCanvasOffsets(pathNode, 20.0f, 110.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(pathNode, 60.0f, 60.0f);
    w.SetVectorShape(pathNode, aero::VectorShapeKind::SvgPath, "M 0,0 L 40,0 L 20,40 Z", 0xFF33EE88u, -1, 0u, -1, 0.0f, aero::Stretch::Uniform, 0.0f, 1.0f);

    w.AddChild(root, trimPath);
    w.SetCanvasOffsets(trimPath, 100.0f, 110.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(trimPath, 60.0f, 60.0f);
    w.SetVectorShape(trimPath, aero::VectorShapeKind::SvgPath, "M 0,10 L 50,10 L 50,50", 0u, -1, 0xFFFFAA00u, -1, 4.0f, aero::Stretch::Uniform, 0.1f, 0.8f);

    w.AddChild(root, arcNode);
    w.SetCanvasOffsets(arcNode, 180.0f, 110.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(arcNode, 60.0f, 60.0f);
    w.SetVectorShape(arcNode, aero::VectorShapeKind::RadialArc, "0,270,0.65", 0u, -1, 0xFF44DDFFu, -1, 4.0f, aero::Stretch::Uniform, 0.0f, 1.0f);

    w.AddChild(root, blurNode);
    w.SetCanvasOffsets(blurNode, 260.0f, 110.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(blurNode, 80.0f, 60.0f);
    w.SetLayoutMode(blurNode, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
    w.SetEffect(blurNode, aero::UIEffectKind::Blur, 6.0f, 0.0f, 0.0f, 0xFFFFFFFFu, 1.0f);
    w.AddChild(blurNode, blurChild);
    w.SetSize(blurChild, 50.0f, 30.0f);
    w.SetCanvasOffsets(blurChild, 15.0f, 15.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetBoxStyle(blurChild, 0xFFFF55AAu, 0u, 0, 0, 0, 0, 4, 4, 4, 4);

    w.AddChild(root, canvasNode);
    w.SetCanvasOffsets(canvasNode, 20.0f, 200.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(canvasNode, 200.0f, 80.0f);
    drawBuf.Begin(canvasNode);
    drawBuf.PushClip(canvasNode, 0.0f, 0.0f, 200.0f, 80.0f);
    drawBuf.PushOpacity(canvasNode, 0.85f);
    drawBuf.FillEllipse(canvasNode, 30.0f, 40.0f, 20.0f, 20.0f, 0xFF00FFAAu);
    drawBuf.PushTransform(canvasNode, 70.0f, 10.0f, 1.0f, 1.0f, 0.0f);
    drawBuf.FillRoundedRect(canvasNode, 0.0f, 0.0f, 60.0f, 40.0f, 8.0f, 8.0f, 0xFFFF4444u);
    drawBuf.DrawText(canvasNode, "CV", 10.0f, 10.0f, 40.0f, 20.0f, "", 14.0f, 0xFFFFFFFFu,
        aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    drawBuf.PopTransform(canvasNode);
    drawBuf.PopOpacity(canvasNode);
    drawBuf.PopClip(canvasNode);
    drawBuf.End(canvasNode);

    w.UpdateLayout(400.0f, 300.0f);

    aero::PagedRenderTree renderTree;
    aero::VisualRenderAdapter adapter;
    const std::size_t emitted = adapter.SyncPagedRenderTree(w, &drawBuf, renderTree, &store);
    CHECK(emitted >= 10u);
    CHECK(!w.HasAnyDirty());
    CHECK(!renderTree.BlurLayerSpans().empty());

    std::vector<std::uint8_t> rgba8;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 400u, 300u, rgba8);
    CHECK(rgba8.size() == 400u * 300u * 4u);

    // Check gradient box pixel around (50, 45), checkerboard pixel at (300, 40), canvas ellipse pixel at (50, 240)
    auto pixelAt = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t idx = (static_cast<std::size_t>(y) * 400u + x) * 4u;
        return std::array<std::uint8_t, 4>{rgba8[idx], rgba8[idx + 1u], rgba8[idx + 2u], rgba8[idx + 3u]};
    };
    const auto pBox = pixelAt(50u, 45u);
    CHECK(pBox[0] > 40u || pBox[2] > 40u);
    const auto pImg = pixelAt(300u, 40u);
    CHECK(pImg[2] >= 200u);
    const auto pEllipse = pixelAt(50u, 240u);
    CHECK(pEllipse[1] >= 120u);
}

void TestTrackBEcsBindingZeroAlloc() {
    aero::VisualWorld w;
    auto root = w.CreateNode(aero::VisualKind::Box);
    auto bar = w.CreateNode(aero::VisualKind::Box);
    w.SetRoot(root);
    w.AddChild(root, bar);
    w.SetAlignment(bar, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    w.UpdateLayout(400.0f, 200.0f);

    float colOpacity[2]{1.0f, 0.45f};
    float colWidth[2]{10.0f, 180.0f};
    float colHeight[2]{10.0f, 24.0f};
    float colTx[2]{0.0f, 35.0f};
    float colTy[2]{0.0f, 12.0f};

    CHECK(w.BindEcsColumn(bar, static_cast<std::uint32_t>(aero::VisualAnimProp::Opacity), colOpacity, 1u, 2u));
    CHECK(w.BindEcsColumn(bar, static_cast<std::uint32_t>(aero::VisualAnimProp::Width), colWidth, 1u, 2u));
    CHECK(w.BindEcsColumn(bar, static_cast<std::uint32_t>(aero::VisualAnimProp::Height), colHeight, 1u, 2u));
    CHECK(w.BindEcsColumn(bar, static_cast<std::uint32_t>(aero::VisualAnimProp::TranslateX), colTx, 1u, 2u));
    CHECK(w.BindEcsColumn(bar, static_cast<std::uint32_t>(aero::VisualAnimProp::TranslateY), colTy, 1u, 2u));

    CHECK(w.FlushTrackB() == 5u);
    CHECK(w.FlushTrackB() == 0u); // unchanged values skip without touching dirty bits

    w.UpdateLayout(400.0f, 200.0f);
    const aero::VisualNode* n = w.GetNode(bar);
    CHECK_NEAR(n->layout.opacity, 0.45f, 0.01f);
    CHECK_NEAR(n->layout.layoutRect.width, 180.0f, 0.1f);
    CHECK_NEAR(n->layout.layoutRect.height, 24.0f, 0.1f);
    CHECK_NEAR(n->layout.worldTransform.tx, 35.0f, 0.1f);
    CHECK_NEAR(n->layout.worldTransform.ty, 12.0f, 0.1f);
}

struct AotTestState {
    int invokedCount = 0;
    bool allowCommand = true;
    std::string lastParam;
};

AotTestState* gAotState = nullptr;

bool AotCanExecuteCommand(
    aero::rs_aot::ExecutionContext&,
    const aero::rs_rt::Value* args,
    std::size_t argCount,
    aero::rs_rt::Value& outRes) {
    (void)args;
    (void)argCount;
    outRes = aero::rs_rt::Value{gAotState ? gAotState->allowCommand : true};
    return true;
}

bool AotExecuteCommand(
    aero::rs_aot::ExecutionContext& ctx,
    const aero::rs_rt::Value* args,
    std::size_t argCount,
    aero::rs_rt::Value& outRes) {
    if (gAotState) {
        ++gAotState->invokedCount;
        if (argCount >= 2u) {
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&args[1])) {
                gAotState->lastParam = sv->str();
            } else if (const auto* ov = std::get_if<aero::rs_rt::ObjectRef>(&args[1]);
                       ov && ov->valid() && ctx.heap()) {
                if (const auto view = ctx.heap()->stringView(*ov)) {
                    gAotState->lastParam.assign(*view);
                }
            }
        }
    }
    outRes = aero::rs_rt::Value{};
    return true;
}

void TestHostBindingsAndReflection() {
    auto simHeap = std::make_shared<aero::rs_rt::ManagedHeap>();
    auto uiHeap = std::make_shared<aero::rs_rt::ManagedHeap>();
    auto handles = std::make_shared<aero::rs_rt::NativeHandleRegistry>();
    aero::ExecutionDomainContext domains(simHeap, uiHeap, handles);
    domains.setActiveDomain(aero::DomainKind::UiPresentation);

    // 1. Test ProgramImage + Interpreter dot-separated reflection + dirty mask + Command
    const aero::rs_sem::SymbolId kChildTypeId =
        aero::rs_sem::stableTypeId(std::string("TestVm::ProfileVm"));
    const aero::rs_sem::SymbolId kRootTypeId =
        aero::rs_sem::stableTypeId(std::string("TestVm::MainVm"));
    const aero::rs_sem::SymbolId kCmdTypeId =
        aero::rs_sem::stableTypeId(std::string("TestVm::TestCmd"));
    constexpr aero::rs_sem::SymbolId kCanExecFnId = 6001u;
    constexpr aero::rs_sem::SymbolId kExecFnId = 6002u;

    aero::rs_bc::Module mod{};
    mod.name = "TestVm";
    mod.languageMetadata.attributes.push_back(
        aero::rs_comp::LanguageAttributeRecord{"TestVm::ProfileVm", "Observable", {}, {}, 0u});
    {
        aero::rs_sem::TypeSymbol childType{};
        childType.id = kChildTypeId;
        childType.moduleName = "TestVm";
        childType.name = "ProfileVm";
        aero::rs_sem::FieldSymbol fTitle{};
        fTitle.declaringTypeId = kChildTypeId;
        fTitle.declaringTypeName = "TestVm::ProfileVm";
        fTitle.index = 0u;
        fTitle.name = "Title";
        fTitle.type = aero::rs_sem::PrimitiveType::String;
        aero::rs_sem::FieldSymbol fScore{};
        fScore.declaringTypeId = kChildTypeId;
        fScore.declaringTypeName = "TestVm::ProfileVm";
        fScore.index = 1u;
        fScore.name = "Score";
        fScore.type = aero::rs_sem::PrimitiveType::Int;
        aero::rs_sem::FieldSymbol fRatio{};
        fRatio.declaringTypeId = kChildTypeId;
        fRatio.declaringTypeName = "TestVm::ProfileVm";
        fRatio.index = 2u;
        fRatio.name = "Ratio";
        fRatio.type = aero::rs_sem::PrimitiveType::Float;
        aero::rs_sem::FieldSymbol fActive{};
        fActive.declaringTypeId = kChildTypeId;
        fActive.declaringTypeName = "TestVm::ProfileVm";
        fActive.index = 3u;
        fActive.name = "Active";
        fActive.type = aero::rs_sem::PrimitiveType::Bool;
        childType.fields = {fTitle, fScore, fRatio, fActive};
        mod.types.push_back(std::move(childType));

        aero::rs_sem::TypeSymbol rootType{};
        rootType.id = kRootTypeId;
        rootType.moduleName = "TestVm";
        rootType.name = "MainVm";
        aero::rs_sem::FieldSymbol fChild{};
        fChild.declaringTypeId = kRootTypeId;
        fChild.declaringTypeName = "TestVm::MainVm";
        fChild.index = 0u;
        fChild.name = "Profile";
        fChild.type = aero::rs_sem::PrimitiveType::Object;
        fChild.typeName = "TestVm::ProfileVm";
        aero::rs_sem::FieldSymbol fCmd{};
        fCmd.declaringTypeId = kRootTypeId;
        fCmd.declaringTypeName = "TestVm::MainVm";
        fCmd.index = 1u;
        fCmd.name = "SubmitCommand";
        fCmd.type = aero::rs_sem::PrimitiveType::Object;
        fCmd.typeName = "TestVm::TestCmd";
        rootType.fields = {fChild, fCmd};
        mod.types.push_back(std::move(rootType));

        aero::rs_sem::TypeSymbol cmdType{};
        cmdType.id = kCmdTypeId;
        cmdType.moduleName = "TestVm";
        cmdType.name = "TestCmd";
        mod.types.push_back(std::move(cmdType));
    }

    aero::rs_rt::RuntimeError linkErr{};
    auto progImgOpt = aero::rs_rt::ProgramImage::link({std::move(mod)}, linkErr);
    if (!progImgOpt.has_value()) {
        std::fprintf(stderr, "ProgramImage::link error: %s\n", linkErr.message.c_str());
    }
    CHECK(progImgOpt.has_value());
    if (!progImgOpt.has_value()) {
        gAotState = nullptr;
        return;
    }
    auto progImgPtr = std::make_shared<const aero::rs_rt::ProgramImage>(std::move(*progImgOpt));
    domains.registerProgramObservableMetadata(*progImgPtr);

    aero::UIResourceStore store;
    aero::VisualWorld world(handles.get(), &store, uiHeap.get());
    aero::DrawingCommandBuffer drawBuf;
    aero::VisualAnimationEngine anim;
    aero::VisualInputRouter router(&world);
    aero::rs_rt::Interpreter interp(progImgPtr, uiHeap);

    auto bindings = std::make_shared<aero::rs_rt::BindingRegistry>();
    CHECK(aero::RegisterVisualHostBindings(
        *bindings, world, drawBuf, anim, router, domains, progImgPtr.get(), &interp, nullptr, nullptr));

    auto invokeBound = [&](const char* name, std::vector<aero::rs_rt::Value> args) {
        aero::rs_bc::FunctionReference ref{};
        ref.name = name;
        aero::rs_rt::RuntimeError err{};
        return bindings->invoke(ref, aero::rs_rt::ValueSpan(args), err);
    };

    // Check canonical, bare, and __rs_native_ aliases exist
    {
        aero::rs_bc::FunctionReference canonRef{};
        canonRef.name = "Aero.Native::Visual_Create";
        aero::rs_bc::FunctionReference bareRef{};
        bareRef.name = "Visual_Create";
        aero::rs_bc::FunctionReference nativeCanonRef{};
        nativeCanonRef.name = "Aero.Native::__rs_native_Visual_Create";
        aero::rs_bc::FunctionReference nativeBareRef{};
        nativeBareRef.name = "__rs_native_Visual_Create";
        CHECK(bindings->resolve(canonRef).has_value());
        CHECK(bindings->resolve(bareRef).has_value());
        CHECK(bindings->resolve(nativeCanonRef).has_value());
        CHECK(bindings->resolve(nativeBareRef).has_value());
    }

    // Color_ParseRgba (#RGB, #ARGB, #RRGGBB, #AARRGGBB, named WPF CornflowerBlue)
    {
        const auto cRgb = invokeBound("Color_ParseRgba", {MakeStrVal("#F0A")});
        CHECK(cRgb.has_value() && std::get<aero::rs_rt::UIntValue>(*cRgb).value == 0xFFAA00FFu);

        const auto cArgb = invokeBound("Color_ParseRgba", {MakeStrVal("#80FF0000")});
        CHECK(cArgb.has_value() && std::get<aero::rs_rt::UIntValue>(*cArgb).value == 0x800000FFu);

        const auto cNamed = invokeBound("Color_ParseRgba", {MakeStrVal("CornflowerBlue")});
        CHECK(cNamed.has_value() && std::get<aero::rs_rt::UIntValue>(*cNamed).value == 0xFFED9564u); // RGBA8 LE of #FF6495ED

        auto readBoundStr = [&](const std::optional<aero::rs_rt::Value>& v) -> std::string {
            if (!v.has_value()) { return "<MISSING>"; }
            if (const auto* sv = std::get_if<aero::rs_rt::StringValue>(&*v)) {
                return sv->str();
            }
            if (const auto* ref = std::get_if<aero::rs_rt::ObjectRef>(&*v);
                ref && ref->valid() && ref->kind == aero::rs_rt::ObjectKind::String) {
                if (const auto opt = domains.stringViewAcrossDomains(*ref)) {
                    return std::string(opt->data(), opt->size());
                }
            }
            return "<NONSTR>";
        };
        const auto sliced = invokeBound("Utf8_SliceCodePoints", {
            MakeStrVal("hello"), aero::rs_rt::Value{2LL}, aero::rs_rt::Value{2147483647LL}});
        CHECK(readBoundStr(sliced) == "llo");

        const auto sLow = invokeBound("Utf8_FromCodePoint", {aero::rs_rt::Value{0xD800LL}});
        CHECK(readBoundStr(sLow).empty());

        const auto sHigh = invokeBound("Utf8_FromCodePoint", {aero::rs_rt::Value{0xDFFFLL}});
        CHECK(readBoundStr(sHigh).empty());

        const auto sEmoji = invokeBound("Utf8_FromCodePoint", {aero::rs_rt::Value{0x1F600LL}});
        CHECK(readBoundStr(sEmoji) == "\xF0\x9F\x98\x80");
    }

    // Invoke Visual_* bindings directly through BindingRegistry (bare & canonical)
    {
        const auto hOpt = invokeBound("Aero.Native::Visual_Create", {aero::rs_rt::Value{0LL}});
        CHECK(hOpt.has_value());
        aero::rs_rt::NativeHandle nh{};
        CHECK(hOpt->tryGetNativeHandle(nh) && nh.valid());
        const auto childOpt = invokeBound("Visual_Create", {aero::rs_rt::Value{0LL}});
        CHECK(childOpt.has_value());
        aero::rs_rt::NativeHandle ch{};
        CHECK(childOpt->tryGetNativeHandle(ch));
        (void)invokeBound("Visual_AddChild", {aero::rs_rt::Value{nh}, aero::rs_rt::Value{ch}});
        (void)invokeBound("Visual_SetLayoutMode", {
            aero::rs_rt::Value{nh},
            aero::rs_rt::Value{static_cast<std::int64_t>(aero::LayoutMode::ScrollViewport)},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{0.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{0.0f}},
            aero::rs_rt::Value{0LL},
            aero::rs_rt::Value{0LL}});
        (void)invokeBound("Visual_SetSize", {
            aero::rs_rt::Value{ch},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{640.0f}},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{480.0f}}});
        world.SetRoot(world.ResolveHandle(nh));
        world.UpdateLayout(200.0f, 150.0f);
        const auto extW = invokeBound("Visual_GetLayoutRect", {aero::rs_rt::Value{nh}, aero::rs_rt::Value{6LL}});
        float extWVal = 0.0f;
        CHECK(extW.has_value() && extW->tryGetFloat(extWVal));
        CHECK_NEAR(extWVal, 640.0f, 0.1f);
        const auto childW = invokeBound("Visual_GetLayoutRect", {aero::rs_rt::Value{ch}, aero::rs_rt::Value{2LL}});
        float childWVal = 0.0f;
        CHECK(childW.has_value() && childW->tryGetFloat(childWVal));
        CHECK_NEAR(childWVal, 640.0f, 0.1f);

        // Void host bindings must return engaged Value{int64_t{0}} (both canonical & __rs_native_)
        const aero::VisualNodeId rootNid = world.ResolveHandle(nh);
        const auto op1 = invokeBound("Aero.Native::Visual_SetOpacity", {
            aero::rs_rt::Value{nh},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{0.42f}}});
        std::int64_t op1Ret = -1;
        CHECK(op1.has_value() && op1->tryGetInt64(op1Ret) && op1Ret == 0);
        CHECK(world.GetNode(rootNid) != nullptr);
        CHECK_NEAR(world.GetNode(rootNid)->layout.opacity, 0.42f, 0.001f);

        const auto op2 = invokeBound("Aero.Native::__rs_native_Visual_SetOpacity", {
            aero::rs_rt::Value{nh},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{0.84f}}});
        std::int64_t op2Ret = -1;
        CHECK(op2.has_value() && op2->tryGetInt64(op2Ret) && op2Ret == 0);
        CHECK_NEAR(world.GetNode(rootNid)->layout.opacity, 0.84f, 0.001f);

        // Stale/invalid handle no-op on void host binding must also return engaged Value{int64_t{0}}
        const aero::rs_rt::NativeHandle staleNh{};
        const auto opStale = invokeBound("Visual_SetOpacity", {
            aero::rs_rt::Value{staleNh},
            aero::rs_rt::Value{aero::rs_rt::FloatValue{0.1f}}});
        std::int64_t opStaleRet = -1;
        CHECK(opStale.has_value() && opStale->tryGetInt64(opStaleRet) && opStaleRet == 0);
    }

    // Allocate RootVm -> ChildVm on uiHeap & test dot-separated Reflect_GetProp* / SetProp* / DirtyMask
    uiHeap->registerTypeHierarchy(*progImgPtr);
    uiHeap->enableTypeDirtyTracking(kChildTypeId, {0u, 1u, 2u, 3u});

    auto strInit = uiHeap->internString("Initial");
    CHECK(strInit.has_value());
    auto childObjOpt = uiHeap->allocateObject(
        kChildTypeId,
        {aero::rs_rt::Value{*strInit},
         aero::rs_rt::Value{12LL},
         aero::rs_rt::Value{aero::rs_rt::FloatValue{0.75f}},
         aero::rs_rt::Value{true}},
        {0u});
    CHECK(childObjOpt.has_value());
    auto rootObjOpt = uiHeap->allocateObject(
        kRootTypeId,
        {aero::rs_rt::Value{*childObjOpt}, aero::rs_rt::Value{aero::rs_rt::NullObject{}}},
        {0u, 1u});
    CHECK(rootObjOpt.has_value());

    const auto gotStr = invokeBound("Reflect_GetPropString", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Title")});
    CHECK(gotStr.has_value() && std::get<aero::rs_rt::StringValue>(*gotStr).str() == "Initial");

    const auto setOk = invokeBound("Reflect_SetPropString", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Title"),
        MakeStrVal("Updated")});
    bool setOkB = false;
    CHECK(setOk.has_value() && setOk->tryGetBool(setOkB) && setOkB);

    const auto gotUpdated = invokeBound("Reflect_GetPropString", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Title")});
    CHECK(gotUpdated.has_value() && std::get<aero::rs_rt::StringValue>(*gotUpdated).str() == "Updated");

    // Reflect_GetPropObject on string fields must return NullObject rather than leaking ObjectKind::String
    const auto strAsObj = invokeBound("Reflect_GetPropObject", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Title")});
    CHECK(strAsObj.has_value() && std::holds_alternative<aero::rs_rt::NullObject>(*strAsObj));

    const auto recordAsObj = invokeBound("Reflect_GetPropObject", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile")});
    CHECK(recordAsObj.has_value() &&
          std::holds_alternative<aero::rs_rt::ObjectRef>(*recordAsObj) &&
          std::get<aero::rs_rt::ObjectRef>(*recordAsObj).kind == aero::rs_rt::ObjectKind::Record);

    (void)invokeBound("Reflect_SetPropInt", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Score"),
        aero::rs_rt::Value{99LL}});
    const auto gotInt = invokeBound("Reflect_GetPropInt", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Score")});
    std::int64_t scoreVal = 0;
    CHECK(gotInt.has_value() && gotInt->tryGetInt64(scoreVal) && scoreVal == 99);

    (void)invokeBound("Reflect_SetPropFloat", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Ratio"),
        aero::rs_rt::Value{aero::rs_rt::FloatValue{0.25f}}});
    const auto gotFloat = invokeBound("Reflect_GetPropFloat", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Ratio")});
    float ratioVal = 0.0f;
    CHECK(gotFloat.has_value() && gotFloat->tryGetFloat(ratioVal));
    CHECK_NEAR(ratioVal, 0.25f, 0.001f);

    (void)invokeBound("Reflect_SetPropBool", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Active"),
        aero::rs_rt::Value{false}});
    const auto gotBool = invokeBound("Reflect_GetPropBool", {
        aero::rs_rt::Value{*rootObjOpt},
        MakeStrVal("Profile.Active")});
    bool activeVal = true;
    CHECK(gotBool.has_value() && gotBool->tryGetBool(activeVal) && !activeVal);

    auto cleanChildOpt = uiHeap->allocateObject(
        kChildTypeId,
        {aero::rs_rt::Value{*strInit},
         aero::rs_rt::Value{1LL},
         aero::rs_rt::Value{aero::rs_rt::FloatValue{0.5f}},
         aero::rs_rt::Value{true}},
        {0u});
    CHECK(cleanChildOpt.has_value());
    const auto cleanDirtyVer = invokeBound("Reflect_GetDirtyMask", {aero::rs_rt::Value{*cleanChildOpt}});
    std::int64_t cleanVerVal = -1;
    CHECK(cleanDirtyVer.has_value() && cleanDirtyVer->tryGetInt64(cleanVerVal) && cleanVerVal == 1LL);

    const auto dirtyMask = invokeBound("Reflect_GetDirtyMask", {aero::rs_rt::Value{*childObjOpt}});
    std::int64_t maskBits = 0;
    CHECK(dirtyMask.has_value() && dirtyMask->tryGetInt64(maskBits) && maskBits >= 5LL);

    // Ensure Reflect_GetDirtyMask reports strictly increasing mutation versions (including across version == 16)
    std::int64_t prevVer = maskBits;
    bool sawModulo16Step = false;
    for (int step = 0; step < 16; ++step) {
        (void)invokeBound("Reflect_SetPropInt", {
            aero::rs_rt::Value{*rootObjOpt},
            MakeStrVal("Profile.Score"),
            aero::rs_rt::Value{static_cast<std::int64_t>(100 + step)}});
        const auto nextDirtyVer = invokeBound("Reflect_GetDirtyMask", {aero::rs_rt::Value{*childObjOpt}});
        std::int64_t nextVer = 0;
        CHECK(nextDirtyVer.has_value() && nextDirtyVer->tryGetInt64(nextVer) && nextVer == prevVer + 1);
        if ((nextVer & 0x0FLL) == 0LL) {
            sawModulo16Step = true;
        }
        prevVer = nextVer;
    }
    CHECK(sawModulo16Step);

    // 2. Test AOT ExecutionContext Reflection & Command execution (CanExecute + Execute)
    AotTestState aotState{};
    gAotState = &aotState;

    const aero::rs_aot::FieldDescriptor kAotRootFields[] = {
        {"SubmitCommand", aero::rs_sem::PrimitiveType::Object, kCmdTypeId, 0u, false, false, false},
        {"$Counter", aero::rs_sem::PrimitiveType::Int, 0u, 1u, true, false, false},
    };
    const aero::rs_aot::TypeDescriptor kAotTypes[] = {
        {kRootTypeId, aero::rs_sem::TypeKind::Class, 0u, "TestAot", "AotVm", kAotRootFields, 2u, nullptr, 0u, nullptr, 0u, false, nullptr, 0u, false, false, false},
        {kCmdTypeId, aero::rs_sem::TypeKind::Class, 0u, "TestAot", "AotCmd", nullptr, 0u, nullptr, 0u, nullptr, 0u, false, nullptr, 0u, false, false, false},
    };
    static const aero::rs_sem::PrimitiveType kCanExecParams[] = {aero::rs_sem::PrimitiveType::Object, aero::rs_sem::PrimitiveType::String};
    static const aero::rs_sem::PrimitiveType kExecParams[] = {aero::rs_sem::PrimitiveType::Object, aero::rs_sem::PrimitiveType::String};
    static const aero::rs_aot::FunctionDescriptor kAotFuncs[] = {
        {kCanExecFnId, "TestAot::AotCmd.CanExecute", aero::rs_sem::PrimitiveType::Bool, 0u, kCanExecParams, nullptr, 2u, &AotCanExecuteCommand, nullptr, nullptr},
        {kExecFnId, "TestAot::AotCmd.Execute", aero::rs_sem::PrimitiveType::Void, 0u, kExecParams, nullptr, 2u, &AotExecuteCommand, nullptr, nullptr},
    };
    aero::rs_aot::ProgramDescriptor aotDesc{};
    aotDesc.name = "AotReflectTest";
    aotDesc.types = kAotTypes;
    aotDesc.typeCount = 2u;
    aotDesc.functions = kAotFuncs;
    aotDesc.functionCount = 2u;

    auto aotBindings = std::make_shared<aero::rs_rt::BindingRegistry>();
    aero::rs_aot::ExecutionContext aotCtx(aotDesc, uiHeap, aotBindings);
    CHECK(aero::RegisterVisualHostBindings(
        *aotBindings, world, drawBuf, anim, router, domains, nullptr, nullptr, &aotCtx, nullptr));

    auto aotCmdOpt = uiHeap->allocateObject(kCmdTypeId, {}, {});
    auto aotVmOpt = uiHeap->allocateObject(
        kRootTypeId, {aero::rs_rt::Value{*aotCmdOpt}, aero::rs_rt::Value{7LL}}, {0u});
    CHECK(aotCmdOpt.has_value() && aotVmOpt.has_value());

    aero::rs_bc::FunctionReference aotGetInt{};
    aotGetInt.name = "Reflect_GetPropInt";
    aero::rs_rt::RuntimeError aotErr{};
    std::vector<aero::rs_rt::Value> getArgs{
        aero::rs_rt::Value{*aotVmOpt},
        MakeStrVal("Counter")};
    const auto gotAotInt = aotBindings->invoke(aotGetInt, aero::rs_rt::ValueSpan(getArgs), aotErr);
    std::int64_t aotCounter = 0;
    CHECK(gotAotInt.has_value() && gotAotInt->tryGetInt64(aotCounter) && aotCounter == 7);

    aero::rs_bc::FunctionReference aotInvokeCmd{};
    aotInvokeCmd.name = "Reflect_InvokeCommand";
    std::vector<aero::rs_rt::Value> cmdArgs{
        aero::rs_rt::Value{*aotVmOpt},
        MakeStrVal("SubmitCommand"),
        MakeStrVal("payload_42")};
    const auto cmdOk = aotBindings->invoke(aotInvokeCmd, aero::rs_rt::ValueSpan(cmdArgs), aotErr);
    bool cmdOkB = false;
    CHECK(cmdOk.has_value() && cmdOk->tryGetBool(cmdOkB) && cmdOkB);
    CHECK(aotState.invokedCount == 1 && aotState.lastParam == "payload_42");

    aotState.allowCommand = false;
    const auto cmdBlocked = aotBindings->invoke(aotInvokeCmd, aero::rs_rt::ValueSpan(cmdArgs), aotErr);
    bool cmdBlockedB = true;
    CHECK(cmdBlocked.has_value() && cmdBlocked->tryGetBool(cmdBlockedB) && !cmdBlockedB);
    CHECK(aotState.invokedCount == 1);
    gAotState = nullptr;
}

void TestBlendModePageHeaderAndBlurSpanAdditive() {
    static_assert(sizeof(aero::AeroVertex) == 24, "AeroVertex must remain exactly 24 bytes");
    static_assert(sizeof(aero::CommandPage) == 4096, "CommandPage must remain exactly 4096 bytes");
    static_assert(aero::kVerticesPerCommandPage == 168u, "CommandPage must remain 168 vertices");

    aero::UIResourceStore store;
    aero::VisualWorld w(nullptr, &store);
    aero::DrawingCommandBuffer drawBuf;

    auto root = w.CreateNode(aero::VisualKind::Box);
    auto bg = w.CreateNode(aero::VisualKind::Box);
    auto addBox = w.CreateNode(aero::VisualKind::Box);
    auto blurAdd = w.CreateNode(aero::VisualKind::Box);
    auto blurChild = w.CreateNode(aero::VisualKind::Box);

    w.SetRoot(root);
    w.SetLayoutMode(root, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
    w.SetSize(root, 240.0f, 160.0f);

    // Background RGB=(64, 32, 16) in 0xAABBGGRR
    w.AddChild(root, bg);
    w.SetCanvasOffsets(bg, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(bg, 240.0f, 160.0f);
    w.SetBoxStyle(bg, 0xFF102040u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // Direct Additive box RGB=(48, 80, 32)
    w.AddChild(root, addBox);
    w.SetCanvasOffsets(addBox, 16.0f, 16.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(addBox, 64.0f, 64.0f);
    w.SetBoxStyle(addBox, 0xFF205030u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    w.GetNode(addBox)->transform2D.blendMode = aero::BlendingMode::Additive;
    w.MarkDirty(addBox, aero::VisualDirty_Visual);

    // Offscreen Blur + Additive composite layer RGB=(48, 80, 32)
    w.AddChild(root, blurAdd);
    w.SetCanvasOffsets(blurAdd, 110.0f, 16.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(blurAdd, 80.0f, 80.0f);
    w.SetLayoutMode(blurAdd, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
    w.SetEffect(blurAdd, aero::UIEffectKind::Blur, 4.0f, 0.0f, 0.0f, 0xFFFFFFFFu, 1.0f);
    w.GetNode(blurAdd)->transform2D.blendMode = aero::BlendingMode::Additive;
    w.MarkDirty(blurAdd, aero::VisualDirty_Visual);

    w.AddChild(blurAdd, blurChild);
    w.SetCanvasOffsets(blurChild, 10.0f, 10.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(blurChild, 60.0f, 60.0f);
    w.SetBoxStyle(blurChild, 0xFF205030u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    w.UpdateLayout(240.0f, 160.0f);

    aero::PagedRenderTree renderTree;
    aero::VisualRenderAdapter adapter;
    const std::size_t emitted = adapter.SyncPagedRenderTree(w, &drawBuf, renderTree, &store);
    CHECK(emitted >= 3u);

    const aero::VisualNode* addPtr = w.GetNode(addBox);
    bool foundAddNodeHeader = false;
    for (std::uint32_t i = 0u; i < renderTree.NodeCount(); ++i) {
        const aero::RenderNodeDescriptor* rd = renderTree.GetNode(i);
        if (rd && rd->sourceElement == addPtr) {
            CHECK(rd->blendMode == aero::BlendingMode::Additive);
            const auto& page = renderTree.GetPage(rd->pageIndex);
            CHECK(page.header.BlendMode() == static_cast<std::uint32_t>(aero::BlendingMode::Additive));
            foundAddNodeHeader = true;
        }
    }
    CHECK(foundAddNodeHeader);

    bool foundAdditiveBlurSpan = false;
    for (const auto& span : renderTree.BlurLayerSpans()) {
        if (span.effectMode == aero::UIEffectKind::Blur &&
            span.blendMode == aero::BlendingMode::Additive) {
            foundAdditiveBlurSpan = true;
        }
    }
    CHECK(foundAdditiveBlurSpan);

    std::vector<std::uint8_t> rgba;
    aero::ZeroCopyGpuStream::RasterizeTreeToRgba8(renderTree, 240u, 160u, rgba);
    CHECK(rgba.size() == 240u * 160u * 4u);

    auto pxAt = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t idx = (static_cast<std::size_t>(y) * 240u + x) * 4u;
        return std::array<std::uint8_t, 4>{rgba[idx], rgba[idx + 1u], rgba[idx + 2u], rgba[idx + 3u]};
    };
    const auto pAdd = pxAt(48u, 48u);
    CHECK(pAdd[0] > 90u && pAdd[1] > 90u);
    const auto pBlurAdd = pxAt(150u, 56u);
    CHECK(pBlurAdd[0] > 85u && pBlurAdd[1] > 85u);
}

void TestImageAtlasConcurrentSamplingSafety() {
    aero::ImageAtlas atlas;
    std::vector<std::uint8_t> initPixels(64u * 64u * 4u, 0u);
    for (std::size_t i = 0u; i < initPixels.size(); i += 4u) {
        initPixels[i + 0u] = 200u;
        initPixels[i + 1u] = 100u;
        initPixels[i + 2u] = 50u;
        initPixels[i + 3u] = 255u;
    }
    const std::uint32_t slotId = atlas.RegisterOrUpdateRgbaTextureSlot(0u, 64u, 64u, std::move(initPixels));
    CHECK(slotId > 0u);

    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&, t]() {
            for (int iter = 0; iter < 4000; ++iter) {
                const float u = static_cast<float>((iter + t * 17) % 100) / 99.0f;
                const float v = static_cast<float>((iter * 3 + t * 29) % 100) / 99.0f;
                const aero::Color c = atlas.SampleBilinearRgba(slotId, u, v);
                if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) || !std::isfinite(c.a) ||
                    c.r < 0.0f || c.r > 1.0f || c.a < 0.0f || c.a > 1.0f) {
                    failed.store(true, std::memory_order_relaxed);
                }
            }
        });
    }

    std::thread writer([&]() {
        for (int w = 0; w < 120; ++w) {
            const std::uint32_t dim = (w % 2 == 0) ? 32u : 96u;
            std::vector<std::uint8_t> px(static_cast<std::size_t>(dim) * dim * 4u, 255u);
            for (std::size_t i = 0u; i < px.size(); i += 4u) {
                px[i + 0u] = static_cast<std::uint8_t>(50 + (w % 150));
                px[i + 1u] = 160u;
                px[i + 2u] = 220u;
                px[i + 3u] = 255u;
            }
            const std::uint32_t updated = atlas.RegisterOrUpdateRgbaTextureSlot(slotId, dim, dim, std::move(px));
            if (updated == 0u) {
                failed.store(true, std::memory_order_relaxed);
            }
        }
        stop.store(true, std::memory_order_release);
    });

    writer.join();
    for (auto& th : readers) {
        th.join();
    }
    CHECK(!failed.load());
    CHECK(stop.load());

    // Also test Clear() race safety with concurrent readers on a fresh slot
    atlas.Clear();
    std::vector<std::uint8_t> px2(32u * 32u * 4u, 128u);
    const std::uint32_t slot2 = atlas.RegisterOrUpdateRgbaTextureSlot(0u, 32u, 32u, std::move(px2));
    CHECK(slot2 > 0u);
    std::thread clearReader([&]() {
        for (int i = 0; i < 1000; ++i) {
            const aero::Color c = atlas.SampleBilinearRgba(slot2, 0.5f, 0.5f);
            if (!std::isfinite(c.r) || !std::isfinite(c.a)) {
                failed.store(true, std::memory_order_relaxed);
            }
        }
    });
    atlas.Clear();
    clearReader.join();
    CHECK(!failed.load());
}

void TestDeepHierarchyOver70Levels() {
    aero::UIResourceStore store;
    aero::VisualWorld w(nullptr, &store);

    constexpr int kDepthCount = 85;
    std::vector<aero::VisualNodeId> chain;
    chain.reserve(kDepthCount);

    for (int d = 0; d < kDepthCount; ++d) {
        auto node = w.CreateNode(aero::VisualKind::Box);
        CHECK(node.IsValid());
        w.SetLayoutMode(node, aero::LayoutMode::Stack, 0.0f, 0.0f, 1, 0);
        if (d == 0) {
            w.SetRoot(node);
        } else {
            CHECK(w.AddChild(chain.back(), node));
            w.SetMargin(node, 1.0f, 1.0f, 0.0f, 0.0f);
        }
        chain.push_back(node);
    }

    const aero::VisualNodeId leaf = chain.back();
    w.SetSize(leaf, 40.0f, 20.0f);
    w.SetAlignment(leaf, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    w.UpdateLayout(800.0f, 600.0f);

    const aero::VisualNode* leafNode = w.GetNode(leaf);
    CHECK(leafNode != nullptr);
    CHECK(leafNode->layout.treeDepth == static_cast<std::uint16_t>(kDepthCount - 1));
    CHECK_NEAR(leafNode->layout.layoutRect.x, static_cast<float>(kDepthCount - 1), 0.1f);
    CHECK_NEAR(leafNode->layout.layoutRect.y, static_cast<float>(kDepthCount - 1), 0.1f);
    CHECK_NEAR(leafNode->layout.layoutRect.width, 40.0f, 0.1f);
    CHECK_NEAR(leafNode->layout.layoutRect.height, 20.0f, 0.1f);

    // Incremental update at depth 84 must also propagate cleanly
    w.SetSize(leaf, 72.0f, 36.0f);
    w.UpdateLayout(800.0f, 600.0f);
    leafNode = w.GetNode(leaf);
    CHECK_NEAR(leafNode->layout.layoutRect.width, 72.0f, 0.1f);
    CHECK_NEAR(leafNode->layout.layoutRect.height, 36.0f, 0.1f);
}

void TestPerspective3DPageVertexClipW() {
    static_assert(sizeof(aero::AeroVertex) == 24, "AeroVertex must stay 24 bytes");
    static_assert(sizeof(aero::CommandPage) == 4096, "CommandPage must stay 4096 bytes");

    aero::UIResourceStore store;
    aero::VisualWorld w(nullptr, &store);
    aero::DrawingCommandBuffer drawBuf;

    auto root = w.CreateNode(aero::VisualKind::Box);
    auto flatBox = w.CreateNode(aero::VisualKind::Box);
    auto card3d = w.CreateNode(aero::VisualKind::Box);
    auto text3d = w.CreateNode(aero::VisualKind::Text);

    w.SetRoot(root);
    w.SetLayoutMode(root, aero::LayoutMode::Canvas, 0.0f, 0.0f, 0, 0);
    w.SetSize(root, 600.0f, 400.0f);

    w.AddChild(root, flatBox);
    w.SetCanvasOffsets(flatBox, 20.0f, 20.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(flatBox, 100.0f, 60.0f);
    w.SetBoxStyle(flatBox, 0xFF335577u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    w.AddChild(root, card3d);
    w.SetCanvasOffsets(card3d, 200.0f, 80.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(card3d, 160.0f, 100.0f);
    w.SetBoxStyle(card3d, 0xFF44AAFFu, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, 4.0f, 4.0f, 4.0f);
    w.SetTransform3D(card3d, 0.0f, 45.0f, 0.0f, 0.0f, 0.0f, 0.0f, 400.0f);

    w.AddChild(root, text3d);
    w.SetCanvasOffsets(text3d, 200.0f, 220.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    w.SetSize(text3d, 140.0f, 36.0f);
    w.SetText(text3d, "3D", "", 18.0f, 0xFFFFFFFFu, aero::TextWrapping::NoWrap, aero::TextAlignment::Left, aero::TextTrimming::None);
    w.SetTransform3D(text3d, 0.0f, 35.0f, 0.0f, 0.0f, 0.0f, 0.0f, 500.0f);

    w.UpdateLayout(600.0f, 400.0f);

    aero::PagedRenderTree renderTree;
    aero::VisualRenderAdapter adapter;
    const std::size_t emitted = adapter.SyncPagedRenderTree(w, &drawBuf, renderTree, &store);
    CHECK(emitted >= 3u);

    const aero::VisualNode* flatPtr = w.GetNode(flatBox);
    const aero::VisualNode* cardPtr = w.GetNode(card3d);
    const aero::VisualNode* textPtr = w.GetNode(text3d);

    bool checkedFlat = false;
    bool checkedCard3d = false;
    bool checkedText3d = false;

    for (std::uint32_t i = 0u; i < renderTree.NodeCount(); ++i) {
        const aero::RenderNodeDescriptor* rd = renderTree.GetNode(i);
        if (!rd) { continue; }
        const float* clipW = renderTree.PageVertexClipW(rd->pageIndex);
        CHECK(clipW != nullptr);
        if (!clipW) { continue; }

        if (rd->sourceElement == flatPtr && rd->vertexCount == 4u) {
            for (std::uint16_t v = 0u; v < 4u; ++v) {
                CHECK_NEAR(clipW[rd->vertexOffsetInPage + v], 1.0f, 1e-4f);
            }
            checkedFlat = true;
        }
        if (rd->sourceElement == cardPtr && rd->vertexCount == 4u) {
            CHECK(rd->hasPerspective3D);
            const float w0 = clipW[rd->vertexOffsetInPage + 0u];
            const float w1 = clipW[rd->vertexOffsetInPage + 1u];
            const float w2 = clipW[rd->vertexOffsetInPage + 2u];
            const float w3 = clipW[rd->vertexOffsetInPage + 3u];
            CHECK(w0 >= 1e-3f && w1 >= 1e-3f && w2 >= 1e-3f && w3 >= 1e-3f);
            CHECK(std::fabs(w0 - w1) > 0.05f);
            CHECK_NEAR(w0, w3, 1e-3f);
            CHECK_NEAR(w1, w2, 1e-3f);
            checkedCard3d = true;
        }
        if (rd->sourceElement == textPtr && rd->vertexCount >= 8u) {
            CHECK(rd->hasPerspective3D);
            const float wFirst = clipW[rd->vertexOffsetInPage + 0u];
            const float wLast = clipW[rd->vertexOffsetInPage + rd->vertexCount - 1u];
            CHECK(wFirst >= 1e-3f && wLast >= 1e-3f);
            CHECK(std::fabs(wFirst - wLast) > 1e-3f);
            checkedText3d = true;
        }
    }
    CHECK(checkedFlat && checkedCard3d && checkedText3d);

    aero::ZeroCopyGpuStream stream(aero::ShaderBackend::DesktopGL33, 16u);
    const aero::ZeroCopySubmissionStats stats = stream.SubmitDirtyPages(renderTree);
    CHECK(stats.dirtyPagesUploaded >= 1u);
    CHECK(!stream.LastBufferSubDataSpans().empty());
    const auto& span0 = stream.LastBufferSubDataSpans().front();
    const std::size_t activeVerts = renderTree.GetPage(span0.pageIndex).header.vertexCount;
    CHECK(span0.sourcePageClipWPtr != nullptr);
    CHECK(span0.clipWByteSize >= activeVerts * sizeof(float));
    const float* mappedW = stream.MappedPageClipW(span0.pageIndex);
    CHECK(mappedW != nullptr);
    if (mappedW) {
        const float* srcW = renderTree.PageVertexClipW(span0.pageIndex);
        for (std::size_t v = 0u; v < activeVerts; ++v) {
            CHECK_NEAR(mappedW[v], srcW[v], 1e-5f);
        }
    }
}

void TestGridStarInsideExplicitWidthAndInfParent() {
    auto store = std::make_shared<aero::UIResourceStore>();
    aero::VisualWorld w(nullptr, store.get());
    const aero::VisualNodeId root = w.CreateNode(aero::VisualKind::Box);
    w.SetRoot(root);
    w.SetLayoutMode(root, aero::LayoutMode::Stack, 0.0f, 0.0f, 1 /* Vertical */, 0);
    w.SetSize(root, 800.0f, 600.0f);

    const aero::VisualNodeId grid = w.CreateNode(aero::VisualKind::Box);
    w.AddChild(root, grid);
    w.SetLayoutMode(grid, aero::LayoutMode::Grid, 0.0f, 0.0f, 0, 0);
    w.SetSize(grid, 300.0f, 120.0f);
    w.SetAlignment(grid, aero::HorizontalAlignment::Left, aero::VerticalAlignment::Top);
    w.GridClearDefs(grid);
    w.GridAddRowDef(grid, aero::GridUnitType::Star, 1.0f);
    w.GridAddColDef(grid, aero::GridUnitType::Star, 1.0f);
    w.GridAddColDef(grid, aero::GridUnitType::Star, 2.0f);

    const aero::VisualNodeId c0 = w.CreateNode(aero::VisualKind::Box);
    w.AddChild(grid, c0);
    w.SetGridCell(c0, 0, 0, 1, 1);

    const aero::VisualNodeId c1 = w.CreateNode(aero::VisualKind::Box);
    w.AddChild(grid, c1);
    w.SetGridCell(c1, 0, 1, 1, 1);

    w.UpdateLayout(800.0f, 600.0f);

    const aero::VisualNode* n0 = w.GetNode(c0);
    const aero::VisualNode* n1 = w.GetNode(c1);
    CHECK(n0 != nullptr && n1 != nullptr);
    if (n0 && n1) {
        CHECK_NEAR(n0->layout.layoutRect.width, 100.0f, 0.5f);
        CHECK_NEAR(n1->layout.layoutRect.width, 200.0f, 0.5f);
        CHECK_NEAR(n1->layout.layoutRect.x, 100.0f, 0.5f);
    }
}

void TestAnimationEngineManagedCustomAndDelayStart() {
    aero::VisualWorld w;
    const aero::VisualNodeId root = w.CreateNode(aero::VisualKind::Box);
    w.SetRoot(root);

    aero::VisualAnimationEngine engine;
    const std::int32_t t1 = engine.StartFloat(
        root, aero::VisualAnimProp::ManagedCustom, 0.0f, 10.0f, 0.2f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, false, 1);
    const std::int32_t t2 = engine.StartColor(
        root, aero::VisualAnimProp::ManagedCustom, 0xFF0000FFu, 0xFFFF0000u, 0.2f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.1f, false, 1);
    CHECK(t1 > 0 && t2 > 0 && t1 != t2);
    CHECK(engine.HasTrack(t1) && engine.HasTrack(t2));
    CHECK(engine.IsTrackStarted(t1));
    CHECK(!engine.IsTrackStarted(t2));

    (void)engine.Tick(0.05f, w);
    CHECK(engine.HasTrack(t1) && engine.HasTrack(t2));
    CHECK(!engine.IsTrackStarted(t2));
    CHECK_NEAR(engine.ReadTrackFloat(t1, -1.0f), 2.5f, 0.1f);

    (void)engine.Tick(0.10f, w);
    CHECK(engine.HasTrack(t2));
    CHECK(engine.IsTrackStarted(t2));
    const std::uint32_t col = engine.ReadTrackColor(t2, 0u);
    const std::uint32_t r = col & 0xFFu;
    const std::uint32_t b = (col >> 16u) & 0xFFu;
    CHECK(r > 40u && r < 220u);
    CHECK(b > 40u && b < 220u);
}

void TestGalleryEngineFixesMicrokernel() {
    aero::VisualWorld w;
    const aero::VisualNodeId root = w.CreateNode(aero::VisualKind::Box);
    w.SetRoot(root);

    const aero::VisualNodeId b1 = w.CreateNode(aero::VisualKind::Box);
    const aero::VisualNodeId b2 = w.CreateNode(aero::VisualKind::Box);
    const aero::VisualNodeId b3 = w.CreateNode(aero::VisualKind::Box);
    const aero::VisualNodeId b4 = w.CreateNode(aero::VisualKind::Box);
    w.AddChild(root, b1);
    w.AddChild(root, b2);
    w.AddChild(root, b3);
    w.AddChild(root, b4);

    // CustomScalar1 passing through/landing on identity (1.0f) and leaving identity
    w.SetLayoutTransform2D(b1, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f);
    // CustomScalar3 (rotDeg) passing through/landing on identity (0.0f) and leaving identity
    w.SetLayoutTransform2D(b3, 1.0f, 1.0f, 45.0f, 0.0f, 0.0f);

    aero::VisualAnimationEngine engine;
    (void)engine.StartFloat(
        b1, aero::VisualAnimProp::CustomScalar1, 0.5f, 1.0f, 0.2f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, false, 1);
    (void)engine.StartFloat(
        b2, aero::VisualAnimProp::CustomScalar1, 1.0f, 0.5f, 0.2f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, false, 1);
    (void)engine.StartFloat(
        b3, aero::VisualAnimProp::CustomScalar3, 45.0f, 0.0f, 0.2f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, false, 1);
    (void)engine.StartFloat(
        b4, aero::VisualAnimProp::CustomScalar3, 0.0f, 45.0f, 0.2f,
        aero::EasingFunctionKind::Linear, aero::EasingMode::EaseOut, 0.0f, false, 1);

    (void)engine.Tick(0.05f, w);
    const aero::VisualNode* n1 = w.GetNode(b1);
    const aero::VisualNode* n2 = w.GetNode(b2);
    const aero::VisualNode* n3 = w.GetNode(b3);
    const aero::VisualNode* n4 = w.GetNode(b4);
    CHECK(n1 && n1->layout.hasLayoutTransform && n1->layout.layoutTransform2D.sx > 0.5f && n1->layout.layoutTransform2D.sx < 1.0f);
    CHECK(n2 && n2->layout.hasLayoutTransform && n2->layout.layoutTransform2D.sx < 1.0f && n2->layout.layoutTransform2D.sx > 0.5f);
    CHECK(n3 && n3->layout.hasLayoutTransform && n3->layout.layoutTransform2D.rotDeg > 0.0f && n3->layout.layoutTransform2D.rotDeg < 45.0f);
    CHECK(n4 && n4->layout.hasLayoutTransform && n4->layout.layoutTransform2D.rotDeg > 0.0f && n4->layout.layoutTransform2D.rotDeg < 45.0f);

    (void)engine.Tick(0.20f, w);
    n1 = w.GetNode(b1);
    n2 = w.GetNode(b2);
    n3 = w.GetNode(b3);
    n4 = w.GetNode(b4);
    CHECK(n1 && !n1->layout.hasLayoutTransform);
    CHECK_NEAR(n1->layout.layoutTransform2D.sx, 1.0f, 1e-4f);
    CHECK(n2 && n2->layout.hasLayoutTransform);
    CHECK_NEAR(n2->layout.layoutTransform2D.sx, 0.5f, 1e-4f);
    CHECK(n3 && !n3->layout.hasLayoutTransform);
    CHECK_NEAR(n3->layout.layoutTransform2D.rotDeg, 0.0f, 1e-4f);
    CHECK(n4 && n4->layout.hasLayoutTransform);
    CHECK_NEAR(n4->layout.layoutTransform2D.rotDeg, 45.0f, 1e-4f);
}

void TestReviewFixesMicrokernel() {
    std::printf("[Test] TestReviewFixesMicrokernel (VisualBrush/BlendMode/Saturation/BiDi/StrokePad/LayoutClip)...\n");

    // 1. VisualBrush masked SvgPath + Ellipse with multi-quad Text content & ImageAtlas::Clear slot safety (P0 #1)
    {
        aero::UIResourceStore store;
        aero::VisualWorld w;
        const aero::VisualNodeId root = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.SetSize(root, 320.0f, 200.0f);

        const aero::VisualNodeId vbSrc = w.CreateNode(aero::VisualKind::Box);
        w.AddChild(root, vbSrc);
        w.SetSize(vbSrc, 80.0f, 40.0f);
        w.SetBoxStyle(vbSrc, 0xFF335577u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

        const aero::VisualNodeId vbTxt = w.CreateNode(aero::VisualKind::Text);
        w.AddChild(vbSrc, vbTxt);
        w.SetSize(vbTxt, 80.0f, 40.0f);
        w.SetText(
            vbTxt, "VB!", "", 14.0f, 0xFFFFFFFFu,
            aero::TextWrapping::NoWrap, aero::TextAlignment::Left, aero::TextTrimming::None);

        const aero::VisualNodeId svgNode = w.CreateNode(aero::VisualKind::Vector);
        w.AddChild(root, svgNode);
        w.SetSize(svgNode, 80.0f, 40.0f);
        w.SetVectorShape(
            svgNode, aero::VectorShapeKind::SvgPath, "M 0,0 L 80,0 L 80,40 L 0,40 Z",
            0xFFFFFFFFu, -1, 0u, -1, 0.0f, aero::Stretch::Fill, 0.0f, 1.0f);
        w.SetVisualBrushSource(svgNode, vbSrc, 1, 1);

        const aero::VisualNodeId ellNode = w.CreateNode(aero::VisualKind::Vector);
        w.AddChild(root, ellNode);
        w.SetSize(ellNode, 64.0f, 64.0f);
        w.SetVectorShape(
            ellNode, aero::VectorShapeKind::Ellipse, "",
            0xFFFFFFFFu, -1, 0u, -1, 0.0f, aero::Stretch::Fill, 0.0f, 1.0f);
        w.SetVisualBrushSource(ellNode, vbSrc, 1, 1);

        w.UpdateLayout(320.0f, 200.0f);
        aero::DrawingCommandBuffer drawBuf;
        aero::PagedRenderTree renderTree;
        aero::VisualRenderAdapter adapter;
        const std::size_t emitted = adapter.SyncPagedRenderTree(w, &drawBuf, renderTree, &store);
        CHECK(emitted >= 3u);

        std::uint32_t svgTexId = 0u;
        std::uint32_t ellTexId = 0u;
        const aero::VisualNode* svgPtr = w.GetNode(svgNode);
        const aero::VisualNode* ellPtr = w.GetNode(ellNode);
        for (std::size_t i = 0u; i < renderTree.NodeCount(); ++i) {
            const aero::RenderNodeDescriptor* desc = renderTree.GetNode(static_cast<std::uint32_t>(i));
            if (!desc || desc->textureId == 0u) continue;
            if (desc->sourceElement == svgPtr) svgTexId = desc->textureId;
            if (desc->sourceElement == ellPtr) ellTexId = desc->textureId;
        }
        CHECK(svgTexId > 0u && store.Images().HasTextureSlot(svgTexId));
        CHECK(ellTexId > 0u && store.Images().HasTextureSlot(ellTexId));

        std::uint32_t ew = 0u, eh = 0u;
        std::shared_ptr<const std::vector<std::uint8_t>> ePx;
        CHECK(store.Images().TryGetTextureSlotSnapshot(ellTexId, ew, eh, ePx) && ePx != nullptr);
        if (ePx && ew > 4u && eh > 4u && ePx->size() >= static_cast<std::size_t>(ew) * eh * 4u) {
            // Top-left padded corner must be masked transparent while center holds VisualBrush content.
            CHECK((*ePx)[3u] == 0u);
            CHECK((*ePx)[(static_cast<std::size_t>(eh / 2u) * ew + (ew / 2u)) * 4u + 3u] > 0u);
        }

        // Clearing the atlas must reset visualBrushSlots_ and re-allocate valid slots on next sync.
        store.Images().Clear();
        CHECK(store.Images().StandaloneTextureCount() == 0u);
        aero::UIResourceStore freshMaskStore;
        aero::PagedRenderTree tree2;
        w.MarkDirty(ellNode, aero::VisualDirty_Visual);
        w.UpdateLayout(320.0f, 200.0f);
        const std::size_t emitted2 = adapter.SyncPagedRenderTree(w, &drawBuf, tree2, &freshMaskStore);
        CHECK(emitted2 >= 3u);
    }

    // 2. BlendMode with post-process Blur applies once at BlurLayerSpan, not inside span nodes (P1 #6)
    {
        aero::UIResourceStore store;
        aero::VisualWorld w;
        const aero::VisualNodeId root = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        w.SetSize(root, 200.0f, 120.0f);
        const aero::VisualNodeId blurMult = w.CreateNode(aero::VisualKind::Box);
        w.AddChild(root, blurMult);
        w.SetSize(blurMult, 80.0f, 50.0f);
        w.SetBoxStyle(blurMult, 0xFFCC8844u, 0u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        w.SetEffect(blurMult, aero::UIEffectKind::Blur, 6.0f, 0.0f, 0.0f, 0xFFFFFFFFu, 1.0f);
        w.SetBlendMode(blurMult, aero::BlendingMode::Multiply);
        w.UpdateLayout(200.0f, 120.0f);

        aero::DrawingCommandBuffer drawBuf;
        aero::PagedRenderTree tree;
        aero::VisualRenderAdapter adapter;
        const std::size_t emitted = adapter.SyncPagedRenderTree(w, &drawBuf, tree, &store);
        CHECK(emitted >= 1u);
        bool foundSpan = false;
        for (const auto& sp : tree.BlurLayerSpans()) {
            if (sp.blendMode == aero::BlendingMode::Multiply) {
                foundSpan = true;
                for (std::uint32_t nid = sp.firstNodeId; nid < sp.endNodeId; ++nid) {
                    const auto* d = tree.GetNode(nid);
                    CHECK(d != nullptr && d->blendMode == aero::BlendingMode::Normal);
                }
            }
        }
        CHECK(foundSpan);
    }

    // 3. shader://saturation? recursion rejection & nested query param preservation (P1 #8)
    {
        aero::ImageAtlas atlas;
        CHECK(atlas.LoadOrGet("shader://saturation?sat=0.5&src=shader://saturation?sat=0.2&src=missing.png") == nullptr);
        const aero::ImageAtlasEntry* satPlasma =
            atlas.LoadOrGet("shader://saturation?sat=0.5&src=shader://plasma?vw=48&vh=24&time=0.5");
        CHECK(satPlasma != nullptr && satPlasma->width == 48u && satPlasma->height == 24u);
    }

    // 4. Leading CRLF line index and BiDi firstCodePointIndex (P1 #9)
    {
        aero::FontAtlas fonts;
        std::vector<aero::RichTextRunInput> runs(1);
        runs[0].text = "\r\nabc";
        const aero::TextLayoutResult lay = fonts.MeasureRichLayout(
            runs, "Segoe UI", 14.0f, 400, false, 1000.0f, aero::TextWrapping::NoWrap);
        CHECK(lay.lines.size() >= 2u);
        if (!lay.lines.empty()) {
            CHECK(lay.lines[0].firstCodePointIndex == 0u);
        }
    }

    // 5. Thick stroke padding > 64px & non-bulging flat cap on short multi-segment path (P2 #14)
    {
        aero::UIResourceStore store;
        aero::RasterizedPathMaskEntry strokeMask{};
        CHECK(store.GetOrRasterizePathStrokeMask(
            0ull, "M 20,50 L 21,50 L 80,50", aero::Rect{20.0f, 50.0f, 60.0f, 1.0f},
            60u, 10u, 80.0f, 90u, strokeMask, 0u, 0u));
        std::uint32_t sw = 0u, sh = 0u;
        std::shared_ptr<const std::vector<std::uint8_t>> px;
        CHECK(store.Images().TryGetTextureSlotSnapshot(strokeMask.textureId, sw, sh, px) && px != nullptr);
        CHECK(sw == 60u + 2u * 90u);
        if (px && sw > 90u && sh > 2u) {
            // 6px behind the flat start cut (pad = 90) must stay transparent despite short seg0=1px and strokeWidth=80px.
            CHECK((*px)[(static_cast<std::size_t>(sh / 2u) * sw + 84u) * 4u + 3u] == 0u);
        }
    }

    // 6. Stretched container without explicit Width/Height must not clip negative-margin child (P2 #14)
    {
        aero::VisualWorld w;
        const aero::VisualNodeId root = w.CreateNode(aero::VisualKind::Box);
        w.SetRoot(root);
        const aero::VisualNodeId stretchParent = w.CreateNode(aero::VisualKind::Box);
        w.AddChild(root, stretchParent);
        w.SetAlignment(stretchParent, aero::HorizontalAlignment::Stretch, aero::VerticalAlignment::Stretch);
        const aero::VisualNodeId bleedChild = w.CreateNode(aero::VisualKind::Box);
        w.AddChild(stretchParent, bleedChild);
        w.SetSize(bleedChild, 120.0f, 40.0f);
        w.SetMargin(bleedChild, -12.0f, 0.0f, 0.0f, 0.0f);
        w.UpdateLayout(200.0f, 100.0f);
        const aero::VisualNode* sp = w.GetNode(stretchParent);
        CHECK(sp != nullptr && !sp->layout.needsLayoutClip);
    }
}

} // namespace

int main() {
    TestGenerationalHandlesAndCycleGuard();
    TestGcRootSafetyAndExternalPools();
    TestAllNineLayoutModes();
    TestIncrementalDepthBuckets();
    TestDrawingCommandBuffer();
    TestAnimationEngine();
    TestHitTestTransformsAndInputRouter();
    TestVisualRenderAdapterAndRasterizer();
    TestTrackBEcsBindingZeroAlloc();
    TestHostBindingsAndReflection();
    TestBlendModePageHeaderAndBlurSpanAdditive();
    TestImageAtlasConcurrentSamplingSafety();
    TestDeepHierarchyOver70Levels();
    TestPerspective3DPageVertexClipW();
    TestGridStarInsideExplicitWidthAndInfParent();
    TestAnimationEngineManagedCustomAndDelayStart();
    TestGalleryEngineFixesMicrokernel();
    TestReviewFixesMicrokernel();

    if (gFailureCount != 0) {
        std::fprintf(stderr, "microkernel_tests FAILED (%d checks failed)\n", gFailureCount);
        return 1;
    }
    std::printf("microkernel_tests PASSED all checks\n");
    return 0;
}
