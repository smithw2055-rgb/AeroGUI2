module QuestLog;
import Aero.UI;

public enum QuestDifficulty
{
    Easy,
    Normal,
    Hard
}

[Observable]
public class Quest
{
    public string Title { get; set; }
    public string Image { get; set; }
    public QuestDifficulty Difficulty { get; set; }
    public string Description { get; set; }
    public bool Completed { get; set; }

    public Quest()
    {
        this.Title = "";
        this.Image = "";
        this.Difficulty = QuestDifficulty.Easy;
        this.Description = "";
        this.Completed = false;
    }

    public Quest(string title, string image, QuestDifficulty difficulty, string description, bool completed)
    {
        this.Title = title;
        this.Image = image;
        this.Difficulty = difficulty;
        this.Description = description;
        this.Completed = completed;
    }
}

[Observable]
public class ViewModel
{
    public ObservableList<Quest> Quests { get; set; }
    public Quest SelectedQuest { get; set; }
    public int SelectedIndex { get; set; }
    public bool IsClosed { get; set; }

    public ViewModel()
    {
        this.Quests = new ObservableList<Quest>(12);

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

        this.SelectedIndex = 0;
        this.SelectedQuest = this.Quests.Get(0);
        this.IsClosed = false;
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

    public void SelectQuest(Quest quest)
    {
        this.ViewModel.SelectQuest(quest);
    }

    public void SelectQuestByIndex(int index)
    {
        this.ViewModel.SelectQuestByIndex(index);
    }

    public void NextQuest()
    {
        this.ViewModel.NextQuest();
    }

    public void PreviousQuest()
    {
        this.ViewModel.PreviousQuest();
    }

    public void ToggleCompleted()
    {
        this.ViewModel.ToggleSelectedQuestCompleted();
    }

    public void ToggleSelectedQuestCompleted()
    {
        this.ViewModel.ToggleSelectedQuestCompleted();
    }

    public void SetQuestCompleted(int index, bool completed)
    {
        this.ViewModel.SetQuestCompleted(index, completed);
    }

    public void CloseQuestLog()
    {
        this.ViewModel.Close();
    }

    public void OpenQuestLog()
    {
        this.ViewModel.Open();
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
