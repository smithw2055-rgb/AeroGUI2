module CustomRender;

import Aero.Native;
import Aero.UI;
import RealScript.Math;

public class Game : CustomDrawElement
{
    public int Score { get; set; } = 0;
    public int Lives { get; set; } = 3;
    public string StatusText { get; set; } = "AERO BREAKOUT";
    public string ActionText { get; set; } = "SPACE: START / PAUSE | LEFT & RIGHT OR MOUSE: MOVE PADDLE";
    public bool IsRunning { get; set; } = false;
    public bool IsGameOver { get; set; } = false;

    private float _arenaW = 1280.0f;
    private float _arenaH = 720.0f;
    private bool _layoutSynced = false;
    private float _paddleX = 340.0f;
    private float _paddleW = 120.0f;
    private float _paddleH = 16.0f;
    private float _ballX = 400.0f;
    private float _ballY = 510.0f;
    private float _ballVX = 220.0f;
    private float _ballVY = -260.0f;
    private float _ballR = 9.0f;
    private int _rows = 5;
    private int _cols = 10;
    private bool[] _bricks;

    public Game() : base()
    {
        this.Focusable = true;
        this._bricks = new bool[50];
        this.ResetGame();
    }

    public override string GetTypeName()
    {
        return "Game";
    }

    public void ResetGame()
    {
        this.Score = 0;
        this.Lives = 3;
        this.IsRunning = false;
        this.IsGameOver = false;
        this._paddleX = (this._arenaW - this._paddleW) * 0.5f;
        this._ballX = this._arenaW * 0.5f;
        this._ballY = this._arenaH - 86.0f;
        this._ballVX = 220.0f;
        this._ballVY = -260.0f;

        int i = 0;
        while (i < this._bricks.Length)
        {
            this._bricks[i] = true;
            i = i + 1;
        }

        this.StatusText = "READY";
        this.ActionText = "PRESS SPACE OR CLICK TO LAUNCH";
        this.NotifyBindings();
        this.InvalidateDraw();
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
                this._ballY = h - 86.0f;
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
        if (this.IsRunning && ParseUtil.IsNativeReady())
        {
            Host_RequestWake();
        }
    }

    private void NotifyBindings()
    {
        BindingEngine.NotifyTargetPropertyChanged(this, "Score", DependencyObject.BoxInt(this.Score));
        BindingEngine.NotifyTargetPropertyChanged(this, "Lives", DependencyObject.BoxInt(this.Lives));
        BindingEngine.NotifyTargetPropertyChanged(this, "StatusText", DependencyObject.BoxString(this.StatusText));
        BindingEngine.NotifyTargetPropertyChanged(this, "ActionText", DependencyObject.BoxString(this.ActionText));
    }

    public override DependencyObject GetPropertyValue(string propName)
    {
        if (propName == "Score") { return DependencyObject.BoxInt(this.Score); }
        if (propName == "Lives") { return DependencyObject.BoxInt(this.Lives); }
        if (propName == "StatusText") { return DependencyObject.BoxString(this.StatusText); }
        if (propName == "ActionText") { return DependencyObject.BoxString(this.ActionText); }
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
            this.InvalidateDraw();
            e.Handled = true;
        }
        else if (e.Key == KeyCode.Right || e.Key == KeyCode.D)
        {
            this._paddleX = Math.Clampf(this._paddleX + 36.0f, 16.0f, w - this._paddleW - 16.0f);
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
        if (!this.IsRunning || this.IsGameOver) { return; }
        float w = this.ActualWidth > 10.0f ? this.ActualWidth : this._arenaW;
        float h = this.ActualHeight > 10.0f ? this.ActualHeight : this._arenaH;
        float step = Math.Clampf(dt, 0.001f, 0.05f);

        this._ballX = this._ballX + this._ballVX * step;
        this._ballY = this._ballY + this._ballVY * step;

        if (this._ballX - this._ballR < 12.0f)
        {
            this._ballX = 12.0f + this._ballR;
            this._ballVX = Math.Absf(this._ballVX);
        }
        else if (this._ballX + this._ballR > w - 12.0f)
        {
            this._ballX = w - 12.0f - this._ballR;
            this._ballVX = -Math.Absf(this._ballVX);
        }

        if (this._ballY - this._ballR < 12.0f)
        {
            this._ballY = 12.0f + this._ballR;
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

        // Brick grid collision
        float marginX = 44.0f;
        float topY = 90.0f;
        float gap = 6.0f;
        float brickW = (w - marginX * 2.0f - gap * (float)(this._cols - 1)) / (float)this._cols;
        float brickH = 22.0f;
        int remaining = 0;

        int r = 0;
        while (r < this._rows)
        {
            int c = 0;
            while (c < this._cols)
            {
                int idx = r * this._cols + c;
                if (this._bricks[idx])
                {
                    float bx = marginX + (float)c * (brickW + gap);
                    float by = topY + (float)r * (brickH + gap);
                    if (this._ballX + this._ballR >= bx && this._ballX - this._ballR <= bx + brickW &&
                        this._ballY + this._ballR >= by && this._ballY - this._ballR <= by + brickH)
                    {
                        this._bricks[idx] = false;
                        this._ballVY = -this._ballVY;
                        this.Score = this.Score + (5 - r) * 10;
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

        if (remaining == 0)
        {
            this.IsRunning = false;
            this.IsGameOver = true;
            this.StatusText = "VICTORY!";
            this.ActionText = "PRESS SPACE OR R TO PLAY AGAIN";
        }
        else if (this._ballY - this._ballR > h)
        {
            this.Lives = this.Lives - 1;
            if (this.Lives <= 0)
            {
                this.IsRunning = false;
                this.IsGameOver = true;
                this.StatusText = "GAME OVER";
                this.ActionText = "PRESS SPACE OR R TO RETRY";
            }
            else
            {
                this.IsRunning = false;
                this._ballX = this._paddleX + this._paddleW * 0.5f;
                this._ballY = paddleTop - 18.0f;
                this._ballVY = -260.0f;
                this.StatusText = "LIFE LOST";
                this.ActionText = "PRESS SPACE TO LAUNCH";
            }
        }

        this.NotifyBindings();
        this.InvalidateDraw();
        if (this.IsRunning && ParseUtil.IsNativeReady())
        {
            Host_RequestWake();
        }
    }

    private uint RowColor(int row)
    {
        if (row == 0) { return ParseUtil.ParseColor("#FFF43F5E"); }
        if (row == 1) { return ParseUtil.ParseColor("#FFFB923C"); }
        if (row == 2) { return ParseUtil.ParseColor("#FFFACC15"); }
        if (row == 3) { return ParseUtil.ParseColor("#FF34D399"); }
        return ParseUtil.ParseColor("#FF38BDF8");
    }

    public override void OnRender(DrawingContext dc)
    {
        float w = this.ActualWidth > 10.0f ? this.ActualWidth : this._arenaW;
        float h = this.ActualHeight > 10.0f ? this.ActualHeight : this._arenaH;
        this.SyncArenaSize(w, h);

        uint bg = ParseUtil.ParseColor("#FF060B14");
        uint border = ParseUtil.ParseColor("#FF1E3A5F");
        uint grid = ParseUtil.ParseColor("#FF0E1A2E");
        uint paddleFill = ParseUtil.ParseColor("#FF38BDF8");
        uint paddleGlow = ParseUtil.ParseColor("#FFBAE6FD");
        uint ballCol = ParseUtil.ParseColor("#FFFFFFFF");
        uint ballHalo = ParseUtil.ParseColor("#8060A5FA");

        dc.FillRect(0.0f, 0.0f, w, h, bg, -1);

        float gx = 40.0f;
        while (gx < w)
        {
            dc.DrawLine(gx, 0.0f, gx, h, grid, 1.0f);
            gx = gx + 40.0f;
        }
        float gy = 40.0f;
        while (gy < h)
        {
            dc.DrawLine(0.0f, gy, w, gy, grid, 1.0f);
            gy = gy + 40.0f;
        }

        dc.StrokeRect(6.0f, 6.0f, w - 12.0f, h - 12.0f, border, 2.0f);

        float marginX = 44.0f;
        float topY = 90.0f;
        float gap = 6.0f;
        float brickW = (w - marginX * 2.0f - gap * (float)(this._cols - 1)) / (float)this._cols;
        float brickH = 22.0f;

        int r = 0;
        while (r < this._rows)
        {
            uint col = this.RowColor(r);
            int c = 0;
            while (c < this._cols)
            {
                int idx = r * this._cols + c;
                if (this._bricks[idx])
                {
                    float bx = marginX + (float)c * (brickW + gap);
                    float by = topY + (float)r * (brickH + gap);
                    dc.FillRoundedRect(bx, by, brickW, brickH, 4.0f, 4.0f, col, -1);
                }
                c = c + 1;
            }
            r = r + 1;
        }

        float paddleTop = h - 64.0f;
        dc.FillRoundedRect(this._paddleX, paddleTop, this._paddleW, this._paddleH, 6.0f, 6.0f, paddleFill, -1);
        dc.StrokeRoundedRect(this._paddleX, paddleTop, this._paddleW, this._paddleH, 6.0f, 6.0f, paddleGlow, 1.5f);

        dc.FillEllipse(this._ballX, this._ballY, this._ballR + 4.0f, this._ballR + 4.0f, ballHalo, -1);
        dc.FillEllipse(this._ballX, this._ballY, this._ballR, this._ballR, ballCol, -1);
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
