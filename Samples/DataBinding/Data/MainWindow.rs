module DataBinding;
import Aero.UI;

[Observable]
public class SolarSystemObject
{
    public string Name { get; set; }
    public float Orbit { get; set; }
    public float Diameter { get; set; }
    public string Image { get; set; }
    public string Details { get; set; }

    public SolarSystemObject()
    {
        this.Name = "";
        this.Orbit = 0.0f;
        this.Diameter = 0.0f;
        this.Image = "";
        this.Details = "";
    }

    public SolarSystemObject(string name, float orbit, float diameter, string image, string details)
    {
        this.Name = name;
        this.Orbit = orbit;
        this.Diameter = diameter;
        this.Image = image;
        this.Details = details;
    }
}

[Observable]
public class ViewModel
{
    public ObservableList<SolarSystemObject> SolarSystemObjects { get; set; }
    public SolarSystemObject SelectedObject { get; set; }
    public int SelectedIndex { get; set; }

    public ViewModel()
    {
        this.SolarSystemObjects = new ObservableList<SolarSystemObject>(10);

        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Sun",
            0.0f,
            1380000.0f,
            "Images/Sun.png",
            "The yellow dwarf star in the center of our solar system."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Mercury",
            0.38f,
            4880.0f,
            "Images/Mercury.png",
            "The small and rocky planet Mercury is the closest planet to the Sun."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Venus",
            0.72f,
            12103.6f,
            "Images/Venus.png",
            "At first glance, if Earth had a twin, it would be Venus."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Earth",
            1.0f,
            12756.3f,
            "Images/Earth.png",
            "Earth, our home planet, is the only planet in our solar system known to harbor life."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Mars",
            1.52f,
            6794.0f,
            "Images/Mars.png",
            "The red planet Mars has inspired wild flights of imagination over the centuries."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Jupiter",
            5.20f,
            142984.0f,
            "Images/Jupiter.png",
            "With its numerous moons and several rings, the Jupiter system is a 'mini-solar system'."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Saturn",
            9.54f,
            120536.0f,
            "Images/Saturn.png",
            "Saturn is the most distant of the five planets known to ancient stargazers."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Uranus",
            19.218f,
            51118.0f,
            "Images/Uranus.png",
            "Uranus gets its blue-green color from methane gas above the deeper cloud layers."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Neptune",
            30.06f,
            49532.0f,
            "Images/Neptune.png",
            "Neptune was the first planet located through mathematical predictions."));
        this.SolarSystemObjects.Add(new SolarSystemObject(
            "Pluto",
            39.5f,
            2274.0f,
            "Images/Pluto.png",
            "Long considered to be the smallest, coldest, and most distant planet from the Sun."));

        this.SelectedIndex = 0;
        this.SelectedObject = this.SolarSystemObjects.Get(0);
    }

    public int ObjectCount()
    {
        return this.SolarSystemObjects.Count();
    }

    public SolarSystemObject GetObject(int index)
    {
        if (index < 0 || index >= this.SolarSystemObjects.Count())
        {
            return null;
        }
        return this.SolarSystemObjects.Get(index);
    }

    public SolarSystemObject FindByName(string name)
    {
        int count = this.SolarSystemObjects.Count();
        int i = 0;
        while (i < count)
        {
            SolarSystemObject item = this.SolarSystemObjects.Get(i);
            if (item != null && item.Name == name)
            {
                return item;
            }
            i = i + 1;
        }
        return null;
    }

    public void SelectByIndex(int index)
    {
        int count = this.SolarSystemObjects.Count();
        if (index < 0 || index >= count)
        {
            return;
        }
        this.SelectedIndex = index;
        this.SelectedObject = this.SolarSystemObjects.Get(index);
    }

    public void SelectByName(string name)
    {
        int count = this.SolarSystemObjects.Count();
        int i = 0;
        while (i < count)
        {
            SolarSystemObject item = this.SolarSystemObjects.Get(i);
            if (item != null && item.Name == name)
            {
                this.SelectedIndex = i;
                this.SelectedObject = item;
                return;
            }
            i = i + 1;
        }
    }

    public void SelectObject(SolarSystemObject target)
    {
        if (target == null)
        {
            this.ClearSelection();
            return;
        }
        int idx = this.SolarSystemObjects.IndexOf(target);
        if (idx >= 0)
        {
            this.SelectedIndex = idx;
        }
        this.SelectedObject = target;
    }

    public void SelectNext()
    {
        int count = this.SolarSystemObjects.Count();
        if (count <= 0)
        {
            return;
        }
        int nextIdx = this.SelectedIndex + 1;
        if (nextIdx >= count || nextIdx < 0)
        {
            nextIdx = 0;
        }
        this.SelectByIndex(nextIdx);
    }

    public void SelectPrevious()
    {
        int count = this.SolarSystemObjects.Count();
        if (count <= 0)
        {
            return;
        }
        int prevIdx = this.SelectedIndex - 1;
        if (prevIdx < 0)
        {
            prevIdx = count - 1;
        }
        this.SelectByIndex(prevIdx);
    }

    public void ClearSelection()
    {
        this.SelectedIndex = -1;
        this.SelectedObject = null;
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

    public void SelectByIndex(int index)
    {
        this.ViewModel.SelectByIndex(index);
    }

    public void SelectByName(string name)
    {
        this.ViewModel.SelectByName(name);
    }

    public void SelectObject(SolarSystemObject target)
    {
        this.ViewModel.SelectObject(target);
    }

    public void SelectNext()
    {
        this.ViewModel.SelectNext();
    }

    public void SelectPrevious()
    {
        this.ViewModel.SelectPrevious();
    }

    public void ClearSelection()
    {
        this.ViewModel.ClearSelection();
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
