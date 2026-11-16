module Scoreboard;

import Aero.UI;

public enum Team
{
    Alliance,
    Horde
}

public enum Class
{
    Fighter,
    Rogue,
    Hunter,
    Mage,
    Cleric
}

[Observable]
public class Player : ObservableObject
{
    private Class _class = Class.Fighter;
    private int _deaths = 0;
    private int _damage = 0;
    private int _heal = 0;
    private int _kills = 0;
    private string _name = "";
    private int _score = 0;
    private Team _team = Team.Alliance;

    public Class Class
    {
        get { return this._class; }
        set
        {
            if (this._class != value)
            {
                this._class = value;
                this.NotifyPropertyChanged("Class");
            }
        }
    }

    public int Deaths
    {
        get { return this._deaths; }
        set { this.SetProperty(ref this._deaths, value, "Deaths"); }
    }

    public int Damage
    {
        get { return this._damage; }
        set { this.SetProperty(ref this._damage, value, "Damage"); }
    }

    public int Heal
    {
        get { return this._heal; }
        set { this.SetProperty(ref this._heal, value, "Heal"); }
    }

    public int Kills
    {
        get { return this._kills; }
        set { this.SetProperty(ref this._kills, value, "Kills"); }
    }

    public string Name
    {
        get { return this._name; }
        set { this.SetProperty(ref this._name, value, "Name"); }
    }

    public int Score
    {
        get { return this._score; }
        set { this.SetProperty(ref this._score, value, "Score"); }
    }

    public Team Team
    {
        get { return this._team; }
        set
        {
            if (this._team != value)
            {
                this._team = value;
                this.NotifyPropertyChanged("Team");
            }
        }
    }

    public Player()
    {
    }

    public Player(Class playerClass, int deaths, int damage, int heal, int kills, string name, int score, Team team)
    {
        this._class = playerClass;
        this._deaths = deaths;
        this._damage = damage;
        this._heal = heal;
        this._kills = kills;
        this._name = name == null ? "" : name;
        this._score = score;
        this._team = team;
    }
}

[Observable]
public class Game : ViewModelBase
{
    private string _name = "Silvershard Mines";
    private int _elapsedTime = 16;
    private int _selectedTeam = 0;
    private int _allianceScore = 0;
    private int _hordeScore = 0;
    private ObservableList<Player> _players;
    private ObservableList<string> _visibleTeams;

    public ObservableList<Player> Players { get { return this._players; } }
    public ObservableList<string> VisibleTeams { get { return this._visibleTeams; } }

    public string Name
    {
        get { return this._name; }
        set
        {
            if (this.SetProperty(ref this._name, value, "Name"))
            {
                this.NotifyPropertyChanged("GameName");
            }
        }
    }

    public int ElapsedTime
    {
        get { return this._elapsedTime; }
        set { this.SetProperty(ref this._elapsedTime, value, "ElapsedTime"); }
    }

    public int SelectedTeam
    {
        get { return this._selectedTeam; }
        set { this.SetProperty(ref this._selectedTeam, value, "SelectedTeam"); }
    }

    public int AllianceScore
    {
        get { return this._allianceScore; }
        set { this.SetProperty(ref this._allianceScore, value, "AllianceScore"); }
    }

    public int HordeScore
    {
        get { return this._hordeScore; }
        set { this.SetProperty(ref this._hordeScore, value, "HordeScore"); }
    }

    public string GameName
    {
        get { return this.Name; }
        set { this.Name = value; }
    }

    public Game()
    {
        this._visibleTeams = new ObservableList<string>();
        this._visibleTeams.Add("Overall");
        this._visibleTeams.Add("Alliance");
        this._visibleTeams.Add("Horde");

        this._players = new ObservableList<Player>();
        this._players.Add(new Player(Class.Mage, 96, 8134124, 1831, 43, "Nam cras aenean", 476, Team.Alliance));
        this._players.Add(new Player(Class.Rogue, 98, 8324715, 2954, 79, "Sed class vestibulum", 414, Team.Horde));
        this._players.Add(new Player(Class.Hunter, 45, 797117, 2615, 99, "Curae praesent", 383, Team.Horde));
        this._players.Add(new Player(Class.Hunter, 93, 481757, 6353, 34, "Adipiscing dis quisque", 327, Team.Alliance));
        this._players.Add(new Player(Class.Fighter, 82, 743715, 37415, 80, "Est donec vivamus", 289, Team.Horde));
        this._players.Add(new Player(Class.Rogue, 21, 383571, 82114, 90, "Duis leo curabitur", 265, Team.Alliance));
        this._players.Add(new Player(Class.Cleric, 86, 441751, 255131, 37, "Mus etiam aliquam", 259, Team.Alliance));
        this._players.Add(new Player(Class.Mage, 60, 201175, 4915, 63, "Nunc mauris accumsan", 225, Team.Horde));
        this._players.Add(new Player(Class.Fighter, 30, 271735, 6715, 20, "Phasellus nullam", 195, Team.Alliance));
        this._players.Add(new Player(Class.Cleric, 18, 87537, 95717, 54, "Consequat bibendum", 180, Team.Horde));
        this._players.Add(new Player(Class.Fighter, 88, 534123, 1241, 66, "Kaelen Ironfist", 175, Team.Alliance));
        this._players.Add(new Player(Class.Mage, 42, 652132, 2341, 22, "Gromdar Frostwolf", 160, Team.Horde));
        this._players.Add(new Player(Class.Rogue, 75, 742131, 8562, 45, "Elara Nightshade", 155, Team.Alliance));
        this._players.Add(new Player(Class.Hunter, 91, 943211, 1124, 88, "Sylas Arrowhawk", 145, Team.Horde));
        this._players.Add(new Player(Class.Cleric, 55, 123456, 456123, 12, "Aveline Dawnblessed", 130, Team.Alliance));
        this._players.Add(new Player(Class.Fighter, 67, 432198, 3214, 59, "Durok Bloodrage", 115, Team.Horde));

        this.RecalculateScores();
    }

    public void RecalculateScores()
    {
        int allianceTotal = 0;
        int hordeTotal = 0;
        int count = this.Players.Count();
        int i = 0;
        while (i < count)
        {
            Player p = this.Players.Get(i);
            if (p != null)
            {
                if (p.Team == Team.Alliance)
                {
                    allianceTotal = allianceTotal + p.Score;
                }
                else if (p.Team == Team.Horde)
                {
                    hordeTotal = hordeTotal + p.Score;
                }
            }
            i = i + 1;
        }
        this.AllianceScore = allianceTotal;
        this.HordeScore = hordeTotal;
    }

    public void SelectTeam(int teamIndex)
    {
        int maxTeams = this.VisibleTeams.Count();
        if (teamIndex < 0 || teamIndex >= maxTeams)
        {
            return;
        }
        this.SelectedTeam = teamIndex;
    }

    public void SelectTeamByName(string teamName)
    {
        int count = this.VisibleTeams.Count();
        int i = 0;
        while (i < count)
        {
            if (this.VisibleTeams.Get(i) == teamName)
            {
                this.SelectedTeam = i;
                return;
            }
            i = i + 1;
        }
    }

    public void AddPlayer(Player player)
    {
        if (player == null)
        {
            return;
        }
        this.Players.Add(player);
        this.RecalculateScores();
    }

    public void AddPlayerScore(int playerIndex, int deltaScore)
    {
        int count = this.Players.Count();
        if (playerIndex < 0 || playerIndex >= count)
        {
            return;
        }
        Player p = this.Players.Get(playerIndex);
        if (p != null)
        {
            p.Score = p.Score + deltaScore;
            this.RecalculateScores();
        }
    }

    public void SetPlayerScore(int playerIndex, int newScore)
    {
        int count = this.Players.Count();
        if (playerIndex < 0 || playerIndex >= count)
        {
            return;
        }
        Player p = this.Players.Get(playerIndex);
        if (p != null)
        {
            p.Score = newScore;
            this.RecalculateScores();
        }
    }

    public void SetElapsedTime(int minutes)
    {
        this.ElapsedTime = minutes;
    }
}

[Observable]
public class ViewModel : Game
{
    public ViewModel()
    {
    }
}

public partial class MainWindow : Page
{
    private Game _vm;

    public Game VM { get { return this._vm; } }
    public Game ViewModel { get { return this._vm; } }

    public MainWindow()
    {
        this._vm = new Game();
        this.DataContext = this._vm;
        this.InitializeComponent();
        BindingEngine.Flush();
    }

    public void OnInitialized()
    {
    }

    public void SelectTeam(int teamIndex)
    {
        if (this.VM != null) { this.VM.SelectTeam(teamIndex); }
    }

    public void SelectTeamByName(string teamName)
    {
        if (this.VM != null) { this.VM.SelectTeamByName(teamName); }
    }

    public void AddPlayerScore(int playerIndex, int deltaScore)
    {
        if (this.VM != null) { this.VM.AddPlayerScore(playerIndex, deltaScore); }
    }

    public void SetPlayerScore(int playerIndex, int newScore)
    {
        if (this.VM != null) { this.VM.SetPlayerScore(playerIndex, newScore); }
    }

    public void RecalculateScores()
    {
        if (this.VM != null) { this.VM.RecalculateScores(); }
    }

    public void SetElapsedTime(int minutes)
    {
        if (this.VM != null) { this.VM.SetElapsedTime(minutes); }
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
