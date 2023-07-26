/**
 * @file player.h
 *
 * Interface of player functionality, leveling, actions, creation, loading, etc.
 */
#pragma once

#include <cstdint>
#include <vector>

#include <algorithm>
#include <array>

#include "diablo.h"
#include "engine.h"
#include "engine/actor_position.hpp"
#include "engine/animationinfo.h"
#include "engine/clx_sprite.hpp"
#include "engine/path.h"
#include "engine/point.hpp"
#include "interfac.h"
#include "items.h"
#include "items/validation.h"
#include "levels/gendung.h"
#include "multi.h"
#include "spelldat.h"
#include "misdat.h"
#include "utils/attributes.h"
#include "utils/enum_traits.h"

namespace devilution {

constexpr int InventoryGridCells = 40;
constexpr int MaxBeltItems = 8;
constexpr int MaxResistance = 75;
constexpr uint8_t MaxSpellLevel = 15;
constexpr int PlayerNameLength = 32;

constexpr size_t NumHotkeys = 12;
constexpr int BaseHitChance = JWK_EDIT_HIT_CHANCE ? 30 : 50;

/** Walking directions */
enum {
	// clang-format off
	WALK_NE   =  1,
	WALK_NW   =  2,
	WALK_SE   =  3,
	WALK_SW   =  4,
	WALK_N    =  5,
	WALK_E    =  6,
	WALK_S    =  7,
	WALK_W    =  8,
	WALK_NONE = -1,
	// clang-format on
};

enum class HeroClass : uint8_t {
	Warrior,
	Rogue,
	Sorcerer,
	Monk,
	Bard,
	Barbarian,

	LAST = Barbarian
};

enum class CharacterAttribute : uint8_t {
	Strength,
	Magic,
	Dexterity,
	Vitality,

	FIRST = Strength,
	LAST = Vitality
};

// Logical equipment locations
enum inv_body_loc : uint8_t {
	INVLOC_HEAD,
	INVLOC_RING_LEFT,
	INVLOC_RING_RIGHT,
	INVLOC_AMULET,
	INVLOC_HAND_LEFT,
	INVLOC_HAND_RIGHT,
	INVLOC_CHEST,
	NUM_INVLOC,
};

enum class player_graphic : uint8_t {
	Stand,
	Walk,
	Attack,
	Hit,
	Lightning,
	Fire,
	Magic,
	Death,
	Block,

	LAST = Block
};

enum class PlayerWeaponGraphic : uint8_t {
	Unarmed,
	UnarmedShield,
	Sword,
	SwordShield,
	Bow,
	Axe,
	Mace,
	MaceShield,
	Staff,
};

enum PLR_MODE : uint8_t {
	PM_STAND,
	PM_WALK_NORTHWARDS,
	PM_WALK_SOUTHWARDS,
	PM_WALK_SIDEWAYS,
	PM_ATTACK,
	PM_RATTACK,
	PM_BLOCK,
	PM_GOTHIT,
	PM_DEATH,
	PM_SPELL,
	PM_NEWLVL,
	PM_QUIT,
};

enum action_id : int8_t {
	// clang-format off
	ACTION_WALK        = -2, // Automatic walk when using gamepad
	ACTION_NONE        = -1,
	ACTION_ATTACK      = 9,
	ACTION_RATTACK     = 10,
	ACTION_SPELL       = 12,
	ACTION_OPERATE     = 13,
	ACTION_DISARM      = 14,
	ACTION_PICKUPITEM  = 15, // put item in hand (inventory screen open)
	ACTION_PICKUPAITEM = 16, // put item in inventory
	ACTION_TALK        = 17,
	ACTION_OPERATETK   = 18, // operate via telekinesis
	ACTION_ATTACKMON   = 20,
	ACTION_ATTACKPLR   = 21,
	ACTION_RATTACKMON  = 22,
	ACTION_RATTACKPLR  = 23,
	ACTION_SPELLMON    = 24,
	ACTION_SPELLPLR    = 25,
	ACTION_SPELLWALL   = 26,
	// clang-format on
};

enum class SpellFlag : uint8_t {
	// clang-format off
	None         = 0,
	Etherealize  = 1 << 0,
	RageActive   = 1 << 1,
	RageCooldown = 1 << 2,
	// bits 3-7 are unused
	// clang-format on
};
use_enum_as_flags(SpellFlag);

/* @brief When the player dies, what is the reason/source why? */
enum class DeathReason {
	/* @brief Monster or Trap (dungeon) */
	MonsterOrTrap,
	/* @brief Other player or selfkill (for example firewall) */
	Player,
	/* @brief HP is zero but we don't know when or where this happend */
	Unknown,
};

/** Maps from armor animation to letter used in graphic files. */
constexpr std::array<char, 3> ArmourChar = {
	'l', // light
	'm', // medium
	'h', // heavy
};
/** Maps from weapon animation to letter used in graphic files. */
constexpr std::array<char, 9> WepChar = {
	'n', // unarmed
	'u', // no weapon + shield
	's', // sword + no shield
	'd', // sword + shield
	'b', // bow
	'a', // axe
	'm', // blunt + no shield
	'h', // blunt + shield
	't', // staff
};

/** Maps from player class to letter used in graphic files. */
constexpr std::array<char, 6> CharChar = {
	'w', // warrior
	'r', // rogue
	's', // sorcerer
	'm', // monk
	'b',
	'c',
};

/**
 * @brief Contains Data (CelSprites) for a player graphic (player_graphic)
 */
struct PlayerAnimationData {
	/**
	 * @brief Sprite lists for each of the 8 directions.
	 */
	OptionalOwnedClxSpriteSheet sprites;

	[[nodiscard]] ClxSpriteList spritesForDirection(Direction direction) const
	{
		return (*sprites)[static_cast<size_t>(direction)];
	}
};

struct SpellCastInfo {
	SpellID spellId;
	SpellType spellType;
	/* @brief Inventory location for scrolls */
	int8_t spellFrom;
	/* @brief Used for spell level */
	int spellLevel;
};

struct Player {
	Player() = default;
	Player(Player &&) noexcept = default;
	Player &operator=(Player &&) noexcept = default;

#if JWK_PREVENT_DUPLICATE_MISSILE_HITS
	MissileGroupList _missileGroupsToIgnoreThisTick;
	MissileGroupRingBuffer _missileGroupsToIgnoreForever;
#endif

	char _pName[PlayerNameLength];
	std::array<Item, NUM_INVLOC> InvBody;
	std::array<Item, InventoryGridCells> InvList;
	std::array<Item, MaxBeltItems> SpdList;
	Item HoldItem;
#if !JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
	int lightId;
#endif
	int _pNumInv; // Number of items in InvList[]
	int _pStrength;
	int _pBaseStr;
	int _pMagic;
	int _pBaseMag;
	int _pDexterity;
	int _pBaseDex;
	int _pVitality;
	int _pBaseVit;
	int _pStatPts;
	int _pDamageMod; // unarmed character damage from str/dex
	int _pBaseToBlk;
	int _pHPBase;      // shifted value.
	int _pMaxHPBase;   // shifted value.
	int _pHitPoints;   // shifted value.  current health of player
	int _pMaxHP;       // shifted value.  maximum health as shown in game (includes all bonuses from items) 
	int _pHPPer;
	int _pManaBase;    // shifted value.
	int _pMaxManaBase; // shifted value.  your maximum mana without items
	int _pMana;        // shifted value.  you current mana as displayed in game
	int _pMaxMana;     // shifted value.  your maximum mana as displayed in game
	int _pManaPer;
	int _pIMinDam; // min base damage of item
	int _pIMaxDam; // max base damage of item
	int _pIAC;
	int _pIBonusDam; // items that have stats like "+58% damage"
	int _pIBonusToHit;
	int _pIBonusAC;
	int _pIBonusDamMod; // items that have stats like "+7 damage"
	int _pIGetHit;
	int _pArmorPierce;
	uint16_t _pIFMinDam; // min fire damage from items
	uint16_t _pIFMaxDam; // max fire damage from items
	uint16_t _pILMinDam; // min lightning damage from items
	uint16_t _pILMaxDam; // max lightning damage from items
	uint16_t _pIMMinDam; // min magic damage from items
	uint16_t _pIMMaxDam; // max magic damage from items
	uint8_t _pIThornsMin;
	uint8_t _pIThornsMax;
	uint32_t _pExperience;
#if JWK_EDIT_PLAYER_SKILLS
	Uint64 _timeOfMostRecentSkillUse; // in milliseconds
	Uint64 GetSkillCooldownMilliseconds();
#endif
	PLR_MODE _pmode;
	int8_t walkpath[MaxPathLength];
	bool plractive;
	action_id destAction;
	int destParam1;
	int destParam2;
	int destParam3;
	int destParam4;
	int _pGold;

	/**
	 * @brief Contains Information for current Animation
	 */
	AnimationInfo AnimInfo;
	/**
	 * @brief Contains a optional preview ClxSprite that is displayed until the current command is handled by the game logic
	 */
	OptionalClxSprite previewCelSprite;
	/**
	 * @brief Contains the progress to next game tick when previewCelSprite was set
	 */
	int8_t progressToNextGameTickWhenPreviewWasSet;
	/** @brief Bitmask using item_special_effect */
	ItemSpecialEffect _pIFlags;
	/**
	 * @brief Contains Data (Sprites) for the different Animations
	 */
	std::array<PlayerAnimationData, enum_size<player_graphic>::value> AnimationData;
	int8_t _pNFrames; // num frames for standing idle
	int8_t _pWFrames; // num frames for walking
	int8_t _pAFrames; // num frames for attacking (depends on weapon type equipped)
	int8_t _pAFNum;   // which frame the attack action occurs
	int8_t _pSFrames; // num frames for spellcasting
	int8_t _pSFNum;   // which frame the spellcast action occurs
	int8_t _pHFrames; // num hit recovery frames
	int8_t _pDFrames; // num death frames
	int8_t _pBFrames; // num blocking frames

	// Grid is indexed as InvGrid[x + 10*y] where x in [0,9], y in [0,3].  [0] is bottom left, [9] is bottom right, [30] is upper left, [39] is upper right.
	// InvGrid[p] == 0 means no item in grid cell p.  InvGrid[p] != 0 means InvList[abs(InvGrid[p]) - 1] is the item at grid cell p.
	// Items that take 1x1 grid cells use positive index InvGrid[p] > 0.  For larger items like a staff which takes 2x3 grid cells,
	// InvGrid[p] > 0 is used for the bottom left grid cell of the item, InvGrid[p] < 0 is used for the other grid cells of the item.
	std::array<int8_t, InventoryGridCells> InvGrid;

	uint8_t currentDungeonLevel;
	bool plrIsOnSetLevel;
	ActorPosition position;
	Direction _pdir; // Direction faced by player (direction enum)
	HeroClass _pHeroClass;

private:
	uint8_t _pLevel = 1; // Use get/setCharacterLevel to ensure this attribute stays within the accepted range

public:
	uint8_t _pgfxnum; // Bitmask indicating what variant of the sprite the player is using. The 3 lower bits define weapon (PlayerWeaponGraphic) and the higher bits define armour (starting with PlayerArmorGraphic)
	int8_t _pISplLvlAdd;
	int8_t _pManaCostMod; // JWK_ALLOW_MANA_COST_MODIFIER - This is a % chance to have free or costly spell (- means free, + means costly)
	/** @brief Specifies whether players are in non-PvP mode. */
	bool friendlyMode = true;

	/** @brief The next queued spell */
	SpellCastInfo queuedSpell;
	/** @brief The spell that is currently being cast */
	SpellCastInfo executedSpell;
	/* @brief Which spell should be executed with CURSOR_TELEPORT */
	SpellID inventorySpell;
	/* @brief Inventory location for scrolls with CURSOR_TELEPORT */
	int8_t spellFrom;
	SpellID _pRSpell;
	SpellType _pRSplType;
	SpellID _pSBkSpell;
	std::array<uint8_t, 64> _pSplLvl;
	/** @brief Bitmask of staff spell */
	uint64_t _pISpells;
	/** @brief Bitmask of learned spells */
	uint64_t _pMemSpells;
#if JWK_GOD_MODE_MAX_SPELLS
	uint64_t _pMemSpellsDebug;
#endif
	/** @brief Bitmask of abilities */
	uint64_t _pAblSpells;
	/** @brief Bitmask of spells available via scrolls */
	uint64_t _pScrlSpells;
	SpellFlag _pSpellFlags;
	SpellID _pSplHotKey[NumHotkeys];
	SpellType _pSplTHotKey[NumHotkeys];
	bool _pBlockFlag;
	bool _pInvincible;
	int8_t _pLightRad; // light radius.  Must be <= 15 because the VisionCrawlTable only contains precomputed values for radius <= 15
	/** @brief True when the player is transitioning between levels */
	bool _pLvlChanging;

	int8_t _pArmorClass;
	int8_t _pMagResist;
	int8_t _pFireResist;
	int8_t _pLghtResist;
	bool _pInfraFlag;
	/** Player's direction when ending movement. Also used for casting direction of SpellID::FireWall. */
	Direction tempDirection;

	std::array<bool, NUMLEVELS> _pLvlVisited;
	std::array<bool, NUMLEVELS> _pSLvlVisited; // only 10 used

	item_misc_id _pOilType;
	uint8_t pTownWarps;
	uint8_t pDungMsgs;
	uint8_t pLvlLoad;
	bool pManaShield;
	bool pSneak; // added for sneak skill (JWK_EDIT_PLAYER_SKILLS)
	uint8_t pDungMsgs2;
	bool pOriginalCathedral;
	uint8_t pDiabloKillLevel;
	uint16_t wReflections;
	ItemSpecialEffectHf pDamAcFlags;

	void CalcScrolls();

	void DamageArmor();

	// Returns the weapon type (or Shield/None) which is used for combat.  Excludes any item that might exist in the hand slot but isn't wearable due to stat limits or durability breakage
	ItemType GetItemTypeForCombat();

	bool CanUseItem(const Item &item) const;

	void BroadcastDurabilityChange(Item& item) const;

	/**
	 * @brief Remove an item from player inventory
	 * @param iv invList index of item to be removed
	 * @param calcScrolls If true, CalcScrolls() gets called after removing item
	 */
	void RemoveInvItem(int iv, bool calcScrolls = true);

	/**
	 * @brief Returns the network identifier for this player
	 */
	[[nodiscard]] uint8_t getId() const;

	void RemoveSpdBarItem(int iv);

	/**
	 * @brief Gets the most valuable item out of all the player's items that match the given predicate.
	 * @param itemPredicate The predicate used to match the items.
	 * @return The most valuable item out of all the player's items that match the given predicate, or 'nullptr' in case no
	 * matching items were found.
	 */
	template <typename TPredicate>
	const Item *GetMostValuableItem(const TPredicate &itemPredicate) const
	{
		const auto getMostValuableItem = [&itemPredicate](const Item *begin, const Item *end, const Item *mostValuableItem = nullptr) {
			for (const auto *item = begin; item < end; item++) {
				if (item->isEmpty() || !itemPredicate(*item)) {
					continue;
				}

				if (mostValuableItem == nullptr || item->_iIvalue > mostValuableItem->_iIvalue) {
					mostValuableItem = item;
				}
			}

			return mostValuableItem;
		};

		const Item *mostValuableItem = getMostValuableItem(&SpdList[0], &SpdList[0] + MaxBeltItems);
		mostValuableItem = getMostValuableItem(&InvBody[0], &InvBody[0] + inv_body_loc::NUM_INVLOC, mostValuableItem);
		mostValuableItem = getMostValuableItem(&InvList[0], &InvList[0] + _pNumInv, mostValuableItem);

		return mostValuableItem;
	}

	/**
	 * @brief Gets the base value of the player's specified attribute.
	 * @param attribute The attribute to retrieve the base value for
	 * @return The base value for the requested attribute.
	 */
	int GetBaseAttributeValue(CharacterAttribute attribute) const;

	/**
	 * @brief Gets the current value of the player's specified attribute.
	 * @param attribute The attribute to retrieve the current value for
	 * @return The current value for the requested attribute.
	 */
	int GetCurrentAttributeValue(CharacterAttribute attribute) const;

	/**
	 * @brief Gets the maximum value of the player's specified attribute.
	 * @param attribute The attribute to retrieve the maximum value for
	 * @return The maximum value for the requested attribute.
	 */
	int GetMaximumAttributeValue(CharacterAttribute attribute) const;

	/**
	 * @brief Get the tile coordinates a player is moving to (if not moving, then it corresponds to current position).
	 */
	Point GetTargetPosition() const;

	/**
	 * @brief Check if position is in player's path.
	 */
	bool IsPositionInPath(Point position);

	/**
	 * @brief Says a speech line.
	 * @todo BUGFIX Prevent more than one speech to be played at a time (reject new requests).
	 */
	void Say(HeroSpeech speechId) const;
	/**
	 * @brief Says a speech line after a given delay.
	 * @param speechId The speech ID to say.
	 * @param delay Multiple of 50ms wait before starting the speech
	 */
	void Say(HeroSpeech speechId, int delay) const;
	/**
	 * @brief Says a speech line, without random variants.
	 */
	void SaySpecific(HeroSpeech speechId) const;

	/**
	 * @brief Attempts to stop the player from performing any queued up action. If the player is currently walking, his walking will
	 * stop as soon as he reaches the next tile. If any action was queued with the previous command (like targeting a monster,
	 * opening a chest, picking an item up, etc) this action will also be cancelled.
	 */
	void Stop();

	/**
	 * @brief Is the player currently walking?
	 */
	bool isWalking() const;

	/**
	 * @brief Returns item location taking into consideration barbarian's ability to hold two-handed maces and clubs in one hand.
	 */
	item_equip_type GetItemLocation(const Item &item) const
	{
		if (_pHeroClass == HeroClass::Barbarian && item._iLoc == ILOC_TWOHAND && IsAnyOf(item._itype, ItemType::Sword, ItemType::Mace))
			return ILOC_ONEHAND;
		return item._iLoc;
	}

	/**
	 * @brief Return player's armor value
	 */
	int GetArmor() const
	{
#if JWK_EDIT_HIT_CHANCE
		return _pIBonusAC + _pIAC + _pDexterity / 4;
#else // original code
		return _pIBonusAC + _pIAC + _pDexterity / 5;
#endif
	}

	/**
	 * @brief Return player's melee to hit value
	 */
	int GetMeleeToHit() const
	{
#if JWK_EDIT_HIT_CHANCE // use the same formula for melee and range (melee uses str, ranged uses dex)
		// Note: Hit chance is modified by: hitChance += 2 * (attacker.level - target.level)
		// See MonsterAttackPlayer(), PlayerAttackPlayer(), and PlayerAttackMonster
		int hitChance = _pStrength + _pIBonusToHit + BaseHitChance;
#else // original code:
		int hitChance = getCharacterLevel() + _pDexterity / 2 + _pIBonusToHit + BaseHitChance;
		if (_pHeroClass == HeroClass::Warrior) {
			hitChance += 20;
		}
#endif
		return hitChance;
	}

	/**
	 * @brief Return player's ranged to hit value
	 */
	int GetRangedToHit() const
	{
#if JWK_EDIT_HIT_CHANCE // use the same formula for melee and range (melee uses str, ranged uses dex)
		// See hit chance formulas in MonsterHitByMissileFromPlayer(), MonsterHitByMissileFromMonsterOrTrap(), PlayerHitByMissle(), and PvPHitByMissile()
		int hitChance = _pDexterity + _pIBonusToHit + BaseHitChance;
#else // original code:
		int hitChance = getCharacterLevel() + _pDexterity + _pIBonusToHit + BaseHitChance;
		if (_pHeroClass == HeroClass::Rogue) {
			hitChance += 20;
		} else if (_pHeroClass == HeroClass::Warrior || _pHeroClass == HeroClass::Bard)
			hitChance += 10;
#endif
		return hitChance;
	}

	/**
	 * @brief Return magic hit chance
	 */
	int GetMagicToHit() const
	{
#if JWK_EDIT_HIT_CHANCE
		int hitChance = _pMagic + BaseHitChance;
#else // original code:
		int hitChance = _pMagic + BaseHitChance;
		if (_pHeroClass == HeroClass::Sorcerer)
			hitChance += 20;
		else if (_pHeroClass == HeroClass::Bard)
			hitChance += 10;
#endif
		return hitChance;
	}

#if JWK_EDIT_BLOCK_CHANCE
	int GetBlockChance(int attackerLevel) const
	{
		return std::clamp(std::min<int>(_pDexterity, _pStrength) + 2 * (getCharacterLevel() - attackerLevel), 0, 100);
	}
#else // original code
	int GetBlockChance(int attackerLevel) const
	{
		return std::clampclamp(_pDexterity + _pBaseToBlk + 2 * (getCharacterLevel() - attackerLevel), 0, 100);
	}
#endif

	/**
	 * @brief Gets the effective spell level for the player, considering item bonuses
	 * @param spell SpellID enum member identifying the spell
	 * @return effective spell level
	 */
	int GetSpellLevel(SpellID spell) const
	{
		if (spell == SpellID::Invalid || static_cast<std::size_t>(spell) >= sizeof(_pSplLvl)) {
			return 0;
		}

		if (JWK_GOD_MODE_MAX_SPELLS && spell != SpellID::Apocalypse) {
			return std::max<int>(_pISplLvlAdd + 15, 0);
		}
		return std::max<int>(_pISplLvlAdd + _pSplLvl[static_cast<std::size_t>(spell)], 0);
	}

	/**
	 * @brief Return monster armor value after including player's armor piercing
	 * @param monsterArmor - monster armor before applying % armor pierce
	 * @param isMelee - indicates if it's melee or ranged combat
	 */
	int CalculateArmorAfterPierce(int monsterArmor, bool isMelee) const
	{
		int tmac = monsterArmor;
		if (_pArmorPierce > 0) {
			if (_pArmorPierce == 1) {
				tmac = tmac * 3 / 4;
			} else if (_pArmorPierce == 2) {
				tmac = tmac * 2 / 3;
			} else {
				assert(_pArmorPierce == 3);
				tmac = tmac * 1 / 2;
			}
		}
		if (tmac < 0)
			tmac = 0;

		return tmac;
	}

	/**
	 * @brief Calculates the players current Hit Points as a percentage of their max HP and stores it for later reference
	 *
	 * The stored value is unused...
	 * @see _pHPPer
	 * @return The players current hit points as a percentage of their maximum (from 0 to 80%)
	 */
	int UpdateHitPointPercentage()
	{
		if (_pMaxHP <= 0) { // divide by zero guard
			_pHPPer = 0;
		} else {
			// Maximum achievable HP is approximately 1200. Diablo uses fixed point integers where the last 6 bits are
			// fractional values. This means that we will never overflow HP values normally by doing this multiplication
			// as the max value is representable in 17 bits and the multiplication result will be at most 23 bits
			_pHPPer = std::clamp(_pHitPoints * 80 / _pMaxHP, 0, 80); // hp should never be greater than maxHP but just in case
		}

		return _pHPPer;
	}

	int UpdateManaPercentage()
	{
		if (_pMaxMana <= 0) {
			_pManaPer = 0;
		} else {
			_pManaPer = std::clamp(_pMana * 80 / _pMaxMana, 0, 80);
		}

		return _pManaPer;
	}

	/**
	 * @brief Restores between 1/8 (inclusive) and 1/4 (exclusive) of the players max HP (further adjusted by class).
	 *
	 * This determines a random amount of non-fractional life points to restore then scales the value based on the
	 *  player class. Warriors/barbarians get between 1/4 and 1/2 life restored per potion, rogue/monk/bard get 3/16
	 *  to 3/8, and sorcerers get the base amount.
	 */
	void RestorePartialLife();

	/**
	 * @brief Resets hp to maxHp
	 */
	void RestoreFullLife()
	{
		_pHitPoints = _pMaxHP;
		_pHPBase = _pMaxHPBase;
	}

	void DoLifeAndManaSteal(int damage);

	/**
	 * @brief Restores between 1/8 (inclusive) and 1/4 (exclusive) of the players max Mana (further adjusted by class).
	 *
	 * This determines a random amount of non-fractional mana points to restore then scales the value based on the
	 *  player class. Sorcerers get between 1/4 and 1/2 mana restored per potion, rogue/monk/bard get 3/16 to 3/8,
	 *  and warrior/barbarian get the base amount. However if the player can't use magic due to an equipped item then
	 *  they get nothing.
	 */
	void RestorePartialMana();

	/**
	 * @brief Resets mana to maxMana (if the player can use magic)
	 */
	void RestoreFullMana()
	{
		if (HasNoneOf(_pIFlags, ItemSpecialEffect::NoMana)) {
			_pMana = _pMaxMana;
			_pManaBase = _pMaxManaBase;
		}
	}
	/**
	 * @brief Sets the readied spell to the spell in the specified equipment slot. Does nothing if the item does not have a valid spell.
	 * @param bodyLocation - the body location whose item will be checked for the spell.
	 * @param forceSpell - if true, always change active spell, if false, only when current spell slot is empty
	 */
	void ReadySpellFromEquipment(inv_body_loc bodyLocation, bool forceSpell);

	/**
	 * @brief Does the player currently have a ranged weapon equipped?
	 */
	bool UsesRangedWeapon() const
	{
		return static_cast<PlayerWeaponGraphic>(_pgfxnum & 0xF) == PlayerWeaponGraphic::Bow;
	}

	bool CanChangeAction()
	{
		if (_pmode == PM_STAND)
			return true;
		if (_pmode == PM_ATTACK && AnimInfo.currentFrame >= _pAFNum)
			return true;
		if (_pmode == PM_RATTACK && AnimInfo.currentFrame >= _pAFNum)
			return true;
		if (_pmode == PM_SPELL && AnimInfo.currentFrame >= _pSFNum)
			return true;
		if (isWalking() && AnimInfo.isLastFrame())
			return true;
		return false;
	}

	[[nodiscard]] player_graphic getGraphic() const;

	[[nodiscard]] uint16_t getSpriteWidth() const;

	void getAnimationFramesAndTicksPerFrame(player_graphic graphics, int8_t &numberOfFrames, int8_t &ticksPerFrame) const;

	[[nodiscard]] ClxSprite currentSprite() const
	{
		return previewCelSprite ? *previewCelSprite : AnimInfo.currentSprite();
	}
	[[nodiscard]] Displacement getRenderingOffset(const ClxSprite sprite) const
	{
		Displacement offset = { -CalculateWidth2(sprite.width()), 0 };
		if (isWalking())
			offset += GetOffsetForWalking(AnimInfo, _pdir);
		return offset;
	}

	/**
	 * @brief Updates previewCelSprite according to new requested command
	 * @param cmdId What command is requested
	 * @param point Point for the command
	 * @param wParam1 First Parameter
	 * @param wParam2 Second Parameter
	 */
	void UpdatePreviewCelSprite(_cmd_id cmdId, Point point, uint16_t wParam1, uint16_t wParam2);

	[[nodiscard]] uint8_t getCharacterLevel() const
	{
		return _pLevel;
	}

	/**
	 * @brief Sets the character level to the target level or nearest valid value.
	 * @param level New character level, will be clamped to the allowed range
	 */
	void setCharacterLevel(uint8_t level);

	[[nodiscard]] uint8_t getMaxCharacterLevel() const;

	[[nodiscard]] bool isMaxCharacterLevel() const
	{
		return getCharacterLevel() >= getMaxCharacterLevel();
	}

private:
	void _addExperience(uint32_t experience, int monsterLevelMinusPlayerLevel);

public:
	/**
	 * @brief Adds experience to the local player based on the current game mode
	 * @param experience base value to add, this will be adjusted to prevent power leveling in multiplayer games
	 */
	void addExperience(uint32_t experience)
	{
		_addExperience(experience, 0);
	}

	/**
	 * @brief Adds experience to the local player based on the difference between the monster level
	 * and current level, then also applying the power level cap in multiplayer games.
	 * @param experience base value to add, will be scaled up/down by the difference between player and monster level
	 * @param monsterLevel level of the monster that has rewarded this experience
	 */
	void addExperience(uint32_t experience, int monsterLevel)
	{
		_addExperience(experience, monsterLevel - getCharacterLevel());
	}

	[[nodiscard]] uint32_t getNextExperienceThreshold() const;

	/** @brief Checks if the player is on the same level as the local player (MyPlayer). */
	bool isOnActiveLevel() const
	{
		if (setlevel)
			return isOnLevel(setlvlnum);
		return isOnLevel(currlevel);
	}

	/** @brief Checks if the player is on the corresponding level. */
	bool isOnLevel(uint8_t level) const
	{
		return !this->plrIsOnSetLevel && this->currentDungeonLevel == level;
	}
	/** @brief Checks if the player is on the corresponding level. */
	bool isOnLevel(_setlevels level) const
	{
		return this->plrIsOnSetLevel && this->currentDungeonLevel == static_cast<uint8_t>(level);
	}
	/** @brief Checks if the player is on a arena level. */
	bool isOnArenaLevel() const
	{
		return plrIsOnSetLevel && IsArenaLevel(static_cast<_setlevels>(currentDungeonLevel));
	}
	void setLevel(uint8_t level)
	{
		this->currentDungeonLevel = level;
		this->plrIsOnSetLevel = false;
	}
	void setLevel(_setlevels level)
	{
		this->currentDungeonLevel = static_cast<uint8_t>(level);
		this->plrIsOnSetLevel = true;
	}

	/** @brief Returns a character's life based on starting life, character level, and base vitality. */
	int32_t calculateBaseLife() const;

	/** @brief Returns a character's mana based on starting mana, character level, and base magic. */
	int32_t calculateBaseMana() const;

	int CalcManaShieldAbsorbPercent() const;

	void GetGolemStats(int spellLevel, uint32_t& outMaxHP, uint32_t& outArmor, uint32_t& outHitChance, uint32_t& outMinDamage, uint32_t& outMaxDamage) const;
	uint32_t GetGolemToHit() const;
};

extern DVL_API_FOR_TEST uint8_t MyPlayerId;
extern DVL_API_FOR_TEST Player *MyPlayer;
extern DVL_API_FOR_TEST std::vector<Player> Players;
/** @brief What Player items and stats should be displayed? Normally this is identical to MyPlayer but can differ when /inspect was used. */
extern Player *InspectPlayer;
/** @brief Do we currently inspect a remote player (/inspect was used)? In this case the (remote) players items and stats can't be modified. */
inline bool IsInspectingPlayer()
{
	return MyPlayer != InspectPlayer;
}
extern bool MyPlayerIsDead;

Player *PlayerAtPosition(Point position, bool ignoreMovingPlayers = false);
Player *FindClosestPlayerInSight(Point source, int rad);

void LoadPlrGFX(Player &player, player_graphic graphic);
void InitPlayerGFX(Player &player);
void ResetPlayerGFX(Player &player);

/**
 * @brief Sets the new Player Animation with all relevant information for rendering
 * @param player The player to set the animation for
 * @param graphic What player animation should be displayed
 * @param dir Direction of the animation
 * @param numberOfFrames Number of Frames in Animation
 * @param delayLen Delay after each Animation sequence
 * @param flags Specifies what special logics are applied to this Animation
 * @param numSkippedFrames Number of Frames that will be skipped (for example with modifier "faster attack")
 * @param distributeFramesBeforeFrame Distribute the numSkippedFrames only before this frame
 */
void NewPlrAnim(Player &player, player_graphic graphic, Direction dir, AnimationDistributionFlags flags = AnimationDistributionFlags::None, int8_t numSkippedFrames = 0, int8_t distributeFramesBeforeFrame = 0);
void SetPlrAnims(Player &player);
void CreateNewPlayer(Player &player, HeroClass c);
int CalcStatDiff(Player &player);
#ifdef _DEBUG
void NextPlrLevel(Player &player);
#endif
void AddPlrMonstExper(int monsterLevel, unsigned int monsterExp, char whoHitMonsterFlags, WorldTilePosition monsterLocation);
void ApplyPlrDamage(DamageType damageType, Player &player, int dam, int minHP /*=0*/, int frac /*=0*/, int hitChanceForUI, int attackerForUI, DeathReason deathReason);
void InitPlayer(Player &player, bool FirstTime);
void InitMultiView();
void PlrClrTrans(Point position);
void PlrDoTrans(Point position);
void SetPlayerOld(Player &player);
void FixPlayerLocation(Player &player, Direction bDir);
void StartStand(Player &player, Direction dir);
void StartPlrBlock(Player &player, Direction dir);
void FixPlrWalkTags(const Player &player);
void StartPlrHit(Player &player, int dam, bool forcehit);
void StartPlayerKill(Player &player, DeathReason deathReason);
/**
 * @brief Strip the top off gold piles that are larger than MaxGold
 */
void StripTopGold(Player &player);
void SyncPlrKill(Player &player, DeathReason deathReason);
void RemovePlrMissiles(const Player &player);
void StartNewLvl(Player &player, interface_mode fom, int lvl);
void RestartTownLvl(Player &player);
void StartWarpLvl(Player &player, size_t pidx);
void ProcessPlayers();
void ClrPlrPath(Player &player);
bool PosOkPlayer(const Player &player, Point position);
void MakePlrPath(Player &player, Point targetPosition, bool endspace);
void CalcPlrStaff(Player &player);
void CheckSpellAndSendCmd(bool isShiftHeld, SpellID spellID = MyPlayer->_pRSpell, SpellType spellType = MyPlayer->_pRSplType);
void SyncPlrAnim(Player &player);
void SyncInitPlrPos(Player &player);
void SyncInitPlr(Player &player);
void CheckStats(Player &player);
void ModifyPlrStr(Player &player, int l);
void ModifyPlrMag(Player &player, int l);
void ModifyPlrDex(Player &player, int l);
void ModifyPlrVit(Player &player, int l);
void SetPlayerHitPoints(Player &player, int val);
void SetPlrStr(Player &player, int v);
void SetPlrMag(Player &player, int v);
void SetPlrDex(Player &player, int v);
void SetPlrVit(Player &player, int v);
void InitDungMsgs(Player &player);
void PlayDungMsgs();

} // namespace devilution
