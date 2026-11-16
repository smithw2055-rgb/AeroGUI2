module TicTacToe;

import Aero.UI;

public enum State
{
    None = 0,
    Player1 = 1,
    Player2 = 2,
    Tie = 3,
    WinRow0 = 4,
    WinRow1 = 5,
    WinRow2 = 6,
    WinCol0 = 7,
    WinCol1 = 8,
    WinCol2 = 9,
    WinDiag1 = 10,
    WinDiag2 = 11
}

[Observable]
public class ViewModel : ViewModelBase
{
    private DelegateCommand _startCommand;
    private DelegateCommand _checkCommand;
    private State _state = State.Player1;
    private int _scorePlayer1 = 0;
    private int _scorePlayer2 = 0;
    private int _scoreTies = 0;
    private int _scoreHalf = 0;
    private int _cell0 = 0;
    private int _cell1 = 0;
    private int _cell2 = 0;
    private int _cell3 = 0;
    private int _cell4 = 0;
    private int _cell5 = 0;
    private int _cell6 = 0;
    private int _cell7 = 0;
    private int _cell8 = 0;

    public DelegateCommand StartCommand { get { return this._startCommand; } }
    public DelegateCommand CheckCommand { get { return this._checkCommand; } }

    public State State
    {
        get { return this._state; }
        set
        {
            if (this._state != value)
            {
                this._state = value;
                this.NotifyPropertyChanged("State");
            }
        }
    }

    public int ScorePlayer1
    {
        get { return this._scorePlayer1; }
        set { this.SetProperty(ref this._scorePlayer1, value, "ScorePlayer1"); }
    }

    public int ScorePlayer2
    {
        get { return this._scorePlayer2; }
        set { this.SetProperty(ref this._scorePlayer2, value, "ScorePlayer2"); }
    }

    public int ScoreTies
    {
        get { return this._scoreTies; }
        set { this.SetProperty(ref this._scoreTies, value, "ScoreTies"); }
    }

    public int ScoreHalf
    {
        get { return this._scoreHalf; }
        set { this.SetProperty(ref this._scoreHalf, value, "ScoreHalf"); }
    }

    public int Cell0 { get { return this._cell0; } set { this.SetProperty(ref this._cell0, value, "Cell0"); } }
    public int Cell1 { get { return this._cell1; } set { this.SetProperty(ref this._cell1, value, "Cell1"); } }
    public int Cell2 { get { return this._cell2; } set { this.SetProperty(ref this._cell2, value, "Cell2"); } }
    public int Cell3 { get { return this._cell3; } set { this.SetProperty(ref this._cell3, value, "Cell3"); } }
    public int Cell4 { get { return this._cell4; } set { this.SetProperty(ref this._cell4, value, "Cell4"); } }
    public int Cell5 { get { return this._cell5; } set { this.SetProperty(ref this._cell5, value, "Cell5"); } }
    public int Cell6 { get { return this._cell6; } set { this.SetProperty(ref this._cell6, value, "Cell6"); } }
    public int Cell7 { get { return this._cell7; } set { this.SetProperty(ref this._cell7, value, "Cell7"); } }
    public int Cell8 { get { return this._cell8; } set { this.SetProperty(ref this._cell8, value, "Cell8"); } }

    public ViewModel()
    {
        this._startCommand = new DelegateCommand(() => { this.OnStart(); });
        this._checkCommand = new DelegateCommand((object param) => { this.OnCheckParam(param); });

        this._scorePlayer1 = 0;
        this._scorePlayer2 = 0;
        this._scoreTies = 0;
        this._scoreHalf = 0;
        this.Start();
    }

    public int GetCell(int index)
    {
        if (index == 0) { return this._cell0; }
        if (index == 1) { return this._cell1; }
        if (index == 2) { return this._cell2; }
        if (index == 3) { return this._cell3; }
        if (index == 4) { return this._cell4; }
        if (index == 5) { return this._cell5; }
        if (index == 6) { return this._cell6; }
        if (index == 7) { return this._cell7; }
        if (index == 8) { return this._cell8; }
        return 0;
    }

    private void SetCell(int index, int playerMark)
    {
        if (index == 0) { this.Cell0 = playerMark; }
        else if (index == 1) { this.Cell1 = playerMark; }
        else if (index == 2) { this.Cell2 = playerMark; }
        else if (index == 3) { this.Cell3 = playerMark; }
        else if (index == 4) { this.Cell4 = playerMark; }
        else if (index == 5) { this.Cell5 = playerMark; }
        else if (index == 6) { this.Cell6 = playerMark; }
        else if (index == 7) { this.Cell7 = playerMark; }
        else if (index == 8) { this.Cell8 = playerMark; }
    }

    public void OnStart()
    {
        this.Start();
    }

    public void Start()
    {
        this.Cell0 = 0;
        this.Cell1 = 0;
        this.Cell2 = 0;
        this.Cell3 = 0;
        this.Cell4 = 0;
        this.Cell5 = 0;
        this.Cell6 = 0;
        this.Cell7 = 0;
        this.Cell8 = 0;

        int numGames = this.ScorePlayer1 + this.ScorePlayer2 + this.ScoreTies;
        this.State = (numGames % 2 == 0) ? State.Player1 : State.Player2;
    }

    private void OnCheckParam(object param)
    {
        int index = -1;
        if (param is BoxedInt)
        {
            var bi = (BoxedInt)param;
            index = bi.Value;
        }
        else if (param is BoxedFloat)
        {
            var bf = (BoxedFloat)param;
            index = (int)bf.Value;
        }
        else if (param is BoxedString)
        {
            var bs = (BoxedString)param;
            string s = bs.Value;
            if (s == "0") { index = 0; }
            else if (s == "1") { index = 1; }
            else if (s == "2") { index = 2; }
            else if (s == "3") { index = 3; }
            else if (s == "4") { index = 4; }
            else if (s == "5") { index = 5; }
            else if (s == "6") { index = 6; }
            else if (s == "7") { index = 7; }
            else if (s == "8") { index = 8; }
        }
        else
        {
            var dep = param as DependencyObject;
            index = DependencyObject.UnboxInt(dep, -1);
        }
        this.OnCheck(index);
    }

    public void OnCheck(int index)
    {
        this.Check(index);
    }

    public void Check(int index)
    {
        if (index < 0 || index >= 9)
        {
            return;
        }
        if (this.State != State.Player1 && this.State != State.Player2)
        {
            return;
        }
        if (this.GetCell(index) != 0)
        {
            return;
        }

        int mark = (this.State == State.Player1) ? 1 : 2;
        this.SetCell(index, mark);
        this.State = this.CheckWin(mark);
    }

    private State CheckWin(int mark)
    {
        int i = 0;
        while (i < 3)
        {
            if (this.CheckRow(i, mark))
            {
                if (i == 0) { return this.Win(State.WinRow0); }
                if (i == 1) { return this.Win(State.WinRow1); }
                return this.Win(State.WinRow2);
            }
            if (this.CheckCol(i, mark))
            {
                if (i == 0) { return this.Win(State.WinCol0); }
                if (i == 1) { return this.Win(State.WinCol1); }
                return this.Win(State.WinCol2);
            }
            i = i + 1;
        }

        if (this.CheckDiag(0, 2, mark))
        {
            return this.Win(State.WinDiag1);
        }
        if (this.CheckDiag(2, 0, mark))
        {
            return this.Win(State.WinDiag2);
        }

        if (this.CheckTie())
        {
            this.ScoreTies = this.ScoreTies + 1;
            this.ScoreHalf = (this.ScorePlayer1 + this.ScorePlayer2 + this.ScoreTies) / 2;
            return State.Tie;
        }

        return (this.State == State.Player1) ? State.Player2 : State.Player1;
    }

    private bool CheckRow(int row, int mark)
    {
        return this.CheckCell(row, 0, mark) &&
               this.CheckCell(row, 1, mark) &&
               this.CheckCell(row, 2, mark);
    }

    private bool CheckCol(int col, int mark)
    {
        return this.CheckCell(0, col, mark) &&
               this.CheckCell(1, col, mark) &&
               this.CheckCell(2, col, mark);
    }

    private bool CheckDiag(int startRow, int endRow, int mark)
    {
        return this.CheckCell(startRow, 0, mark) &&
               this.CheckCell(1, 1, mark) &&
               this.CheckCell(endRow, 2, mark);
    }

    private bool CheckCell(int row, int col, int mark)
    {
        return this.GetCell(row * 3 + col) == mark;
    }

    private bool CheckTie()
    {
        int i = 0;
        while (i < 9)
        {
            if (this.GetCell(i) == 0)
            {
                return false;
            }
            i = i + 1;
        }
        return true;
    }

    private State Win(State winState)
    {
        if (this.State == State.Player1)
        {
            this.ScorePlayer1 = this.ScorePlayer1 + 1;
        }
        else
        {
            this.ScorePlayer2 = this.ScorePlayer2 + 1;
        }
        this.ScoreHalf = (this.ScorePlayer1 + this.ScorePlayer2 + this.ScoreTies) / 2;
        return winState;
    }
}

public partial class MainWindow : Page
{
    private ViewModel _vm;

    public ViewModel VM { get { return this._vm; } }
    public ViewModel ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new ViewModel();
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void OnInitialized()
    {
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
