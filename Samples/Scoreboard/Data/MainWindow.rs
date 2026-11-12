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
public class Player
{
    public Class Class { get; set; }
    public int Deaths { get; set; }
    public int Damage { get; set; }
    public int Heal { get; set; }
    public int Kills { get; set; }
    public string Name { get; set; }
    public int Score { get; set; }
    public Team Team { get; set; }

    public Player()
    {
        this.Class = Class.Fighter;
        this.Deaths = 0;
        this.Damage = 0;
        this.Heal = 0;
        this.Kills = 0;
        this.Name = "";
        this.Score = 0;
        this.Team = Team.Alliance;
    }

    public Player(Class playerClass, int deaths, int damage, int heal, int kills, string name, int score, Team team)
    {
        this.Class = playerClass;
        this.Deaths = deaths;
        this.Damage = damage;
        this.Heal = heal;
        this.Kills = kills;
        this.Name = name;
        this.Score = score;
        this.Team = team;
    }
}

[Observable]
public class ViewModel
{
    public string Name { get; set; }
    public int ElapsedTime { get; set; }
    public int SelectedTeam { get; set; }
    public int AllianceScore { get; set; }
    public int HordeScore { get; set; }
    public ObservableList<Player> Players { get; set; }
    public ObservableList<string> VisibleTeams { get; set; }

    public string GameName
    {
        get { return this.Name; }
        set { this.Name = value; }
    }

    public ViewModel()
    {
        this.Name = "Silvershard Mines";
        this.ElapsedTime = 16;
        this.SelectedTeam = 0;
        this.AllianceScore = 0;
        this.HordeScore = 0;

        this.VisibleTeams = new ObservableList<string>(3);
        this.VisibleTeams.Add("Overall");
        this.VisibleTeams.Add("Alliance");
        this.VisibleTeams.Add("Horde");

        this.Players = new ObservableList<Player>(16);
        this.Players.Add(new Player(Class.Mage, 96, 8134124, 1831, 43, "Nam cras aenean", 476, Team.Alliance));
        this.Players.Add(new Player(Class.Rogue, 98, 8324715, 2954, 79, "Sed class vestibulum", 414, Team.Horde));
        this.Players.Add(new Player(Class.Hunter, 45, 797117, 2615, 99, "Curae praesent", 383, Team.Horde));
        this.Players.Add(new Player(Class.Hunter, 93, 481757, 6353, 34, "Adipiscing dis quisque", 327, Team.Alliance));
        this.Players.Add(new Player(Class.Fighter, 82, 743715, 37415, 80, "Est donec vivamus", 289, Team.Horde));
        this.Players.Add(new Player(Class.Rogue, 21, 383571, 82114, 90, "Duis leo curabitur", 265, Team.Alliance));
        this.Players.Add(new Player(Class.Cleric, 86, 441751, 255131, 37, "Mus etiam aliquam", 259, Team.Alliance));
        this.Players.Add(new Player(Class.Mage, 60, 201175, 4915, 63, "Nunc mauris accumsan", 225, Team.Horde));
        this.Players.Add(new Player(Class.Fighter, 30, 271735, 6715, 20, "Phasellus nullam", 195, Team.Alliance));
        this.Players.Add(new Player(Class.Cleric, 18, 87537, 95717, 54, "Consequat bibendum", 180, Team.Horde));
        this.Players.Add(new Player(Class.Fighter, 88, 534123, 1241, 66, "Kaelen Ironfist", 175, Team.Alliance));
        this.Players.Add(new Player(Class.Mage, 42, 652132, 2341, 22, "Gromdar Frostwolf", 160, Team.Horde));
        this.Players.Add(new Player(Class.Rogue, 75, 742131, 8562, 45, "Elara Nightshade", 155, Team.Alliance));
        this.Players.Add(new Player(Class.Hunter, 91, 943211, 1124, 88, "Sylas Arrowhawk", 145, Team.Horde));
        this.Players.Add(new Player(Class.Cleric, 55, 123456, 456123, 12, "Aveline Dawnblessed", 130, Team.Alliance));
        this.Players.Add(new Player(Class.Fighter, 67, 432198, 3214, 59, "Durok Bloodrage", 115, Team.Horde));

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
public partial class MainWindow
{
    public ViewModel ViewModel { get; set; }

    public MainWindow()
    {
        this.ViewModel = new ViewModel();
    }

    public void OnInitialized()
    {
    }

    public void SelectTeam(int teamIndex)
    {
        this.ViewModel.SelectTeam(teamIndex);
    }

    public void SelectTeamByName(string teamName)
    {
        this.ViewModel.SelectTeamByName(teamName);
    }

    public void AddPlayerScore(int playerIndex, int deltaScore)
    {
        this.ViewModel.AddPlayerScore(playerIndex, deltaScore);
    }

    public void SetPlayerScore(int playerIndex, int newScore)
    {
        this.ViewModel.SetPlayerScore(playerIndex, newScore);
    }

    public void RecalculateScores()
    {
        this.ViewModel.RecalculateScores();
    }

    public void SetElapsedTime(int minutes)
    {
        this.ViewModel.SetElapsedTime(minutes);
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
