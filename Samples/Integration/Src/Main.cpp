////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


// -------------------------------------------------------------------------------------------------
// Annotated minimal multiplatform sample using NoesisGUI. For more advanced and
// realistic scenarios, please refer to the other samples. The primary goal of this sample
// is absolute clarity at each step. With that in mind, the full Application framework
// is not used here. Only a few abstraction classes (Display and RenderContext) are
// utilized to maintain multiplatform support
// -------------------------------------------------------------------------------------------------


// The core Noesis library is exposed in the 'Noesis' namespace. The application framework,
// a helper library provided with source code, resides in the 'NoesisApp' namespace.
// For the 'Noesis' namespace, a single header library ('NoesisPCH.h') is provided for
// convenience, which is highly useful when using precompiled headers
#include <NsRender/RenderContext.h>
#include <NsCore/HighResTimer.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/UserControl.h>
#include <NsGui/IRenderer.h>
#include <NsGui/IView.h>
#include <NsGui/ResourceDictionary.h>
#include <NsApp/EntryPoint.h>
#include <NsApp/Launcher.h>
#include <NsApp/Display.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/ThemeProviders.h>

// For this sample we are embedding needed resources using our tool 'binh'
#include "MainPage.xaml.bin.h"
#include "HermeneusOne-Regular.ttf.bin.h"


////////////////////////////////////////////////////////////////////////////////////////////////////
void SetResourceProviders()
{
    // Whenever a resource (XAML, texture, font) is requested, the corresponding provider is invoked
    // to get a stream to the content. A provider must be installed for each required resource.
    // The application framework includes several implementations (e.g., LocalXamlProvider to load
    // from disk). For this sample, resources are embedded directly in a C array
    NoesisApp::EmbeddedXaml xamls[] = { { "MainPage.xaml", MainPage_xaml } };
    Noesis::GUI::SetXamlProvider(Noesis::MakePtr<NoesisApp::EmbeddedXamlProvider>(xamls));

    NoesisApp::EmbeddedFont fonts[] = { { "", HermeneusOne_Regular_ttf } };
    Noesis::GUI::SetFontProvider(Noesis::MakePtr<NoesisApp::EmbeddedFontProvider>(fonts));

    // Installs providers for the theme
    NoesisApp::SetThemeProviders();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
Noesis::Ptr<NoesisApp::RenderContext> CreateContext(NoesisApp::Display* display)
{
    // The RenderContext initializes the graphics subsystem (D3D11, GL, Metal, etc.). It also
    // provides a RenderDevice object, which is needed to initialize each View. This is a helper
    // class, not part of the core library. The only mandatory requirement for integration is an
    // implementation of the RenderDevice base class. Reference implementations for many
    // subsystems are provided
    uint32_t samples = 1;
    Noesis::Ptr<NoesisApp::RenderContext> context = NoesisApp::RenderContext::Create();
    context->Init(display->GetNativeHandle(), samples, true, false);
    return context;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
Noesis::Ptr<Noesis::IView> CreateView(Noesis::RenderDevice* device)
{
    Noesis::Ptr<Noesis::IView> view;

    // A View is required to render and interact with the user interface. A View holds a tree of
    // elements. The easiest way to construct this tree is from a XAML file
    view = Noesis::GUI::CreateView(Noesis::GUI::LoadXaml<Noesis::UserControl>("MainPage.xaml"));

    // Because MSAA is not used in this sample, PPAA is enabled here. PPAA is an efficient
    // antialiasing technique that extrudes and smooths the contours of the geometry
    view->SetFlags(Noesis::RenderFlags_PPAA | Noesis::RenderFlags_LCD);

    // Once the View is created, its internal renderer must be initialized with a valid
    // RenderDevice. This can be done in a separate render thread, but for simplicity,
    // the main thread is used in this sample
    view->GetRenderer()->Init(device);

    return view;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void SetupDisplay(NoesisApp::Display* display, NoesisApp::RenderContext* ctx, Noesis::IView* view)
{
    // The following delegates gather all hardware input (mouse, keyboard, touch)
    // and window events (resize, activation) from the Display, routing them
    // directly to the View so the user interface can react accordingly

    display->SizeChanged() += [view, ctx](NoesisApp::Display* display, uint32_t, uint32_t)
    {
        view->SetSize(display->GetClientWidth(), display->GetClientHeight());
        ctx->Resize();
    };

    display->Activated() += [view](NoesisApp::Display*)
    {
        view->Activate();
    };

    display->Deactivated() += [view](NoesisApp::Display*)
    {
        view->Deactivate();
    };

    display->MouseButtonUp() += [view](NoesisApp::Display*, int x, int y, Noesis::MouseButton b)
    {
        view->MouseButtonUp(x, y, b);
    };

    display->MouseButtonDown() += [view](NoesisApp::Display*, int x, int y, Noesis::MouseButton b)
    {
        view->MouseButtonDown(x, y, b);
    };

    display->MouseMove() += [view](NoesisApp::Display*, int x, int y)
    {
        view->MouseMove(x, y);
    };

    display->MouseDoubleClick() += [view](NoesisApp::Display*, int x, int y, Noesis::MouseButton b)
    {
        view->MouseDoubleClick(x, y, b);
    };

    display->MouseWheel() += [view](NoesisApp::Display*, int x, int y, int delta)
    {
        view->MouseWheel(x, y, delta);
    };

    display->Scroll() += [view](NoesisApp::Display*, float delta)
    {
        view->Scroll(delta);
    };

    display->HScroll() += [view](NoesisApp::Display*, float delta)
    {
        view->HScroll(delta);
    };

    display->TouchUp() += [view](NoesisApp::Display*, int x, int y, uint64_t id)
    {
        view->TouchUp(x, y, id);
    };

    display->TouchDown() += [view](NoesisApp::Display*, int x, int y, uint64_t id)
    {
        view->TouchDown(x, y, id);
    };

    display->TouchMove() += [view](NoesisApp::Display*, int x, int y, uint64_t id)
    {
        view->TouchMove(x, y, id);
    };

    display->KeyDown() += [view](NoesisApp::Display*, Noesis::Key key)
    {
        view->KeyDown(key);
    };

    display->KeyUp() += [view](NoesisApp::Display*, Noesis::Key key)
    {
        view->KeyUp(key);
    };

    display->Char() += [view](NoesisApp::Display*, uint32_t c)
    {
        view->Char(c);
    };

    // Finally, makes the window visible
    display->Show();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void RenderView(Noesis::IView* view, NoesisApp::RenderContext* ctx, uint32_t width, uint32_t height)
{
    // Begin rendering
    ctx->BeginRender();

    // The offscreen rendering phase must be executed before binding the primary framebuffer
    // This step populates all internal textures required for the active frame
    view->GetRenderer()->UpdateRenderTree();
    view->GetRenderer()->RenderOffscreen();

    // Binds the framebuffer and viewport. This must be done per frame because the offscreen
    // phase modifies these states
    ctx->SetDefaultRenderTarget(width, height, true);

    // -> # At this point, you can render your 3D scene... # <-
    // Note that each Noesis renderer function invocation modifies the GPU state. You must
    // save and restore it properly within your application.

    // After rendering the scene, the UI is drawn on top of it. Note that this render is
    // performed in the active framebuffer (whether it is the backbuffer or a render texture)
    view->GetRenderer()->Render();

    // Ends rendering and swaps the back buffer
    ctx->EndRender();
    ctx->Swap();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int NsMain(int, char**)
{
    // A logging handler is installed here to redirect Noesis messages to the console
    // Custom error handlers or memory allocators can also be installed at this stage
    Noesis::GUI::SetLogHandler([](const char*, uint32_t, uint32_t level, const char*, const char* msg)
    {
        // [TRACE] [DEBUG] [INFO] [WARNING] [ERROR]
        const char* prefixes[] = { "T", "D", "I", "W", "E" };
        printf("[NOESIS/%s] %s\n", prefixes[level], msg);
    });

    // Configures the active NoesisGUI license
    // https://www.noesisengine.com/docs/Gui.Core.Licensing.html
    Noesis::GUI::SetLicense(NS_LICENSE_NAME, NS_LICENSE_KEY);

    // Initializes Noesis. This must be the first step before using any NoesisGUI functionality
    Noesis::GUI::Init();

    // Registers 'app' components. This example requires a few, such as Display and RenderContext
    NoesisApp::Launcher::RegisterAppComponents();

    // Sets up providers for each resource type
    SetResourceProviders();

    // Loads the Dark Blue theme
    Noesis::GUI::LoadApplicationResources(NoesisApp::Theme::DarkBlue());

    // A Display is an abstraction over a system window (HWND, NSWindow, UIWindow, XWindow, etc.).
    // Because this is a helper class and not part of the core library, it resides in 'NoesisApp'
    Noesis::Ptr<NoesisApp::Display> display = NoesisApp::CreateDisplay();
    display->SetTitle("NoesisGUI Integration Sample");

    // Creates the RenderContext
    Noesis::Ptr<NoesisApp::RenderContext> context = CreateContext(display);

    // Creates the View, passing the RenderDevice implementation generated by the context
    Noesis::Ptr<Noesis::IView> view = CreateView(context->GetDevice());

    // Sets up input event redirection from the window to the View
    SetupDisplay(display, context, view);

    // Main loop execution
    display->Render() += [&view, &context](NoesisApp::Display* display)
    {
        static uint64_t startTime = Noesis::HighResTimer::Ticks();

        // Updates layout and animations. Note that global time is used rather than a delta
        uint64_t time = Noesis::HighResTimer::Ticks();
        view->Update(Noesis::HighResTimer::Seconds(time - startTime));

        // Renders the UI (this could be offloaded to a separate render thread)
        RenderView(view, context, display->GetClientWidth(), display->GetClientHeight());
    };

    // Enters the application message loop. Execution will block here until the display is closed
    display->EnterMessageLoop();

    // Shuts down the renderer (must be on the initialization thread)
    view->GetRenderer()->Shutdown();
    context->Shutdown();

    // It is mandatory to release all Noesis objects before shutting down the engine
    view.Reset();
    context.Reset();
    display.Reset();

    // Shuts down Noesis, freeing all internally allocated subsystems
    // This is the mandatory counterpart to Noesis::GUI::Init()
    Noesis::GUI::Shutdown();

    return 0;
}
