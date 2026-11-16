module QuestLog;

import Aero.UI;

public enum QuestDifficulty
{
    Easy,
    Normal,
    Hard
}

[Observable]
public class Quest : ObservableObject
{
    private string _title = "";
    private string _image = "";
    private QuestDifficulty _difficulty = QuestDifficulty.Easy;
    private string _description = "";
    private bool _completed = false;

    public string Title
    {
        get { return this._title; }
        set { this.SetProperty(ref this._title, value, "Title"); }
    }

    public string Image
    {
        get { return this._image; }
        set { this.SetProperty(ref this._image, value, "Image"); }
    }

    public QuestDifficulty Difficulty
    {
        get { return this._difficulty; }
        set
        {
            if (this._difficulty != value)
            {
                this._difficulty = value;
                this.NotifyPropertyChanged("Difficulty");
            }
        }
    }

    public string Description
    {
        get { return this._description; }
        set { this.SetProperty(ref this._description, value, "Description"); }
    }

    public bool Completed
    {
        get { return this._completed; }
        set { this.SetProperty(ref this._completed, value, "Completed"); }
    }

    public Quest()
    {
    }

    public Quest(string title, string image, QuestDifficulty difficulty, string description, bool completed)
    {
        this._title = title == null ? "" : title;
        this._image = image == null ? "" : image;
        this._difficulty = difficulty;
        this._description = description == null ? "" : description;
        this._completed = completed;
    }
}

[Observable]
public class ViewModel : ViewModelBase
{
    private Quest _selectedQuest = null;
    private int _selectedIndex = 0;
    private bool _isClosed = false;
    private ObservableList<Quest> _quests;

    public ObservableList<Quest> Quests { get { return this._quests; } }

    public Quest SelectedQuest
    {
        get { return this._selectedQuest; }
        set
        {
            if (this._selectedQuest != value)
            {
                this._selectedQuest = value;
                this.NotifyPropertyChanged("SelectedQuest");
            }
        }
    }

    public int SelectedIndex
    {
        get { return this._selectedIndex; }
        set { this.SetProperty(ref this._selectedIndex, value, "SelectedIndex"); }
    }

    public bool IsClosed
    {
        get { return this._isClosed; }
        set { this.SetProperty(ref this._isClosed, value, "IsClosed"); }
    }

    public ViewModel()
    {
        this._quests = new ObservableList<Quest>();

        string image0 = "Images/Image0.png";
        string image1 = "Images/Image1.png";
        string image2 = "Images/Image2.png";

        this.Quests.Add(new Quest(
            "Nature's Uprising",
            image0,
            QuestDifficulty.Easy,
            "“The timeless, tireless jaws of nature, which shaped our world since its inception, shall one day devour us all.”",
            false));
        this.Quests.Add(new Quest(
            "Calming the Wake",
            image1,
            QuestDifficulty.Normal,
            "“The rangers warned us of reports of malevolent evil energy swelling and pouring out of the dark Marshes.”",
            true));
        this.Quests.Add(new Quest(
            "Tomen's Curiosity",
            image2,
            QuestDifficulty.Easy,
            "“Back to camp from the battle, lieutenant Tomen awaits your return with the loot of last night's skirmish.”",
            false));
        this.Quests.Add(new Quest(
            "Forest in Dispair",
            image0,
            QuestDifficulty.Normal,
            "“Wild animals are growing more and more aggressive near Eastburg, and our men suffers constant attacks from wolves!”",
            true));
        this.Quests.Add(new Quest(
            "Retaliation",
            image1,
            QuestDifficulty.Easy,
            "“The goblins are among the most ancient and bitter rivals of the Dwarven brethen, and yet again we should clash.”",
            true));
        this.Quests.Add(new Quest(
            "Living Hearts",
            image2,
            QuestDifficulty.Easy,
            "“Venture into the depths of this land, and old lover's curse may be a blessing in disguise near the BrokenHeart's Well.”",
            false));
        this.Quests.Add(new Quest(
            "Scout their Defenses",
            image0,
            QuestDifficulty.Easy,
            "“Elwin believes that you can score a telling blow against the Black Ogres and their masters in the northern Amberwood.”",
            false));
        this.Quests.Add(new Quest(
            "Rescue the Leader",
            image1,
            QuestDifficulty.Hard,
            "“Enter into their dwelling and release as many of the captives as possible. Find them quickly!”",
            true));
        this.Quests.Add(new Quest(
            "Golden Oportunity",
            image2,
            QuestDifficulty.Hard,
            "“You restored the good name of the Purple Guard, and master Proudbeard is delighted at what you have done.”",
            false));
        this.Quests.Add(new Quest(
            "Loyal Subordinates",
            image2,
            QuestDifficulty.Easy,
            "“You have helped us before, and if you continue to do so we will not forget it. Neither will the Orcs, to be sure.”",
            true));
        this.Quests.Add(new Quest(
            "Legacy of the Past",
            image1,
            QuestDifficulty.Normal,
            "“Word of your deeds travelled far, and you became much sought-after by those who have need of such a champion!”",
            true));
        this.Quests.Add(new Quest(
            "Purging the Dead",
            image2,
            QuestDifficulty.Hard,
            "“I think he said he hid it in a tall tree east of the River Falls in the Autumn Valley. Beware of the Necromancer!”",
            false));

        this._selectedIndex = 0;
        this._selectedQuest = this.Quests.Get(0);
        this._isClosed = false;
    }

    public int QuestCount()
    {
        return this.Quests.Count();
    }

    public void SelectQuest(Quest quest)
    {
        if (quest == null)
        {
            return;
        }
        int idx = this.Quests.IndexOf(quest);
        if (idx >= 0)
        {
            this.SelectedIndex = idx;
        }
        this.SelectedQuest = quest;
    }

    public void SelectQuestByIndex(int index)
    {
        int count = this.Quests.Count();
        if (index < 0 || index >= count)
        {
            return;
        }
        this.SelectedIndex = index;
        this.SelectedQuest = this.Quests.Get(index);
    }

    public void NextQuest()
    {
        int count = this.Quests.Count();
        if (count <= 0)
        {
            return;
        }
        int nextIdx = this.SelectedIndex + 1;
        if (nextIdx >= count)
        {
            nextIdx = 0;
        }
        this.SelectQuestByIndex(nextIdx);
    }

    public void PreviousQuest()
    {
        int count = this.Quests.Count();
        if (count <= 0)
        {
            return;
        }
        int prevIdx = this.SelectedIndex - 1;
        if (prevIdx < 0)
        {
            prevIdx = count - 1;
        }
        this.SelectQuestByIndex(prevIdx);
    }

    public void ToggleSelectedQuestCompleted()
    {
        if (this.SelectedQuest != null)
        {
            this.SelectedQuest.Completed = !this.SelectedQuest.Completed;
        }
    }

    public void SetQuestCompleted(int index, bool completed)
    {
        int count = this.Quests.Count();
        if (index < 0 || index >= count)
        {
            return;
        }
        Quest q = this.Quests.Get(index);
        if (q != null)
        {
            q.Completed = completed;
        }
    }

    public void Close()
    {
        this.IsClosed = true;
    }

    public void Open()
    {
        this.IsClosed = false;
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

    public void SelectQuest(Quest quest)
    {
        if (this.VM != null) { this.VM.SelectQuest(quest); }
    }

    public void SelectQuestByIndex(int index)
    {
        if (this.VM != null) { this.VM.SelectQuestByIndex(index); }
    }

    public void NextQuest()
    {
        if (this.VM != null) { this.VM.NextQuest(); }
    }

    public void PreviousQuest()
    {
        if (this.VM != null) { this.VM.PreviousQuest(); }
    }

    public void ToggleCompleted()
    {
        if (this.VM != null) { this.VM.ToggleSelectedQuestCompleted(); }
    }

    public void ToggleSelectedQuestCompleted()
    {
        if (this.VM != null) { this.VM.ToggleSelectedQuestCompleted(); }
    }

    public void SetQuestCompleted(int index, bool completed)
    {
        if (this.VM != null) { this.VM.SetQuestCompleted(index, completed); }
    }

    public void CloseQuestLog()
    {
        if (this.VM != null) { this.VM.Close(); }
    }

    public void OpenQuestLog()
    {
        if (this.VM != null) { this.VM.Open(); }
    }
}

public static MainWindow CreateMainWindow()
{
    return new MainWindow();
}
