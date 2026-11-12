////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ViewModel.h"

#include <NsCore/ReflectionImplement.h>
#include <NsCore/ReflectionImplementEnum.h>
#include <NsGui/BitmapImage.h>
#include <NsGui/Uri.h>
#include <NsDrawing/Int32Rect.h>
#include <NsApp/DelegateCommand.h>


using namespace Inventory;
using namespace Noesis;
using namespace NoesisApp;


////////////////////////////////////////////////////////////////////////////////////////////////////
Item::Item(ItemCategory category_): category(category_), life(0), mana(0), dps(0), armor(0)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
Item::Item(const char* name_, const char* desc_, ItemCategory category_, int life_, int mana_,
    int dps_, int armor_, Noesis::ImageSource* icon_): name(name_), description(desc_),
    category(category_), life(life_), mana(mana_), dps(dps_), armor(armor_), icon(icon_)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////
Player::Player(const char* name_, int life_, int mana_, int dps_, int armor_): name(name_),
    life(life_), mana(mana_), dps(dps_), armor(armor_)
{
    slots.Add(MakePtr<Item>(ItemCategory_Head));
    slots.Add(MakePtr<Item>(ItemCategory_Chest));
    slots.Add(MakePtr<Item>(ItemCategory_Hand));
    slots.Add(MakePtr<Item>(ItemCategory_Legs));
    slots.Add(MakePtr<Item>(ItemCategory_Ring));
    slots.Add(MakePtr<Item>(ItemCategory_Arms));
    slots.Add(MakePtr<Item>(ItemCategory_Hand));
    slots.Add(MakePtr<Item>(ItemCategory_Feet));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const ObservableCollection<Item>* Player::GetSlots() const
{
    return &slots;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
ViewModel::ViewModel():
  #if defined(NS_PLATFORM_GAME_CORE) || defined(NS_PLATFORM_NX)
    platform(Platform_XBOX),
  #elif defined(NS_PLATFORM_PS4) || defined(NS_PLATFORM_PS5)
    platform(Platform_PS4),
  #else
    platform(Platform_PC),
  #endif
    player("Morgan Hearson", 1423, 361, 2174, 218)
{
    struct ItemData
    {
        const char* name;
        const char* description;
        ItemCategory category;
        int life;
        int mana;
        int dps;
        int armor;
    };

    const ItemData handcraftedItems[] =
    {
        // --- WEAPONS & SHIELDS (Hand) ---
        { "Sword", "A well-balanced steel blade.", ItemCategory_Hand, 0, 0, 15, 0 },
        { "Club", "A crude wooden bludgeon.", ItemCategory_Hand, 0, 0, 8, 0 },
        { "Mace", "A heavy metal mace.", ItemCategory_Hand, 0, 0, 12, 0 },
        { "MorningStar", "A spiked ball on a wooden shaft.", ItemCategory_Hand, 0, 0, 16, 0 },
        { "Hammer", "A standard forging tool repurposed for war.", ItemCategory_Hand, 0, 0, 10, 0 },
        { "WarHammer", "A massive hammer requiring two hands.", ItemCategory_Hand, 0, 0, 25, -5 },
        { "Mallet", "A wooden mallet, not very threatening.", ItemCategory_Hand, 0, 0, 5, 0 },
        { "Axe", "A sharp chopping tool.", ItemCategory_Hand, 0, 0, 14, 0 },
        { "Halberd", "A polearm combining a spear and an axe.", ItemCategory_Hand, 0, 0, 20, 0 },
        { "BattleAxe", "A devastating double-bladed axe.", ItemCategory_Hand, 0, 0, 24, 0 },
        { "ShortSword", "A lightweight, quick blade.", ItemCategory_Hand, 0, 0, 11, 0 },
        { "WoodenShield", "A basic shield of reinforced oak.", ItemCategory_Hand, 0, 0, 0, 10 },
        { "MetalShield", "A sturdy shield of polished steel.", ItemCategory_Hand, 10, 0, 0, 25 },

        // --- RINGS (Ring) ---
        { "SilverRing", "A simple ring of tarnished silver.", ItemCategory_Ring, 0, 20, 0, 0 },
        { "GoldRing", "An elegant gold band.", ItemCategory_Ring, 25, 0, 0, 0 },
        { "DwarvenRing", "Forged in the deep mines.", ItemCategory_Ring, 30, 0, 0, 10 },
        { "ElvenRing", "Humming with ancient forest magic.", ItemCategory_Ring, 10, 50, 0, 0 },
        { "OuroborosRing", "The snake consumes its own tail.", ItemCategory_Ring, 40, 40, 0, 0 },
        { "SageRing", "Worn by the scholars of old.", ItemCategory_Ring, 0, 100, 0, 0 },
        { "EnchantedRing", "Glows with a faint blue light.", ItemCategory_Ring, 0, 30, 5, 0 },
        { "GuardianRing", "Radiates a protective aura.", ItemCategory_Ring, 40, 0, 0, 15 },

        // --- CLOTHING & LIGHT ARMOR ---
        { "WoolShirt", "Itches slightly, but keeps you warm.", ItemCategory_Chest, 5, 0, 0, 2 },
        { "LeatherGloves", "Supple leather offering good grip.", ItemCategory_Arms, 0, 0, 0, 3 },
        { "Breeches", "Standard linen trousers.", ItemCategory_Legs, 0, 0, 0, 2 },
        { "Sandals", "Better than walking barefoot.", ItemCategory_Feet, 0, 5, 0, 1 },
        { "Hood", "Conceals your identity.", ItemCategory_Head, 0, 10, 0, 2 },
        { "Vest", "A lightweight sleeveless garment.", ItemCategory_Chest, 0, 0, 0, 3 },
        { "Frock", "A loose outer garment.", ItemCategory_Chest, 0, 15, 0, 2 },
        { "Breacan", "A traditional tartan cloth.", ItemCategory_Legs, 10, 0, 0, 3 },
        { "Boots", "Sturdy leather walking boots.", ItemCategory_Feet, 0, 0, 0, 4 },
        { "Cowl", "A heavy draped hood.", ItemCategory_Head, 0, 20, 0, 3 },
        { "Jerkin", "A close-fitting leather jacket.", ItemCategory_Chest, 10, 0, 0, 6 },
        { "Pantaloons", "Baggy and comfortable.", ItemCategory_Legs, 0, 10, 0, 2 },

        // --- MEDIUM ARMOR ---
        { "LeatherVambrace", "Thickened leather for forearm protection.", ItemCategory_Arms, 0, 0, 0, 5 },
        { "PaddedGreaves", "Quilted leg guards.", ItemCategory_Legs, 10, 0, 0, 6 },
        { "MailHood", "Interlocking steel rings for the head.", ItemCategory_Head, 0, 0, 0, 12 },
        { "MailChemise", "A full shirt of chainmail.", ItemCategory_Chest, 0, 0, 0, 15 },
        { "MailVambrace", "Chainmail arm protection.", ItemCategory_Arms, 0, 0, 0, 10 },
        { "MailSkirt", "A skirt made of heavy chainmail.", ItemCategory_Legs, 0, 0, 0, 12 },
        { "MailGreaves", "Chainmail for the lower legs.", ItemCategory_Feet, 0, 0, 0, 10 },

        // --- HEAVY ARMOR ---
        { "KnightHelmet", "A closed helm with a visor.", ItemCategory_Head, 20, 0, 0, 20 },
        { "PlateArmor", "A mastercrafted steel breastplate.", ItemCategory_Chest, 50, 0, 0, 35 },
        { "PlateVambrace", "Solid steel arm guards.", ItemCategory_Arms, 15, 0, 0, 15 },
        { "PlateCuisses", "Articulated thigh armor.", ItemCategory_Legs, 25, 0, 0, 20 },
        { "PlateGreaves", "Heavy steel boots and shin guards.", ItemCategory_Feet, 20, 0, 0, 18 }
    };

    for (const ItemData& item : handcraftedItems)
    {
        String icon;
        icon.Format("Images/Items/%s.png", item.name);

        inventory.Add(MakePtr<Item>
        (
            item.name,
            item.description,
            item.category,
            item.life,
            item.mana,
            item.dps,
            item.armor,
            MakePtr<BitmapImage>(icon.Str())
        ));
    }

    for (uint32_t i = 0; i <= ItemCategory_All; ++i)
    {
        defaults.Add(MakePtr<Item>((ItemCategory)i));
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const Player* ViewModel::GetPlayer() const
{
    return &player;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const ObservableCollection<Item>* ViewModel::GetInventory() const
{
    return &inventory;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
const Collection<Item>* ViewModel::GetDefaults() const
{
    return &defaults;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
NS_BEGIN_COLD_REGION

NS_IMPLEMENT_REFLECTION(Item)
{
    NsProp("Name", &Item::name);
    NsProp("Description", &Item::description);
    NsProp("Category", &Item::category);
    NsProp("Life", &Item::life);
    NsProp("Mana", &Item::mana);
    NsProp("Dps", &Item::dps);
    NsProp("Armor", &Item::armor);
    NsProp("Icon", &Item::icon);
}

NS_IMPLEMENT_REFLECTION(Player)
{
    NsProp("Name", &Player::name);
    NsProp("Life", &Player::life);
    NsProp("Mana", &Player::mana);
    NsProp("Dps", &Player::dps);
    NsProp("Armor", &Player::armor);
    NsProp("Slots", &Player::GetSlots);
}

NS_IMPLEMENT_REFLECTION(ViewModel)
{
    NsProp("Platform", &ViewModel::platform);
    NsProp("Player", &ViewModel::GetPlayer);
    NsProp("Inventory", &ViewModel::GetInventory);
    NsProp("Defaults", &ViewModel::GetDefaults);
}

NS_IMPLEMENT_REFLECTION_ENUM(Inventory::Platform, "Inventory.Platform")
{
    NsVal("PC", Platform_PC);
    NsVal("XBOX", Platform_XBOX);
    NsVal("PS4", Platform_PS4);
}

NS_IMPLEMENT_REFLECTION_ENUM(ItemCategory, "Inventory.ItemCategory")
{
    NsVal("Head", ItemCategory_Head);
    NsVal("Chest", ItemCategory_Chest);
    NsVal("Arms", ItemCategory_Arms);
    NsVal("Legs", ItemCategory_Legs);
    NsVal("Feet", ItemCategory_Feet);
    NsVal("Hand", ItemCategory_Hand);
    NsVal("Ring", ItemCategory_Ring);
    NsVal("All", ItemCategory_All);
}

NS_END_COLD_REGION
