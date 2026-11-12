////////////////////////////////////////////////////////////////////////////////////////////////////
// NoesisGUI - http://www.noesisengine.com
// Copyright (c) 2013 Noesis Technologies S.L. All Rights Reserved.
////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __INVENTORY_VIEWMODEL_H__
#define __INVENTORY_VIEWMODEL_H__


#include <NsCore/Noesis.h>
#include <NsCore/BaseComponent.h>
#include <NsCore/String.h>
#include <NsCore/ReflectionDeclare.h>
#include <NsCore/ReflectionDeclareEnum.h>
#include <NsGui/Collection.h>
#include <NsGui/ObservableCollection.h>
#include <NsGui/INotifyPropertyChanged.h>
#include <NsDrawing/Rect.h>


namespace Noesis
{
class ImageSource;
}

namespace Inventory
{

////////////////////////////////////////////////////////////////////////////////////////////////////
enum Platform
{
    Platform_PC,
    Platform_XBOX,
    Platform_PS4
};

////////////////////////////////////////////////////////////////////////////////////////////////////
enum ItemCategory
{
    ItemCategory_Head,
    ItemCategory_Chest,
    ItemCategory_Arms,
    ItemCategory_Legs,
    ItemCategory_Feet,
    ItemCategory_Hand,
    ItemCategory_Ring,
    ItemCategory_All
};

////////////////////////////////////////////////////////////////////////////////////////////////////
struct Item final: public Noesis::BaseComponent
{
    Item(ItemCategory category_);
    Item(const char* name_, const char* desc_, ItemCategory category_,
        int life_, int mana_, int dps_, int armor_, Noesis::ImageSource* icon_);

    Noesis::String name;
    Noesis::String description;
    ItemCategory category;
    int life;
    int mana;
    int dps;
    int armor;
    Noesis::Ptr<Noesis::ImageSource> icon;

    NS_DECLARE_REFLECTION(Item, BaseComponent)
};

////////////////////////////////////////////////////////////////////////////////////////////////////
struct Player final: public Noesis::BaseComponent
{
    Player(const char* name_, int life_, int mana_, int dps_, int armor_);

    Noesis::String name;
    int life;
    int mana;
    int dps;
    int armor;

    Noesis::ObservableCollection<Item> slots;
    const Noesis::ObservableCollection<Item>* GetSlots() const;

    NS_DECLARE_REFLECTION(Player, Noesis::BaseComponent)
};

////////////////////////////////////////////////////////////////////////////////////////////////////
struct ViewModel final: public Noesis::BaseComponent
{
    ViewModel();

    Platform platform;

    Player player;
    const Player* GetPlayer() const;

    Noesis::ObservableCollection<Item> inventory;
    const Noesis::ObservableCollection<Item>* GetInventory() const;

    Noesis::Collection<Item> defaults;
    const Noesis::Collection<Item>* GetDefaults() const;

    NS_DECLARE_REFLECTION(ViewModel, Noesis::BaseComponent)
};

}

NS_DECLARE_REFLECTION_ENUM(Inventory::Platform)
NS_DECLARE_REFLECTION_ENUM(Inventory::ItemCategory)


#endif
