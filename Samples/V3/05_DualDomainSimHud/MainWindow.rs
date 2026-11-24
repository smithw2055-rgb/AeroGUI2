module Samples.V3.DualDomainSimHud;

import Aero.UI;

[SimState]
public class SimTelemetryState {
    public float Health = 320.0f;
    public float Energy = 260.0f;
    public float Thrust = 180.0f;
    public float HeadingDeg = 22.0f;
    public bool ShieldsActive = true;
    public int CommandCount = 0;

    [SimCommand]
    public void ApplyAfterburner() {
        Thrust = Thrust + 45.0f;
        if (Thrust > 376.0f) { Thrust = 376.0f; }
        Energy = Energy - 35.0f;
        if (Energy < 40.0f) { Energy = 40.0f; }
        HeadingDeg = HeadingDeg + 18.0f;
        CommandCount = CommandCount + 1;
    }

    [SimCommand]
    public void ToggleShields() {
        ShieldsActive = !ShieldsActive;
        if (ShieldsActive) {
            Health = Health + 20.0f;
            if (Health > 376.0f) { Health = 376.0f; }
        }
        CommandCount = CommandCount + 1;
    }

    [SimCommand]
    public void ResetTelemetry() {
        Health = 320.0f;
        Energy = 260.0f;
        Thrust = 180.0f;
        HeadingDeg = 22.0f;
        ShieldsActive = true;
        CommandCount = CommandCount + 1;
    }

    public void AdvanceTick(float dt) {
        if (!ShieldsActive && Energy < 360.0f) {
            Energy = Energy + 24.0f * dt;
            if (Energy > 360.0f) { Energy = 360.0f; }
        }
        HeadingDeg = HeadingDeg + 12.0f * dt;
    }
}

public static SimTelemetryState CreateSimState() {
    return new SimTelemetryState();
}

public partial class MainWindow : Window {
    public SimTelemetryState Sim { get; private set; }
    private int _lastSyncedCmdCount = -1;
    private RotateTransform _needleRotate = null;
    private float _lastNeedleHeading = -99999.0f;
    private int _lastShieldInt = -1;
    private int _lastHullInt = -1;
    private int _lastEnergyInt = -1;
    private int _lastThrustInt = -1;
    private int _lastStatsCmd = -1;

    public MainWindow() {
        InitializeComponent();
        WireHudControls();
    }

    public void AttachSimState(SimTelemetryState sim) {
        Sim = sim;
        _lastSyncedCmdCount = -1;
        SyncHudFromSim();
    }

    private void WireHudControls() {
        if (BoostBtn != null) {
            BoostBtn.Click += (object s, RoutedEventArgs e) => {
                Sim.ApplyAfterburner();
                SyncHudFromSim();
            };
        }
        if (ShieldBtn != null) {
            ShieldBtn.Click += (object s, RoutedEventArgs e) => {
                Sim.ToggleShields();
                SyncHudFromSim();
            };
        }
        if (ResetSimBtn != null) {
            ResetSimBtn.Click += (object s, RoutedEventArgs e) => {
                Sim.ResetTelemetry();
                SyncHudFromSim();
            };
        }
    }

    public void StepSimulation(float dt) {
        if (Sim != null) {
            Sim.AdvanceTick(dt);
            SyncHudFromSim();
        }
    }

    public void SyncHudFromSim() {
        if (Sim == null) { return; }
        if (_lastSyncedCmdCount != Sim.CommandCount) {
            _lastSyncedCmdCount = Sim.CommandCount;
            if (HealthFill != null) { HealthFill.Width = Sim.Health; }
            if (EnergyFill != null) { EnergyFill.Width = Sim.Energy; }
            if (ThrustFill != null) { ThrustFill.Width = Sim.Thrust; }
        }
        if (SpeedNeedle != null && _lastNeedleHeading != Sim.HeadingDeg) {
            _lastNeedleHeading = Sim.HeadingDeg;
            if (_needleRotate == null) {
                _needleRotate = new RotateTransform(Sim.HeadingDeg);
                SpeedNeedle.RenderTransform = _needleRotate;
            } else {
                _needleRotate.Angle = Sim.HeadingDeg;
            }
        }
        int shieldInt = Sim.ShieldsActive ? 1 : 0;
        if (ShieldStatusText != null && _lastShieldInt != shieldInt) {
            _lastShieldInt = shieldInt;
            ShieldStatusText.Text = Sim.ShieldsActive ? "SHIELDS: ONLINE" : "SHIELDS: STANDBY";
        }
        int hullI = (int)Sim.Health;
        int energyI = (int)Sim.Energy;
        int thrustI = (int)Sim.Thrust;
        if (TelemetrySummaryText != null &&
            (hullI != _lastHullInt || energyI != _lastEnergyInt || thrustI != _lastThrustInt)) {
            _lastHullInt = hullI;
            _lastEnergyInt = energyI;
            _lastThrustInt = thrustI;
            TelemetrySummaryText.Text = "Hull " + hullI + " | Reactor " + energyI + " | Thrust " + thrustI;
        }
        if (DomainStatsText != null && _lastStatsCmd != Sim.CommandCount) {
            _lastStatsCmd = Sim.CommandCount;
            DomainStatsText.Text = "Sim commands processed: " + Sim.CommandCount;
        }
    }
}
