module Inventory;
import Aero.UI;

public enum Platform
{
    PC = 0,
    XBOX = 1,
    PS4 = 2
}

public enum ItemCategory
{
    Head = 0,
    Chest = 1,
    Arms = 2,
    Legs = 3,
    Feet = 4,
    Hand = 5,
    Ring = 6,
    All = 7
}

[Observable]
public class Item
{
    public string Name { get; set; } = "";
    public string Description { get; set; } = "";
    public ItemCategory Category { get; set; } = ItemCategory.Head;
    public int Life { get; set; } = 0;
    public int Mana { get; set; } = 0;
    public int Dps { get; set; } = 0;
    public int Armor { get; set; } = 0;
    public string Icon { get; set; } = "";

    public Item()
    {
    }

    public Item(ItemCategory category)
    {
        this.Name = "";
        this.Description = "";
        this.Category = category;
        this.Life = 0;
        this.Mana = 0;
        this.Dps = 0;
        this.Armor = 0;
        this.Icon = "";
    }

    public Item(
        string name,
        string description,
        ItemCategory category,
        int life,
        int mana,
        int dps,
        int armor,
        string icon)
    {
        this.Name = name;
        this.Description = description;
        this.Category = category;
        this.Life = life;
        this.Mana = mana;
        this.Dps = dps;
        this.Armor = armor;
        this.Icon = icon;
    }

    public bool IsEmpty()
    {
        return this.Name == "";
    }
}

[Observable]
public class Player
{
    public string Name { get; set; } = "";
    public int Life { get; set; } = 0;
    public int Mana { get; set; } = 0;
    public int Dps { get; set; } = 0;
    public int Armor { get; set; } = 0;
    public int TotalLife { get; set; } = 0;
    public int TotalMana { get; set; } = 0;
    public int TotalDps { get; set; } = 0;
    public int TotalArmor { get; set; } = 0;
    public ObservableList<Item> Slots { get; set; } = null;

    public Player()
    {
        this.Slots = new ObservableList<Item>(8);
    }

    public Player(string name, int life, int mana, int dps, int armor)
    {
        this.Name = name;
        this.Life = life;
        this.Mana = mana;
        this.Dps = dps;
        this.Armor = armor;
        this.TotalLife = life;
        this.TotalMana = mana;
        this.TotalDps = dps;
        this.TotalArmor = armor;
        this.Slots = new ObservableList<Item>(8);
    }
}

[Observable]
public class ViewModel
{
    public Platform Platform { get; set; } = Platform.PC;
    public Player Player { get; set; } = null;
    public ObservableList<Item> Inventory { get; set; } = null;
    public ObservableList<Item> Defaults { get; set; } = null;

    public ViewModel()
    {
        this.Platform = Platform.PC;
        this.Player = new Player("Morgan Hearson", 1423, 361, 2174, 218);
        this.Defaults = new ObservableList<Item>(8);
        this.Defaults.Add(new Item(ItemCategory.Head));
        this.Defaults.Add(new Item(ItemCategory.Chest));
        this.Defaults.Add(new Item(ItemCategory.Arms));
        this.Defaults.Add(new Item(ItemCategory.Legs));
        this.Defaults.Add(new Item(ItemCategory.Feet));
        this.Defaults.Add(new Item(ItemCategory.Hand));
        this.Defaults.Add(new Item(ItemCategory.Ring));
        this.Defaults.Add(new Item(ItemCategory.All));

        // Initial 8 equipped items in Player.Slots[0..7] matching slot categories:
        // [0]=Head, [1]=Chest, [2]=Hand (Left), [3]=Legs, [4]=Ring, [5]=Arms, [6]=Hand (Right), [7]=Feet
        this.Player.Slots.Add(this.MakeItem("KnightHelmet", "A closed helm with a visor.", ItemCategory.Head, 20, 0, 0, 20));
        this.Player.Slots.Add(this.MakeItem("PlateArmor", "A mastercrafted steel breastplate.", ItemCategory.Chest, 50, 0, 0, 35));
        this.Player.Slots.Add(this.MakeItem("Sword", "A well-balanced steel blade.", ItemCategory.Hand, 0, 0, 15, 0));
        this.Player.Slots.Add(this.MakeItem("PlateCuisses", "Articulated thigh armor.", ItemCategory.Legs, 25, 0, 0, 20));
        this.Player.Slots.Add(this.MakeItem("GoldRing", "An elegant gold band.", ItemCategory.Ring, 25, 0, 0, 0));
        this.Player.Slots.Add(this.MakeItem("PlateVambrace", "Solid steel arm guards.", ItemCategory.Arms, 15, 0, 0, 15));
        this.Player.Slots.Add(this.MakeItem("MetalShield", "A sturdy shield of polished steel.", ItemCategory.Hand, 10, 0, 0, 25));
        this.Player.Slots.Add(this.MakeItem("PlateGreaves", "Heavy steel boots and shin guards.", ItemCategory.Feet, 20, 0, 0, 18));

        // Remaining 37 handcrafted items in Inventory bag (8 + 37 = 45 total items from Src/ViewModel.cpp)
        this.Inventory = new ObservableList<Item>(45);

        // --- WEAPONS & SHIELDS (Hand) ---
        this.Inventory.Add(this.MakeItem("Club", "A crude wooden bludgeon.", ItemCategory.Hand, 0, 0, 8, 0));
        this.Inventory.Add(this.MakeItem("Mace", "A heavy metal mace.", ItemCategory.Hand, 0, 0, 12, 0));
        this.Inventory.Add(this.MakeItem("MorningStar", "A spiked ball on a wooden shaft.", ItemCategory.Hand, 0, 0, 16, 0));
        this.Inventory.Add(this.MakeItem("Hammer", "A standard forging tool repurposed for war.", ItemCategory.Hand, 0, 0, 10, 0));
        this.Inventory.Add(this.MakeItem("WarHammer", "A massive hammer requiring two hands.", ItemCategory.Hand, 0, 0, 25, -5));
        this.Inventory.Add(this.MakeItem("Mallet", "A wooden mallet, not very threatening.", ItemCategory.Hand, 0, 0, 5, 0));
        this.Inventory.Add(this.MakeItem("Axe", "A sharp chopping tool.", ItemCategory.Hand, 0, 0, 14, 0));
        this.Inventory.Add(this.MakeItem("Halberd", "A polearm combining a spear and an axe.", ItemCategory.Hand, 0, 0, 20, 0));
        this.Inventory.Add(this.MakeItem("BattleAxe", "A devastating double-bladed axe.", ItemCategory.Hand, 0, 0, 24, 0));
        this.Inventory.Add(this.MakeItem("ShortSword", "A lightweight, quick blade.", ItemCategory.Hand, 0, 0, 11, 0));
        this.Inventory.Add(this.MakeItem("WoodenShield", "A basic shield of reinforced oak.", ItemCategory.Hand, 0, 0, 0, 10));

        // --- RINGS (Ring) ---
        this.Inventory.Add(this.MakeItem("SilverRing", "A simple ring of tarnished silver.", ItemCategory.Ring, 0, 20, 0, 0));
        this.Inventory.Add(this.MakeItem("DwarvenRing", "Forged in the deep mines.", ItemCategory.Ring, 30, 0, 0, 10));
        this.Inventory.Add(this.MakeItem("ElvenRing", "Humming with ancient forest magic.", ItemCategory.Ring, 10, 50, 0, 0));
        this.Inventory.Add(this.MakeItem("OuroborosRing", "The snake consumes its own tail.", ItemCategory.Ring, 40, 40, 0, 0));
        this.Inventory.Add(this.MakeItem("SageRing", "Worn by the scholars of old.", ItemCategory.Ring, 0, 100, 0, 0));
        this.Inventory.Add(this.MakeItem("EnchantedRing", "Glows with a faint blue light.", ItemCategory.Ring, 0, 30, 5, 0));
        this.Inventory.Add(this.MakeItem("GuardianRing", "Radiates a protective aura.", ItemCategory.Ring, 40, 0, 0, 15));

        // --- CLOTHING & LIGHT ARMOR ---
        this.Inventory.Add(this.MakeItem("WoolShirt", "Itches slightly, but keeps you warm.", ItemCategory.Chest, 5, 0, 0, 2));
        this.Inventory.Add(this.MakeItem("LeatherGloves", "Supple leather offering good grip.", ItemCategory.Arms, 0, 0, 0, 3));
        this.Inventory.Add(this.MakeItem("Breeches", "Standard linen trousers.", ItemCategory.Legs, 0, 0, 0, 2));
        this.Inventory.Add(this.MakeItem("Sandals", "Better than walking barefoot.", ItemCategory.Feet, 0, 5, 0, 1));
        this.Inventory.Add(this.MakeItem("Hood", "Conceals your identity.", ItemCategory.Head, 0, 10, 0, 2));
        this.Inventory.Add(this.MakeItem("Vest", "A lightweight sleeveless garment.", ItemCategory.Chest, 0, 0, 0, 3));
        this.Inventory.Add(this.MakeItem("Frock", "A loose outer garment.", ItemCategory.Chest, 0, 15, 0, 2));
        this.Inventory.Add(this.MakeItem("Breacan", "A traditional tartan cloth.", ItemCategory.Legs, 10, 0, 0, 3));
        this.Inventory.Add(this.MakeItem("Boots", "Sturdy leather walking boots.", ItemCategory.Feet, 0, 0, 0, 4));
        this.Inventory.Add(this.MakeItem("Cowl", "A heavy draped hood.", ItemCategory.Head, 0, 20, 0, 3));
        this.Inventory.Add(this.MakeItem("Jerkin", "A close-fitting leather jacket.", ItemCategory.Chest, 10, 0, 0, 6));
        this.Inventory.Add(this.MakeItem("Pantaloons", "Baggy and comfortable.", ItemCategory.Legs, 0, 10, 0, 2));

        // --- MEDIUM ARMOR ---
        this.Inventory.Add(this.MakeItem("LeatherVambrace", "Thickened leather for forearm protection.", ItemCategory.Arms, 0, 0, 0, 5));
        this.Inventory.Add(this.MakeItem("PaddedGreaves", "Quilted leg guards.", ItemCategory.Legs, 10, 0, 0, 6));
        this.Inventory.Add(this.MakeItem("MailHood", "Interlocking steel rings for the head.", ItemCategory.Head, 0, 0, 0, 12));
        this.Inventory.Add(this.MakeItem("MailChemise", "A full shirt of chainmail.", ItemCategory.Chest, 0, 0, 0, 15));
        this.Inventory.Add(this.MakeItem("MailVambrace", "Chainmail arm protection.", ItemCategory.Arms, 0, 0, 0, 10));
        this.Inventory.Add(this.MakeItem("MailSkirt", "A skirt made of heavy chainmail.", ItemCategory.Legs, 0, 0, 0, 12));
        this.Inventory.Add(this.MakeItem("MailGreaves", "Chainmail for the lower legs.", ItemCategory.Feet, 0, 0, 0, 10));

        this.RecalculateStats();
    }

    private Item MakeItem(
        string name,
        string description,
        ItemCategory category,
        int life,
        int mana,
        int dps,
        int armor)
    {
        return new Item(
            name,
            description,
            category,
            life,
            mana,
            dps,
            armor,
            "Images/Items/" + name + ".png");
    }

    public ItemCategory GetSlotCategory(int slotIndex)
    {
        if (slotIndex == 0) { return ItemCategory.Head; }
        if (slotIndex == 1) { return ItemCategory.Chest; }
        if (slotIndex == 2) { return ItemCategory.Hand; }
        if (slotIndex == 3) { return ItemCategory.Legs; }
        if (slotIndex == 4) { return ItemCategory.Ring; }
        if (slotIndex == 5) { return ItemCategory.Arms; }
        if (slotIndex == 6) { return ItemCategory.Hand; }
        if (slotIndex == 7) { return ItemCategory.Feet; }
        return ItemCategory.All;
    }

    public Item GetDefaultForSlot(int slotIndex)
    {
        if (slotIndex == 0) { return this.Defaults.Get(0); }
        if (slotIndex == 1) { return this.Defaults.Get(1); }
        if (slotIndex == 2) { return this.Defaults.Get(5); }
        if (slotIndex == 3) { return this.Defaults.Get(3); }
        if (slotIndex == 4) { return this.Defaults.Get(6); }
        if (slotIndex == 5) { return this.Defaults.Get(2); }
        if (slotIndex == 6) { return this.Defaults.Get(5); }
        if (slotIndex == 7) { return this.Defaults.Get(4); }
        return this.Defaults.Get(7);
    }

    public void RecalculateStats()
    {
        int totalLife = this.Player.Life;
        int totalMana = this.Player.Mana;
        int totalDps = this.Player.Dps;
        int totalArmor = this.Player.Armor;

        int n = this.Player.Slots.Count();
        int i = 0;
        while (i < n)
        {
            Item slotItem = this.Player.Slots.Get(i);
            if (slotItem != null && !slotItem.IsEmpty())
            {
                totalLife = totalLife + slotItem.Life;
                totalMana = totalMana + slotItem.Mana;
                totalDps = totalDps + slotItem.Dps;
                totalArmor = totalArmor + slotItem.Armor;
            }
            i = i + 1;
        }

        this.Player.TotalLife = totalLife;
        this.Player.TotalMana = totalMana;
        this.Player.TotalDps = totalDps;
        this.Player.TotalArmor = totalArmor;
    }

    public bool EquipItem(int slotIndex, Item item)
    {
        if (slotIndex < 0 || slotIndex >= 8 || item == null || item.IsEmpty())
        {
            return false;
        }
        ItemCategory required = this.GetSlotCategory(slotIndex);
        if (item.Category != required)
        {
            return false;
        }

        Item currentInTarget = this.Player.Slots.Get(slotIndex);
        if (currentInTarget == item)
        {
            return true;
        }

        // Check if item is already in another equipment slot
        int s = 0;
        while (s < 8)
        {
            if (s != slotIndex && this.Player.Slots.Get(s) == item)
            {
                return this.SwapSlots(s, slotIndex);
            }
            s = s + 1;
        }

        // Remove from bag if present
        int bagIdx = this.Inventory.IndexOf(item);
        if (bagIdx >= 0)
        {
            this.Inventory.RemoveAt(bagIdx);
        }

        if (currentInTarget != null && !currentInTarget.IsEmpty())
        {
            if (bagIdx >= 0 && bagIdx <= this.Inventory.Count())
            {
                this.Inventory.Insert(bagIdx, currentInTarget);
            }
            else
            {
                this.Inventory.Add(currentInTarget);
            }
        }

        this.Player.Slots.Set(slotIndex, item);
        this.RecalculateStats();
        return true;
    }

    public bool UnequipSlot(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= 8)
        {
            return false;
        }
        Item current = this.Player.Slots.Get(slotIndex);
        if (current == null || current.IsEmpty())
        {
            return false;
        }

        this.Player.Slots.Set(slotIndex, this.GetDefaultForSlot(slotIndex));
        this.Inventory.Add(current);
        this.RecalculateStats();
        return true;
    }

    public bool SwapSlots(int fromSlot, int toSlot)
    {
        if (fromSlot < 0 || fromSlot >= 8 || toSlot < 0 || toSlot >= 8)
        {
            return false;
        }
        if (fromSlot == toSlot)
        {
            return true;
        }
        if (this.GetSlotCategory(fromSlot) != this.GetSlotCategory(toSlot))
        {
            return false;
        }

        Item first = this.Player.Slots.Get(fromSlot);
        Item second = this.Player.Slots.Get(toSlot);
        this.Player.Slots.Set(fromSlot, second);
        this.Player.Slots.Set(toSlot, first);
        this.RecalculateStats();
        return true;
    }

    public bool OnDropItem(int fromSlot, int toSlot, int bagIndex)
    {
        if (fromSlot < 0 && toSlot >= 0)
        {
            if (bagIndex < 0 || bagIndex >= this.Inventory.Count())
            {
                return false;
            }
            Item bagItem = this.Inventory.Get(bagIndex);
            return this.EquipItem(toSlot, bagItem);
        }
        if (fromSlot >= 0 && toSlot < 0)
        {
            if (fromSlot >= 8)
            {
                return false;
            }
            Item equipped = this.Player.Slots.Get(fromSlot);
            if (equipped == null || equipped.IsEmpty())
            {
                return false;
            }
            this.Player.Slots.Set(fromSlot, this.GetDefaultForSlot(fromSlot));
            if (bagIndex >= 0 && bagIndex <= this.Inventory.Count())
            {
                this.Inventory.Insert(bagIndex, equipped);
            }
            else
            {
                this.Inventory.Add(equipped);
            }
            this.RecalculateStats();
            return true;
        }
        if (fromSlot >= 0 && toSlot >= 0)
        {
            return this.SwapSlots(fromSlot, toSlot);
        }
        return false;
    }
}

[Observable]
public partial class MainWindow
{
    public ViewModel ViewModel { get; set; } = null;

    public MainWindow()
    {
        Application_SetResources("Resources.xaml");
        this.InitializeComponent();

        this.ViewModel = new ViewModel();
        this.SetDataContext<ViewModel>(this.ViewModel);
    }

    public void OnInitialized()
    {
    }

    public bool EquipItem(int slotIndex, Item item)
    {
        return this.ViewModel.EquipItem(slotIndex, item);
    }

    public bool EquipFromInventory(int inventoryIndex, int slotIndex)
    {
        if (inventoryIndex < 0 || inventoryIndex >= this.ViewModel.Inventory.Count())
        {
            return false;
        }
        Item item = this.ViewModel.Inventory.Get(inventoryIndex);
        return this.ViewModel.EquipItem(slotIndex, item);
    }

    public bool UnequipSlot(int slotIndex)
    {
        return this.ViewModel.UnequipSlot(slotIndex);
    }

    public bool SwapSlots(int fromSlot, int toSlot)
    {
        return this.ViewModel.SwapSlots(fromSlot, toSlot);
    }

    public bool OnDropItem(int fromSlot, int toSlot, int bagIndex)
    {
        return this.ViewModel.OnDropItem(fromSlot, toSlot, bagIndex);
    }

    public void SetPlatform(Platform platform)
    {
        this.ViewModel.Platform = platform;
    }
}

public MainWindow CreateMainWindow()
{
    return new MainWindow();
}
