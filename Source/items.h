/**
 * @file items.h
 *
 * Interface of item functionality.
 */
#pragma once

#include <cstdint>

#include "DiabloUI/ui_flags.hpp"
#include "engine.h"
#include "engine/animationinfo.h"
#include "engine/point.hpp"
#include "itemdat.h"
#include "monster.h"
#include "utils/stdcompat/optional.hpp"
#include "utils/string_or_view.hpp"

namespace devilution {

#define MAXITEMS 127 // Max number of items per zone.  No new items will be created in the zone once this cap is reached (but you can create more items in another zone).  I tried increasing MAXITEMS to 250 and placing stacks of gold in town.  After 127 stacks, the game still deleted the next stack I dropped so the original value of 127 seems to be hard-coded somewhere else as well.
#define ITEMTYPES 43

#define GOLD_SMALL_LIMIT 1000
#define GOLD_MEDIUM_LIMIT 2500
#define GOLD_MAX_LIMIT 5000

// Item indestructible durability
#define DUR_INDESTRUCTIBLE 255

constexpr int MaxVendorValue = 500000; // jwk - original code: 140000;
constexpr int MaxVendorValueHf = 200000;
constexpr int MaxBoyValue = 500000; // jwk - original code: 90000;
constexpr int MaxBoyValueHf = 200000;

enum item_quality : uint8_t {
	ITEM_QUALITY_NORMAL,
	ITEM_QUALITY_MAGIC,
	ITEM_QUALITY_UNIQUE,
};

/*
CF_LEVEL: Item Level (6 bits; value ranges from 0-63)
CF_ONLYGOOD: Item is not able to have affixes with PLOK set to false
CF_UPER15: Item is from a Unique Monster and has 15% chance of being a Unique Item
CF_UPER1: Item is from the dungeon and has a 1% chance of being a Unique Item
CF_UNIQUE: Item is a Unique Item
CF_SMITH_BASIC: Item is from Griswold (Basic)
CF_SMITH_PREMIUM: Item is from Griswold (Premium)
CF_BOY: Item is from Wirt
CF_WITCH: Item is from Adria
CF_HEALER: Item is from Pepin
CF_PREGEN: Item is pre-generated, mostly associated with Quest items found in the dungeon or potions on the dungeon floor

Items that have both CF_UPER15 and CF_UPER1 are CF_USEFUL, which is used to generate Potions and Town Portal scrolls on the dungeon floor
Items that have any of CF_SMITH_BASIC, CF_SMITH_PREMIUM, CF_BOY, CF_WICTH, and CF_HEALER are CF_TOWN, indicating the item is sourced from an NPC
*/
enum icreateinfo_flag {
	// clang-format off
	CF_LEVEL         = (1 << 6) - 1,
	CF_ONLYGOOD      = 1 << 6,
	CF_UPER15        = 1 << 7,
	CF_UPER1         = 1 << 8,
	CF_UNIQUE        = 1 << 9,
	CF_SMITH_BASIC   = 1 << 10,
	CF_SMITH_PREMIUM = 1 << 11,
	CF_BOY           = 1 << 12,
	CF_WITCH         = 1 << 13,
	CF_HEALER        = 1 << 14,
	CF_PREGEN        = 1 << 15,

	CF_USEFUL = CF_UPER15 | CF_UPER1,
	CF_TOWN   = CF_SMITH_BASIC | CF_SMITH_PREMIUM | CF_BOY | CF_WITCH | CF_HEALER,
	// clang-format on
};

enum icreateinfo_flag2 {
	// clang-format off
	CF_HELLFIRE = 1,
	// clang-format on
};

// All item animation frames have this width.
constexpr int ItemAnimWidth = 96;

// Defined in player.h, forward declared here to allow for functions which operate in the context of a player.
struct Player;

struct Item {
	/** Randomly generated identifier */
	uint32_t _iSeed = 0;
	uint16_t _iCreateInfo = 0;
	ItemType _itype = ItemType::None;
	bool _iAnimFlag = false;
	Point position = { 0, 0 };
	/*
	 * @brief Contains Information for current Animation
	 */
	AnimationInfo AnimInfo;
	bool _iDelFlag = false; // set when item is flagged for deletion, deprecated in 1.02
	uint8_t _iSelFlag = 0;
	bool _iPostDraw = false;
	bool _iIdentified = false;  // whether or not the item has been identified
	item_quality _iMagical = ITEM_QUALITY_NORMAL;
	char _iName[64] = {}; // name of the base item "Field plate"
	char _iIName[64] = {}; // name of the full item "Godly plate of the whale"
	item_equip_type _iLoc = ILOC_NONE;
	item_class _iClass = ICLASS_NONE;
	uint8_t _iCurs = 0;
	int _ivalue = 0;
	int _iIvalue = 0;
	uint8_t _iMinDam = 0;
	uint8_t _iMaxDam = 0; // can be modified by hellfire oils
	uint8_t _iThornsMin = 0;
	uint8_t _iThornsMax = 0;
	int16_t _iAC = 0;
	ItemSpecialEffect _iFlags = ItemSpecialEffect::None;
	item_misc_id _iMiscId = IMISC_NONE;
	SpellID _iSpell = SpellID::Null;
	BaseItemIdx IDidx = IDI_NONE; // index of this item in the data table AllItemsList[]
	int _iCharges = 0;
	int _iMaxCharges = 0;
	int _iDurability = 0;
	int _iMaxDur = 0;
	int16_t _iPLDam = 0;
	int16_t _iPLToHit = 0; // can be modified by hellfire oils
	int16_t _iPLAC = 0;
	int16_t _iPLStr = 0;
	int16_t _iPLMag = 0;
	int16_t _iPLDex = 0;
	int16_t _iPLVit = 0;
	int16_t _iPLFR = 0;
	int16_t _iPLLR = 0;
	int16_t _iPLMR = 0;
	int16_t _iPLMana = 0;
	int16_t _iPLHP = 0;
	int16_t _iPLDamMod = 0;
	int16_t _iPLGetHit = 0;
	int16_t _iPLLight = 0;
	int8_t _iPLManaCostMod = 0; // JWK_ALLOW_MANA_COST_MODIFIER (negative value means cost reduction, positive value means cost increase)
	int8_t _iSplLvlAdd = 0;
	bool _iRequest = false;
	/** Unique item ID, used as an index into UniqueItemList */
	int _iUid = 0;
	uint16_t _iFMinDam = 0;
	uint16_t _iFMaxDam = 0;
	uint16_t _iLMinDam = 0;
	uint16_t _iLMaxDam = 0;
	uint16_t _iMMinDam = 0;
	uint16_t _iMMaxDam = 0;
	int16_t _iPLArmorPierce = 0;
	enum item_effect_type _iPrefixPower = IPL_INVALID;
	enum item_effect_type _iSuffixPower = IPL_INVALID;
	int _iVAdd1 = 0;
	int _iVMult1 = 0;
	int _iVAdd2 = 0;
	int _iVMult2 = 0;
	uint8_t _iMinStr = 0;
	uint8_t _iMinMag = 0;
	uint8_t _iMinDex = 0;
	bool _iStatFlag = false; // true if player can wear this item (meets stat requirements)
	ItemSpecialEffectHf _iDamAcFlags = ItemSpecialEffectHf::None;
	uint32_t dwBuff = 0;

	/**
	 * @brief Clears this item and returns the old value
	 */
	Item pop() &
	{
		Item temp = std::move(*this);
		clear();
		return temp;
	}

	/**
	 * @brief Resets the item so isEmpty() returns true without needing to reinitialise the whole object
	 */
	DVL_REINITIALIZES void clear()
	{
		this->_itype = ItemType::None;
	}

	/**
	 * @brief Checks whether this item is empty or not.
	 * @return 'True' in case the item is empty and 'False' otherwise.
	 */
	bool isEmpty() const
	{
		return this->_itype == ItemType::None;
	}

	/**
	 * @brief Checks whether this item is an equipment.
	 * @return 'True' in case the item is an equipment and 'False' otherwise.
	 */
	bool isEquipment() const
	{
		if (this->isEmpty()) {
			return false;
		}

		switch (this->_iLoc) {
		case ILOC_AMULET:
		case ILOC_ARMOR:
		case ILOC_HELM:
		case ILOC_ONEHAND:
		case ILOC_RING:
		case ILOC_TWOHAND:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is a weapon.
	 * @return 'True' in case the item is a weapon and 'False' otherwise.
	 */
	bool isWeapon() const
	{
		if (this->isEmpty()) {
			return false;
		}

		switch (this->_itype) {
		case ItemType::Axe:
		case ItemType::Bow:
		case ItemType::Mace:
		case ItemType::Staff:
		case ItemType::Sword:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is an armor.
	 * @return 'True' in case the item is an armor and 'False' otherwise.
	 */
	bool isArmor() const
	{
		if (this->isEmpty()) {
			return false;
		}

		switch (this->_itype) {
		case ItemType::HeavyArmor:
		case ItemType::LightArmor:
		case ItemType::MediumArmor:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is a helm.
	 * @return 'True' in case the item is a helm and 'False' otherwise.
	 */
	bool isHelm() const
	{
		return !this->isEmpty() && this->_itype == ItemType::Helm;
	}

	/**
	 * @brief Checks whether this item is a shield.
	 * @return 'True' in case the item is a shield and 'False' otherwise.
	 */
	bool isShield() const
	{
		return !this->isEmpty() && this->_itype == ItemType::Shield;
	}

	/**
	 * @brief Checks whether this item is a jewelry.
	 * @return 'True' in case the item is a jewelry and 'False' otherwise.
	 */
	bool isJewelry() const
	{
		if (this->isEmpty()) {
			return false;
		}

		switch (this->_itype) {
		case ItemType::Amulet:
		case ItemType::Ring:
			return true;

		default:
			return false;
		}
	}

	[[nodiscard]] bool isScroll() const
	{
		return IsAnyOf(_iMiscId, IMISC_SCROLL, IMISC_SCROLLT);
	}

	[[nodiscard]] bool isScrollOf(SpellID spellId) const
	{
		return isScroll() && _iSpell == spellId;
	}

	[[nodiscard]] bool isRune() const
	{
		return _iMiscId > IMISC_RUNEFIRST && _iMiscId < IMISC_RUNELAST;
	}

	[[nodiscard]] bool isRuneOf(SpellID spellId) const
	{
		if (!isRune())
			return false;
		switch (_iMiscId) {
		case IMISC_RUNEF:
			return spellId == SpellID::RuneOfFire;
		case IMISC_RUNEL:
			return spellId == SpellID::RuneOfLight;
		case IMISC_GR_RUNEL:
			return spellId == SpellID::RuneOfNova;
		case IMISC_GR_RUNEF:
			return spellId == SpellID::RuneOfImmolation;
		case IMISC_RUNES:
			return spellId == SpellID::RuneOfStone;
		default:
			return false;
		}
	}

	[[nodiscard]] bool isUsable() const;

	[[nodiscard]] bool keyAttributesMatch(uint32_t seed, BaseItemIdx itemIndex, uint16_t createInfo) const
	{
		return _iSeed == seed && IDidx == itemIndex && _iCreateInfo == createInfo;
	}

	UiFlags getTextColor() const
	{
		switch (_iMagical) {
		case ITEM_QUALITY_MAGIC:
			return UiFlags::ColorBlue;
		case ITEM_QUALITY_UNIQUE:
			return UiFlags::ColorWhitegold;
		default:
			return UiFlags::ColorWhite;
		}
	}

	UiFlags getTextColorWithStatCheck() const
	{
		if (!_iStatFlag)
			return UiFlags::ColorRed;
		return getTextColor();
	}

	/**
	 * @brief Sets the current Animation for the Item
	 * @param showAnimation Definies if the Animation (Flipping) is shown or if only the final Frame (item on the ground) is shown
	 */
	void setNewAnimation(bool showAnimation);

	/**
	 * @brief If this item is a spell book, calculates the magic requirement to learn a new level, then for all items sets _iStatFlag
	 * @param player Player to compare stats against requirements
	 */
	void updateRequiredStatsCacheForPlayer(const Player &player);

	/** @brief Returns the translated item name to display (respects identified flag) */
	StringOrView getName() const;
};

struct ItemGetRecordStruct {
	uint32_t nSeed;
	uint16_t wCI;
	int nIndex;
	uint32_t dwTimestamp;
};

struct CornerStoneStruct {
	Point position;
	bool activated;
	Item item;
	bool isAvailable();
};

/** Contains the items on ground in the current game. */
extern Item Items[MAXITEMS + 1];
extern uint8_t ActiveItems[MAXITEMS];
extern uint8_t ActiveItemCount;
/** Contains the location of dropped items. */
extern int8_t dItem[MAXDUNX][MAXDUNY];
extern bool ShowUniqueItemInfoBox;
extern CornerStoneStruct CornerStone;
extern std::array<bool, 256> UniqueItemFlags; // Which unique items currently exist in game?  Ideally, we don't want duplicates.

uint8_t GetOutlineColor(const Item &item, bool checkReq);
bool IsItemAvailable(int i);
bool IsUniqueAvailable(int i);
void ClearUniqueItemFlags();
void InitItemGFX();
void InitItems();
void CalcPlayerPowerFromItems(Player &player, bool Loadgfx);
void CalcPlayerInventory(Player &player, bool Loadgfx);
void InitializeItemToDefaultValues(Item &item, BaseItemIdx itemData);
void GenerateNewSeed(Item &h);
int GetGoldCursor(int value);

/**
 * @brief Update the gold cursor on the given gold item
 * @param gold The item to update
 */
void SetPlrHandGoldCurs(Item &gold);
void CreateNewPlayerItems(Player &player);
bool IsItemSpaceOk(Point position);
int AllocateItem();
/**
 * @brief Moves the item onto the floor of the current dungeon level
 * @param item The source of the item data, should not be used after calling this function
 * @param position Coordinates of the tile to place the item on
 * @return The index assigned to the item
 */
uint8_t PlaceItemInWorld(Item &&item, WorldTilePosition position);
Point GetSuperItemLoc(Point position);
void GenerateRandomPropertiesForBaseItem(Item &item, BaseItemIdx itemIndex, int lvl);
void SetupItem(Item &item);
// Create item functions - these functions create an item and drop it on the floor.
// position - item will be dropped on a floor tile nearest to this (x,y) coord
// onlygood - if true, bad affixes will be avoided and the item will have a small bonus iLevel
// sendmsg - should be true if you want network players to see the drop and have the item persist on the floor when you leave the zone.  If sendmsg=false then the item will disappear when you leave the zone unless you either identify the item or you leave the zone with the item in your inventory.  Once you ID the item or leave with it in your inventory, the item won't be deleted if you re-drop it in any zone.
// delta - ??
// spawn - ??
Item *CreateUniqueItem(UniqueItemIdx uid, Point position, std::optional<int> ilevel = std::nullopt, bool sendmsg = true, bool exactPosition = false);
void CreateItemFromMonster(Monster &monster, Point position, bool sendmsg, bool spawn);
void CreateRandomItem(Point position, bool onlygood, bool sendmsg, bool delta); // completely random item (based on dungeon/difficulty level)
void CreateRandomUsefulItem(Point position, bool sendmsg); // random potion or scroll, etc (based on dungeon/difficulty level)
void CreateRandomItemOfType(Point position, ItemType itemType, bool onlygood, bool sendmsg, bool delta); // (based on dungeon/difficulty level)
void CreateRandomItemOfTypeOrMisc(Point position, ItemType itemType, int iMiscId, bool onlygood, bool sendmsg, bool delta, bool spawn); // (based on dungeon/difficulty level)
void CreateRandomItemSpecificBase(Point position, BaseItemIdx idx, bool onlygood, bool sendmsg, bool delta, bool spawn); // (based on dungeon/difficulty level)
void CreateSpellBook(Point position, SpellID spellID, bool sendmsg, bool delta); // pass SpellID::Null to select a random spell for loot drops (based on dungeon/difficulty level)
void CreateGold(Point position, int value, bool sendmsg, bool delta, bool spawn); // pass value=0 to select a random value for loot drops (based on dungeon/difficulty level)

void RecreateItem(Item &item, BaseItemIdx idx, uint16_t icreateinfo, uint32_t iseed, int ivalue, bool isHellfire);
void RecreateEar(Item &item, uint16_t ic, uint32_t iseed, uint8_t bCursval, string_view heroName);
void CornerstoneSave();
void CornerstoneLoad(Point position);
void SpawnQuestItem(BaseItemIdx itemid, Point position, int randarea, int selflag, bool sendmsg);
void SpawnRewardItem(BaseItemIdx itemid, Point position, bool sendmsg);
void SpawnMapOfDoom(Point position, bool sendmsg);
void SpawnRuneBomb(Point position, bool sendmsg);
void SpawnTheodore(Point position, bool sendmsg);
void RespawnItem(Item &item, bool FlipFlag);
void DeleteItem(int i);
void ProcessItems();
void FreeItemGFX();
void GetItemFrm(Item &item);
void GetItemStr(Item &item);
void CheckIdentify(Player &player, int cii);
void DoRepair(Player &player, int cii);
void DoRecharge(Player &player, int cii);
bool DoOil(Player &player, int cii);
[[nodiscard]] StringOrView PrintItemPower(char plidx, const Item &item);
void DrawUniqueInfo(const Surface &out);
void PrintItemDetails(const Item &item);
void PrintItemDur(const Item &item);
void UseItem(size_t pnum, item_misc_id Mid, SpellID spellID, int spellFrom);
bool UseItemOpensHive(const Item &item, Point position);
bool UseItemOpensGrave(const Item &item, Point position);
void SpawnBasicItemsForSmith(int dungeonLevelUpTo16);
void SpawnPremiumItemsForSmith(const Player &player);
void SpawnItemsForWitch(int dungeonLevelUpTo16);
void SpawnItemsForBoy(int playerLevel);
void SpawnItemsForHealer(int dungeonLevelUpTo16);
void MakeGoldStackForInventory(Item &goldItem, int value);
int ItemNoFlippy();
bool GetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);
void SetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);
void PutItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);

/**
 * @brief Resets item get records.
 */
void initItemGetRecords();

void RepairItem(Item &item, Player &player);
void RechargeItem(Item &item, Player &player);
bool ApplyOilToItem(Item &item, Player &player);
/**
 * @brief Checks if the item is generated in vanilla hellfire. If yes it updates dwBuff to include CF_HELLFIRE.
 */
void UpdateHellfireFlag(Item &item, const char *identifiedItemName);

#if defined(_DEBUG) || JWK_ALLOW_DEBUG_COMMANDS_IN_RELEASE
std::string DebugSpawnItem(std::string itemName);
std::string DebugSpawnUniqueItem(std::string itemName);
#endif
/* data */

extern int MaxGold;

extern int8_t ItemCAnimTbl[];
extern _sfx_id ItemInvSnds[];

} // namespace devilution
