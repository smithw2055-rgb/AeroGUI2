module Samples.V3.VectorAndCanvas;

import Aero.UI;
import RealScript.Math;

public class TelemetryCanvasElement : CustomDrawElement {
    private float _phase = 0.25f;
    private int _stepCount = 0;

    public TelemetryCanvasElement() : base() {
        Width = 396.0f;
        Height = 356.0f;
        HorizontalAlignment = HorizontalAlignment.Stretch;
        VerticalAlignment = VerticalAlignment.Stretch;
    }

    public float Phase {
        get { return _phase; }
        set { _phase = value; InvalidateDraw(); }
    }

    public int StepCount {
        get { return _stepCount; }
    }

    public void AdvancePhase(float delta) {
        _phase = _phase + delta;
        _stepCount = _stepCount + 1;
        InvalidateDraw();
    }

    public override void OnRender(DrawingContext dc) {
        uint gridCol = ParseUtil.ParseColor("#FF1E293B");
        uint bgCol = ParseUtil.ParseColor("#FF090F1E");
        uint barCol = ParseUtil.ParseColor("#FF38BDF8");
        uint accentCol = ParseUtil.ParseColor("#FFF472B6");
        uint emeraldCol = ParseUtil.ParseColor("#FF10B981");
        uint textCol = ParseUtil.ParseColor("#FFE2E8F0");

        dc.FillRoundedRect(8.0f, 8.0f, 380.0f, 340.0f, 8.0f, 8.0f, bgCol);

        // Background grid lines
        for (int gx = 32; gx <= 360; gx += 44) {
            dc.DrawLine((float)gx, 24.0f, (float)gx, 300.0f, gridCol, 1.0f);
        }
        for (int gy = 36; gy <= 280; gy += 44) {
            dc.DrawLine(20.0f, (float)gy, 372.0f, (float)gy, gridCol, 1.0f);
        }

        // Animated histogram bars using transformed DrawingContext primitives
        for (int bi = 0; bi < 7; bi += 1) {
            float bx = 34.0f + (float)bi * 48.0f;
            float wave = 0.45f + 0.40f * Math.Absf(Math.Sinf(_phase + (float)bi * 0.65f));
            float bh = 160.0f * wave;
            float by = 260.0f - bh;
            uint fillC = (bi % 2 == 0) ? barCol : emeraldCol;
            dc.FillRoundedRect(bx, by, 30.0f, bh, 5.0f, 5.0f, fillC);
        }

        // Retained vector path & orbital ellipse with local transform stack
        dc.PushTransform(200.0f, 110.0f, 1.0f, 1.0f, _phase * 18.0f);
        dc.FillEllipse(0.0f, 0.0f, 32.0f, 32.0f, accentCol);
        dc.StrokeEllipse(0.0f, 0.0f, 44.0f, 44.0f, textCol, 2.0f);
        dc.PopTransform();

        dc.DrawPath("M 30,275 L 95,210 L 165,245 L 245,175 L 345,220", 0.0f, 0.0f, 380.0f, 320.0f, 0u, accentCol, 3.0f);
        dc.DrawText("Retained Step #" + _stepCount, 22.0f, 308.0f, 340.0f, 28.0f, "Segoe UI", 13.0f, textCol);
    }
}

public partial class MainWindow : Window {
    public TelemetryCanvasElement RetainedCanvas { get; private set; }
    private Storyboard _pulseStoryboard;
    private int _animCount = 0;

    public MainWindow() {
        InitializeComponent();
        SetupVectorScene();
    }

    private void SetupVectorScene() {
        // 1. Construct RadialGradientBrush using Aero.UI 4-float constructor
        if (RadialOrbHost != null) {
            var radial = new RadialGradientBrush(0.5f, 0.5f, 0.55f, 0.55f);
            radial.AddStop("#FFFDE047", 0.0f);
            radial.AddStop("#FFEC4899", 0.55f);
            radial.AddStop("#FF312E81", 1.0f);
            RadialOrbHost.Background = radial;
        }

        // 2. Apply SkewTransform via TransformGroup on GlassBlurBox
        if (GlassBlurBox != null) {
            var tg = new TransformGroup();
            tg.Add(new SkewTransform(4.0f, 0.0f));
            GlassBlurBox.RenderTransform = tg;
        }

        // 3. Attach custom retained CustomDrawElement inside CanvasHost
        RetainedCanvas = new TelemetryCanvasElement();
        if (CanvasHost != null) {
            CanvasHost.Child = RetainedCanvas;
        }

        // 4. Build multi-track Storyboard with DoubleAnimation, ColorAnimation, and SpringAnimation
        _pulseStoryboard = new Storyboard();
        _pulseStoryboard.AddDoubleAnimation("Card3D", "Opacity", 0.72f, 1.0f, 0.45f, EasingFunctionKind.Cubic, EasingMode.EaseOut);
        _pulseStoryboard.AddColorAnimation("RadialOrbHost", "BorderBrush", "#FF38BDF8", "#FFF472B6", 0.45f, EasingFunctionKind.Sine, EasingMode.EaseInOut);
        _pulseStoryboard.AddSpringAnimation("Card3D", "TranslateX", 14.0f, 260.0f, 20.0f);

        if (AnimateBtn != null) {
            AnimateBtn.Click += (object s, RoutedEventArgs e) => {
                TriggerPulseStoryboard();
            };
        }
        if (PulseCanvasBtn != null) {
            PulseCanvasBtn.Click += (object s, RoutedEventArgs e) => {
                StepCanvasWave();
            };
        }

        // Start initial animation once the Window template & Card3D enter the live visual tree
        Loaded += (object s, RoutedEventArgs e) => {
            if (_pulseStoryboard != null) {
                _pulseStoryboard.Begin(this);
            }
        };
    }

    public void TriggerPulseStoryboard() {
        _animCount = _animCount + 1;
        if (_pulseStoryboard != null) {
            _pulseStoryboard.Begin(this);
        }
        if (RetainedCanvas != null) {
            RetainedCanvas.AdvancePhase(0.55f);
        }
        if (VectorStatusText != null) {
            VectorStatusText.Text = "Storyboard triggered #" + _animCount + " (canvas steps: " + RetainedCanvas.StepCount + ")";
        }
    }

    public void StepCanvasWave() {
        if (RetainedCanvas != null) {
            RetainedCanvas.AdvancePhase(0.75f);
        }
        if (Card3D != null) {
            Card3D.Rotation3DY = Card3D.Rotation3DY + 3.0f;
        }
        if (VectorStatusText != null) {
            VectorStatusText.Text = "Canvas wave step #" + RetainedCanvas.StepCount;
        }
    }
}
