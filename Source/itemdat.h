/**
 * @file itemdat.h
 *
 * Interface of all item data.
 */
#pragma once

#include <cstdint>
#include <string_view>

#include "objdat.h"
#include "spelldat.h"

namespace devilution {

/** @todo add missing values and apply */
enum BaseItemIdx : int16_t { // This defines named constants for indexing the AllItemsList array in itemdat.cpp
// jwk - There's a bunch of missing indices in this enum but that just means the integer hasn't been named.  Basically, any item index that isn't referred by name doesn't need to be in this enum.
	IDI_GOLD,
	IDI_WARRIOR,
	IDI_WARRSHLD,
	IDI_WARRCLUB,
	IDI_ROGUE,
	IDI_SORCERER,
	IDI_CLEAVER, IDI_FIRSTQUEST = IDI_CLEAVER,
	IDI_SKCROWN,
	IDI_INFRARING,
	IDI_ROCK,
	IDI_OPTAMULET,
	IDI_TRING,
	IDI_BANNER,
	IDI_HARCREST,
	IDI_STEELVEIL,
	IDI_GLDNELIX,
	IDI_ANVIL,
	IDI_MUSHROOM,
	IDI_BRAIN,
	IDI_FUNGALTM,
	IDI_SPECELIX,
	IDI_BLDSTONE,
	IDI_MAPOFDOOM, IDI_LASTQUEST = IDI_MAPOFDOOM,
	IDI_EAR,
	IDI_HEAL,
	IDI_MANA,
	IDI_IDENTIFY,
	IDI_PORTAL,
	IDI_ARMOFVAL,
	IDI_FULLHEAL,
	IDI_FULLMANA,
	IDI_GRISWOLD,
	IDI_LGTFORGE,
	IDI_LAZSTAFF,
	IDI_RESURRECT,
	IDI_OIL,
	IDI_SHORTSTAFF,
	IDI_BARDSWORD,
	IDI_BARDDAGGER,
	IDI_RUNEBOMB,
	IDI_THEODORE,
	IDI_AURIC,
	IDI_NOTE1,
	IDI_NOTE2,
	IDI_NOTE3,
	IDI_FULLNOTE,
	IDI_BROWNSUIT,
	IDI_GREYSUIT,
	// ...missing entries...
	IDI_RINGMAIL = 62,
	IDI_CHAINMAIL,
	IDI_SCALEMAIL,
	IDI_BREASTPLATE,
	IDI_SPLINTMAIL,
	IDI_PLATEMAIL,
	IDI_FIELDPLATE,
	IDI_GOTHICPLATE,
	IDI_FULLPLATE,
	// ...missing entries...
	IDI_OILOFSMITH = 83,
	IDI_OILOFACC,
	IDI_OILOFSHARP,
	IDI_OILOF,
	IDI_ELIXSTR = 87,
	IDI_ELIXMAG,
	IDI_ELIXDEX,
	IDI_ELIXVIT,
	// ...missing entries...
	IDI_SEARCH = 92,
	// ...missing entries...
	IDI_STONECURSE = 105,
	IDI_CHAINLIGHT = 106,
	IDI_GUARDIAN = 107,
	// ...missing entries...
	IDI_NOVA = 109,
	IDI_GOLEM = 110,
	// ...missing entries...
	IDI_TELEPORT = 112,
	IDI_APOCALYPSE = 113,
	// ...missing entries...
	IDI_BOOK1 = 114,
	IDI_BOOK2,
	IDI_BOOK3,
	IDI_BOOK4,
	// ...missing entries...
	IDI_BARBARIAN = 139,
	// ...missing entries...
	IDI_RING = 156,
	// ...missing entries...
	IDI_AMULET = 159,
	// ...missing entries...
	IDI_RUNEFIRE = 161,
	IDI_RUNELIGHT,
	IDI_RUNEGRFIRE,
	IDI_RUNEGRLIGHT,
	IDI_RUNESTONE,
	IDI_SORCERER_DIABLO,
	IDI_ARENAPOT,
	IDI_NUM = 168,
	IDI_NONE = -1,
};

// Define some named constants in the ItemPrefixes[] array
enum ItemPrefix : int16_t {
	Prefix_Jesters = 83,
	Prefix_Crystalline = 84,
	Prefix_Doppelgangers = 85,
	Prefix_NUM = 86,
};

// Define some named constants in the ItemSuffixes[] array
enum ItemSuffix : int16_t {
	Suffix_Devastation = 101,
	Suffix_Decay = 102,
	Suffix_Peril = 103,
	Suffix_Casting = 104,
	Suffix_Mistakes = 105,
	Suffix_Channeling = 106,
	Suffix_Evocation = 107,
	Suffix_NUM = 108,
};

enum UniqueItemIdx : int8_t { // index into the UniqueItems[] array
// jwk - There's a bunch of missing indices in this enum but that just means the integer hasn't been named.  Basically, any item index that isn't referred by name doesn't need to be in this enum.
	UITEM_CLEAVER,
	UITEM_SKCROWN,
	UITEM_INFRARING,
	UITEM_OPTAMULET,
	UITEM_TRING,
	UITEM_HARCREST,
	UITEM_STEELVEIL,
	UITEM_ARMOFVAL,
	UITEM_GRISWOLD,
	UITEM_BOVINE,
	UITEM_RIFTBOW,
	UITEM_NEEDLER,
	UITEM_CELESTBOW,
	UITEM_DEADLYHUNT,
	UITEM_BOWOFDEAD,
	UITEM_BLKOAKBOW,
	UITEM_FLAMEDART,
	UITEM_FLESHSTING,
	UITEM_WINDFORCE,
	UITEM_EAGLEHORN,
	UITEM_GONNAGALDIRK,
	UITEM_DEFENDER,
	UITEM_GRYPHONCLAW,
	UITEM_BLACKRAZOR,
	UITEM_GIBBOUSMOON,
	UITEM_ICESHANK,
	UITEM_EXECUTIONER,
	UITEM_BONESAW,
	UITEM_SHADHAWK,
	UITEM_WIZSPIKE,
	UITEM_LGTSABRE,
	UITEM_FALCONTALON,
	UITEM_INFERNO,
	UITEM_DOOMBRINGER,
	UITEM_GRIZZLY,
	UITEM_GRANDFATHER,
	UITEM_MANGLER,
	UITEM_SHARPBEAK,
	UITEM_BLOODLSLAYER,
	UITEM_CELESTAXE,
	UITEM_WICKEDAXE,
	UITEM_STONECLEAV,
	UITEM_AGUHATCHET,
	UITEM_HELLSLAYER,
	UITEM_MESSERREAVER,
	UITEM_CRACKRUST,
	UITEM_JHOLMHAMM,
	UITEM_CIVERBS,
	UITEM_CELESTSTAR,
	UITEM_BARANSTAR,
	UITEM_GNARLROOT,
	UITEM_CRANBASH,
	UITEM_SCHAEFHAMM,
	UITEM_DREAMFLANGE,
	UITEM_STAFFOFSHAD,
	UITEM_IMMOLATOR,
	UITEM_STORMSPIRE,
	UITEM_GLEAMSONG,
	UITEM_THUNDERCALL,
	UITEM_PROTECTOR,
	UITEM_NAJPUZZLE,
	UITEM_MINDCRY,
	UITEM_RODOFONAN,
	UITEM_SPIRITSHELM,
	UITEM_THINKINGCAP,
	UITEM_OVERLORDHELM,
	UITEM_FOOLSCREST,
	UITEM_GOTTERDAM,
	UITEM_ROYCIRCLET,
	UITEM_TORNFLESH,
	UITEM_GLADBANE,
	UITEM_RAINCLOAK,
	UITEM_LEATHAUT,
	UITEM_WISDWRAP,
	UITEM_SPARKMAIL,
	UITEM_SCAVCARAP,
	UITEM_NIGHTSCAPE,
	UITEM_NAJPLATE,
	UITEM_DEMONSPIKE,
	UITEM_DEFLECTOR,
	UITEM_SKULLSHLD,
	UITEM_DRAGONBRCH,
	UITEM_BLKOAKSHLD,
	UITEM_HOLYDEF,
	UITEM_STORMSHLD,
	UITEM_BRAMBLE,
	UITEM_REGHA,
	UITEM_BLEEDER,
	UITEM_CONSTRICT,
	UITEM_ENGAGE,
	UITEM_LAST_DIABLO = UITEM_ENGAGE,
	// A bunch of missing IDs...
	UITEM_NUM = 110,
	UITEM_INVALID = -1,
};

enum item_drop_rate : uint8_t {
	IDROP_NEVER,
	IDROP_REGULAR,
	IDROP_DOUBLE,
};

enum item_class : uint8_t {
	ICLASS_NONE,
	ICLASS_WEAPON,
	ICLASS_ARMOR,
	ICLASS_MISC,
	ICLASS_GOLD,
	ICLASS_QUEST,
};

enum item_equip_type : int8_t {
	ILOC_NONE,
	ILOC_ONEHAND,
	ILOC_TWOHAND,
	ILOC_ARMOR,
	ILOC_HELM,
	ILOC_RING,
	ILOC_AMULET,
	ILOC_UNEQUIPABLE,
	ILOC_BELT,
	ILOC_INVALID = -1,
};

/// Item graphic IDs; frame_num-11 of objcurs.cel.
enum item_cursor_graphic : uint8_t {
	// clang-format off
	ICURS_POTION_OF_FULL_MANA         = 0,
	ICURS_SCROLL_OF                   = 1,
	ICURS_GOLD_SMALL                  = 4,
	ICURS_GOLD_MEDIUM                 = 5,
	ICURS_GOLD_LARGE                  = 6,
	ICURS_RING_OF_TRUTH               = 10,
	ICURS_RING                        = 12,
	ICURS_SPECTRAL_ELIXIR             = 15,
	ICURS_ARENA_POTION                = 16,
	ICURS_GOLDEN_ELIXIR               = 17,
	ICURS_EMPYREAN_BAND               = 18,
	ICURS_EAR_SORCERER                = 19,
	ICURS_EAR_WARRIOR                 = 20,
	ICURS_EAR_ROGUE                   = 21,
	ICURS_BLOOD_STONE                 = 25,
	ICURS_OIL                         = 30,
	ICURS_ELIXIR_OF_VITALITY          = 31,
	ICURS_POTION_OF_HEALING           = 32,
	ICURS_POTION_OF_FULL_REJUVENATION = 33,
	ICURS_ELIXIR_OF_MAGIC             = 34,
	ICURS_POTION_OF_FULL_HEALING      = 35,
	ICURS_ELIXIR_OF_DEXTERITY         = 36,
	ICURS_POTION_OF_REJUVENATION      = 37,
	ICURS_ELIXIR_OF_STRENGTH          = 38,
	ICURS_POTION_OF_MANA              = 39,
	ICURS_BRAIN                       = 40,
	ICURS_OPTIC_AMULET                = 44,
	ICURS_AMULET                      = 45,
	ICURS_DAGGER                      = 51,
	ICURS_BLADE                       = 56,
	ICURS_BASTARD_SWORD               = 57,
	ICURS_MACE                        = 59,
	ICURS_LONG_SWORD                  = 60,
	ICURS_BROAD_SWORD                 = 61,
	ICURS_FALCHION                    = 62,
	ICURS_MORNING_STAR                = 63,
	ICURS_SHORT_SWORD                 = 64,
	ICURS_CLAYMORE                    = 65,
	ICURS_CLUB                        = 66,
	ICURS_SABRE                       = 67,
	ICURS_SPIKED_CLUB                 = 70,
	ICURS_SCIMITAR                    = 72,
	ICURS_FULL_HELM                   = 75,
	ICURS_MAGIC_ROCK                  = 76,
	ICURS_THE_UNDEAD_CROWN            = 78,
	ICURS_HELM                        = 82,
	ICURS_BUCKLER                     = 83,
	ICURS_VIEL_OF_STEEL               = 85,
	ICURS_BOOK_GREY                   = 86,
	ICURS_BOOK_RED                    = 87,
	ICURS_BOOK_BLUE                   = 88,
	ICURS_BLACK_MUSHROOM              = 89,
	ICURS_SKULL_CAP                   = 90,
	ICURS_CAP                         = 91,
	ICURS_HARLEQUIN_CREST             = 93,
	ICURS_CROWN                       = 95,
	ICURS_MAP_OF_THE_STARS            = 96,
	ICURS_FUNGAL_TOME                 = 97,
	ICURS_GREAT_HELM                  = 98,
	ICURS_BATTLE_AXE                  = 101,
	ICURS_HUNTERS_BOW                 = 102,
	ICURS_FIELD_PLATE                 = 103,
	ICURS_SMALL_SHIELD                = 105,
	ICURS_CLEAVER                     = 106,
	ICURS_STUDDED_LEATHER_ARMOR       = 107,
	ICURS_SHORT_STAFF                 = 109,
	ICURS_TWO_HANDED_SWORD            = 110,
	ICURS_CHAIN_MAIL                  = 111,
	ICURS_SMALL_AXE                   = 112,
	ICURS_KITE_SHIELD                 = 113,
	ICURS_SCALE_MAIL                  = 114,
	ICURS_SHORT_BOW                   = 118,
	ICURS_LONG_BATTLE_BOW             = 119,
	ICURS_LONG_WAR_BOW                = 120,
	ICURS_WAR_HAMMER                  = 121,
	ICURS_MAUL                        = 122,
	ICURS_LONG_STAFF                  = 123,
	ICURS_WAR_STAFF                   = 124,
	ICURS_TAVERN_SIGN                 = 126,
	ICURS_HARD_LEATHER_ARMOR          = 127,
	ICURS_RAGS                        = 128,
	ICURS_QUILTED_ARMOR               = 129,
	ICURS_FLAIL                       = 131,
	ICURS_TOWER_SHIELD                = 132,
	ICURS_COMPOSITE_BOW               = 133,
	ICURS_GREAT_SWORD                 = 134,
	ICURS_LEATHER_ARMOR               = 135,
	ICURS_SPLINT_MAIL                 = 136,
	ICURS_ROBE                        = 137,
	ICURS_ANVIL_OF_FURY               = 140,
	ICURS_BROAD_AXE                   = 141,
	ICURS_LARGE_AXE                   = 142,
	ICURS_GREAT_AXE                   = 143,
	ICURS_AXE                         = 144,
	ICURS_LARGE_SHIELD                = 147,
	ICURS_GOTHIC_SHIELD               = 148,
	ICURS_CLOAK                       = 149,
	ICURS_CAPE                        = 150,
	ICURS_FULL_PLATE_MAIL             = 151,
	ICURS_GOTHIC_PLATE                = 152,
	ICURS_BREAST_PLATE                = 153,
	ICURS_RING_MAIL                   = 154,
	ICURS_STAFF_OF_LAZARUS            = 155,
	ICURS_ARKAINES_VALOR              = 157,
	ICURS_SHORT_WAR_BOW               = 165,
	ICURS_COMPOSITE_STAFF             = 166,
	ICURS_SHORT_BATTLE_BOW            = 167,
	ICURS_GOLD                        = 168,
	ICURS_AURIC_AMULET                = 180,
	ICURS_RUNE_BOMB                   = 187,
	ICURS_THEODORE                    = 188,
	ICURS_TORN_NOTE_1                 = 189,
	ICURS_TORN_NOTE_2                 = 190,
	ICURS_TORN_NOTE_3                 = 191,
	ICURS_RECONSTRUCTED_NOTE          = 192,
	ICURS_RUNE_OF_FIRE                = 193,
	ICURS_GREATER_RUNE_OF_FIRE        = 194,
	ICURS_RUNE_OF_LIGHTNING           = 195,
	ICURS_GREATER_RUNE_OF_LIGHTNING   = 196,
	ICURS_RUNE_OF_STONE               = 197,
	ICURS_GREY_SUIT                   = 198,
	ICURS_BROWN_SUIT                  = 199,
	ICURS_BOVINE                      = 226,
	// clang-format on
};

enum class ItemType : int8_t {
	Misc,
	Sword,
	Axe,
	Bow,
	Mace,
	Shield,
	LightArmor,
	Helm,
	MediumArmor,
	HeavyArmor,
	Staff,
	Gold,
	Ring,
	Amulet,
	None = -1,
};

std::string_view ItemTypeToString(ItemType itemType);

enum unique_base_item : int8_t {
	UITYPE_NONE,
	UITYPE_SHORTBOW,
	UITYPE_LONGBOW,
	UITYPE_HUNTBOW,
	UITYPE_COMPBOW,
	UITYPE_WARBOW,
	UITYPE_BATTLEBOW,
	UITYPE_DAGGER,
	UITYPE_FALCHION,
	UITYPE_CLAYMORE,
	UITYPE_BROADSWR,
	UITYPE_SABRE,
	UITYPE_SCIMITAR,
	UITYPE_LONGSWR,
	UITYPE_BASTARDSWR,
	UITYPE_TWOHANDSWR,
	UITYPE_GREATSWR,
	UITYPE_CLEAVER,
	UITYPE_LARGEAXE,
	UITYPE_BROADAXE,
	UITYPE_SMALLAXE,
	UITYPE_BATTLEAXE,
	UITYPE_GREATAXE,
	UITYPE_MACE,
	UITYPE_MORNSTAR,
	UITYPE_SPIKCLUB,
	UITYPE_MAUL,
	UITYPE_WARHAMMER,
	UITYPE_FLAIL,
	UITYPE_LONGSTAFF,
	UITYPE_SHORTSTAFF,
	UITYPE_COMPSTAFF,
	UITYPE_QUARSTAFF,
	UITYPE_WARSTAFF,
	UITYPE_SKULLCAP,
	UITYPE_HELM,
	UITYPE_GREATHELM,
	UITYPE_CROWN,
	UITYPE_38,
	UITYPE_RAGS,
	UITYPE_STUDARMOR,
	UITYPE_CLOAK,
	UITYPE_ROBE,
	UITYPE_CHAINMAIL,
	UITYPE_LEATHARMOR,
	UITYPE_BREASTPLATE,
	UITYPE_CAPE,
	UITYPE_PLATEMAIL,
	UITYPE_FULLPLATE,
	UITYPE_BUCKLER,
	UITYPE_SMALLSHIELD,
	UITYPE_LARGESHIELD,
	UITYPE_KITESHIELD,
	UITYPE_GOTHSHIELD,
	UITYPE_RING,
	UITYPE_55,
	UITYPE_AMULET,
	UITYPE_SKCROWN,
	UITYPE_INFRARING,
	UITYPE_OPTAMULET,
	UITYPE_TRING,
	UITYPE_HARCREST,
	UITYPE_MAPOFDOOM,
	UITYPE_ELIXIR,
	UITYPE_ARMOFVAL,
	UITYPE_STEELVEIL,
	UITYPE_GRISWOLD,
	UITYPE_LGTFORGE,
	UITYPE_LAZSTAFF,
	UITYPE_BOVINE,
	UITYPE_INVALID = -1,
};

enum class ItemSpecialEffect : uint32_t {
	// clang-format off
	None                   = 0,
	RandomStealLife        = 1 << 1,
	DrainLife              = 1 << 2,
	RandomArrowVelocity    = 1 << 3,
	FireArrows             = 1 << 4,
	LightningArrows        = 1 << 5,
	PoisonArrows           = 1 << 6,
	MultipleArrows         = 1 << 7,
	FireDamage             = 1 << 8, // fire damage on melee weapon
	LightningDamage        = 1 << 9, // lightning damage on melee weapon
	// unused
	Knockback              = 1 << 11,
	// unused
	StealMana3             = 1 << 13,
	StealMana5             = 1 << 14,
	StealLife3             = 1 << 15,
	StealLife5             = 1 << 16,
	QuickAttack            = 1 << 17,
	FastAttack             = 1 << 18,
	FasterAttack           = 1 << 19,
	FastestAttack          = 1 << 20,
	FastHitRecovery        = 1 << 21,
	FasterHitRecovery      = 1 << 22,
	FastestHitRecovery     = 1 << 23,
	FastBlock              = 1 << 24,
	FastCast               = 1 << 25, // JWK_ALLOW_FASTER_CASTING
	Thorns                 = 1 << 26,
	NoMana                 = 1 << 27,
	HalfTrapDamage         = 1 << 28,
	// unused
	TripleDemonDamage      = 1 << 30,
	ZeroResistance         = 1U << 31,
	// clang-format on
};
use_enum_as_flags(ItemSpecialEffect);

enum class ItemSpecialEffectHf : uint8_t {
	// clang-format off
	None               = 0,
	Devastation        = 1 << 0,
	Decay              = 1 << 1,
	Peril              = 1 << 2,
	Jesters            = 1 << 3,
	Doppelganger       = 1 << 4,
	ACAgainstDemons    = 1 << 5,
	ACAgainstUndead    = 1 << 6,
	// clang-format on
};
use_enum_as_flags(ItemSpecialEffectHf);

enum item_misc_id : int8_t {
	IMISC_NONE,
	IMISC_USEFIRST,
	IMISC_FULLHEAL,
	IMISC_HEAL,
	IMISC_0x4, // Unused
	IMISC_0x5, // Unused
	IMISC_MANA,
	IMISC_FULLMANA,
	IMISC_0x8, // Unused
	IMISC_0x9, // Unused
	IMISC_ELIXSTR,
	IMISC_ELIXMAG,
	IMISC_ELIXDEX,
	IMISC_ELIXVIT,
	IMISC_0xE,  // Unused
	IMISC_0xF,  // Unused
	IMISC_0x10, // Unused
	IMISC_0x11, // Unused
	IMISC_REJUV,
	IMISC_FULLREJUV,
	IMISC_USELAST,
	IMISC_SCROLL,
	IMISC_SCROLLT,
	IMISC_STAFF,
	IMISC_BOOK,
	IMISC_RING,
	IMISC_AMULET,
	IMISC_UNIQUE,
	IMISC_0x1C, // Unused
	IMISC_OILFIRST,
	IMISC_OILOF, /* oils are beta or hellfire only */
	IMISC_OILACC,
	IMISC_OILMAST,
	IMISC_OILSHARP,
	IMISC_OILDEATH,
	IMISC_OILSKILL,
	IMISC_OILBSMTH,
	IMISC_OILFORT,
	IMISC_OILPERM,
	IMISC_OILHARD,
	IMISC_OILIMP,
	IMISC_OILLAST,
	IMISC_MAPOFDOOM,
	IMISC_EAR,
	IMISC_SPECELIX,
	IMISC_0x2D, // Unused
	IMISC_RUNEFIRST,
	IMISC_RUNEF,
	IMISC_RUNEL,
	IMISC_GR_RUNEL,
	IMISC_GR_RUNEF,
	IMISC_RUNES,
	IMISC_RUNELAST,
	IMISC_AURIC,
	IMISC_NOTE,
	IMISC_ARENAPOT,
	IMISC_INVALID = -1,
};

struct BaseItemData {
	enum item_drop_rate iRnd;
	enum item_class iClass;
	enum item_equip_type iLoc;
	enum item_cursor_graphic iCurs;
	enum ItemType itype;
	enum unique_base_item iItemId;
	const char *iName;  // full name:  "Studded Leather Armor"
	const char *iSName; // short name: "Armor"  -  used instead of the full name if the full name is too long to fit in UI character limit
	uint8_t iMinMLvl; // Indicates what monster level is required to drop this item.  This is BASE ITEM level, not to be confused with level requirements for prefixes and suffixes.
	uint8_t iDurability;
	uint8_t iMinDam;
	uint8_t iMaxDam;
	uint8_t iMinAC;
	uint8_t iMaxAC;
	uint8_t iMinStr;
	uint8_t iMinMag;
	uint8_t iMinDex;
	ItemSpecialEffect iFlags; // ItemSpecialEffect as bit flags
	enum item_misc_id iMiscId;
	SpellID iSpell;
	bool iUsable;
	uint16_t iValue;
};

enum item_effect_type : int8_t { // I don't think this enum is used to index any array, so the numerical values are just for save/load compatability with vanilla game
	IPL_TOHIT,
	IPL_TOHIT_CURSE,
	IPL_DAMP,
	IPL_DAMP_CURSE,
	IPL_TOHIT_DAMP,
	IPL_TOHIT_DAMP_CURSE,
	IPL_ACP,
	IPL_ACP_CURSE,
	IPL_FIRERES,
	IPL_LIGHTRES,
	IPL_MAGICRES,
	IPL_ALLRES,
	IPL_SPLLVLADD = 14,
	IPL_CHARGES,
	IPL_FIRE_DAM, // fire damage on melee attack
	IPL_LIGHTNING_DAM, // lightning damage on melee attack
	IPL_STR = 19,
	IPL_STR_CURSE,
	IPL_MAG,
	IPL_MAG_CURSE,
	IPL_DEX,
	IPL_DEX_CURSE,
	IPL_VIT,
	IPL_VIT_CURSE,
	IPL_ATTRIBS,
	IPL_ATTRIBS_CURSE,
	IPL_GETHIT_CURSE,
	IPL_GETHIT,
	IPL_LIFE,
	IPL_LIFE_CURSE,
	IPL_MANA,
	IPL_MANA_CURSE,
	IPL_DUR,
	IPL_DUR_CURSE,
	IPL_INDESTRUCTIBLE,
	IPL_LIGHT,
	IPL_LIGHT_CURSE,
	IPL_MULT_ARROWS = 41,
	IPL_FIRE_ARROWS,
	IPL_LIGHTNING_ARROWS,
	IPL_POISON_ARROWS,
	IPL_INVCURS,
	IPL_THORNS,
	IPL_NOMANA,
	IPL_ABSHALFTRAP = 52,
	IPL_KNOCKBACK,
	IPL_STEALMANA = 55,
	IPL_STEALLIFE,
	IPL_TARGAC,
	IPL_FASTATTACK,
	IPL_FASTRECOVER,
	IPL_FASTBLOCK,
	IPL_DAMMOD,
	IPL_RNDARROWVEL,
	IPL_SETDAM,
	IPL_SETDUR,
	IPL_NOMINSTR,
	IPL_SPELL,
	IPL_ONEHAND = 68,
	IPL_3XDAMVDEM,
	IPL_ALLRESZERO,
	IPL_DRAINLIFE = 72,
	IPL_RNDSTEALLIFE,
	IPL_SETAC = 75,
	IPL_AC_CURSE = 79,
	IPL_LASTDIABLO = IPL_AC_CURSE,
	IPL_FIRERES_CURSE,
	IPL_LIGHTRES_CURSE,
	IPL_MAGICRES_CURSE,
	IPL_DEVASTATION = 84,
	IPL_DECAY,
	IPL_PERIL,
	IPL_JESTERS,
	IPL_CRYSTALLINE,
	IPL_DOPPELGANGER,
	IPL_ACDEMON,
	IPL_ACUNDEAD,
	IPL_MANATOLIFE,
	IPL_LIFETOMANA,
	IPL_FIRST_JWK,
	IPL_FASTCAST = IPL_FIRST_JWK, // JWK_ALLOW_FASTER_CASTING
	IPL_MANA_COST, // JWK_ALLOW_MANA_COST_MODIFIER
	IPL_MANA_COST_CURSE, // JWK_ALLOW_MANA_COST_MODIFIER
	IPL_INVALID = -1,
};

enum goodorevil : uint8_t {
	GOE_ANY,
	GOE_EVIL,
	GOE_GOOD,
};

enum class AffixItemType : uint8_t {
	// clang-format off
	None      = 0,
	Misc      = 1 << 0,
	Bow       = 1 << 1,
	Staff     = 1 << 2,
	Weapon    = 1 << 3,
	Shield    = 1 << 4,
	Armor     = 1 << 5,
	// clang-format on
};
use_enum_as_flags(AffixItemType);

struct ItemPower {
	item_effect_type type = IPL_INVALID;
	int param1 = 0;
	int param2 = 0;
};

struct ItemAffixData {
	const char *PLName;
	ItemPower power;
	int8_t PLMinLvl;
	AffixItemType PLIType; // AffixItemType as bit flags
	enum goodorevil PLGOE; // Good or Evil (from a lore perspective).  Not necessarily related to helpful/harmful.  Used to avoid generating items like "Angel's staff of the dark" which have both "good" and "evil" affixes
	bool PLDouble; // If true, this affix has double the chance of occuring on generated items
	bool PLOk; // True if the affix can be helpful on an item.  False if it's a harmful affix.
	bool PLDesirable; // jwk - I added this attribute to mark item affixes as "desirable" so the affix won't be skipped when the affix would otherwise be considered too low level.
	int minVal; // for determining gold value
	int maxVal; // for determining gold value
	int multVal; // for determining gold value
};

struct UniqueItem {
	const char *UIName;
	enum unique_base_item UIItemId;
	int8_t UIMinLvl;
	uint8_t UINumPL;
	int UIValue;
	ItemPower powers[6];
};

extern const std::array<BaseItemData, BaseItemIdx::IDI_NUM + 1> AllItemsList;
extern const std::array<ItemAffixData, ItemPrefix::Prefix_NUM + 1> ItemPrefixes;
extern const std::array<ItemAffixData, ItemSuffix::Suffix_NUM + 1> ItemSuffixes;
extern const std::array<UniqueItem, UniqueItemIdx::UITEM_NUM + 1> UniqueItems;

} // namespace devilution
