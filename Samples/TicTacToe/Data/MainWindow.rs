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
public class ViewModel
{
    public State State { get; set; } = State.Player1;
    public int ScorePlayer1 { get; set; } = 0;
    public int ScorePlayer2 { get; set; } = 0;
    public int ScoreTies { get; set; } = 0;
    public int ScoreHalf { get; set; } = 0;

    public int Cell0 { get; set; } = 0;
    public int Cell1 { get; set; } = 0;
    public int Cell2 { get; set; } = 0;
    public int Cell3 { get; set; } = 0;
    public int Cell4 { get; set; } = 0;
    public int Cell5 { get; set; } = 0;
    public int Cell6 { get; set; } = 0;
    public int Cell7 { get; set; } = 0;
    public int Cell8 { get; set; } = 0;

    public ViewModel()
    {
        this.ScorePlayer1 = 0;
        this.ScorePlayer2 = 0;
        this.ScoreTies = 0;
        this.ScoreHalf = 0;
        this.Start();
    }

    public int GetCell(int index)
    {
        if (index == 0) { return this.Cell0; }
        if (index == 1) { return this.Cell1; }
        if (index == 2) { return this.Cell2; }
        if (index == 3) { return this.Cell3; }
        if (index == 4) { return this.Cell4; }
        if (index == 5) { return this.Cell5; }
        if (index == 6) { return this.Cell6; }
        if (index == 7) { return this.Cell7; }
        if (index == 8) { return this.Cell8; }
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

    public void StartCommand()
    {
        this.Start();
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

    public void CheckCommand(int index)
    {
        this.Check(index);
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

[Observable]
public partial class MainWindow
{
    public ViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        this.ViewModel = new ViewModel();
    }

    public void OnInitialized()
    {
    }

    // Note: MainPage.xaml defines <Button x:Name="Start">, so MainWindow.g.rs emits
    // `public handle Start;`. StartCommand / OnStart / StartGame avoid colliding with that field.
    public void StartCommand()
    {
        this.ViewModel.Start();
    }

    public void OnStart()
    {
        this.ViewModel.Start();
    }

    public void StartGame()
    {
        this.ViewModel.Start();
    }

    public void CheckCommand(int index)
    {
        this.ViewModel.Check(index);
    }

    public void Check(int index)
    {
        this.ViewModel.Check(index);
    }

    public void OnCheck(int index)
    {
        this.ViewModel.Check(index);
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
