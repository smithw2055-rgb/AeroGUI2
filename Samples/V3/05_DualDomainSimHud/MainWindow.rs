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
            if (SpeedNeedle != null) {
                SpeedNeedle.RenderTransform = new RotateTransform(Sim.HeadingDeg);
            }
        }
        if (ShieldStatusText != null) {
            ShieldStatusText.Text = Sim.ShieldsActive ? "SHIELDS: ONLINE" : "SHIELDS: STANDBY";
        }
        if (TelemetrySummaryText != null) {
            TelemetrySummaryText.Text = "Hull " + (int)Sim.Health + " | Reactor " + (int)Sim.Energy + " | Thrust " + (int)Sim.Thrust;
        }
        if (DomainStatsText != null) {
            DomainStatsText.Text = "Sim commands processed: " + Sim.CommandCount;
        }
    }
}
