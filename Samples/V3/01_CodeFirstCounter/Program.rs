module Samples.V3.CodeFirstCounter;

import Aero.UI;

[Observable]
public class CounterViewModel : ViewModelBase {
    private int _count = 0;
    private int _step = 1;
    private bool _doubleStep = false;
    private string _status = "Ready - click + or - to update counter";

    public int Count {
        get { return _count; }
        set {
            if (SetProperty(ref _count, value, "Count")) {
                NotifyPropertyChanged("CountDisplay");
            }
        }
    }

    public int Step {
        get { return _step; }
        set { SetProperty(ref _step, value, "Step"); }
    }

    public bool DoubleStep {
        get { return _doubleStep; }
        set {
            if (SetProperty(ref _doubleStep, value, "DoubleStep")) {
                Step = value ? 2 : 1;
            }
        }
    }

    public string Status {
        get { return _status; }
        set { SetProperty(ref _status, value, "Status"); }
    }

    public string CountDisplay {
        get { return "Count: " + _count; }
    }

    public void Increment() {
        Count = Count + Step;
        Status = "Incremented by " + Step + " -> " + Count;
    }

    public void Decrement() {
        Count = Count - Step;
        Status = "Decremented by " + Step + " -> " + Count;
    }

    public void Reset() {
        DoubleStep = false;
        Count = 0;
        Status = "Counter reset to 0";
    }
}

public static Window CreateMainWindow() {
    var vm = new CounterViewModel();

    var headerBadge = Box("#FF1E3A8A", 12.0f, 6.0f);
    headerBadge.HorizontalAlignment = HorizontalAlignment.Left;
    headerBadge.Child = Label("PURE CODE-FIRST RS", 11.0f, "#FFBFDBFE");

    var title = Heading("AeroGUI 3.0 Code-First Counter", 24.0f, "#FFF8FAFC");
    var subtitle = MutedLabel("Zero XAML: built exclusively with Aero.UI FluentBuilders & BindingEngine", 13.0f);

    var countText = Label("Count: 0", 40.0f, "#FF38BDF8");
    countText.Name = "CountText";
    countText.FontWeight = "Bold";
    AeroFluent.BindText(countText, () => {
        var state = vm;
        return state.CountDisplay;
    });

    var countCard = Box("#FF0F172A", 10.0f, 18.0f);
    AeroFluent.WithBorder(countCard, "#FF334155", 1.0f, 10.0f);
    countCard.Child = countText;

    var incBtn = PrimaryBtn("Increment (+)", () => {
        var state = vm;
        state.Increment();
    });
    incBtn.Name = "IncBtn";
    incBtn.Width = 148.0f;
    incBtn.Height = 38.0f;

    var decBtn = Btn("Decrement (-)", () => {
        var state = vm;
        state.Decrement();
    });
    decBtn.Name = "DecBtn";
    decBtn.Width = 148.0f;
    decBtn.Height = 38.0f;

    var resetBtn = Btn("Reset", () => {
        var state = vm;
        state.Reset();
    });
    resetBtn.Name = "ResetBtn";
    resetBtn.Width = 104.0f;
    resetBtn.Height = 38.0f;

    var controlsRow = HStack(10.0f, new UIElement[] { incBtn, decBtn, resetBtn });

    var stepCheck = Check("Double step (x2)", false, (bool isChecked) => {
        var state = vm;
        state.DoubleStep = isChecked;
        state.Status = isChecked ? "Step size switched to 2" : "Step size switched to 1";
    });
    stepCheck.Name = "Step2Check";

    var statusText = Label("Ready - click + or - to update counter", 13.0f, "#FFCBD5E1");
    statusText.Name = "StatusText";
    AeroFluent.BindText(statusText, () => {
        var state = vm;
        return state.Status;
    });

    var cardBody = VStack(14.0f, new UIElement[] {
        headerBadge,
        title,
        subtitle,
        Divider(),
        countCard,
        controlsRow,
        stepCheck,
        Divider(),
        statusText
    });

    var mainCard = Card(cardBody, 24.0f, 14.0f);
    mainCard.Width = 540.0f;
    mainCard.HorizontalAlignment = HorizontalAlignment.Center;
    mainCard.VerticalAlignment = VerticalAlignment.Center;
    AeroFluent.WithShadow(mainCard, 16.0f, 6.0f, 0.45f);

    var rootBg = Box("#FF0B1120", 0.0f, 24.0f);
    rootBg.Child = mainCard;

    var win = Win("AeroGUI v3 - Code-First Counter", 640.0f, 440.0f, rootBg);
    win.DataContext = vm;
    return win;
}
