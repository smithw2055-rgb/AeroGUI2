module CustomRender;

import Aero.Native;
import Aero.UI;
import RealScript.Math;

public class Game : CustomDrawElement
{
    public int Score { get; set; } = 0;
    public int Lives { get; set; } = 1;
    public string StatusText { get; set; } = "READY";
    public string ActionText { get; set; } = "PRESS SPACE OR CLICK TO LAUNCH";
    public bool IsRunning { get; set; } = false;
    public bool IsGameOver { get; set; } = false;
    public int ActiveParticleCount { get; set; } = 0;

    private float _arenaW = 1280.0f;
    private float _arenaH = 720.0f;
    private bool _layoutSynced = false;
    private float _paddleX = 565.0f;
    private float _paddleW = 150.0f;
    private float _paddleH = 26.0f;
    private float _ballX = 640.0f;
    private float _ballY = 634.0f;
    private float _ballVX = 220.0f;
    private float _ballVY = -260.0f;
    private float _ballR = 11.0f;
    private int _rows = 5;
    private int _cols = 13;
    private int[] _bricks;
    private int _bricksRemaining = 65;

    // Brick shatter particle pool
    private int _maxParticles = 160;
    private int[] _partAlive;
    private float[] _partX;
    private float[] _partY;
    private float[] _partVX;
    private float[] _partVY;
    private float[] _partLife;
    private float[] _partMaxLife;
    private float[] _partSize;
    private int[] _partRow;
    private int _nextParticleCursor = 0;
    private int _rngSeed = 1337;

    // Cached 3D Brushes & packed ARGB colors (avoid per-frame string parsing / GC churn)
    private RadialGradientBrush _bgBrush;
    private LinearGradientBrush[] _brickBrushes;
    private LinearGradientBrush _paddleSilverBrush;
    private LinearGradientBrush _paddleRedBrush;
    private RadialGradientBrush _paddleTipBrush;
    private RadialGradientBrush _ballSphereBrush;
    private uint[] _rowPartColors;
    private uint _colBrickShadow = 0u;
    private uint _colBrickSpec = 0u;
    private uint _colBrickRim = 0u;
    private uint _colBrickBottomShade = 0u;
    private uint _colPadShadow = 0u;
    private uint _colGroove = 0u;
    private uint _colGrooveHi = 0u;
    private uint _colCollar = 0u;
    private uint _colPaddleRim = 0u;
    private uint _colBallShadow = 0u;
    private uint _colSpecOuter = 0u;
    private uint _colSpecCore = 0u;
    private bool _brushesReady = false;

    private int _lastBoundScore = -1;
    private int _lastBoundLives = -1;
    private string _lastBoundStatus = "";
    private string _lastBoundAction = "";

    public Game() : base()
    {
        this.Focusable = true;
        this._bricks = new int[0];
        int bi = 0;
        while (bi < 65) { this._bricks.Push(1); bi = bi + 1; }
        this._partAlive = new int[0];
        this._partX = new float[0];
        this._partY = new float[0];
        this._partVX = new float[0];
        this._partVY = new float[0];
        this._partLife = new float[0];
        this._partMaxLife = new float[0];
        this._partSize = new float[0];
        this._partRow = new int[0];
        int pi = 0;
        while (pi < 160)
        {
            this._partAlive.Push(0);
            this._partX.Push(0.0f);
            this._partY.Push(0.0f);
            this._partVX.Push(0.0f);
            this._partVY.Push(0.0f);
            this._partLife.Push(0.0f);
            this._partMaxLife.Push(1.0f);
            this._partSize.Push(5.0f);
            this._partRow.Push(0);
            pi = pi + 1;
        }
        this.ResetGame();
    }

    public override string GetTypeName()
    {
        return "Game";
    }

    private void EnsureBrushes()
    {
        if (this._brushesReady) { return; }
        this._brushesReady = true;

        this._bgBrush = new RadialGradientBrush(0.5f, 0.5f, 0.5f, 0.5f, 0.72f, 0.72f);
        this._bgBrush.AddStop("#FF2D313B", 0.0f);
        this._bgBrush.AddStop("#FF20232B", 0.55f);
        this._bgBrush.AddStop("#FF13151A", 1.0f);

        this._brickBrushes = new LinearGradientBrush[0];

        var b0 = new LinearGradientBrush(0.0f, 0.0f, 0.0f, 1.0f);
        b0.AddStop("#FFFF5252", 0.0f);
        b0.AddStop("#FFE61919", 0.45f);
        b0.AddStop("#FF990F0F", 1.0f);
        this._brickBrushes.Push(b0);

        var b1 = new LinearGradientBrush(0.0f, 0.0f, 0.0f, 1.0f);
        b1.AddStop("#FFFFF04D", 0.0f);
        b1.AddStop("#FFEAB308", 0.48f);
        b1.AddStop("#FFA16207", 1.0f);
        this._brickBrushes.Push(b1);

        var b2 = new LinearGradientBrush(0.0f, 0.0f, 0.0f, 1.0f);
        b2.AddStop("#FF38BDF8", 0.0f);
        b2.AddStop("#FF0284C7", 0.45f);
        b2.AddStop("#FF1E3A8A", 1.0f);
        this._brickBrushes.Push(b2);

        var b3 = new LinearGradientBrush(0.0f, 0.0f, 0.0f, 1.0f);
        b3.AddStop("#FFF472B6", 0.0f);
        b3.AddStop("#FFD900D9", 0.45f);
        b3.AddStop("#FF86198F", 1.0f);
        this._brickBrushes.Push(b3);

        var b4 = new LinearGradientBrush(0.0f, 0.0f, 0.0f, 1.0f);
        b4.AddStop("#FF4ADE80", 0.0f);
        b4.AddStop("#FF16A34A", 0.48f);
        b4.AddStop("#FF14532D", 1.0f);
        this._brickBrushes.Push(b4);

        this._paddleSilverBrush = new LinearGradientBrush(0.0f, 0.0f, 0.0f, 1.0f);
        this._paddleSilverBrush.AddStop("#FFF8FAFC", 0.0f);
        this._paddleSilverBrush.AddStop("#FFCBD5E1", 0.35f);
        this._paddleSilverBrush.AddStop("#FF64748B", 0.78f);
        this._paddleSilverBrush.AddStop("#FF334155", 1.0f);

        this._paddleRedBrush = new LinearGradientBrush(0.0f, 0.0f, 0.0f, 1.0f);
        this._paddleRedBrush.AddStop("#FFFF6B6B", 0.0f);
        this._paddleRedBrush.AddStop("#FFDC2626", 0.5f);
        this._paddleRedBrush.AddStop("#FF7F1D1D", 1.0f);

        this._paddleTipBrush = new RadialGradientBrush(0.45f, 0.38f, 0.35f, 0.32f, 0.65f, 0.65f);
        this._paddleTipBrush.AddStop("#FFE0F2FE", 0.0f);
        this._paddleTipBrush.AddStop("#FF38BDF8", 0.55f);
        this._paddleTipBrush.AddStop("#FF0369A1", 1.0f);

        this._ballSphereBrush = new RadialGradientBrush(0.36f, 0.32f, 0.30f, 0.26f, 0.72f, 0.72f);
        this._ballSphereBrush.AddStop("#FFE0F2FE", 0.0f);
        this._ballSphereBrush.AddStop("#FF38BDF8", 0.28f);
        this._ballSphereBrush.AddStop("#FF0284C7", 0.62f);
        this._ballSphereBrush.AddStop("#FF0F172A", 1.0f);

        this._rowPartColors = new uint[0];
        this._rowPartColors.Push(ParseUtil.ParseColor("#FFFF3B3B"));
        this._rowPartColors.Push(ParseUtil.ParseColor("#FFFACC15"));
        this._rowPartColors.Push(ParseUtil.ParseColor("#FF38BDF8"));
        this._rowPartColors.Push(ParseUtil.ParseColor("#FFE843E8"));
        this._rowPartColors.Push(ParseUtil.ParseColor("#FF22E538"));

        this._colBrickShadow = ParseUtil.ParseColor("#96000000");
        this._colBrickSpec = ParseUtil.ParseColor("#55FFFFFF");
        this._colBrickRim = ParseUtil.ParseColor("#78CBD5E1");
        this._colBrickBottomShade = ParseUtil.ParseColor("#44000000");
        this._colPadShadow = ParseUtil.ParseColor("#A8000000");
        this._colGroove = ParseUtil.ParseColor("#FF334155");
        this._colGrooveHi = ParseUtil.ParseColor("#90FFFFFF");
        this._colCollar = ParseUtil.ParseColor("#FFE2E8F0");
        this._colPaddleRim = ParseUtil.ParseColor("#B0CBD5E1");
        this._colBallShadow = ParseUtil.ParseColor("#AA000000");
        this._colSpecOuter = ParseUtil.ParseColor("#B8BAE6FD");
        this._colSpecCore = ParseUtil.ParseColor("#F2FFFFFF");
    }

    private float NextRand01()
    {
        // Overflow-safe 32-bit LCG: 65520 * 4093 + 1277 = 268,174,637 << INT32_MAX
        int s = this._rngSeed % 65521;
        if (s < 0) { s = -s; }
        s = (s * 4093 + 1277) % 65521;
        if (s < 0) { s = -s; }
        this._rngSeed = s;
        return (float)s / 65521.0f;
    }

    private void ClearParticles()
    {
        int i = 0;
        while (i < this._maxParticles)
        {
            this._partAlive[i] = 0;
            i = i + 1;
        }
        this._nextParticleCursor = 0;
        this.ActiveParticleCount = 0;
    }

    private uint RowParticleColor(int row)
    {
        this.EnsureBrushes();
        if (row >= 0 && row < this._rowPartColors.Length)
        {
            return this._rowPartColors[row];
        }
        return this._rowPartColors[this._rowPartColors.Length - 1];
    }

    private void SpawnBrickShatter(float bx, float by, float bw, float bh, int row)
    {
        int spawned = 0;
        int checkedSlots = 0;
        while (checkedSlots < this._maxParticles && spawned < 14)
        {
            int idx = this._nextParticleCursor;
            this._nextParticleCursor = (this._nextParticleCursor + 1) % this._maxParticles;
            if (this._partAlive[idx] == 0)
            {
                float rx = this.NextRand01();
                float ry = this.NextRand01();
                this._partAlive[idx] = 1;
                this._partX[idx] = bx + 6.0f + rx * Math.Maxf(8.0f, bw - 12.0f);
                this._partY[idx] = by + 4.0f + ry * Math.Maxf(6.0f, bh - 8.0f);
                this._partVX[idx] = (rx - 0.5f) * 210.0f + (this.NextRand01() - 0.5f) * 70.0f;
                this._partVY[idx] = -70.0f + this.NextRand01() * 190.0f;
                this._partMaxLife[idx] = 0.55f + this.NextRand01() * 0.35f;
                this._partLife[idx] = this._partMaxLife[idx];
                this._partSize[idx] = 5.0f + this.NextRand01() * 2.5f;
                this._partRow[idx] = row;
                spawned = spawned + 1;
            }
            checkedSlots = checkedSlots + 1;
        }
        while (spawned < 14)
        {
            int idx = this._nextParticleCursor;
            this._nextParticleCursor = (this._nextParticleCursor + 1) % this._maxParticles;
            float rx = this.NextRand01();
            float ry = this.NextRand01();
            this._partAlive[idx] = 1;
            this._partX[idx] = bx + 6.0f + rx * Math.Maxf(8.0f, bw - 12.0f);
            this._partY[idx] = by + 4.0f + ry * Math.Maxf(6.0f, bh - 8.0f);
            this._partVX[idx] = (rx - 0.5f) * 210.0f + (this.NextRand01() - 0.5f) * 70.0f;
            this._partVY[idx] = -70.0f + this.NextRand01() * 190.0f;
            this._partMaxLife[idx] = 0.55f + this.NextRand01() * 0.35f;
            this._partLife[idx] = this._partMaxLife[idx];
            this._partSize[idx] = 5.0f + this.NextRand01() * 2.5f;
            this._partRow[idx] = row;
            spawned = spawned + 1;
        }
        this.RecountParticles();
    }

    private void RecountParticles()
    {
        int alive = 0;
        int i = 0;
        while (i < this._maxParticles)
        {
            if (this._partAlive[i] != 0) { alive = alive + 1; }
            i = i + 1;
        }
        this.ActiveParticleCount = alive;
    }

    private bool StepParticles(float step)
    {
        int alive = 0;
        int i = 0;
        while (i < this._maxParticles)
        {
            if (this._partAlive[i] != 0)
            {
                this._partLife[i] = this._partLife[i] - step;
                if (this._partLife[i] <= 0.0f)
                {
                    this._partAlive[i] = 0;
                }
                else
                {
                    this._partVY[i] = this._partVY[i] + 440.0f * step;
                    this._partX[i] = this._partX[i] + this._partVX[i] * step;
                    this._partY[i] = this._partY[i] + this._partVY[i] * step;
                    alive = alive + 1;
                }
            }
            i = i + 1;
        }
        this.ActiveParticleCount = alive;
        return alive > 0;
    }

    private void RestoreBricks()
    {
        int i = 0;
        while (i < this._bricks.Length)
        {
            this._bricks[i] = 1;
            i = i + 1;
        }
        this._bricksRemaining = this._bricks.Length;
    }

    private void ResetBallOnPaddle(float w, float h)
    {
        this._paddleX = (w - this._paddleW) * 0.5f;
        this._ballX = w * 0.5f;
        this._ballY = h - 64.0f - this._ballR - 2.0f;
        this._ballVX = 220.0f;
        this._ballVY = -260.0f;
    }

    public void ResetGame()
    {
        float w = this.ActualWidth > 10.0f ? this.ActualWidth : this._arenaW;
        float h = this.ActualHeight > 10.0f ? this.ActualHeight : this._arenaH;
        this._arenaW = w;
        this._arenaH = h;

        this.Score = 0;
        this.Lives = 1;
        this.IsRunning = false;
        this.IsGameOver = false;
        this.ResetBallOnPaddle(w, h);
        this.ClearParticles();
        this.RestoreBricks();

        this.StatusText = "READY";
        this.ActionText = "PRESS SPACE OR CLICK TO LAUNCH";
        this.NotifyBindings();
        this.InvalidateDraw();
        if (ParseUtil.IsNativeReady())
        {
            Host_RequestWake();
        }
    }

    private void SyncArenaSize(float w, float h)
    {
        if (w <= 10.0f || h <= 10.0f) { return; }
        if (!this._layoutSynced || Math.Absf(w - this._arenaW) > 1.0f || Math.Absf(h - this._arenaH) > 1.0f)
        {
            float sx = this._arenaW > 10.0f ? (w / this._arenaW) : 1.0f;
            this._arenaW = w;
            this._arenaH = h;
            this._paddleX = Math.Clampf(this._paddleX * sx, 16.0f, w - this._paddleW - 16.0f);
            if (!this.IsRunning && !this.IsGameOver)
            {
                this._ballX = this._paddleX + this._paddleW * 0.5f;
                this._ballY = h - 64.0f - this._ballR - 2.0f;
            }
            this._layoutSynced = true;
        }
    }

    public void ToggleRun()
    {
        if (this.IsGameOver)
        {
            this.ResetGame();
        }
        this.IsRunning = !this.IsRunning;
        this.StatusText = this.IsRunning ? "" : "PAUSED";
        this.ActionText = this.IsRunning ? "" : "PRESS SPACE TO RESUME";
        this.NotifyBindings();
        this.InvalidateDraw();
        if (ParseUtil.IsNativeReady())
        {
            Host_RequestWake();
        }
    }

    private void NotifyBindings()
    {
        if (this._lastBoundScore != this.Score)
        {
            this._lastBoundScore = this.Score;
            BindingEngine.NotifyTargetPropertyChanged(this, "Score", DependencyObject.BoxInt(this.Score));
        }
        if (this._lastBoundLives != this.Lives)
        {
            this._lastBoundLives = this.Lives;
            BindingEngine.NotifyTargetPropertyChanged(this, "Lives", DependencyObject.BoxInt(this.Lives));
        }
        if (this._lastBoundStatus != this.StatusText)
        {
            this._lastBoundStatus = this.StatusText;
            BindingEngine.NotifyTargetPropertyChanged(this, "StatusText", DependencyObject.BoxString(this.StatusText));
        }
        if (this._lastBoundAction != this.ActionText)
        {
            this._lastBoundAction = this.ActionText;
            BindingEngine.NotifyTargetPropertyChanged(this, "ActionText", DependencyObject.BoxString(this.ActionText));
        }
    }

    public override DependencyObject GetPropertyValue(string propName)
    {
        if (propName == "Score") { return DependencyObject.BoxInt(this.Score); }
        if (propName == "Lives") { return DependencyObject.BoxInt(this.Lives); }
        if (propName == "StatusText") { return DependencyObject.BoxString(this.StatusText); }
        if (propName == "ActionText") { return DependencyObject.BoxString(this.ActionText); }
        if (propName == "BricksRemaining") { return DependencyObject.BoxInt(this._bricksRemaining); }
        if (propName == "ActiveParticleCount") { return DependencyObject.BoxInt(this.ActiveParticleCount); }
        return base.GetPropertyValue(propName);
    }

    public override void OnMouseMove(MouseEventArgs e)
    {
        float w = this.ActualWidth > 10.0f ? this.ActualWidth : this._arenaW;
        float h = this.ActualHeight > 10.0f ? this.ActualHeight : this._arenaH;
        this.SyncArenaSize(w, h);
        float localX = e.X - this.LayoutX;
        this._paddleX = Math.Clampf(localX - this._paddleW * 0.5f, 16.0f, w - this._paddleW - 16.0f);
        if (!this.IsRunning && !this.IsGameOver)
        {
            this._ballX = this._paddleX + this._paddleW * 0.5f;
        }
        this.InvalidateDraw();
        e.Handled = true;
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        this.Focus();
        if (!this.IsRunning)
        {
            this.ToggleRun();
        }
        else
        {
            this.StepFrame(0.016f);
        }
    }

    public override void OnKeyDown(KeyEventArgs e)
    {
        float w = this.ActualWidth > 10.0f ? this.ActualWidth : this._arenaW;
        if (e.Key == KeyCode.Left || e.Key == KeyCode.A)
        {
            this._paddleX = Math.Clampf(this._paddleX - 36.0f, 16.0f, w - this._paddleW - 16.0f);
            if (!this.IsRunning && !this.IsGameOver)
            {
                this._ballX = this._paddleX + this._paddleW * 0.5f;
            }
            this.InvalidateDraw();
            e.Handled = true;
        }
        else if (e.Key == KeyCode.Right || e.Key == KeyCode.D)
        {
            this._paddleX = Math.Clampf(this._paddleX + 36.0f, 16.0f, w - this._paddleW - 16.0f);
            if (!this.IsRunning && !this.IsGameOver)
            {
                this._ballX = this._paddleX + this._paddleW * 0.5f;
            }
            this.InvalidateDraw();
            e.Handled = true;
        }
        else if (e.Key == KeyCode.Space)
        {
            this.ToggleRun();
            e.Handled = true;
        }
        else if (e.Key == KeyCode.R)
        {
            this.ResetGame();
            e.Handled = true;
        }
    }

    public void StepFrame(float dt)
    {
        if (dt <= 0.0f) { return; }
        float step = Math.Clampf(dt, 0.001f, 0.05f);
        bool hadParticles = this.ActiveParticleCount > 0;
        bool stillParticles = hadParticles ? this.StepParticles(step) : false;

        if (!this.IsRunning || this.IsGameOver)
        {
            if (hadParticles)
            {
                this.InvalidateDraw();
                if (stillParticles && ParseUtil.IsNativeReady())
                {
                    Host_RequestWake();
                }
            }
            return;
        }

        float w = this.ActualWidth > 10.0f ? this.ActualWidth : this._arenaW;
        float h = this.ActualHeight > 10.0f ? this.ActualHeight : this._arenaH;

        this._ballX = this._ballX + this._ballVX * step;
        this._ballY = this._ballY + this._ballVY * step;

        if (this._ballX - this._ballR < 8.0f)
        {
            this._ballX = 8.0f + this._ballR;
            this._ballVX = Math.Absf(this._ballVX);
        }
        else if (this._ballX + this._ballR > w - 8.0f)
        {
            this._ballX = w - 8.0f - this._ballR;
            this._ballVX = -Math.Absf(this._ballVX);
        }

        if (this._ballY - this._ballR < 8.0f)
        {
            this._ballY = 8.0f + this._ballR;
            this._ballVY = Math.Absf(this._ballVY);
        }

        float paddleTop = h - 64.0f;
        if (this._ballVY > 0.0f &&
            this._ballY + this._ballR >= paddleTop &&
            this._ballY - this._ballR <= paddleTop + this._paddleH &&
            this._ballX >= this._paddleX - this._ballR &&
            this._ballX <= this._paddleX + this._paddleW + this._ballR)
        {
            this._ballY = paddleTop - this._ballR;
            this._ballVY = -Math.Absf(this._ballVY);
            float hitNorm = ((this._ballX - this._paddleX) / this._paddleW) * 2.0f - 1.0f;
            this._ballVX = hitNorm * 320.0f;
        }

        // Brick grid collision (13 cols x 5 rows matching reference)
        float marginX = 6.0f;
        float topY = 148.0f;
        float gap = 5.0f;
        float brickW = (w - marginX * 2.0f - gap * (float)(this._cols - 1)) / (float)this._cols;
        float brickH = 33.0f;
        int remaining = 0;
        bool bouncedBrick = false;

        int r = 0;
        while (r < this._rows)
        {
            int c = 0;
            while (c < this._cols)
            {
                int idx = r * this._cols + c;
                if (this._bricks[idx] != 0)
                {
                    float bx = marginX + (float)c * (brickW + gap);
                    float by = topY + (float)r * (brickH + gap);
                    if (!bouncedBrick &&
                        this._ballX + this._ballR >= bx && this._ballX - this._ballR <= bx + brickW &&
                        this._ballY + this._ballR >= by && this._ballY - this._ballR <= by + brickH)
                    {
                        this._bricks[idx] = 0;
                        bouncedBrick = true;
                        float overlapLeft = (this._ballX + this._ballR) - bx;
                        float overlapRight = (bx + brickW) - (this._ballX - this._ballR);
                        float overlapTop = (this._ballY + this._ballR) - by;
                        float overlapBottom = (by + brickH) - (this._ballY - this._ballR);
                        float minOverlapX = Math.Minf(overlapLeft, overlapRight);
                        float minOverlapY = Math.Minf(overlapTop, overlapBottom);
                        if (minOverlapX < minOverlapY)
                        {
                            this._ballVX = -this._ballVX;
                        }
                        else
                        {
                            this._ballVY = -this._ballVY;
                        }
                        this.Score = this.Score + 10;
                        this.SpawnBrickShatter(bx, by, brickW, brickH, r);
                    }
                    else
                    {
                        remaining = remaining + 1;
                    }
                }
                c = c + 1;
            }
            r = r + 1;
        }

        this._bricksRemaining = remaining;
        if (remaining == 0)
        {
            this.RestoreBricks();
            this.ResetBallOnPaddle(w, h);
            this.IsRunning = false;
            this.IsGameOver = true;
            this.StatusText = "VICTORY!";
            this.ActionText = "PRESS SPACE OR CLICK TO PLAY AGAIN";
        }
        else if (this._ballY - this._ballR > h)
        {
            this.Lives = this.Lives - 1;
            if (this.Lives <= 0)
            {
                this.RestoreBricks();
                this.ResetBallOnPaddle(w, h);
                this.IsRunning = false;
                this.IsGameOver = true;
                this.StatusText = "GAME OVER";
                this.ActionText = "PRESS SPACE OR CLICK TO RETRY";
            }
            else
            {
                this.IsRunning = false;
                this._ballX = this._paddleX + this._paddleW * 0.5f;
                this._ballY = paddleTop - this._ballR - 2.0f;
                this._ballVY = -260.0f;
                this.StatusText = "LIFE LOST";
                this.ActionText = "PRESS SPACE TO LAUNCH";
            }
        }

        this.NotifyBindings();
        this.InvalidateDraw();
        if ((this.IsRunning || this.ActiveParticleCount > 0) && ParseUtil.IsNativeReady())
        {
            Host_RequestWake();
        }
    }

    public override void OnRender(DrawingContext dc)
    {
        float w = this.ActualWidth > 10.0f ? this.ActualWidth : this._arenaW;
        float h = this.ActualHeight > 10.0f ? this.ActualHeight : this._arenaH;
        this.SyncArenaSize(w, h);
        this.EnsureBrushes();

        // 1. Dark radial vignette arena background
        dc.FillRect(0.0f, 0.0f, w, h, this._bgBrush);

        // 2. 3D beveled rounded bricks with drop shadow, specular highlight & inner rim
        float marginX = 6.0f;
        float topY = 148.0f;
        float gap = 5.0f;
        float brickW = (w - marginX * 2.0f - gap * (float)(this._cols - 1)) / (float)this._cols;
        float brickH = 33.0f;

        uint brickShadow = this._colBrickShadow;
        uint brickSpec = this._colBrickSpec;
        uint brickRim = this._colBrickRim;
        uint brickBottomShade = this._colBrickBottomShade;

        int r = 0;
        while (r < this._rows)
        {
            LinearGradientBrush rowBrush = this._brickBrushes[r];
            int c = 0;
            while (c < this._cols)
            {
                int idx = r * this._cols + c;
                if (this._bricks[idx] != 0)
                {
                    float bx = marginX + (float)c * (brickW + gap);
                    float by = topY + (float)r * (brickH + gap);
                    dc.FillRoundedRect(bx + 3.0f, by + 4.0f, brickW, brickH, 5.0f, 5.0f, brickShadow, -1);
                    dc.FillRoundedRect(bx, by, brickW, brickH, 5.0f, 5.0f, rowBrush);
                    dc.FillRoundedRect(bx + 3.0f, by + 2.0f, brickW - 6.0f, 6.0f, 3.0f, 3.0f, brickSpec, -1);
                    dc.FillRoundedRect(bx + 2.0f, by + brickH - 5.0f, brickW - 4.0f, 3.5f, 2.0f, 2.0f, brickBottomShade, -1);
                    dc.StrokeRoundedRect(bx + 0.5f, by + 0.5f, brickW - 1.0f, brickH - 1.0f, 5.0f, 5.0f, brickRim, 1.2f);
                }
                c = c + 1;
            }
            r = r + 1;
        }

        // 3. Shatter fragments with subtle 3D bevel + shadow
        int pi = 0;
        while (pi < this._maxParticles)
        {
            if (this._partAlive[pi] != 0)
            {
                float ratio = Math.Clampf(this._partLife[pi] / Math.Maxf(0.05f, this._partMaxLife[pi]), 0.0f, 1.0f);
                float sz = this._partSize[pi] * (0.45f + 0.55f * ratio);
                float px = this._partX[pi] - sz * 0.5f;
                float py = this._partY[pi] - sz * 0.5f;
                uint pCol = ParseUtil.ApplyOpacityToArgb(this.RowParticleColor(this._partRow[pi]), ratio);
                uint pShadow = ParseUtil.ApplyOpacityToArgb(brickShadow, ratio * 0.7f);
                uint pHi = ParseUtil.ApplyOpacityToArgb(brickSpec, ratio * 0.85f);
                dc.FillRoundedRect(px + 2.0f, py + 2.5f, sz, sz, 1.5f, 1.5f, pShadow, -1);
                dc.FillRoundedRect(px, py, sz, sz, 1.5f, 1.5f, pCol, -1);
                dc.FillRect(px + 1.0f, py + 1.0f, Math.Maxf(1.0f, sz - 2.0f), 1.5f, pHi, -1);
            }
            pi = pi + 1;
        }

        // 4. 3D metallic capsule paddle (cyan outer tips + red shoulder caps + brushed silver center + 2 grooves)
        float px0 = this._paddleX;
        float py0 = h - 64.0f;
        float pw = this._paddleW;
        float ph = this._paddleH;
        float halfH = ph * 0.5f;

        dc.FillRoundedRect(px0 + 4.0f, py0 + 6.0f, pw, ph, halfH, halfH, this._colPadShadow, -1);
        // Cyan glowing outer tips
        dc.FillRoundedRect(px0, py0 + 3.0f, 22.0f, ph - 6.0f, (ph - 6.0f) * 0.5f, (ph - 6.0f) * 0.5f, this._paddleTipBrush);
        dc.FillRoundedRect(px0 + pw - 22.0f, py0 + 3.0f, 22.0f, ph - 6.0f, (ph - 6.0f) * 0.5f, (ph - 6.0f) * 0.5f, this._paddleTipBrush);
        // Glossy red shoulder caps
        dc.FillRoundedRect(px0 + 8.0f, py0, pw - 16.0f, ph, halfH, halfH, this._paddleRedBrush);
        // Brushed silver central cylinder
        float bodyX = px0 + 31.0f;
        float bodyW = pw - 62.0f;
        dc.FillRect(bodyX, py0, bodyW, ph, this._paddleSilverBrush);
        // White-silver collar rings
        dc.FillRect(bodyX - 2.0f, py0, 2.5f, ph, this._colCollar, -1);
        dc.FillRect(bodyX + bodyW - 0.5f, py0, 2.5f, ph, this._colCollar, -1);
        // Center horizontal mechanical grooves
        float grooveX = bodyX + 14.0f;
        float grooveW = bodyW - 28.0f;
        dc.FillRect(grooveX, py0 + ph * 0.40f, grooveW, 1.8f, this._colGroove, -1);
        dc.FillRect(grooveX, py0 + ph * 0.62f, grooveW, 1.8f, this._colGroove, -1);
        dc.FillRoundedRect(px0 + 10.0f, py0 + 2.0f, pw - 20.0f, 4.5f, 2.2f, 2.2f, this._colGrooveHi, -1);
        dc.StrokeRoundedRect(px0 + 8.0f, py0 + 0.5f, pw - 16.0f, ph - 1.0f, halfH, halfH, this._colPaddleRim, 1.1f);

        // 5. 3D glossy blue sphere ball (shadow + specular highlight + main FillEllipse LAST for test invariant)
        float br = this._ballR;
        dc.FillRoundedRect(this._ballX - br + 4.0f, this._ballY - br + 5.0f, br * 2.0f, br * 2.0f, br, br, this._colBallShadow, -1);
        // Main 3D sphere FillEllipse MUST be the final FillEllipse command in OnRender
        dc.FillEllipse(this._ballX, this._ballY, br, br, this._ballSphereBrush);
        // Crisp specular highlight dots rendered via rounded-rect circles so FillEllipse invariant stays exact
        dc.FillRoundedRect(this._ballX - br * 0.56f, this._ballY - br * 0.58f, br * 0.62f, br * 0.50f, br * 0.28f, br * 0.25f, this._colSpecOuter, -1);
        dc.FillRoundedRect(this._ballX - br * 0.44f, this._ballY - br * 0.48f, br * 0.34f, br * 0.28f, br * 0.15f, br * 0.14f, this._colSpecCore, -1);
    }
}

[Observable]
public partial class MainWindow : Window
{
    public MainWindow()
    {
        this.InitializeComponent();
        if (this.GameControl != null)
        {
            this.GameControl.Focus();
            this.Loaded += (object s, RoutedEventArgs e) => {
                if (this.GameControl != null) { this.GameControl.Focus(); }
            };
        }
        BindingEngine.Flush();
    }

    public void StepGame(float dt)
    {
        if (this.GameControl != null)
        {
            this.GameControl.StepFrame(dt);
        }
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
