////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include <NsCore/Noesis.h>
#include <NsCore/RegisterComponent.h>
#include <NsCore/HighResTimer.h>
#include <NsGui/Pen.h>
#include <NsGui/IView.h>
#include <NsGui/Mouse.h>
#include <NsGui/Keyboard.h>
#include <NsGui/DrawingContext.h>
#include <NsGui/SolidColorBrush.h>
#include <NsGui/RadialGradientBrush.h>
#include <NsGui/LinearGradientBrush.h>
#include <NsGui/GradientStopCollection.h>
#include <NsGui/FrameworkElement.h>
#include <NsGui/Colors.h>
#include <NsGui/DependencyData.h>
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/EmbeddedXamlProvider.h>
#include <NsApp/EmbeddedFontProvider.h>
#include <NsApp/EntryPoint.h>

#include "MainWindow.xaml.bin.h"
#include "Orbitron.ttf.bin.h"


using namespace Noesis;
using namespace NoesisApp;


static const float Width = 800.0f;
static const float Height = 600.0f;
static const float PadWidth = 85.0f;
static const float PadHeight = 22.0f;
static const float BlockWidth = Width / 13.0f;
static const float BlockHeight = 30.0f;
static const float BallRadius = 7.0f;


namespace CustomRender
{

////////////////////////////////////////////////////////////////////////////////////////////////////
class Game final: public Noesis::FrameworkElement
{
public:
    Game()
    {
        srand((unsigned int)HighResTimer::Ticks());
        InitBrushes();
        Reset();
    }

    void OnConnectToView(IView* view) override
    {
        ParentClass::OnConnectToView(view);
        view->Rendering() += MakeDelegate(this, &Game::Tick);
    }

    void OnDisconnectFromView() override
    {
        GetView()->Rendering() -= MakeDelegate(this, &Game::Tick);
        ParentClass::OnDisconnectFromView();
    }

    enum class GameState { Start, Playing, GameOver };
    GameState mState = GameState::Start;

    int GetScore() const { return GetValue<int>(ScoreProperty); }
    void SetScore(int value) { SetValue<int>(ScoreProperty, value); }

    const char* GetStatusText() const { return GetValue<String>(StatusTextProperty).Str(); }
    void SetStatusText(const char* value) { SetValue<String>(StatusTextProperty, value); }

    const char* GetActionText() const { return GetValue<String>(ActionTextProperty).Str(); }
    void SetActionText(const char* value) { SetValue<String>(ActionTextProperty, value); }

    void Tick(IView*)
    {
        uint64_t t = HighResTimer::Ticks();
        float dt = (float)HighResTimer::Seconds(t - mLastTime);
        mLastTime = t;

        Update(dt > 0.05f ? 0.05f : dt);
        InvalidateVisual();
    }

    void Reset()
    {
        mLastTime = HighResTimer::Ticks();

        memset(mBlocks, 0, sizeof(mBlocks));

        mPadX = mPosX = Width / 2.0f;
        mPosY = Height - PadHeight - 50.0f;
        mVelX = -2.0f;
        mVelY = -4.0f;

        for (int i = 0; i < 100; ++i) mParticles[i].life = 0.0f;

        mLastMousePos = Noesis::Point(0.0f, 0.0f);
    }

    void Update(float dt)
    {
        // Handle Game States
        if (mState != GameState::Playing)
        {
            Keyboard* keyboard = GetKeyboard();
            if (keyboard->IsKeyDown(Key_Space) || keyboard->IsKeyDown(Key_GamepadAccept))
            {
                Reset();
                mState = GameState::Playing;
                SetStatusText("");
                SetActionText("");
                SetScore(0);
            }

            return;
        }

        // Update Paddle Input
        Noesis::Point mouse = PointFromScreen(GetMouse()->GetPosition());
        bool mouseMoved = fabsf(mouse.x - mLastMousePos.x) > 0.1f;
        mLastMousePos = mouse;

        if (mouseMoved && mouse.x > 0.0f && mouse.x < Width && mouse.y > 0 && mouse.y < Height)
        {
            mPadX = Min(Max(mouse.x, PadWidth * 0.5f), Width - PadWidth * 0.5f);
        }
        else
        {
            Keyboard* keyboard = GetKeyboard();
            if (keyboard->IsKeyDown(Key_Left) || keyboard->IsKeyDown(Key_GamepadLeft))
            {
                mPadX = Max(PadWidth * 0.5f, mPadX - 600.0f * dt);
            }
            if (keyboard->IsKeyDown(Key_Right) || keyboard->IsKeyDown(Key_GamepadRight))
            {
                mPadX = Min(mPadX + 600.0f * dt, Width - PadWidth * 0.5f);
            }
        }

        // Update Ball Physics
        mPosX += mVelX * dt * 60.0f;
        mPosY += mVelY * dt * 60.0f;

        // Wall collisions
        if (mPosX < BallRadius) { mPosX = BallRadius; mVelX = -mVelX; }
        if (mPosX > Width - BallRadius) { mPosX = Width - BallRadius; mVelX = -mVelX; }
        if (mPosY < BallRadius) { mPosY = BallRadius; mVelY = -mVelY; }

        if (mPosY > Height) 
        {
            mState = GameState::GameOver;
            SetStatusText("GAME OVER");
            SetActionText("PRESS SPACE TO RESTART");
        }

        // Speed ramp up
        mVelX *= 1.00005f;
        mVelY *= 1.0001f;

        // Paddle Collision
        float padX0 = mPadX - PadWidth * 0.5f;
        float padX1 = padX0 + PadWidth;
        float padY0 = 550.0f - PadHeight * 0.5f;
        float padY1 = padY0 + PadHeight;

        if (mPosX >= padX0 && mPosX <= padX1 && mPosY >= padY0 && mPosY <= padY1)
        {
            if (mVelY > 0.0f) mVelY = -mVelY;

            float vScale = (mPosX - mPadX) / (PadWidth * 0.5f);
            mVelX += vScale * 2.0f;
            
            // Correction to prevent ball getting stuck inside paddle
            mPosY = padY0 - BallRadius; 
        }

        // Block Collision
        bool hitBlockThisFrame = false;
        for (uint32_t i = 0; i < 5 && !hitBlockThisFrame; i++)
        {
            for (uint32_t j = 0; j < 13; j++)
            {
                if (mBlocks[i][j]) continue;

                float x0 = (float)j * BlockWidth;
                float x1 = x0 + BlockWidth;
                float y0 = (float)i * BlockHeight + 100.0f;
                float y1 = y0 + BlockHeight;

                if (mPosX >= x0 && mPosX <= x1 && mPosY >= y0 && mPosY <= y1)
                {
                    mBlocks[i][j] = true;
                    SetScore(GetScore() + 10);
                    hitBlockThisFrame = true;

                    // --- SPAWN EXPLOSION ---
                    int spawned = 0;
                    for (int k = 0; k < 100 && spawned < 10; ++k)
                    {
                        if (mParticles[k].life <= 0.0f) // Find a dead particle
                        {
                            mParticles[k].x = x0 + BlockWidth * 0.5f;
                            mParticles[k].y = y0 + BlockHeight * 0.5f;
                            
                            // Explode outward in random directions
                            mParticles[k].vx = ((rand() % 100) / 50.0f - 1.0f) * 350.0f;
                            mParticles[k].vy = ((rand() % 100) / 50.0f - 1.0f) * 350.0f;
                            
                            mParticles[k].maxLife = mParticles[k].life = 0.2f + (rand() % 100) / 500.0f;
                            mParticles[k].colorIndex = i; // Match the broken block's color
                            spawned++;
                        }
                    }

                    float dx0 = fabsf(mPosX - x0);
                    float dx1 = fabsf(x1 - mPosX);
                    float dy0 = fabsf(mPosY - y0);
                    float dy1 = fabsf(y1 - mPosY);

                    if (Min(dx0, dx1) < Min(dy0, dy1))
                    {
                        mVelX = -mVelX;
                    }
                    else
                    {
                        mVelY = -mVelY;
                    }

                    break;
                }
            }
        }

        // Update Particles
        for (int k = 0; k < 100; ++k)
        {
            if (mParticles[k].life > 0.0f)
            {
                mParticles[k].x += mParticles[k].vx * dt;
                mParticles[k].y += mParticles[k].vy * dt;
                mParticles[k].vy += 900.0f * dt; // Heavy gravity pulls them down
                mParticles[k].life -= dt;
            }
        }
    }

    void OnRender(DrawingContext* context) override
    {
        context->DrawRectangle(mBgBrush, nullptr, Noesis::Rect(0, 0, Width, Height));

        DrawBlocks(context);
        DrawParticles(context);
        DrawPaddle(context);
        DrawBall(context);
    }

private:
    void DrawBlocks(DrawingContext* context)
    {
        for (uint32_t i = 0; i < 5; i++)
        {
            for (uint32_t j = 0; j < 13; j++)
            {
                if (mBlocks[i][j]) continue;

                float x0 = (float)j * BlockWidth;
                float y0 = (float)i * BlockHeight + 100.0f;

                Noesis::Rect blockRect(x0 + 2.0f, y0 + 2.0f, x0 + BlockWidth - 2.0f, y0 + BlockHeight - 2.0f);

                // Shadow
                Noesis::Rect shadowRect = blockRect;
                shadowRect.Offset(Noesis::Point(4.0f, 4.0f)); // Slightly larger offset for more depth
                context->DrawRoundedRectangle(mShadowBrush, nullptr, shadowRect, 4.0f, 4.0f);

                // Main block body (Gradient + Glassy Pen Stroke)
                context->DrawRoundedRectangle(mBlockBrushes[i], mBlockPen, blockRect, 4.0f, 4.0f);
            }
        }
    }

    void DrawParticles(DrawingContext* context)
    {
        for (int k = 0; k < 100; ++k)
        {
            if (mParticles[k].life > 0.0f)
            {
                float lifeRatio = mParticles[k].life / mParticles[k].maxLife;
                float size = 5.0f * lifeRatio; // Shrink as they die

                Noesis::Rect pRect(mParticles[k].x - size, mParticles[k].y - size, mParticles[k].x + size,
                    mParticles[k].y + size);

                // Draw tiny rounded rectangles that match the glossy block colors
                context->DrawRoundedRectangle(mBlockBrushes[mParticles[k].colorIndex], nullptr,
                    pRect, 1.0f, 1.0f);
            }
        }
    }

    void DrawPaddle(DrawingContext* context)
    {
        float padX0 = mPadX - PadWidth * 0.5f;
        float padY0 = 550.0f - PadHeight * 0.5f;
        float capWidth = 14.0f; 
        float cornerRadius = PadHeight * 0.5f;

        Noesis::Rect fullPadRect(padX0, padY0, padX0 + PadWidth, padY0 + PadHeight);

        // Shadow for the heavy paddle
        Noesis::Rect padShadow = fullPadRect;
        padShadow.Offset(Noesis::Point(6.0f, 6.0f));
        context->DrawRoundedRectangle(mShadowBrush, nullptr, padShadow, cornerRadius, cornerRadius);

        // Cyan Energy Tips (Glass Diodes)
        float tipWidth = 8.0f;
        float tipHeight = 10.0f;
        float tipY = padY0 + (PadHeight - tipHeight) * 0.5f;
        float cornerRadTip = tipHeight * 0.5f; // Make them perfectly rounded pills

        // Left tip
        context->DrawRoundedRectangle(mPadTipBrush, mBlockPen, 
            Noesis::Rect(padX0 - tipWidth + 3.0f, tipY, padX0 + 5.0f, tipY + tipHeight),
            cornerRadTip, cornerRadTip);
        
        // Right tip
        context->DrawRoundedRectangle(mPadTipBrush, mBlockPen, 
            Noesis::Rect(padX0 + PadWidth - 5.0f, tipY, padX0 + PadWidth + tipWidth - 3.0f, tipY + tipHeight),
            cornerRadTip, cornerRadTip);

        // Red Caps (Forms the base shell)
        context->DrawRoundedRectangle(mPadCapBrush, mBlockPen, fullPadRect, cornerRadius, cornerRadius);

        // Silver Body (Overlaid in the center, flush with the caps)
        Noesis::Rect centerRect(padX0 + capWidth, padY0, padX0 + PadWidth - capWidth, padY0 + PadHeight);
        context->DrawRectangle(mPadBodyBrush, mBlockPen, centerRect);

        // 3D Engraved Seams & Chassis Grooves
        Noesis::Ptr<Pen> shadowPen = MakePtr<Pen>();
        shadowPen->SetBrush(MakePtr<SolidColorBrush>(Color(0, 0, 0, 180)));
        shadowPen->SetThickness(1.0f);

        Noesis::Ptr<Pen> highlightPen = MakePtr<Pen>();
        highlightPen->SetBrush(MakePtr<SolidColorBrush>(Color(255, 255, 255, 120)));
        highlightPen->SetThickness(1.0f);

        // Left joint seam (Shadow on the left, highlight on the right)
        context->DrawLine(shadowPen, Noesis::Point(padX0 + capWidth, padY0),
            Noesis::Point(padX0 + capWidth, padY0 + PadHeight));
        context->DrawLine(highlightPen, Noesis::Point(padX0 + capWidth + 1.0f, padY0),
            Noesis::Point(padX0 + capWidth + 1.0f, padY0 + PadHeight));

        // Right joint seam
        context->DrawLine(shadowPen, Noesis::Point(padX0 + PadWidth - capWidth - 1.0f, padY0),
            Noesis::Point(padX0 + PadWidth - capWidth - 1.0f, padY0 + PadHeight));
        context->DrawLine(highlightPen, Noesis::Point(padX0 + PadWidth - capWidth, padY0),
            Noesis::Point(padX0 + PadWidth - capWidth, padY0 + PadHeight));

        // Center Chassis Horizontal Grooves
        float midY = padY0 + PadHeight * 0.5f;
        float grooveMargin = padX0 + capWidth + 10.0f;
        float grooveEnd = padX0 + PadWidth - capWidth - 10.0f;

        // Top horizontal groove
        context->DrawLine(shadowPen, Noesis::Point(grooveMargin, midY - 3.0f), Noesis::Point(grooveEnd, midY - 3.0f));
        context->DrawLine(highlightPen, Noesis::Point(grooveMargin, midY - 2.0f), Noesis::Point(grooveEnd, midY - 2.0f));

        // Bottom horizontal groove
        context->DrawLine(shadowPen, Noesis::Point(grooveMargin, midY + 2.0f), Noesis::Point(grooveEnd, midY + 2.0f));
        context->DrawLine(highlightPen, Noesis::Point(grooveMargin, midY + 3.0f), Noesis::Point(grooveEnd, midY + 3.0f));
    }

    void DrawBall(DrawingContext* context)
    {
        Noesis::Point shadowCenter(mPosX + 4.0f, mPosY + 4.0f);
        context->DrawEllipse(mShadowBrush, nullptr, shadowCenter, BallRadius, BallRadius);

        Noesis::Point ballCenter(mPosX, mPosY);
        context->DrawEllipse(mBallBrush, nullptr, ballCenter, BallRadius, BallRadius);
    }
    
    Noesis::Ptr<LinearGradientBrush> CreateBlockBrush(Color topColor, Color bottomColor)
    {
        Noesis::Ptr<GradientStopCollection> stops = MakePtr<GradientStopCollection>();
        stops->Add(MakePtr<GradientStop>(topColor, 0.0f));
        stops->Add(MakePtr<GradientStop>(bottomColor, 1.0f));
        
        Noesis::Ptr<LinearGradientBrush> brush = MakePtr<LinearGradientBrush>();
        brush->SetGradientStops(stops);
        brush->SetStartPoint(Noesis::Point(0.0f, 0.0f));
        brush->SetEndPoint(Noesis::Point(0.0f, 1.0f)); // Vertical gradient
        return brush;
    }

    Noesis::Ptr<LinearGradientBrush> CreateCylinderBrush(Color baseDark, Color baseLight, Color highlight)
    {
        Noesis::Ptr<GradientStopCollection> stops = MakePtr<GradientStopCollection>();
        stops->Add(MakePtr<GradientStop>(baseDark, 0.0f));       // Dark top edge
        stops->Add(MakePtr<GradientStop>(baseLight, 0.15f));     // Transition
        stops->Add(MakePtr<GradientStop>(highlight, 0.35f));     // Sharp specular highlight (off-center)
        stops->Add(MakePtr<GradientStop>(baseLight, 0.6f));      // Transition back
        stops->Add(MakePtr<GradientStop>(baseDark, 1.0f));       // Dark bottom edge
        
        Noesis::Ptr<LinearGradientBrush> brush = MakePtr<LinearGradientBrush>();
        brush->SetGradientStops(stops);
        brush->SetStartPoint(Noesis::Point(0.0f, 0.0f));
        brush->SetEndPoint(Noesis::Point(0.0f, 1.0f)); // Vertical gradient
        return brush;
    }

    void InitBrushes()
    {
        // Background
        Noesis::Ptr<GradientStopCollection> bgStops = MakePtr<GradientStopCollection>();
        bgStops->Add(MakePtr<GradientStop>(Color(40, 45, 55, 255), 0.0f));
        bgStops->Add(MakePtr<GradientStop>(Color(10, 10, 15, 255), 1.0f));

        mBgBrush = MakePtr<RadialGradientBrush>();
        mBgBrush->SetGradientStops(bgStops);
        mBgBrush->SetRadiusX(1.0f);
        mBgBrush->SetRadiusY(1.0f);

        // Shadows & Strokes
        mShadowBrush = MakePtr<SolidColorBrush>(Color(0, 0, 0, 180));
        mBlockPen = MakePtr<Pen>();
        mBlockPen->SetThickness(1.5f);
        mBlockPen->SetBrush(MakePtr<SolidColorBrush>(Color(255, 255, 255, 80)));

        // Ball
        Noesis::Ptr<GradientStopCollection> ballStops = MakePtr<GradientStopCollection>();
        ballStops->Add(MakePtr<GradientStop>(Colors::White(), 0.0f));
        ballStops->Add(MakePtr<GradientStop>(Colors::DeepSkyBlue(), 0.4f));
        ballStops->Add(MakePtr<GradientStop>(Colors::DarkBlue(), 1.0f));

        mBallBrush = MakePtr<RadialGradientBrush>();
        mBallBrush->SetGradientStops(ballStops);
        mBallBrush->SetGradientOrigin(Noesis::Point(0.3f, 0.3f));
        mBallBrush->SetCenter(Noesis::Point(0.5f, 0.5f));
        mBallBrush->SetRadiusX(0.5f);
        mBallBrush->SetRadiusY(0.5f);

        // Blocks
        mBlockBrushes[0] = CreateBlockBrush(Colors::Red(), Colors::DarkRed());
        mBlockBrushes[1] = CreateBlockBrush(Colors::Yellow(), Colors::DarkGoldenrod());
        mBlockBrushes[2] = CreateBlockBrush(Colors::DeepSkyBlue(), Colors::MediumBlue());
        mBlockBrushes[3] = CreateBlockBrush(Colors::Fuchsia(), Colors::DarkMagenta());
        mBlockBrushes[4] = CreateBlockBrush(Colors::Lime(), Colors::Green());

        // Arkanoid Vaus Paddle
        mPadBodyBrush = CreateCylinderBrush(Color(70, 75, 80, 255), Color(160, 165, 175, 255), Colors::White());
        mPadCapBrush = CreateCylinderBrush(Color(120, 0, 0, 255), Color(220, 40, 40, 255), Color(255, 180, 180, 255));
        mPadTipBrush = CreateCylinderBrush(Color(0, 80, 120, 255), Color(0, 180, 220, 255), Colors::LightCyan());
    }

    Noesis::Point mLastMousePos;

    bool mBlocks[5][13];
    float mPadX, mPosX, mPosY, mVelX, mVelY;
    uint64_t mLastTime;

    Noesis::Ptr<Brush> mShadowBrush;
    Noesis::Ptr<RadialGradientBrush> mBallBrush;
    Noesis::Ptr<RadialGradientBrush> mBgBrush;
    Noesis::Ptr<Brush> mBlockBrushes[5];
    Noesis::Ptr<Pen> mBlockPen;
    
    Noesis::Ptr<LinearGradientBrush> mPadBodyBrush;
    Noesis::Ptr<LinearGradientBrush> mPadCapBrush;
    Noesis::Ptr<Brush> mPadTipBrush;

    struct Particle
    {
        float x, y, vx, vy;
        float life, maxLife;
        int colorIndex;
    };

    Particle mParticles[100];

    static const DependencyProperty* ScoreProperty;
    static const DependencyProperty* StatusTextProperty;
    static const DependencyProperty* ActionTextProperty;

    NS_IMPLEMENT_INLINE_REFLECTION(Game, FrameworkElement, "CustomRender.Game")
    {
        auto data = NsMeta<DependencyData>(TypeOf<SelfClass>());
        data->RegisterProperty<int>(ScoreProperty, "Score", PropertyMetadata::Create(0));
        data->RegisterProperty<String>(StatusTextProperty, "StatusText", PropertyMetadata::Create(String("")));
        data->RegisterProperty<String>(ActionTextProperty, "ActionText", PropertyMetadata::Create(String("PRESS SPACE TO START")));
    }
};

const DependencyProperty* Game::ScoreProperty;
const DependencyProperty* Game::StatusTextProperty;
const DependencyProperty* Game::ActionTextProperty;

}

////////////////////////////////////////////////////////////////////////////////////////////////////
class AppLauncher final: public ApplicationLauncher
{
private:
    void RegisterComponents() const override
    {
        RegisterComponent<CustomRender::Game>();
    }

    Noesis::Ptr<XamlProvider> GetXamlProvider() const override
    {
        EmbeddedXaml xamls[] =
        {
            { "MainWindow.xaml", MainWindow_xaml }
        };

        return *new EmbeddedXamlProvider(xamls);
    }

    Noesis::Ptr<FontProvider> GetFontProvider() const override
    {
        EmbeddedFont fonts[] =
        {
            { "", Orbitron_ttf }
        };

        return *new EmbeddedFontProvider(fonts);
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
int NsMain(int argc, char** argv)
{
    AppLauncher launcher;
    launcher.SetArguments(argc, argv);
    launcher.SetStartupUri("MainWindow.xaml");
    return launcher.Run();
}
