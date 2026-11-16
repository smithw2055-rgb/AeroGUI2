module IntegrationGLUT;

import Aero.UI;
import RealScript.Math;

[Observable]
public class HudViewModel : ViewModelBase
{
    public float YawDeg { get; set; } = 32.0f;
    public float PitchDeg { get; set; } = 22.0f;
    public bool ShowHelloCard { get; set; } = true;
    public string TelemetryLabel { get; set; } = "3D Wireframe Viewport | Yaw: 32 deg | Pitch: 22 deg | Drag or Arrow keys to orbit";
    public string AngleSummary { get; set; } = "3D Wireframe + HUD Overlay | Yaw 32 deg";

    public HudViewModel()
    {
    }

    public void UpdateAngles(float yaw, float pitch)
    {
        this.YawDeg = yaw;
        this.PitchDeg = pitch;
        int iy = (int)yaw;
        int ip = (int)pitch;
        this.TelemetryLabel = "3D Wireframe Viewport | Yaw: " + iy + " deg | Pitch: " + ip + " deg | Drag or Arrow keys to orbit";
        this.AngleSummary = "3D Wireframe + HUD Overlay | Yaw " + iy + " deg";
        this.NotifyPropertyChanged("YawDeg");
        this.NotifyPropertyChanged("PitchDeg");
        this.NotifyPropertyChanged("TelemetryLabel");
        this.NotifyPropertyChanged("AngleSummary");
    }
}

public class GlutViewport : CustomDrawElement
{
    public float YawDeg { get; set; } = 32.0f;
    public float PitchDeg { get; set; } = 22.0f;
    private float _lastDragX = 0.0f;
    private float _lastDragY = 0.0f;
    private bool _dragging = false;
    private HudViewModel _hud = null;

    public GlutViewport() : base()
    {
    }

    public void AttachHud(HudViewModel hud)
    {
        this._hud = hud;
        if (this._hud != null)
        {
            this._hud.UpdateAngles(this.YawDeg, this.PitchDeg);
        }
    }

    public void RotateBy(float deltaYaw, float deltaPitch)
    {
        this.YawDeg = this.YawDeg + deltaYaw;
        this.PitchDeg = Math.Clampf(this.PitchDeg + deltaPitch, -75.0f, 75.0f);
        if (this._hud != null)
        {
            this._hud.UpdateAngles(this.YawDeg, this.PitchDeg);
        }
        this.InvalidateDraw();
    }

    public override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        this._dragging = true;
        this._lastDragX = e.X;
        this._lastDragY = e.Y;
        this.Focus();
    }

    public override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        this._dragging = false;
    }

    public override void OnMouseMove(MouseEventArgs e)
    {
        if (!this._dragging) { return; }
        float dx = e.X - this._lastDragX;
        float dy = e.Y - this._lastDragY;
        this._lastDragX = e.X;
        this._lastDragY = e.Y;
        this.RotateBy(dx * 0.5f, dy * 0.35f);
    }

    public override void OnKeyDown(KeyEventArgs e)
    {
        if (e.Key == KeyCode.Left || e.Key == KeyCode.A)
        {
            this.RotateBy(-15.0f, 0.0f);
            e.Handled = true;
        }
        else if (e.Key == KeyCode.Right || e.Key == KeyCode.D)
        {
            this.RotateBy(15.0f, 0.0f);
            e.Handled = true;
        }
        else if (e.Key == KeyCode.Up || e.Key == KeyCode.W)
        {
            this.RotateBy(0.0f, -8.0f);
            e.Handled = true;
        }
        else if (e.Key == KeyCode.Down || e.Key == KeyCode.S)
        {
            this.RotateBy(0.0f, 8.0f);
            e.Handled = true;
        }
    }

    private float ProjectX(float x, float y, float z, float cx, float scale, float cosY, float sinY)
    {
        float rx = x * cosY + z * sinY;
        float rz = -x * sinY + z * cosY;
        float depth = 3.6f + rz;
        return cx + (rx * scale) / (depth > 0.4f ? depth : 0.4f);
    }

    private float ProjectY(float x, float y, float z, float cy, float scale, float cosY, float sinY, float cosP, float sinP)
    {
        float rz = -x * sinY + z * cosY;
        float ry = y * cosP - rz * sinP;
        float pz = y * sinP + rz * cosP;
        float depth = 3.6f + pz;
        return cy - (ry * scale) / (depth > 0.4f ? depth : 0.4f);
    }

    public override void OnRender(DrawingContext dc)
    {
        float w = this.ActualWidth > 1.0f ? this.ActualWidth : 1024.0f;
        float h = this.ActualHeight > 1.0f ? this.ActualHeight : 640.0f;
        float cx = w * 0.5f;
        float cy = h * 0.52f;
        float scale = Math.Minf(w, h) * 0.85f;

        uint bgCol = ParseUtil.ParseColor("#FF0A111D");
        uint gridCol = ParseUtil.ParseColor("#FF16253C");
        uint edgeFront = ParseUtil.ParseColor("#FF43B0FF");
        uint edgeBack = ParseUtil.ParseColor("#FF1F5480");
        uint axisXCol = ParseUtil.ParseColor("#FFEF5350");
        uint axisYCol = ParseUtil.ParseColor("#FF66BB6A");
        uint axisZCol = ParseUtil.ParseColor("#FF42A5F5");

        dc.FillRect(0.0f, 0.0f, w, h, bgCol, -1);

        float gx = 0.0f;
        while (gx <= w)
        {
            dc.DrawLine(gx, 0.0f, gx, h, gridCol, 1.0f);
            gx = gx + 64.0f;
        }
        float gy = 0.0f;
        while (gy <= h)
        {
            dc.DrawLine(0.0f, gy, w, gy, gridCol, 1.0f);
            gy = gy + 64.0f;
        }

        float rad = 0.0174532925f;
        float yr = this.YawDeg * rad;
        float pr = this.PitchDeg * rad;
        float cosY = Math.Cosf(yr);
        float sinY = Math.Sinf(yr);
        float cosP = Math.Cosf(pr);
        float sinP = Math.Sinf(pr);

        float[] sx = new float[8];
        float[] sy = new float[8];
        int i = 0;
        while (i < 8)
        {
            float vx = (i % 2 == 0) ? -1.0f : 1.0f;
            float vy = ((i / 2) % 2 == 0) ? -1.0f : 1.0f;
            float vz = (i < 4) ? -1.0f : 1.0f;
            sx[i] = this.ProjectX(vx, vy, vz, cx, scale, cosY, sinY);
            sy[i] = this.ProjectY(vx, vy, vz, cy, scale, cosY, sinY, cosP, sinP);
            i = i + 1;
        }

        // Back quad (0..3) and connectors
        dc.DrawLine(sx[0], sy[0], sx[1], sy[1], edgeBack, 2.0f);
        dc.DrawLine(sx[1], sy[1], sx[3], sy[3], edgeBack, 2.0f);
        dc.DrawLine(sx[3], sy[3], sx[2], sy[2], edgeBack, 2.0f);
        dc.DrawLine(sx[2], sy[2], sx[0], sy[0], edgeBack, 2.0f);

        dc.DrawLine(sx[0], sy[0], sx[4], sy[4], edgeBack, 2.0f);
        dc.DrawLine(sx[1], sy[1], sx[5], sy[5], edgeBack, 2.0f);
        dc.DrawLine(sx[2], sy[2], sx[6], sy[6], edgeBack, 2.0f);
        dc.DrawLine(sx[3], sy[3], sx[7], sy[7], edgeBack, 2.0f);

        // Front quad (4..7)
        dc.DrawLine(sx[4], sy[4], sx[5], sy[5], edgeFront, 2.5f);
        dc.DrawLine(sx[5], sy[5], sx[7], sy[7], edgeFront, 2.5f);
        dc.DrawLine(sx[7], sy[7], sx[6], sy[6], edgeFront, 2.5f);
        dc.DrawLine(sx[6], sy[6], sx[4], sy[4], edgeFront, 2.5f);

        // Coordinate tripod in bottom-left viewport corner
        float ox = 86.0f;
        float oy = h - 78.0f;
        float axLen = 46.0f;
        dc.DrawLine(ox, oy, ox + axLen * cosY, oy - axLen * sinY * sinP, axisXCol, 2.5f);
        dc.DrawLine(ox, oy, ox, oy - axLen * cosP, axisYCol, 2.5f);
        dc.DrawLine(ox, oy, ox + axLen * sinY, oy + axLen * cosY * sinP, axisZCol, 2.5f);
    }
}

[Observable]
public partial class MainWindow : Window
{
    public HudViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.InitializeComponent();
        this.ViewModel = new HudViewModel();
        this.DataContext = this.ViewModel;

        if (this.Viewport3D != null)
        {
            this.Viewport3D.AttachHud(this.ViewModel);
        }
        if (this.RotateLeftBtn != null)
        {
            this.RotateLeftBtn.Click += (object s, RoutedEventArgs e) => { this.RotateScene(-15.0f, 0.0f); };
        }
        if (this.RotateRightBtn != null)
        {
            this.RotateRightBtn.Click += (object s, RoutedEventArgs e) => { this.RotateScene(15.0f, 0.0f); };
        }
        if (this.ToggleOverlayBtn != null)
        {
            this.ToggleOverlayBtn.Click += (object s, RoutedEventArgs e) => { this.ToggleCard(); };
        }

        BindingEngine.Flush();
    }

    public void RotateScene(float deltaYaw, float deltaPitch)
    {
        if (this.Viewport3D != null)
        {
            this.Viewport3D.RotateBy(deltaYaw, deltaPitch);
        }
    }

    public void ToggleCard()
    {
        if (this.ViewModel == null) { return; }
        this.ViewModel.ShowHelloCard = !this.ViewModel.ShowHelloCard;
        if (this.HelloCard != null)
        {
            this.HelloCard.Visibility = this.ViewModel.ShowHelloCard ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
