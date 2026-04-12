/**
 * @file spelldat.h
 *
 * Interface of all spell data.
 */
#pragma once

#include <cstdint>
#include <type_traits>

#include "effects.h"
#include "utils/enum_traits.h"

#define JWK_BLACK_DEATH_NO_PERM_HP_LOSS 1 // Instead of -1HP to your permanent max health, black death does insane damage instead (likely killing the player in one hit).
#define JWK_EDIT_CRITICAL_STRIKE 1 // Reduce melee critical strike chance for warriors/barbarians from 50% to 30%.  Add a similar critical strike chance for rogues using ranged attacks.
#define JWK_USE_CONSISTENT_MELEE_AND_RANGED_DAMAGE 1 // Use the same ranged attack damage formula for all classes instead of halving damage for non-rogue damage.  To compensate rogue, toggle JWK_EDIT_CRITICAL_STRIKE=1 which gives her a critical strike chance.
#define JWK_EDIT_HIT_CHANCE 1 // If defined==2: consistent formula for everything.  If defined==1: Spells always hit, ranged has no distance penalty.  In both cases, remove the class-specific bonuses to hit chance so everything is based on stats (more consistent and easier for players to understand).
#define JWK_EDIT_BLOCK_CHANCE 1 // Use new formula based on str/dex instead of purely dex
#define JWK_EDIT_FAST_BLOCK 1 // Make fastblock -2 frames instead of -4 frames.  This only affects sorcerer because he's the only one who needs -4 frames to reach the block speed limit.
#define JWK_RESISTANT_TARGETS_CAN_BLOCK 1 // If true, resistance doesn't affect blocking.  If false (original code), having fire/lightning/magic resistance (even 1%) prevents your character from blocking the attack.
#define JWK_RESISTANT_TARGETS_CAN_BE_STUNNED 1 // If true, resistance doesn't affect stuns other than reduced damage reduces chance of stun.  If false (original code), having fire/lightning/magic resistance (even 1%) prevents players/monsters from being stunned.
#define JWK_ALLOW_LEECH_IN_PVP 1 // If true, allow life and mana leech weapons to function in pvp.  Original code does not allow weapon-based leech but it DOES allow life steal crown, which seems like a bug.
#define JWK_BUFF_LIFE_STEAL_CROWN 1 // If true, buff life steal crown from random 0-12% leech to a constant 12% leech
#define JWK_REDUCE_ITEM_DURABILITY_LOSS 1 // Make items get damaged at a slower rate (about half)
#define JWK_BUFF_DAMAGE_MULTIPIER_PREFIX 1 // Buff damage-only weapon prefixes like "Merciless" because otherwise "Kings" weapons have the same damage bonus AND hit chance increase, which makes the "Merciless" prefix kinda useless.
#define JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY 1 // If true, monsters/chests drop loot based on their difficulty-adjusted level (+15 in nightmare, +30 in hell).  The original game didn't include the difficulty factor which meant monsters/chests could never drop loot above item level 30!
#define JWK_ALLOW_DIABLO_LOOT 1 // If true, make Diablo drop more loot, and give the player time to loot before the game automatically ends
#define JWK_EDIT_EXP_GAIN 1 // Raise experience cap per monster to 1/4 the experience needed for next player level.  Otherwise, the original code caps the experience per monster to 200*playerlvl which means a level 50 player only gains 10000 exp for killing Diablo!
#define JWK_INCREASE_MONSTER_HEALTH 1 // Increase in nightmare/hell difficulty
#define JWK_BUFF_MONSTERS_IN_MULTIPLAYER 1 // Similar to Diablo 2, increase monster health, experience, and loot by 50% for each extra player in a multiplayer game
#define JWK_EDIT_SPELLBOOK_DROPS 1 // Simplify the logic for spellbook drops (spellbooks become more rare depending on the book page they appear on)
#define JWK_EDIT_PLAYER_STAT_CAPS 1 // Modest rebalancing
#define JWK_EDIT_GOLEM 1
#define JWK_FIREBALL_DOES_REDUCED_SPLASH_DAMAGE 1 // Instead of doing 100% damage to adjacent targets, do 1/2 damage to adjacent targets
#define JWK_DYNAMIC_PRICE_FOR_IDENTIFY 1 // If true, cain charges more to identify items of higher iLevel
#define JWK_ITEMS_HAVE_REDUCED_RESALE_VALUE 1 // If true, vendors won't pay the player huge amounts of gold for valuable items
#define JWK_EDIT_SPELL_COSTS 1 // rebalance spell mana costs
#define JWK_EDIT_NOVA 1 // Fire a ring of magic bolts instead of lightning.  Also, allow player to learn the spell.
#define JWK_EDIT_INFERNO 0 // Basically the same spell but I rewrote the logic to make the damage formula much simpler and easier to tune.
#define JWK_EDIT_BONE_SPIRIT 1 // Instead of doing 1/3 HP as magic damage (which is either overpowered or useless if immune), make bone spirit remove the top x% of a target's HP but it does nothing if the target is already below that threshold.  Physical effect but undead/diablo are immune.
#define JWK_EDIT_CHAIN_LIGHTNING 1 // Use completely new chain lightning more like chain lightning in Diablo 2
#define JWK_EDIT_STONE_CURSE 1 // When casting stone curse, don't target already-cursed monsters (This makes targetting feel much better).  Increase duration every spell level instead of capping it.  Monsters that are stone cursed take reduced damage.
#define JWK_EDIT_ELEMENTAL 1 // Make elemental single target instead of AoE.  Fireball does AoE, elemental does homing.  Elemental shouldn't do both otherwise it's just better in all cases.
#define JWK_EDIT_CHARGED_BOLT 1 // Make bolts more slower and branch more often instead of having multiple bolts take the same path
#define JWK_EDIT_APOCALYPSE 1 // Instead of an overpowered AoE, apocalypse becomes a single target nuke (still a bit overpowered but not as much)
#define JWK_APOCALYPSE_NEEDS_LINE_OF_SIGHT 1 // Use the hellfire line-of-sight requirement, otherwise you can kill monsters through walls when they can't attack you
#define JWK_EDIT_MANA_SHIELD 1 // Rebalance mana shield (absorbs less damage, costs most to cast, also I fixed some bugs with it)
#define JWK_ALLOW_FASTER_CASTING 1 // If true, items can increases cast speed (but cast speed is capped to the sorcerer's cast speed, much like fastblock is capped to warrior's block speed)
#define JWK_USE_CONSISTENT_FASTER_ATTACK 1 // Fix the bugs with faster attack melee weapons.  Allow bows to have the same faster attack as melee.
#define JWK_ALLOW_MANA_COST_MODIFIER 1 // If true, items reduce mana cost of spells
#define JWK_INCREASE_VALUE_OF_STAFF_CHARGES_IF_SPELL_CANT_BE_LEARNED 1
#define JWK_EDIT_RECHARGE_COST_AT_WITCH 1 // recharge costs more depending on the spell and whether or not the spell can be learned
#define JWK_EDIT_PLAYER_SKILLS 1 // Change skills staff recharge and item repair so they don't ruin items.  Limit skill uses per hour as balance.  Add a sneak skill which replaces etherealize.
#define JWK_FIX_LIGHTING 1 // There were multiple lighting bugs.  Light location teleported forward when walking northward but not when walking southward.  Also, vision wasn't shared properly in multiplayer.  Can debug using JWK_DEBUG_SET_LIGHTING_EQUAL_VISION
#define JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER 1 // If true, show the light radius of remote players.  If false, they are completely dark (original code).
#define JWK_FIX_NETWORK_SYNC_AND_AUTHORITY 1 // If true, players have consistent authority over their own health,damage,death and also their own golem.  With original code, golem was almost never in sync.
#define JWK_REDUCE_DAMAGE_IN_PVP 1 // Otherwise players two shot each other.
#define JWK_REVEAL_RESISTANCES_WHEN_DAMAGED 1 // Reveal each resistance after hitting a monster with that damage type
#define JWK_BUFF_UNIQUE_MONSTERS 1 // buffs monster level, health, hit chance, armor, resistances, experience, and loot
#define JWK_EDIT_HOLY_BOLT_RESISTANCE 1 // Similar to hellfire, make Diablo/Bone demon resistance to holy bolt.  However, make this resistance 50% instead of 75%.
#define JWK_DIABLO_CANT_BE_STUNLOCKED 1 // Similar to hellfire, make Diablo less easy to stunlock
#define JWK_GUARDIAN_TARGETS_HOSTILE_PLAYERS 1 // If true, the guardian will target any players who have their pvp flag enabled (even if the caster doesn't have their flag enabled).  This makes the guardian more of a "defensive guardian" rather than something offensive.
#define JWK_ALLOW_MORE_KEYBINDS 1 // Instead of just F5-F8, allow F2-F8 for binding spells.  This disables the quicksave/quickload keys F2,F3 in single player games but save/load can be accessed from the ESC menu.  If keybinds aren't working for a pre-existing game, check your diablo.ini file and make sure QuickSpell1=F2,etc and delete any QuickLoad/QuickSave keybinds.

// god modes and debug modes
#define JWK_GOD_MODE_PLAYER_IMMUNE_TO_STUN 0
#define JWK_GOD_MODE_PLAYER_TAKES_NO_DAMAGE 0 // As a side effect, this also means head/chest armor doesn't get damaged
#define JWK_GOD_MODE_SPELLS_COST_NOTHING 0 // Don't consume any mana, scrolls, or staff charges when casting a spell
#define JWK_GOD_MODE_MAX_SPELLS 0 // Temporarily grant player level 15 spells (as if learned) but player will revert back to previous spell levels when this god mode is disabled.
#define JWK_GOD_MODE_NO_ITEM_DAMAGE 0 // Don't damage player's items
#define JWK_GOD_MODE_INFINITE_PETRIFY 0 // Once petrified, a monster stays petrified forever
#define JWK_GOD_MODE_ADJUST_STR_BY_AMOUNT 0 // Adds this value to your player's strength stat as if you had an item with +strength on it.  The amount can be negative if you want to decrease your stats.
#define JWK_GOD_MODE_ADJUST_MAG_BY_AMOUNT 0 // Adds this value to your player's magic stat as if you had an item with +magic on it.  The amount can be negative if you want to decrease your stats.
#define JWK_GOD_MODE_ADJUST_DEX_BY_AMOUNT 0 // Adds this value to your player's dexterity stat as if you had an item with +dexterity on it.  The amount can be negative if you want to decrease your stats.
#define JWK_GOD_MODE_ADJUST_VIT_BY_AMOUNT 0 // Adds this value to your player's vitality stat as if you had an item with +vitality on it.  The amount can be negative if you want to decrease your stats.
#define JWK_DEBUG_DISABLE_NETWORK_TIMEOUT 0 // If true, setting breapoints in multiplayer games won't cause players to be dropped (It will just hourglass indefinitely)
#define JWK_DEBUG_ALL_MONSTERS_HAVE_125000_HEALTH 0 // Useful to testing attacks versus monsters without the monster dying too quickly
#define JWK_ALLOW_DEBUG_COMMANDS_IN_RELEASE 0 // This is useful for generating many debug items because optimized code runs faster

namespace devilution {

#define MAX_SPELLS 52

enum class SpellType : uint8_t {
	Skill,
	FIRST = Skill,
	Spell,
	Scroll,
	Charges,
	LAST = Charges,
	Invalid,
};

enum class SpellID : int8_t {
	         Null = 0,
	/* 00 */ FIRST = Null,
	/* 01 */ Firebolt,
	/* 02 */ Healing,
	/* 03 */ Lightning,
	/* 04 */ Flash,
	/* 05 */ Identify,
	/* 06 */ FireWall,
	/* 07 */ TownPortal,
	/* 08 */ StoneCurse,
	/* 09 */ Infravision,
	/* 10 */ Phasing,
	/* 11 */ ManaShield,
	/* 12 */ Fireball,
	/* 13 */ Guardian,
	/* 14 */ ChainLightning,
	/* 15 */ FlameWave,
	/* 16 */ DoomSerpents,
	/* 17 */ BloodRitual,
	/* 18 */ Nova,
	/* 19 */ Invisibility,
	/* 20 */ Inferno,
	/* 21 */ Golem,
	/* 22 */ Rage,
	/* 23 */ Teleport,
	/* 24 */ Apocalypse,
	/* 25 */ Etherealize,
	/* 26 */ ItemRepair,
	/* 27 */ StaffRecharge,
	/* 28 */ TrapDisarm,
	/* 29 */ Elemental,
	/* 30 */ ChargedBolt,
	/* 31 */ HolyBolt,
	/* 32 */ Resurrect,
	/* 33 */ Telekinesis,
	/* 34 */ HealOther,
	/* 35 */ BloodStar,
	/* 36 */ BoneSpirit,
	/* 37 */ LastDiablo = BoneSpirit,
	/* 38 */ Mana,
	/* 39 */ Magi,
	/* 40 */ Jester,
	/* 41 */ LightningWall,
	/* 42 */ Immolation,
	/* 43 */ Warp,
	/* 44 */ Reflect,
	/* 45 */ Berserk,
	/* 46 */ RingOfFire,
	/* 47 */ Search,
	/* 48 */ RuneOfFire,
	/* 49 */ RuneOfLight,
	/* 50 */ RuneOfNova,
	/* 51 */ RuneOfImmolation,
	/* 52 */ RuneOfStone,
	         LAST = RuneOfStone,
	         Invalid = -1,
};

enum class MagicType : uint8_t {
	Fire,
	Lightning,
	Magic,
};

enum class MissileID : int8_t {
	// clang-format off
	Arrow,
	FireArrow,
	LightningArrow,
	PoisonArrow,
	SpectralArrow,
	Firebolt,
	GuardianBolt, // jwk added
	Guardian,
	Phasing,
	NovaBall,
	FireWallSingleTile,
	Fireball,
	LightningControl,
	Lightning,
	MagmaBallExplosion,
	TownPortal,
	FlashBottom,
	FlashTop,
	ManaShield,
	FlameWave,
	ChainLightning,
	ChainBall, // unused
	BloodHit, // unused
	BoneHit, // unused
	MetalHit, // unused
	Rhino,
	MagmaBall,
	ThinLightningControl,
	ThinLightning,
	BloodStar,
	BloodStarExplosion,
	Teleport,
	DoomSerpents, // unused
	FireOnly, // unused
	StoneCurse,
	BloodRitual, // unused
	Invisibility, // unused
	Golem,
	Etherealize,
	Spurt, // unused
	ApocalypseBoom,
	Healing,
	FireWallControl,
	Infravision,
	Identify,
	FlameWaveControl,
	Nova,
	Rage, // BloodBoil in Diablo
	Apocalypse,
	ItemRepair,
	StaffRecharge,
	TrapDisarm,
	Inferno,
	InfernoControl,
	FireMan, // unused
	Krull, // unused
	ChargedBolt,
	HolyBolt,
	Resurrect,
	Telekinesis,
	Acid,
	AcidSplat,
	AcidPuddle,
	AcidPuddleHuge, // jwk added
	HealOther,
	Elemental,
	ResurrectBeam,
	BoneSpirit,
	WeaponExplosion,
	RedPortal,
	DiabloApocalypseBoom,
	DiabloApocalypse,
	Mana,
	Magi,
	LightningWallSingleTile,
	LightningWallControl,
	Immolation,
	FireballBow,
	LightningBow,
	ChargedBoltBow,
	HolyBoltBow,
	Warp,
	Reflect,
	Berserk,
	RingOfFire,
	StealPotions,
	StealMana,
	RingOfLightning, // unused
	Search,
	Aura, // unused
	Aura2, // unused
	SpiralFireball, // unused
	RuneOfFire,
	RuneOfLight,
	RuneOfNova,
	RuneOfImmolation,
	RuneOfStone,
	BigExplosion,
	HorkSpawn,
	Jester,
	OpenNest,
	OrangeFlare,
	BlueFlare,
	RedFlare,
	YellowFlare,
	BlueFlare2,
	YellowExplosion,
	RedExplosion,
	BlueExplosion,
	BlueExplosion2,
	OrangeExplosion,
	Null = -1,
	// clang-format on
};

enum class SpellDataFlags : uint8_t {
	// The lower 2 bytes are used to store MagicType.
	Fire = static_cast<uint8_t>(MagicType::Fire),
	Lightning = static_cast<uint8_t>(MagicType::Lightning),
	Magic = static_cast<uint8_t>(MagicType::Magic),
	Targeted = 1U << 2,
	AllowedInTown = 1U << 3,
};
use_enum_as_flags(SpellDataFlags);

struct SpellData {
	const char *sNameText;
	_sfx_id sSFX;
	uint16_t bookCost10;
	uint8_t staffCost10;
	uint8_t sManaCost;
	SpellDataFlags flags;
	int8_t sBookLvl;
	int8_t sStaffLvl;
	uint8_t minInt;
	MissileID sMissiles[2];
	uint8_t sManaAdj;
	uint8_t sMinMana;
	uint8_t sStaffMin;
	uint8_t sStaffMax;

	[[nodiscard]] MagicType type() const
	{
		return static_cast<MagicType>(static_cast<std::underlying_type<SpellDataFlags>::type>(flags) & 0b11U);
	}

	[[nodiscard]] uint32_t bookCost() const
	{
		return bookCost10 * 10;
	}

	[[nodiscard]] uint16_t staffCost() const
	{
		return staffCost10 * 10;
	}

	[[nodiscard]] bool isTargeted() const
	{
		return HasAnyOf(flags, SpellDataFlags::Targeted);
	}

	[[nodiscard]] bool isAllowedInTown() const
	{
		return HasAnyOf(flags, SpellDataFlags::AllowedInTown);
	}
};

extern const SpellData SpellsData[];

inline const SpellData &GetSpellData(SpellID spellId)
{
	return SpellsData[static_cast<std::underlying_type<SpellID>::type>(spellId)];
}

} // namespace devilution
