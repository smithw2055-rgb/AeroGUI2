////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


// -------------------------------------------------------------------------------------------------
// Minimal integration example. For simplicity, only basic input events are handled,
// and resource providers are not used. GLUT is used for the window system. A more 
// comprehensive multiplatform integration sample with step-by-step comments is 
// available in 'Samples/Integration'.
// -------------------------------------------------------------------------------------------------


#ifdef _WIN32
#include "glut.h"
#endif

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <GLUT/glut.h>
#endif

#ifdef __EMSCRIPTEN__
#include <GL/glut.h>
#include <GLES3/gl32.h>
#include <emscripten/html5.h>
#endif

#ifdef __linux__
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glut.h>
#endif

#include <NsApp/ThemeProviders.h>
#include <NsRender/GLFactory.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/IRenderer.h>
#include <NsGui/IView.h>
#include <NsGui/Grid.h>


static Noesis::Ptr<Noesis::IView> _view;


///////////////////////////////////////////////////////////////////////////////////////////////////
static void NoesisShutdown()
{
    // Safely terminates the renderer before destroying the view
    _view->GetRenderer()->Shutdown();

    // All Noesis objects must be released before shutting down the engine
    _view.Reset();

    // Performs the final teardown of Noesis and its underlying subsystems
    Noesis::GUI::Shutdown();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void NoesisInit()
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

    // Noesis initialization. This must be the first step before using any NoesisGUI functionality
    Noesis::GUI::Init();

    // Install providers for the theme and set default font
    NoesisApp::SetThemeProviders();

    // Load the Dark Blue theme
    Noesis::GUI::LoadApplicationResources(NoesisApp::Theme::DarkBlue());

    // For simplicity purposes, we are not using resource providers in this sample. ParseXaml() is
    // enough if there are no XAML dependencies (Dictionaries, Textures or Fonts)
    Noesis::Ptr<Noesis::Grid> xaml(Noesis::GUI::ParseXaml<Noesis::Grid>(
    R"(
        <Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
          <Grid.Background>
            <LinearGradientBrush StartPoint="0,0" EndPoint="0,1">
              <GradientStop Offset="0" Color="#FF123F61"/>
              <GradientStop Offset="0.6" Color="#FF0E4B79"/>
              <GradientStop Offset="0.7" Color="#FF106097"/>
            </LinearGradientBrush>
          </Grid.Background>
          <Viewbox>
            <StackPanel Margin="50">
              <Button Content="Hello World!" Margin="0,30,0,0"/>
              <Rectangle Height="5" Margin="-10,20,-10,0">
                <Rectangle.Fill>
                  <RadialGradientBrush>
                    <GradientStop Offset="0" Color="#40000000"/>
                    <GradientStop Offset="1" Color="#00000000"/>
                  </RadialGradientBrush>
                </Rectangle.Fill>
              </Rectangle>
            </StackPanel>
          </Viewbox>
        </Grid>
    )"));

    // Creates a View to render and interact with the user interface
    _view = Noesis::GUI::CreateView(xaml);
    _view->SetFlags(Noesis::RenderFlags_PPAA | Noesis::RenderFlags_LCD);

    // Initializes the renderer with an OpenGL device
    _view->GetRenderer()->Init(NoesisApp::GLFactory::CreateDevice(false));

    // Register shutdown callback to ensure clean exit
    atexit(NoesisShutdown);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void DisplayFunc()
{
    // Updates the view (layout, animations, etc.)
    _view->Update(glutGet(GLUT_ELAPSED_TIME) / 1000.0);

    // The offscreen rendering phase populates textures required for on-screen rendering
    _view->GetRenderer()->UpdateRenderTree();
    _view->GetRenderer()->RenderOffscreen();

    // If integrating with a custom engine, the GPU state must be restored here because
    // Noesis modifies it. In this example, only the framebuffer and viewport require restoration
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
    glDisable(GL_SCISSOR_TEST);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.25f, 0.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // Performs on-screen rendering into the active framebuffer
    _view->GetRenderer()->Render();

    glutSwapBuffers();
    glutPostRedisplay();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
static void ReshapeFunc(int width, int height)
{
    _view->SetSize(width, height);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
static void MouseMoveFunc(int x, int y)
{
    _view->MouseMove(x, y);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
static void MouseFunc(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
        if (state == GLUT_DOWN)
        {
            _view->MouseButtonDown(x, y, Noesis::MouseButton_Left);
        }
        else
        {
            _view->MouseButtonUp(x, y, Noesis::MouseButton_Left);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_STENCIL);
    glutInitWindowSize(1000, 600);

  #ifdef __EMSCRIPTEN__
    double width, height;
    emscripten_get_element_css_size("#canvas", &width, &height);
    emscripten_set_canvas_element_size("#canvas", (uint32_t)width, (uint32_t)height);
    glutInitWindowSize((uint32_t)width, (uint32_t)height);
  #endif

    glutCreateWindow("NoesisGUI - GLUT integration");
    NoesisInit();

    glutDisplayFunc(&DisplayFunc);
    glutReshapeFunc(&ReshapeFunc);
    glutPassiveMotionFunc(&MouseMoveFunc);
    glutMouseFunc(&MouseFunc);

    glutMainLoop();
    return 0;
}
