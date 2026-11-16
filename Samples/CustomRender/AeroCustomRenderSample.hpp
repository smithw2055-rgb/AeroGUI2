#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "aero/controls/Controls.hpp"
#include "aero/core/DependencyProperty.hpp"
#include "aero/core/UIElement.hpp"
#include "aero/core/UnifiedValue.hpp"

namespace aero::samples {

enum class BreakoutGameState : std::uint8_t {
    Start = 0,
    Playing = 1,
    GameOver = 2
};

struct CustomRenderSamplePropIds {
    std::uint16_t gameScore = 0xFFFFu;
    std::uint16_t gameStatusText = 0xFFFFu;
    std::uint16_t gameActionText = 0xFFFFu;
};

[[nodiscard]] inline CustomRenderSamplePropIds& GetCustomRenderSamplePropIds() noexcept {
    static CustomRenderSamplePropIds ids{};
    return ids;
}

class BreakoutGameControl : public UIElement {
public:
    static constexpr float kBoardWidth = 800.0f;
    static constexpr float kBoardHeight = 600.0f;
    static constexpr int kBrickRows = 5;
    static constexpr int kBrickCols = 13;
    static constexpr float kBrickWidth = kBoardWidth / static_cast<float>(kBrickCols);
    static constexpr float kBrickHeight = 30.0f;
    static constexpr float kBrickStartY = 100.0f;
    static constexpr float kBrickInset = 2.0f;
    static constexpr float kBrickCornerRadius = 4.0f;
    static constexpr float kPaddleWidth = 85.0f;
    static constexpr float kPaddleHeight = 22.0f;
    static constexpr float kPaddleCenterY = 550.0f;
    static constexpr float kPaddleSpeedPerTick = 10.0f;
    static constexpr float kBallRadius = 7.0f;
    static constexpr float kBallInitX = 400.0f;
    static constexpr float kBallInitY = 528.0f;
    static constexpr float kBallInitVelX = -2.0f;
    static constexpr float kBallInitVelY = -4.0f;
    static constexpr float kParticleGravity = 900.0f;
    static constexpr int kParticlesPerBrick = 10;

    struct Brick {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        Color color{};
        bool alive = true;
    };

    struct Particle {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float life = 0.0f;
        float maxLife = 0.6f;
        Color color{};
    };

    BreakoutGameControl() noexcept {
        ResetBoard();
        SyncGameProperties();
    }

    [[nodiscard]] bool HasCustomRender() const override {
        return true;
    }

    [[nodiscard]] bool NeedsContinuousTick() const noexcept override {
        return true;
    }

    [[nodiscard]] BreakoutGameState GetState() const noexcept {
        return state_;
    }

    [[nodiscard]] int GetScore() const noexcept {
        return score_;
    }

    [[nodiscard]] PointF GetBallPos() const noexcept {
        return PointF{ballX_, ballY_};
    }

    [[nodiscard]] PointF GetBallVelocity() const noexcept {
        return PointF{velX_, velY_};
    }

    [[nodiscard]] float GetPaddleX() const noexcept {
        return padX_;
    }

    [[nodiscard]] int GetAliveBrickCount() const noexcept {
        int alive = 0;
        for (const auto& b : bricks_) {
            if (b.alive) {
                ++alive;
            }
        }
        return alive;
    }

    [[nodiscard]] const Brick& GetBrick(int row, int col) const noexcept {
        return bricks_[static_cast<std::size_t>(row * kBrickCols + col)];
    }

    [[nodiscard]] const std::vector<Particle>& GetParticles() const noexcept {
        return particles_;
    }

    [[nodiscard]] int GetActiveParticleCount() const noexcept {
        int active = 0;
        for (const auto& p : particles_) {
            if (p.life > 0.0f) {
                ++active;
            }
        }
        return active;
    }

    void SetBallStateForTest(float x, float y, float vx, float vy) noexcept {
        ballX_ = x;
        ballY_ = y;
        velX_ = vx;
        velY_ = vy;
    }

    void StartGame() {
        ResetBoard();
        state_ = BreakoutGameState::Playing;
        statusText_.clear();
        actionText_.clear();
        SyncGameProperties();
        InvalidateVisual();
    }

    bool OnKeyDown(KeyCode key, std::uint8_t modifiers) override {
        (void)modifiers;
        if (key == KeyCode::Space && state_ != BreakoutGameState::Playing) {
            StartGame();
            return true;
        }
        if (key == KeyCode::Left || key == KeyCode::A) {
            leftDown_ = true;
            return true;
        }
        if (key == KeyCode::Right || key == KeyCode::D) {
            rightDown_ = true;
            return true;
        }
        return false;
    }

    bool OnKeyUp(KeyCode key, std::uint8_t modifiers) override {
        (void)modifiers;
        if (key == KeyCode::Left || key == KeyCode::A) {
            leftDown_ = false;
            return true;
        }
        if (key == KeyCode::Right || key == KeyCode::D) {
            rightDown_ = false;
            return true;
        }
        return false;
    }

    bool OnPointerMove(Point localPoint, std::uint8_t modifiers) override {
        (void)modifiers;
        if (!hasLastMouseX_ || std::fabs(localPoint.x - lastMouseX_) > 0.1f) {
            lastMouseX_ = localPoint.x;
            hasLastMouseX_ = true;
            const float halfPad = kPaddleWidth * 0.5f;
            padX_ = std::clamp(localPoint.x, halfPad, kBoardWidth - halfPad);
            InvalidateVisual();
            return true;
        }
        return false;
    }

    void TickControl(float dtSeconds) override {
        const float dt = std::clamp(dtSeconds, 0.0f, 0.1f);
        if (dt <= 0.0f) {
            return;
        }

        for (auto& p : particles_) {
            if (p.life <= 0.0f) {
                continue;
            }
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += kParticleGravity * dt;
            p.life = std::max(0.0f, p.life - dt);
        }

        if (state_ == BreakoutGameState::Playing) {
            const float halfPad = kPaddleWidth * 0.5f;
            const float moveDir = (rightDown_ ? 1.0f : 0.0f) - (leftDown_ ? 1.0f : 0.0f);
            if (std::fabs(moveDir) > 0.0f) {
                padX_ = std::clamp(
                    padX_ + moveDir * kPaddleSpeedPerTick * dt * 60.0f,
                    halfPad,
                    kBoardWidth - halfPad);
            }

            ballX_ += velX_ * dt * 60.0f;
            ballY_ += velY_ * dt * 60.0f;

            if (ballX_ < kBallRadius) {
                ballX_ = kBallRadius;
                velX_ = std::fabs(velX_);
            } else if (ballX_ > kBoardWidth - kBallRadius) {
                ballX_ = kBoardWidth - kBallRadius;
                velX_ = -std::fabs(velX_);
            }

            if (ballY_ < kBallRadius) {
                ballY_ = kBallRadius;
                velY_ = std::fabs(velY_);
            } else if (ballY_ > kBoardHeight + kBallRadius) {
                state_ = BreakoutGameState::GameOver;
                statusText_ = "GAME OVER";
                actionText_ = "PRESS SPACE TO RESTART";
                SyncGameProperties();
                InvalidateVisual();
                return;
            }

            // Paddle collision
            const float padLeft = padX_ - kPaddleWidth * 0.5f;
            const float padTop = kPaddleCenterY - kPaddleHeight * 0.5f;
            if (velY_ > 0.0f &&
                ballX_ + kBallRadius >= padLeft &&
                ballX_ - kBallRadius <= padLeft + kPaddleWidth &&
                ballY_ + kBallRadius >= padTop &&
                ballY_ - kBallRadius <= padTop + kPaddleHeight) {
                ballY_ = padTop - kBallRadius;
                const float hitNorm =
                    std::clamp((ballX_ - padX_) / (kPaddleWidth * 0.5f), -1.0f, 1.0f);
                const float speed = std::hypot(velX_, velY_);
                const float targetSpeed = std::max(speed, std::hypot(kBallInitVelX, kBallInitVelY));
                velX_ = hitNorm * targetSpeed * 0.78f;
                const float remSq = std::max(4.0f, targetSpeed * targetSpeed - velX_ * velX_);
                velY_ = -std::sqrt(remSq);
            }

            // Brick collision
            for (auto& brick : bricks_) {
                if (!brick.alive) {
                    continue;
                }
                const float closestX = std::clamp(ballX_, brick.x, brick.x + brick.w);
                const float closestY = std::clamp(ballY_, brick.y, brick.y + brick.h);
                const float dx = ballX_ - closestX;
                const float dy = ballY_ - closestY;
                if (dx * dx + dy * dy <= kBallRadius * kBallRadius) {
                    brick.alive = false;
                    score_ += 10;
                    SpawnParticles(
                        brick.x + brick.w * 0.5f,
                        brick.y + brick.h * 0.5f,
                        brick.color);

                    const float penLeft = (ballX_ + kBallRadius) - brick.x;
                    const float penRight = (brick.x + brick.w) - (ballX_ - kBallRadius);
                    const float penTop = (ballY_ + kBallRadius) - brick.y;
                    const float penBottom = (brick.y + brick.h) - (ballY_ - kBallRadius);
                    const float minHoriz = std::min(penLeft, penRight);
                    const float minVert = std::min(penTop, penBottom);
                    if (minHoriz < minVert) {
                        velX_ = -velX_;
                    } else {
                        velY_ = -velY_;
                    }

                    if (GetAliveBrickCount() == 0) {
                        state_ = BreakoutGameState::GameOver;
                        statusText_ = "YOU WIN!";
                        actionText_ = "PRESS SPACE TO PLAY AGAIN";
                    }
                    SyncGameProperties();
                    break;
                }
            }
        }

        InvalidateVisual();
    }

    void OnRender(CustomDrawingContext& dc) override {
        // Dark slate game board background
        dc.FillRect(
            RectF{0.0f, 0.0f, kBoardWidth, kBoardHeight},
            Color{0.07f, 0.10f, 0.17f, 1.0f});
        dc.DrawRect(
            RectF{1.0f, 1.0f, kBoardWidth - 2.0f, kBoardHeight - 2.0f},
            Color{0.20f, 0.28f, 0.42f, 0.90f},
            2.0f);

        // Score HUD inside top margin
        dc.DrawText(
            "SCORE " + std::to_string(score_),
            RectF{16.0f, 12.0f, 260.0f, 32.0f},
            Color{0.92f, 0.96f, 1.0f, 1.0f},
            18.0f,
            HorizontalAlignment::Left,
            VerticalAlignment::Top);

        // Bricks with +4/+4 drop shadow
        const Color shadowColor{0.0f, 0.0f, 0.0f, 0.45f};
        for (const auto& brick : bricks_) {
            if (!brick.alive) {
                continue;
            }
            const RectF br{
                brick.x + kBrickInset,
                brick.y + kBrickInset,
                brick.w - 2.0f * kBrickInset,
                brick.h - 2.0f * kBrickInset};
            dc.FillRoundedRect(
                RectF{br.x + 4.0f, br.y + 4.0f, br.width, br.height},
                kBrickCornerRadius,
                kBrickCornerRadius,
                shadowColor);
            dc.FillRoundedRect(br, kBrickCornerRadius, kBrickCornerRadius, brick.color);
            dc.DrawRoundedRect(
                br,
                kBrickCornerRadius,
                kBrickCornerRadius,
                Color{1.0f, 1.0f, 1.0f, 0.35f},
                1.0f);
        }

        // Particles
        for (const auto& p : particles_) {
            if (p.life <= 0.0f) {
                continue;
            }
            const float normLife = std::clamp(p.life / std::max(0.01f, p.maxLife), 0.0f, 1.0f);
            const float pr = std::max(0.5f, 5.0f * normLife);
            Color c = p.color;
            c.a = normLife;
            dc.FillEllipse(PointF{p.x, p.y}, pr, pr, c);
        }

        // Paddle (85x22 centered at (padX_, 550))
        const float padLeft = padX_ - kPaddleWidth * 0.5f;
        const float padTop = kPaddleCenterY - kPaddleHeight * 0.5f;
        const RectF padRect{padLeft, padTop, kPaddleWidth, kPaddleHeight};
        dc.FillRoundedRect(
            RectF{padRect.x + 4.0f, padRect.y + 4.0f, padRect.width, padRect.height},
            8.0f,
            8.0f,
            shadowColor);
        dc.FillRoundedRect(padRect, 8.0f, 8.0f, Color{0.78f, 0.84f, 0.90f, 1.0f});
        // Red & cyan end caps inspired by Arkanoid Vaus
        dc.FillRoundedRect(
            RectF{padLeft, padTop, 16.0f, kPaddleHeight},
            7.0f,
            7.0f,
            Color{0.88f, 0.22f, 0.20f, 1.0f});
        dc.FillRoundedRect(
            RectF{padLeft + kPaddleWidth - 16.0f, padTop, 16.0f, kPaddleHeight},
            7.0f,
            7.0f,
            Color{0.88f, 0.22f, 0.20f, 1.0f});
        dc.FillRoundedRect(
            RectF{padLeft + 14.0f, padTop + 2.0f, kPaddleWidth - 28.0f, kPaddleHeight - 4.0f},
            4.0f,
            4.0f,
            Color{0.86f, 0.90f, 0.95f, 1.0f});
        dc.DrawRoundedRect(
            padRect,
            8.0f,
            8.0f,
            Color{0.18f, 0.85f, 1.0f, 0.85f},
            1.5f);

        // Ball with +4/+4 shadow
        dc.FillEllipse(
            PointF{ballX_ + 4.0f, ballY_ + 4.0f},
            kBallRadius,
            kBallRadius,
            shadowColor);
        dc.FillEllipse(
            PointF{ballX_, ballY_},
            kBallRadius,
            kBallRadius,
            Color{1.0f, 1.0f, 1.0f, 1.0f});
        dc.DrawEllipse(
            PointF{ballX_, ballY_},
            kBallRadius,
            kBallRadius,
            Color{0.75f, 0.92f, 1.0f, 1.0f},
            1.0f);

        // Status / Action prompts when paused or ended
        if (!statusText_.empty()) {
            dc.DrawText(
                statusText_,
                RectF{0.0f, 280.0f, kBoardWidth, 48.0f},
                Color{1.0f, 0.90f, 0.30f, 1.0f},
                32.0f,
                HorizontalAlignment::Center,
                VerticalAlignment::Center);
        }
        if (!actionText_.empty()) {
            dc.DrawText(
                actionText_,
                RectF{0.0f, 340.0f, kBoardWidth, 36.0f},
                Color{0.92f, 0.96f, 1.0f, 1.0f},
                20.0f,
                HorizontalAlignment::Center,
                VerticalAlignment::Center);
        }
    }

protected:
    [[nodiscard]] bool DefaultFocusable() const noexcept override {
        return true;
    }

    Size MeasureCore(Size availableSize) noexcept override {
        (void)availableSize;
        return Size{kBoardWidth, kBoardHeight};
    }

private:
    void ResetBoard() noexcept {
        static constexpr Color kRowColors[kBrickRows] = {
            Color{1.0f, 0.0f, 0.0f, 1.0f},         // Red
            Color{1.0f, 1.0f, 0.0f, 1.0f},         // Yellow
            Color{0.0f, 0.749f, 1.0f, 1.0f},       // DeepSkyBlue
            Color{1.0f, 0.0f, 1.0f, 1.0f},         // Fuchsia
            Color{0.0f, 1.0f, 0.0f, 1.0f},         // Lime
        };

        for (int row = 0; row < kBrickRows; ++row) {
            const float y0 = static_cast<float>(row) * kBrickHeight + kBrickStartY;
            for (int col = 0; col < kBrickCols; ++col) {
                const float x0 = static_cast<float>(col) * kBrickWidth;
                bricks_[static_cast<std::size_t>(row * kBrickCols + col)] = Brick{
                    x0,
                    y0,
                    kBrickWidth,
                    kBrickHeight,
                    kRowColors[row],
                    true};
            }
        }

        particles_.clear();
        padX_ = kBoardWidth * 0.5f;
        ballX_ = kBallInitX;
        ballY_ = kBallInitY;
        velX_ = kBallInitVelX;
        velY_ = kBallInitVelY;
        score_ = 0;
        leftDown_ = false;
        rightDown_ = false;
        hasLastMouseX_ = false;
        state_ = BreakoutGameState::Start;
        statusText_.clear();
        actionText_ = "PRESS SPACE TO START";
    }

    void SpawnParticles(float cx, float cy, const Color& color) {
        constexpr float kTwoPi = 6.28318530717958647692f;
        for (int i = 0; i < kParticlesPerBrick; ++i) {
            const float ang =
                (static_cast<float>(i) / static_cast<float>(kParticlesPerBrick)) * kTwoPi + 0.27f;
            const float speed = 90.0f + static_cast<float>((i % 4) * 45);
            const float maxLife = 0.45f + static_cast<float>(i % 3) * 0.15f;
            Particle p{};
            p.x = cx;
            p.y = cy;
            p.vx = std::cos(ang) * speed;
            p.vy = std::sin(ang) * speed - 80.0f;
            p.life = maxLife;
            p.maxLife = maxLife;
            p.color = color;

            bool reused = false;
            for (auto& slot : particles_) {
                if (slot.life <= 0.0f) {
                    slot = p;
                    reused = true;
                    break;
                }
            }
            if (!reused && particles_.size() < 200u) {
                particles_.push_back(p);
            }
        }
    }

    void SyncGameProperties() {
        const auto& ids = GetCustomRenderSamplePropIds();
        if (ids.gameScore != 0xFFFFu) {
            (void)SetValue(
                ids.gameScore,
                value::FromInt(static_cast<std::int64_t>(score_)),
                ValueSourceRank::Local);
        }
        if (ids.gameStatusText != 0xFFFFu) {
            (void)SetValue(
                ids.gameStatusText,
                value::FromString(statusText_),
                ValueSourceRank::Local);
        }
        if (ids.gameActionText != 0xFFFFu) {
            (void)SetValue(
                ids.gameActionText,
                value::FromString(actionText_),
                ValueSourceRank::Local);
        }
    }

    std::array<Brick, static_cast<std::size_t>(kBrickRows * kBrickCols)> bricks_{};
    std::vector<Particle> particles_{};
    BreakoutGameState state_ = BreakoutGameState::Start;
    int score_ = 0;
    float padX_ = kBoardWidth * 0.5f;
    float ballX_ = kBallInitX;
    float ballY_ = kBallInitY;
    float velX_ = kBallInitVelX;
    float velY_ = kBallInitVelY;
    float lastMouseX_ = 0.0f;
    bool hasLastMouseX_ = false;
    bool leftDown_ = false;
    bool rightDown_ = false;
    std::string statusText_{};
    std::string actionText_{"PRESS SPACE TO START"};
};

[[nodiscard]] inline IntrusivePtr<UIElement> CreateBreakoutGameControl() {
    return MakeIntrusive<BreakoutGameControl>();
}

inline void RegisterCustomRenderSampleTypes() {
    auto& ids = GetCustomRenderSamplePropIds();
    ids.gameScore = RegisterCustomProperty(
        "Game", "Score", CustomPropertyValueKind::Int, value::FromInt(0));
    ids.gameStatusText = RegisterCustomProperty(
        "Game", "StatusText", CustomPropertyValueKind::String, value::FromString(""));
    ids.gameActionText = RegisterCustomProperty(
        "Game",
        "ActionText",
        CustomPropertyValueKind::String,
        value::FromString("PRESS SPACE TO START"));

    RegisterCustomControlType("Game", &CreateBreakoutGameControl, false);
    RegisterCustomControlType("local:Game", &CreateBreakoutGameControl, false);
    RegisterCustomControlType("CustomRender.Game", &CreateBreakoutGameControl, false);
}

} // namespace aero::samples

namespace aero {
using samples::RegisterCustomRenderSampleTypes;
} // namespace aero
