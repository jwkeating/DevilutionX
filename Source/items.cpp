/**
 * @file items.cpp
 *
 * Implementation of item functionality.
 */
#include "items.h"

#include <algorithm>
#include <bitset>
#ifdef _DEBUG
#include <random>
#endif
#include <climits>
#include <cstdint>

#include <fmt/core.h>
#include <fmt/format.h>

#include "DiabloUI/ui_flags.hpp"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "doom.h"
#include "engine/backbuffer_state.hpp"
#include "engine/clx_sprite.hpp"
#include "engine/dx.h"
#include "engine/load_cel.hpp"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "init.h"
#include "inv_iterators.hpp"
#include "levels/town.h"
#include "lighting.h"
#include "minitext.h"
#include "missiles.h"
#include "options.h"
#include "panels/info_box.hpp"
#include "panels/ui_panels.hpp"
#include "player.h"
#include "playerdat.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "stores.h"
#include "utils/format_int.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/math.h"
#include "utils/stdcompat/algorithm.hpp"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

#define JWK_DISABLE_ITEM_NAME_TRANSLATION 1 // This is mandatory because I changed the item generation code, and I didn't update the language translation code to match (The generators produce different items using the same random seed)
#define JWK_DONT_SKIP_DESIRABLE_ITEM_PROPERTIES 1 // In the original game, it was possible for desirable item properties ("of the heavens") to be bypassed because the suffix was too low level for the item.  I flagged derirable properties so they aren't skipped for being too low.
#define JWK_SMITH_SELLS_MORE_ITEMS 1 // spawn more premium items at the blacksmith, as in hellfire
#define JWK_HEALER_SELLS_VITALITY_ELIXIRS 1
#define JWK_USE_MODIFIED_HELLFIRE_WITCH 1 // In hellfire

namespace devilution {

Item Items[MAXITEMS + 1];
uint8_t ActiveItems[MAXITEMS];
uint8_t ActiveItemCount;
int8_t dItem[MAXDUNX][MAXDUNY];
bool ShowUniqueItemInfoBox;
CornerStoneStruct CornerStone;
std::array<bool, 256> UniqueItemFlags; // used to avoid dropping the same unique item more than once in the same game
int MaxGold = GOLD_MAX_LIMIT;

/** Maps from item_cursor_graphic to in-memory item type. */
int8_t ItemCAnimTbl[] = {
	20, 16, 16, 16, 4, 4, 4, 12, 12, 12,
	12, 12, 12, 12, 12, 21, 21, 25, 12, 28,
	28, 28, 38, 38, 38, 32, 38, 38, 38, 24,
	24, 26, 2, 25, 22, 23, 24, 21, 27, 27,
	29, 0, 0, 0, 12, 12, 12, 12, 12, 0,
	8, 8, 0, 8, 8, 8, 8, 8, 8, 6,
	8, 8, 8, 6, 8, 8, 6, 8, 8, 6,
	6, 6, 8, 8, 8, 5, 9, 13, 13, 13,
	5, 5, 5, 15, 5, 5, 18, 18, 18, 30,
	5, 5, 14, 5, 14, 13, 16, 18, 5, 5,
	7, 1, 3, 17, 1, 15, 10, 14, 3, 11,
	8, 0, 1, 7, 0, 7, 15, 7, 3, 3,
	3, 6, 6, 11, 11, 11, 31, 14, 14, 14,
	6, 6, 7, 3, 8, 14, 0, 14, 14, 0,
	33, 1, 1, 1, 1, 1, 7, 7, 7, 14,
	14, 17, 17, 17, 0, 34, 1, 0, 3, 17,
	8, 8, 6, 1, 3, 3, 11, 3, 12, 12,
	12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	12, 12, 12, 12, 12, 12, 12, 35, 39, 36,
	36, 36, 37, 38, 38, 38, 38, 38, 41, 42,
	8, 8, 8, 17, 0, 6, 8, 11, 11, 3,
	3, 1, 6, 6, 6, 1, 8, 6, 11, 3,
	6, 8, 1, 6, 6, 17, 40, 0, 0
};

/** Maps of drop sounds effect of placing the item in the inventory. */
_sfx_id ItemInvSnds[] = {
	IS_IHARM,
	IS_IAXE,
	IS_IPOT,
	IS_IBOW,
	IS_GOLD,
	IS_ICAP,
	IS_ISWORD,
	IS_ISHIEL,
	IS_ISWORD,
	IS_IROCK,
	IS_IAXE,
	IS_ISTAF,
	IS_IRING,
	IS_ICAP,
	IS_ILARM,
	IS_ISHIEL,
	IS_ISCROL,
	IS_IHARM,
	IS_IBOOK,
	IS_IHARM,
	IS_IPOT,
	IS_IPOT,
	IS_IPOT,
	IS_IPOT,
	IS_IPOT,
	IS_IPOT,
	IS_IPOT,
	IS_IPOT,
	IS_IBODY,
	IS_IBODY,
	IS_IMUSH,
	IS_ISIGN,
	IS_IBLST,
	IS_IANVL,
	IS_ISTAF,
	IS_IROCK,
	IS_ISCROL,
	IS_ISCROL,
	IS_IROCK,
	IS_IMUSH,
	IS_IHARM,
	IS_ILARM,
	IS_ILARM,
};

static OptionalOwnedClxSpriteList itemanims[ITEMTYPES];

enum class PlayerArmorGraphic : uint8_t {
	// clang-format off
	Light  = 0,
	Medium = 1 << 4,
	Heavy  = 1 << 5,
	// clang-format on
};

static Item curruitem;

/** Holds item get records, tracking items being recently looted. This is in an effort to prevent items being picked up more than once. */
static ItemGetRecordStruct itemrecord[MAXITEMS];

static bool itemhold[3][3];

/** Specifies the number of active item get records. */
static int gnNumGetRecords;

static int OilLevels[] = { 1, 10, 1, 10, 4, 1, 5, 17, 1, 10 };
static int OilValues[] = { 500, 2500, 500, 2500, 1500, 100, 2500, 15000, 500, 2500 };
static item_misc_id OilMagic[] = {
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
};
static char OilNames[10][25] = {
	N_("Oil of Accuracy"),
	N_("Oil of Mastery"),
	N_("Oil of Sharpness"),
	N_("Oil of Death"),
	N_("Oil of Skill"),
	N_("Blacksmith Oil"),
	N_("Oil of Fortitude"),
	N_("Oil of Permanence"),
	N_("Oil of Hardening"),
	N_("Oil of Imperviousness")
};

/** Map of item type .cel file names. */
static const char *const ItemDropNames[] = {
	"armor2",
	"axe",
	"fbttle",
	"bow",
	"goldflip",
	"helmut",
	"mace",
	"shield",
	"swrdflip",
	"rock",
	"cleaver",
	"staff",
	"ring",
	"crownf",
	"larmor",
	"wshield",
	"scroll",
	"fplatear",
	"fbook",
	"food",
	"fbttlebb",
	"fbttledy",
	"fbttleor",
	"fbttlebr",
	"fbttlebl",
	"fbttleby",
	"fbttlewh",
	"fbttledb",
	"fear",
	"fbrain",
	"fmush",
	"innsign",
	"bldstn",
	"fanvil",
	"flazstaf",
	"bombs1",
	"halfps1",
	"wholeps1",
	"runes1",
	"teddys1",
	"cows1",
	"donkys1",
	"mooses1",
};
/** Maps of item drop animation length. */
static int8_t ItemAnimLs[] = {
	15,
	13,
	16,
	13,
	10,
	13,
	13,
	13,
	13,
	10,
	13,
	13,
	13,
	13,
	13,
	13,
	13,
	13,
	13,
	1,
	16,
	16,
	16,
	16,
	16,
	16,
	16,
	16,
	13,
	12,
	12,
	13,
	13,
	13,
	8,
	10,
	16,
	16,
	10,
	10,
	15,
	15,
	15,
};
/** Maps of drop sounds effect of dropping the item on ground. */
static _sfx_id ItemDropSnds[] = {
	IS_FHARM,
	IS_FAXE,
	IS_FPOT,
	IS_FBOW,
	IS_GOLD,
	IS_FCAP,
	IS_FSWOR,
	IS_FSHLD,
	IS_FSWOR,
	IS_FROCK,
	IS_FAXE,
	IS_FSTAF,
	IS_FRING,
	IS_FCAP,
	IS_FLARM,
	IS_FSHLD,
	IS_FSCRL,
	IS_FHARM,
	IS_FBOOK,
	IS_FLARM,
	IS_FPOT,
	IS_FPOT,
	IS_FPOT,
	IS_FPOT,
	IS_FPOT,
	IS_FPOT,
	IS_FPOT,
	IS_FPOT,
	IS_FBODY,
	IS_FBODY,
	IS_FMUSH,
	IS_FSIGN,
	IS_FBLST,
	IS_FANVL,
	IS_FSTAF,
	IS_FROCK,
	IS_FSCRL,
	IS_FSCRL,
	IS_FROCK,
	IS_FMUSH,
	IS_FHARM,
	IS_FLARM,
	IS_FLARM,
};
/** Maps from Griswold premium item number to a quality level delta as added to the base quality level. */
static int premiumlvladd[] = {
	// clang-format off
	-1,
	-1,
	 0,
	 0,
	 1,
	 2,
	// clang-format on
};
/** Maps from Griswold premium item number to a quality level delta as added to the base quality level. */
static int premiumLvlAddHellfire[] = {
	// clang-format off
	-1,
	-1,
	-1,
	 0,
	 0,
	 0,
	 0,
	 1,
	 1,
	 1,
	 1,
	 2,
	 2,
	 3,
	 3,
	// clang-format on
};

static bool IsPrefixValidForItemType(int i, AffixItemType flgs, bool hellfireItem)
{
	AffixItemType itemTypes = ItemPrefixes[i].PLIType;

	if (!hellfireItem) {
		if (i >= 83 && i <= 85)
			return false;

		if (i >= 12 && i <= 20)
			itemTypes &= ~AffixItemType::Staff;
	}

	return HasAnyOf(flgs, itemTypes);
}

static bool IsSuffixValidForItemType(int i, AffixItemType flgs, bool hellfireItem)
{
	AffixItemType itemTypes = ItemSuffixes[i].PLIType;

	if (!JWK_ALLOW_FASTER_CASTING && i == 98)
		return false;

	if (!JWK_ALLOW_MANA_COST_MODIFIER && i >= 99 && i <= 101)
		return false;

	if (!hellfireItem) {
		if (i >= 95 && i <= 97) {
			return false;
		}
		if ((i >=  0 && i <=  1)
         || (i >= 14 && i <= 15)
         || (i >= 21 && i <= 22)
         || (i >= 34 && i <= 36)
         || (i >= 41 && i <= 44)
         || (i >= 60 && i <= 63)) {
			itemTypes &= ~AffixItemType::Staff;
		}
	}

	return HasAnyOf(flgs, itemTypes);
}

static unsigned int GetCurrentLevelForDrops()
{
	if (setlevel) {
		switch (setlvlnum) {
		case SL_SKELKING:
			return Quests[Q_SKELKING]._qlevel;
		case SL_BONECHAMB:
			return Quests[Q_SCHAMB]._qlevel;
		case SL_POISONWATER:
			return Quests[Q_PWATER]._qlevel;
		case SL_VILEBETRAYER:
			return Quests[Q_BETRAYER]._qlevel;
		default:
			return 1;
		}
	}
	else if (leveltype == DTYPE_NEST) {
		assert(currlevel >= 8);
		return currlevel - 8;
	} else if (leveltype == DTYPE_CRYPT) {
		assert(currlevel >= 7);
		return currlevel - 7;
	} else {
		return currlevel;
	}
}
static unsigned int GetCurrentBaseItemLevelForDrops()
{
	// Base items don't require a high level.  Full plate has iMinMLvl=25 in vanilla game.
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	// We should allow better base items to drop earlier on higher difficulty but for lore reasons, we don't want the cathedral dropping full plate and great axes.
	return GetCurrentLevelForDrops() * 2 + sgGameInitInfo.nDifficulty * 4; // In normal difficulty, full plate drops on dungeon level 13+ because 13*2 + 0 >= 25.  On hell difficulty, full plate drops on dungeon level 9+ because 9*2 + 8 >= 25
#else // original code
	return GetCurrentLevelForDrops() * 2;
#endif
}
static unsigned int GetCurrentAffixLevelForDrops()
{
	// Affixes require higher level (up to level 60 in vanilla game)
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	return GetCurrentLevelForDrops() * 2 + sgGameInitInfo.nDifficulty * 15; // This allows affix level 60 on dungeon level 15 in hell difficulty because 15*2 + 2*15 = 60
#else // original code
	return GetCurrentLevelForDrops() * 2; // In vanilla Diablo, this only goes up to 16*2=32
#endif
}

static bool ItemPlace(Point position)
{
	if (dMonster[position.x][position.y] != 0)
		return false;
	if (dPlayer[position.x][position.y] != 0)
		return false;
	if (dItem[position.x][position.y] != 0)
		return false;
	if (IsObjectAtPosition(position))
		return false;
	if (TileContainsSetPiece(position))
		return false;
	if (IsTileSolid(position))
		return false;

	return true;
}

static Point GetRandomAvailableItemPosition()
{
	Point position = {};
	do {
		position = Point { static_cast<int32_t>(GenerateRnd(80)), static_cast<int32_t>(GenerateRnd(80)) } + Displacement { 16, 16 };
	} while (!ItemPlace(position));

	return position;
}

static void AddInitItems()
{
	int curlv = GetCurrentBaseItemLevelForDrops();
	int rnd = GenerateRnd(3) + 3;
	for (int j = 0; j < rnd; j++) {
		int ii = AllocateItem();
		auto &item = Items[ii];

		Point position = GetRandomAvailableItemPosition();
		item.position = position;

		dItem[position.x][position.y] = ii + 1;

		item._iSeed = AdvanceRndSeed();
		SetRndSeed(item._iSeed);

		GenerateRandomPropertiesForBaseItem(item, PickRandomlyAmong({ IDI_MANA, IDI_HEAL }), curlv);

		item._iCreateInfo = (curlv & CF_LEVEL) | CF_PREGEN;
		SetupItem(item);
		item.AnimInfo.currentFrame = item.AnimInfo.numberOfFrames - 1;
		item._iAnimFlag = false;
		item._iSelFlag = 1;
		DeltaAddItem(ii);
	}
}

static void SpawnNote()
{
	BaseItemIdx id;

	switch (currlevel) {
	case 22:
		id = IDI_NOTE2;
		break;
	case 23:
		id = IDI_NOTE3;
		break;
	default:
		id = IDI_NOTE1;
		break;
	}

	Point position = GetRandomAvailableItemPosition();
	SpawnQuestItem(id, position, 0, 1, false);
}

static void UnequipGearWhichCantBeWorn(Player &player)
{
	int sa = 0;
	int ma = 0;
	int da = 0;

	// first iteration is used for collecting stat bonuses from items
	for (Item &equipment : EquippedPlayerItemsRange(player)) {
		equipment._iStatFlag = true;
		if (equipment._iIdentified) {
			sa += equipment._iPLStr;
			ma += equipment._iPLMag;
			da += equipment._iPLDex;
		}
	}
	sa += JWK_GOD_MODE_ADJUST_STR_BY_AMOUNT;
	ma += JWK_GOD_MODE_ADJUST_MAG_BY_AMOUNT;
	da += JWK_GOD_MODE_ADJUST_DEX_BY_AMOUNT;

	bool changeflag;
	do {
		// cap stats to 0
		const int currstr = std::max(0, sa + player._pBaseStr);
		const int currmag = std::max(0, ma + player._pBaseMag);
		const int currdex = std::max(0, da + player._pBaseDex);

		changeflag = false;
		// Iterate over equipped items and remove stat bonuses if they are not valid
		for (Item &equipment : EquippedPlayerItemsRange(player)) {
			if (!equipment._iStatFlag)
				continue;

			bool isValid = IsItemValid(equipment);

			if (currstr < equipment._iMinStr
			    || currmag < equipment._iMinMag
			    || currdex < equipment._iMinDex)
				isValid = false;

			if (isValid)
				continue;

			changeflag = true;
			equipment._iStatFlag = false;
			if (equipment._iIdentified) {
				sa -= equipment._iPLStr;
				ma -= equipment._iPLMag;
				da -= equipment._iPLDex;
			}
		}
	} while (changeflag);
}

// Look for a free spot on the floor immediately next to the player
static bool GetItemSpace(Point position, int8_t inum)
{
	int xx = 0;
	int yy = 0;
	for (int j = position.y - 1; j <= position.y + 1; j++) {
		xx = 0;
		for (int i = position.x - 1; i <= position.x + 1; i++) {
			itemhold[xx][yy] = IsItemSpaceOk({ i, j });
			xx++;
		}
		yy++;
	}

	bool savail = false;
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) { // NOLINT(modernize-loop-convert)
			if (itemhold[i][j])
				savail = true;
		}
	}

	int rs = GenerateRnd(15) + 1;

	if (!savail)
		return false;

	xx = 0;
	yy = 0;
	while (rs > 0) {
		if (itemhold[xx][yy])
			rs--;
		if (rs <= 0)
			continue;
		xx++;
		if (xx != 3)
			continue;
		xx = 0;
		yy++;
		if (yy == 3)
			yy = 0;
	}

	xx += position.x - 1;
	yy += position.y - 1;
	Items[inum].position = { xx, yy };
	dItem[xx][yy] = inum + 1;

	return true;
}

// look for a free spot on the floor in a wide area around the player
static void GetSuperItemSpace(Point position, int8_t inum)
{
	Point positionToCheck = position;
	if (GetItemSpace(positionToCheck, inum))
		return;
	for (int k = 2; k < 50; k++) {
		for (int j = -k; j <= k; j++) {
			for (int i = -k; i <= k; i++) {
				Displacement offset = { i, j };
				positionToCheck = position + offset;
				if (!IsItemSpaceOk(positionToCheck))
					continue;
				Items[inum].position = positionToCheck;
				dItem[positionToCheck.x][positionToCheck.y] = inum + 1;
				return;
			}
		}
	}
}

static void CalcItemValue(Item &item)
{
#if 1 // jwk - Simplify the code.  Starting with base item value from AllItemsList[], modify the item value based on prefix multiplier/add and suffix multiplier/add
	int multiplier = item._iVMult1 + item._iVMult2;
	if (multiplier > 0) {
		item._iIvalue *= multiplier;
	} else {
		item._iIvalue = item._iIvalue / std::max(1, abs(multiplier)); // large negative multiplier decreases base item value
	}
	item._iIvalue += item._iVAdd1 + item._iVAdd2;
	item._iIvalue = std::max(1, item._iIvalue);
#if JWK_INCREASE_VALUE_OF_STAFF_CHARGES_IF_SPELL_CANT_BE_LEARNED
	if (item._iMaxCharges > 0) {
		if (GetSpellBookLevel(item._iSpell, true) < 0) { // then spell can't be learned
			int normalMaxCharges = GetSpellData(item._iSpell).sStaffMax;
			if (item._iMaxCharges > normalMaxCharges) { // then staff has bonus charges
				// Plentiful and Bountiful prefixes already multiply the staff's value (including value from charges) via _iVMult1 and _iVMult2 but we want an additional factor if the spell can't be learned
				item._iIvalue = (item._iIvalue * item._iMaxCharges) / normalMaxCharges;
			}
		}
	}
#endif
#else // original code
	int v = item._iVMult1 + item._iVMult2;
	if (v > 0) {
		v *= item._ivalue;
	}
	if (v < 0) {
		v = item._ivalue / v; // This always sets v=0.  Maybe a bug?
	}
	v = item._iVAdd1 + item._iVAdd2 + v;
	item._iIvalue = std::max(v, 1);
#endif
}

static SpellID SelectRandomSpell(int maxLevel, bool onlyLearnable, bool decreaseChanceOfHigherSpells)
{
	int maxSpells = gbIsHellfire ? MAX_SPELLS : static_cast<int>(SpellID::LastDiablo);

#if 1 // jwk - simplify choosing a random spell
	std::array<SpellID, MAX_SPELLS> allSpells;
	int numChoices = 0;
	for (int s = 1; s <= maxSpells; s++) {
		int sLevel = GetSpellBookLevel(static_cast<SpellID>(s), onlyLearnable);
		if (sLevel < 0 || sLevel > maxLevel)
			continue;
		if (!gbIsMultiplayer && (s == static_cast<int8_t>(SpellID::Resurrect) || s == static_cast<int8_t>(SpellID::HealOther)))
			continue;
		if (decreaseChanceOfHigherSpells && GenerateRnd(sLevel / 6 + 1) != 0)
			continue;
		allSpells[numChoices++] = static_cast<SpellID>(s);
	}
	SpellID chosenSpell = (numChoices > 0) ? allSpells[GenerateRnd(numChoices)] : SpellID::Firebolt;
#else // original code (convoluted logic, can cause infinite loops)
	int rv = GenerateRnd(maxSpells) + 1;
	int s = static_cast<int8_t>(SpellID::Firebolt);
	SpellID chosenSpell = SpellID::Firebolt;
	while (rv > 0) {
		int sLevel = GetSpellBookLevel(static_cast<SpellID>(s), onlyLearnable);
		if (sLevel != -1 && maxLevel >= sLevel) {
			rv--;
			chosenSpell = static_cast<SpellID>(s);
		}
		s++;
		if (!gbIsMultiplayer) {
			if (s == static_cast<int8_t>(SpellID::Resurrect))
				s = static_cast<int8_t>(SpellID::Telekinesis);
		}
		if (!gbIsMultiplayer) {
			if (s == static_cast<int8_t>(SpellID::HealOther))
				s = static_cast<int8_t>(SpellID::BloodStar);
		}
		if (s == maxSpells)
			s = 1;
	}
#endif
	return chosenSpell;
}

static void SelectRandomSpellBook(Item &item, int maxLevel, bool decreaseChanceOfHigherSpells)
{
	if (maxLevel <= 0)
		maxLevel = 1;
	if (gbIsDemoGame && maxLevel > 5)
		maxLevel = 5;

	SpellID chosenSpell = SelectRandomSpell(maxLevel, true, decreaseChanceOfHigherSpells);

	const string_view spellName = GetSpellData(chosenSpell).sNameText;
	const size_t iNameLen = string_view(item._iName).size();
	const size_t iINameLen = string_view(item._iIName).size();
	CopyUtf8(item._iName + iNameLen, spellName, sizeof(item._iName) - iNameLen);
	CopyUtf8(item._iIName + iINameLen, spellName, sizeof(item._iIName) - iINameLen);
	item._iSpell = chosenSpell;
	const SpellData &spellData = GetSpellData(chosenSpell);
	item._iMinMag = spellData.minInt;
	item._ivalue += spellData.bookCost();
	item._iIvalue += spellData.bookCost();
	switch (spellData.type()) {
	case MagicType::Fire:
		item._iCurs = ICURS_BOOK_RED;
		break;
	case MagicType::Lightning:
		item._iCurs = ICURS_BOOK_BLUE;
		break;
	case MagicType::Magic:
		item._iCurs = ICURS_BOOK_GREY;
		break;
	}
}

static int CalculateDamageMultiplier(int low, int high)
{
#if JWK_BUFF_DAMAGE_MULTIPIER_PREFIX
	// use 3/2 multiplier because this is the same ratio of +To Hit that "Strange" weapons have over "King's" weapons
	return GenerateRndInRange(low * 3 / 2, high * 3 / 2);
#else // original code
	return GenerateRndInRange(low, high);
#endif
}

static int CalculateToHitBonus(int level)
{
	// Give "King's" item prefix a slightly lower +To Hit bonus than an equivalent "Strange" item prefix because "King's" also gives bonus damage
	switch (level) {
	case -50:
		return -GenerateRndInRange(6, 10);
	case -25:
		return -GenerateRndInRange(1, 5);
	case 20:
		return GenerateRndInRange(1, 5);
	case 36:
		return GenerateRndInRange(6, 10);
	case 51:
		return GenerateRndInRange(11, 15);
	case 66:
		return GenerateRndInRange(16, 20);
	case 81:
		return GenerateRndInRange(21, 30);
	case 96:
		return GenerateRndInRange(31, 40);
	case 111:
		return GenerateRndInRange(41, 50);
	case 126:
		return GenerateRndInRange(51, 75);
	case 151:
		return GenerateRndInRange(76, 100);
	default:
		app_fatal("Unknown to hit bonus");
	}
}

// Generate the derived stats associated with an ItemPower, and add these stats to the item.
static int AddItemPowerToItem(Item &item, ItemPower &power)
{
	if (!gbIsHellfire) {
		if (power.type == IPL_TARGAC) {
			power.param1 = 1 << power.param1;
			power.param2 = 3 << power.param2;
		}
	}
	power.param1 = std::max(power.param1, 0);
	power.param2 = std::max(power.param2, power.param1);
	int r = GenerateRndInRange(power.param1, power.param2);

	switch (power.type) {
	case IPL_TOHIT:
		item._iPLToHit += r;
		break;
	case IPL_TOHIT_CURSE:
		item._iPLToHit -= r;
		break;
	case IPL_DAMP:
		item._iPLDam += CalculateDamageMultiplier(power.param1, power.param2);
		break;
	case IPL_DAMP_CURSE:
		item._iPLDam -= r;
		break;
	case IPL_DOPPELGANGER:
		item._iDamAcFlags |= ItemSpecialEffectHf::Doppelganger;
		[[fallthrough]];
	case IPL_TOHIT_DAMP:
		item._iPLDam += r;
		item._iPLToHit += CalculateToHitBonus(power.param1);
		break;
	case IPL_TOHIT_DAMP_CURSE:
		item._iPLDam -= r;
		item._iPLToHit += CalculateToHitBonus(-power.param1);
		break;
	case IPL_ACP:
		item._iPLAC += r;
		break;
	case IPL_ACP_CURSE:
		item._iPLAC -= r;
		break;
	case IPL_SETAC:
		item._iAC = r;
		break;
	case IPL_AC_CURSE:
		item._iAC -= r;
		break;
	case IPL_FIRERES:
		item._iPLFR += r;
		break;
	case IPL_LIGHTRES:
		item._iPLLR += r;
		break;
	case IPL_MAGICRES:
		item._iPLMR += r;
		break;
	case IPL_ALLRES:
		item._iPLFR = std::max(item._iPLFR + r, 0);
		item._iPLLR = std::max(item._iPLLR + r, 0);
		item._iPLMR = std::max(item._iPLMR + r, 0);
		break;
	case IPL_SPLLVLADD:
		item._iSplLvlAdd = r;
		break;
	case IPL_CHARGES:
		item._iCharges *= power.param1;
		item._iMaxCharges = item._iCharges;
		break;
	case IPL_SPELL:
		item._iSpell = static_cast<SpellID>(power.param1);
		item._iCharges = power.param2;
		item._iMaxCharges = power.param2;
		break;
	case IPL_FIREDAM:
		item._iFlags |= ItemSpecialEffect::FireDamage;
		item._iFlags &= ~ItemSpecialEffect::LightningDamage;
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 0;
		item._iLMaxDam = 0;
		break;
	case IPL_LIGHTDAM:
		item._iFlags |= ItemSpecialEffect::LightningDamage;
		item._iFlags &= ~ItemSpecialEffect::FireDamage;
		item._iLMinDam = power.param1;
		item._iLMaxDam = power.param2;
		item._iFMinDam = 0;
		item._iFMaxDam = 0;
		break;
	case IPL_STR:
		item._iPLStr += r;
		break;
	case IPL_STR_CURSE:
		item._iPLStr -= r;
		break;
	case IPL_MAG:
		item._iPLMag += r;
		break;
	case IPL_MAG_CURSE:
		item._iPLMag -= r;
		break;
	case IPL_DEX:
		item._iPLDex += r;
		break;
	case IPL_DEX_CURSE:
		item._iPLDex -= r;
		break;
	case IPL_VIT:
		item._iPLVit += r;
		break;
	case IPL_VIT_CURSE:
		item._iPLVit -= r;
		break;
	case IPL_ATTRIBS:
		item._iPLStr += r;
		item._iPLMag += r;
		item._iPLDex += r;
		item._iPLVit += r;
		break;
	case IPL_ATTRIBS_CURSE:
		item._iPLStr -= r;
		item._iPLMag -= r;
		item._iPLDex -= r;
		item._iPLVit -= r;
		break;
	case IPL_GETHIT_CURSE:
		item._iPLGetHit += r;
		break;
	case IPL_GETHIT:
		item._iPLGetHit -= r;
		break;
	case IPL_LIFE:
		item._iPLHP += r << 6;
		break;
	case IPL_LIFE_CURSE:
		item._iPLHP -= r << 6;
		break;
	case IPL_MANA:
		item._iPLMana += r << 6;
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_MANA_CURSE:
		item._iPLMana -= r << 6;
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_DUR: {
		int bonus = r * item._iMaxDur / 100;
		item._iMaxDur += bonus;
		item._iDurability += bonus;
	} break;
	case IPL_CRYSTALLINE:
		item._iPLDam += 140 + CalculateDamageMultiplier(power.param1, power.param2) * 2;
		[[fallthrough]];
	case IPL_DUR_CURSE:
		item._iMaxDur -= r * item._iMaxDur / 100;
		item._iMaxDur = std::max<uint8_t>(item._iMaxDur, 1);
		item._iDurability = item._iMaxDur;
		break;
	case IPL_INDESTRUCTIBLE:
		item._iDurability = DUR_INDESTRUCTIBLE;
		item._iMaxDur = DUR_INDESTRUCTIBLE;
		break;
	case IPL_LIGHT:
		item._iPLLight += power.param1;
		break;
	case IPL_LIGHT_CURSE:
		item._iPLLight -= power.param1;
		break;
#if JWK_ALLOW_MANA_COST_MODIFIER
	case IPL_MANA_COST:
		item._iPLManaCostMod -= r;
		break;
	case IPL_MANA_COST_CURSE:
		item._iPLManaCostMod += r;
		break;
#endif
	case IPL_MULT_ARROWS:
		item._iFlags |= ItemSpecialEffect::MultipleArrows;
		break;
	case IPL_FIRE_ARROWS:
		item._iFlags |= ItemSpecialEffect::FireArrows;
		item._iFlags &= ~ItemSpecialEffect::LightningArrows;
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 0;
		item._iLMaxDam = 0;
		break;
	case IPL_LIGHT_ARROWS:
		item._iFlags |= ItemSpecialEffect::LightningArrows;
		item._iFlags &= ~ItemSpecialEffect::FireArrows;
		item._iLMinDam = power.param1;
		item._iLMaxDam = power.param2;
		item._iFMinDam = 0;
		item._iFMaxDam = 0;
		break;
	case IPL_FIREBALL:
		item._iFlags |= (ItemSpecialEffect::LightningArrows | ItemSpecialEffect::FireArrows);
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 0;
		item._iLMaxDam = 0;
		break;
	case IPL_THORNS:
		item._iFlags |= ItemSpecialEffect::Thorns;
		break;
	case IPL_NOMANA:
		item._iFlags |= ItemSpecialEffect::NoMana;
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_ABSHALFTRAP:
		item._iFlags |= ItemSpecialEffect::HalfTrapDamage;
		break;
	case IPL_KNOCKBACK:
		item._iFlags |= ItemSpecialEffect::Knockback;
		break;
	case IPL_3XDAMVDEM:
		item._iFlags |= ItemSpecialEffect::TripleDemonDamage;
		break;
	case IPL_ALLRESZERO:
		item._iFlags |= ItemSpecialEffect::ZeroResistance;
		break;
	case IPL_STEALMANA:
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::StealMana3;
		if (power.param1 == 5)
			item._iFlags |= ItemSpecialEffect::StealMana5;
		RedrawComponent(PanelDrawComponent::Mana);
		break;
	case IPL_STEALLIFE:
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::StealLife3;
		if (power.param1 == 5)
			item._iFlags |= ItemSpecialEffect::StealLife5;
		RedrawComponent(PanelDrawComponent::Health);
		break;
	case IPL_TARGAC:
		if (gbIsHellfire)
			item._iPLArmorPierce = power.param1;
		else
			item._iPLArmorPierce += r;
		break;
	case IPL_FASTATTACK:
		if (power.param1 == 1)
			item._iFlags |= ItemSpecialEffect::QuickAttack;
		if (power.param1 == 2)
			item._iFlags |= ItemSpecialEffect::FastAttack;
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::FasterAttack;
		if (power.param1 == 4)
			item._iFlags |= ItemSpecialEffect::FastestAttack;
		break;
	case IPL_FASTRECOVER:
		if (power.param1 == 1)
			item._iFlags |= ItemSpecialEffect::FastHitRecovery;
		if (power.param1 == 2)
			item._iFlags |= ItemSpecialEffect::FasterHitRecovery;
		if (power.param1 == 3)
			item._iFlags |= ItemSpecialEffect::FastestHitRecovery;
		break;
	case IPL_FASTBLOCK:
		item._iFlags |= ItemSpecialEffect::FastBlock;
		break;
#if JWK_ALLOW_FASTER_CASTING
	case IPL_FASTCAST:
		item._iFlags |= ItemSpecialEffect::FastCast;
		break;
#endif
	case IPL_DAMMOD:
		item._iPLDamMod += r;
		break;
	case IPL_RNDARROWVEL:
		item._iFlags |= ItemSpecialEffect::RandomArrowVelocity;
		break;
	case IPL_SETDAM:
		item._iMinDam = power.param1;
		item._iMaxDam = power.param2;
		break;
	case IPL_SETDUR:
		item._iDurability = power.param1;
		item._iMaxDur = power.param1;
		break;
	case IPL_ONEHAND:
		item._iLoc = ILOC_ONEHAND;
		break;
	case IPL_DRAINLIFE:
		item._iFlags |= ItemSpecialEffect::DrainLife;
		break;
	case IPL_RNDSTEALLIFE:
		item._iFlags |= ItemSpecialEffect::RandomStealLife;
		break;
	case IPL_NOMINSTR:
		item._iMinStr = 0;
		break;
	case IPL_INVCURS:
		item._iCurs = power.param1;
		break;
	case IPL_ADDACLIFE:
		item._iFlags |= (ItemSpecialEffect::LightningArrows | ItemSpecialEffect::FireArrows);
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 1;
		item._iLMaxDam = 0;
		break;
	case IPL_ADDMANAAC:
		item._iFlags |= (ItemSpecialEffect::LightningDamage | ItemSpecialEffect::FireDamage);
		item._iFMinDam = power.param1;
		item._iFMaxDam = power.param2;
		item._iLMinDam = 2;
		item._iLMaxDam = 0;
		break;
	case IPL_FIRERES_CURSE:
		item._iPLFR -= r;
		break;
	case IPL_LIGHTRES_CURSE:
		item._iPLLR -= r;
		break;
	case IPL_MAGICRES_CURSE:
		item._iPLMR -= r;
		break;
	case IPL_DEVASTATION:
		item._iDamAcFlags |= ItemSpecialEffectHf::Devastation;
		break;
	case IPL_DECAY:
		item._iDamAcFlags |= ItemSpecialEffectHf::Decay;
		item._iPLDam += CalculateDamageMultiplier(power.param1, power.param2);
		break;
	case IPL_PERIL:
		item._iDamAcFlags |= ItemSpecialEffectHf::Peril;
		break;
	case IPL_JESTERS:
		item._iDamAcFlags |= ItemSpecialEffectHf::Jesters;
		break;
	case IPL_ACDEMON:
		item._iDamAcFlags |= ItemSpecialEffectHf::ACAgainstDemons;
		break;
	case IPL_ACUNDEAD:
		item._iDamAcFlags |= ItemSpecialEffectHf::ACAgainstUndead;
		break;
#if 1
		// jwk - TODO: Implement IPL_MANATOLIFE and IPL_LIFETOMANA after I removed "const Player &player" from the function arguments.
		// Instead of modifying _iPLMana and _iPLHP, there could be a property on the item "convert health to mana" which is true/false.
		// Then any player who equips the item computes their mana/health changes in CalcPlayerPowerFromItems().  This is how all other item properties work.
#else // original code
	case IPL_MANATOLIFE: {
		int portion = ((player._pMaxManaBase >> 6) * 50 / 100) << 6;
		item._iPLMana -= portion;
		item._iPLHP += portion;
	} break;
	case IPL_LIFETOMANA: {
		int portion = ((player._pMaxHPBase >> 6) * 40 / 100) << 6;
		item._iPLHP -= portion;
		item._iPLMana += portion;
	} break;
#endif
	default:
		break;
	}

	return r;
}

static bool StringInPanel(const char *str)
{
	return GetLineWidth(str, GameFont12, 2) < 254;
}

static void AddAffixToItem(Item &item, const ItemAffixData &affix)
{
	ItemPower power = affix.power;
	int randomPowerIntensity = AddItemPowerToItem(item, power);

	// For computing item gold value, we need to record the value of the item affix we just added to the item.
	// If "of the zodiac" has power range of +16 to +20, obtain the value of +18 by interpolation:
	int valueAdded = affix.minVal;
	if (power.param2 > power.param1 && affix.maxVal > affix.minVal) {
		valueAdded = affix.minVal + (affix.maxVal - affix.minVal) * (100 * (randomPowerIntensity - power.param1) / (power.param2 - power.param1)) / 100;
	}

	// Item potentially has two value modifiers (prefix and suffix)
	if (item._iVAdd1 != 0 || item._iVMult1 != 0) {
		item._iVAdd2 = valueAdded;
		item._iVMult2 = affix.multVal;
	} else {
		item._iVAdd1 = valueAdded;
		item._iVMult1 = affix.multVal;
	}
}

static std::string GetStaffName(const BaseItemData &baseItemData, SpellID spellId, bool translate)
{
	string_view baseName = translate ? _(baseItemData.iName) : baseItemData.iName;
	string_view spellName = translate ? pgettext("spell", GetSpellData(spellId).sNameText) : GetSpellData(spellId).sNameText;
	string_view normalFmt = translate ? pgettext("spell", /* TRANSLATORS: Constructs item names. Format: {Item} of {Spell}. Example: War Staff of Firewall */ "{0} of {1}") : "{0} of {1}";
	std::string name = fmt::format(fmt::runtime(normalFmt), baseName, spellName);
	if (!StringInPanel(name.c_str())) {
		string_view shortName = translate ? _(baseItemData.iSName) : baseItemData.iSName;
		name = fmt::format(fmt::runtime(normalFmt), shortName, spellName);
	}
	return name;
}

static std::string GetIdentifiedStaffName(const BaseItemData &baseItemData, SpellID spellId, int preidx, bool translate, std::optional<bool> forceNameLengthCheck)
{
	string_view baseName = translate ? _(baseItemData.iName) : baseItemData.iName;
	string_view magicFmt = translate ? pgettext("spell", /* TRANSLATORS: Constructs item names. Format: {Prefix} {Item} of {Spell}. Example: King's War Staff of Firewall */ "{0} {1} of {2}") : "{0} {1} of {2}";
	string_view spellName = translate ? pgettext("spell", GetSpellData(spellId).sNameText) : GetSpellData(spellId).sNameText;
	string_view prefixName = translate ? _(ItemPrefixes[preidx].PLName) : ItemPrefixes[preidx].PLName;

	std::string identifiedName = fmt::format(fmt::runtime(magicFmt), prefixName, baseName, spellName);
	if (forceNameLengthCheck ? *forceNameLengthCheck : !StringInPanel(identifiedName.c_str())) {
		string_view shortName = translate ? _(baseItemData.iSName) : baseItemData.iSName;
		identifiedName = fmt::format(fmt::runtime(magicFmt), prefixName, shortName, spellName);
	}
	return identifiedName;
}

static std::string GetIdentifiedItemName(const string_view &baseNamel, const ItemAffixData *pPrefix, const ItemAffixData *pSufix, bool translate)
{
	if (pPrefix != nullptr && pSufix != nullptr) {
		string_view fmt = translate ? _(/* TRANSLATORS: Constructs item names. Format: {Prefix} {Item} of {Suffix}. Example: King's Long Sword of the Whale */ "{0} {1} of {2}") : "{0} {1} of {2}";
		return fmt::format(fmt::runtime(fmt), translate ? _(pPrefix->PLName) : pPrefix->PLName, baseNamel, translate ? _(pSufix->PLName) : pSufix->PLName);
	} else if (pPrefix != nullptr) {
		string_view fmt = translate ? _(/* TRANSLATORS: Constructs item names. Format: {Prefix} {Item}. Example: King's Long Sword */ "{0} {1}") : "{0} {1}";
		return fmt::format(fmt::runtime(fmt), translate ? _(pPrefix->PLName) : pPrefix->PLName, baseNamel);
	} else if (pSufix != nullptr) {
		string_view fmt = translate ? _(/* TRANSLATORS: Constructs item names. Format: {Item} of {Suffix}. Example: Long Sword of the Whale */ "{0} of {1}") : "{0} of {1}";
		return fmt::format(fmt::runtime(fmt), baseNamel, translate ? _(pSufix->PLName) : pSufix->PLName);
	}

	return std::string(baseNamel);
}

static int GeneratePrefixForStaffWithCharges(int minlvl, int maxlvl, bool onlygood, bool hellfireItem)
{
	int preidx = -1;
	if (FlipCoin(10) || onlygood) {
		int nl = 0;
		std::array<int, 256> possibleChoices;
		for (int j = 0; ItemPrefixes[j].power.type != IPL_INVALID; j++) {
			if (!IsPrefixValidForItemType(j, AffixItemType::Staff, hellfireItem))
				continue;
#if JWK_DONT_SKIP_DESIRABLE_ITEM_PROPERTIES // In this case, we ADD a skip for prefixes which ARE too low level
			if ((ItemPrefixes[j].PLMinLvl < minlvl && !ItemPrefixes[j].PLDesirable) || ItemPrefixes[j].PLMinLvl > maxlvl)
#else // original code
			if (ItemPrefixes[j].PLMinLvl > maxlvl)
#endif
				continue;
			if (onlygood && !ItemPrefixes[j].PLOk)
				continue;
			possibleChoices[nl] = j;
			nl++;
			if (ItemPrefixes[j].PLDouble) {
				possibleChoices[nl] = j;
				nl++;
			}
		}
		if (nl != 0) {
			preidx = possibleChoices[GenerateRnd(nl)];
		}
	}
	return preidx;
}

static void GenerateAffixesForStaffWithCharges(Item &item, int minlvl, int maxlvl, bool onlygood)
{
	// We only need a prefix because the suffix is always "of firebolt" etc.
	int preidx = GeneratePrefixForStaffWithCharges(minlvl, maxlvl, onlygood, gbIsHellfire);
	if (preidx != -1) {
		item._iMagical = ITEM_QUALITY_MAGIC;
		AddAffixToItem(item, ItemPrefixes[preidx]);
		item._iPrefixPower = ItemPrefixes[preidx].power.type;
	}

	const BaseItemData &baseItemData = AllItemsList[item.IDidx];
	std::string staffName = GetStaffName(baseItemData, item._iSpell, false); // unlike non-staff items, we don't just use the base item name.  We use the base item name plus the spell:  "short staff of fireball"

	CopyUtf8(item._iName, staffName, sizeof(item._iName));
	if (preidx != -1) {
		std::string staffNameMagical = GetIdentifiedStaffName(baseItemData, item._iSpell, preidx, false, std::nullopt); // "obsidian short staff of fireball"   Note: The resistances of obsidian prefix don't apply to player until they identify the staff
		CopyUtf8(item._iIName, staffNameMagical, sizeof(item._iIName));
	} else {
		CopyUtf8(item._iIName, item._iName, sizeof(item._iIName));
	}

	CalcItemValue(item);
}

static void GenerateItemAffixes(int minlvl, int maxlvl, AffixItemType flgs, bool onlygood, bool hellfireItem, tl::function_ref<void(const ItemAffixData &prefix)> prefixFound, tl::function_ref<void(const ItemAffixData &suffix)> suffixFound)
{
	int preidx = -1;
	int sufidx = -1;

	int possibleChoices[256];
	goodorevil goe;

	bool allocatePrefix = FlipCoin(4);
	bool allocateSuffix = !FlipCoin(3);
	if (!allocatePrefix && !allocateSuffix) {
		// At least try and give each item a prefix or suffix
		if (FlipCoin())
			allocatePrefix = true;
		else
			allocateSuffix = true;
	}
	goe = GOE_ANY;
	if (!onlygood && !FlipCoin(3))
		onlygood = true;
	if (allocatePrefix) {
		int nt = 0;
		for (int j = 0; ItemPrefixes[j].power.type != IPL_INVALID; j++) {
			if (!IsPrefixValidForItemType(j, flgs, hellfireItem))
				continue;
#if JWK_DONT_SKIP_DESIRABLE_ITEM_PROPERTIES
			if ((ItemPrefixes[j].PLMinLvl < minlvl && !ItemPrefixes[j].PLDesirable) || ItemPrefixes[j].PLMinLvl > maxlvl)
#else // original code
			if (ItemPrefixes[j].PLMinLvl < minlvl || ItemPrefixes[j].PLMinLvl > maxlvl)
#endif
				continue;
			if (onlygood && !ItemPrefixes[j].PLOk)
				continue;
			if (HasAnyOf(flgs, AffixItemType::Staff) && ItemPrefixes[j].power.type == IPL_CHARGES) // Don't allow spell charges because staffs with spell charges are handled by GenerateAffixesForStaffWithCharges().
				continue;
			possibleChoices[nt] = j;
			nt++;
			if (ItemPrefixes[j].PLDouble) {
				possibleChoices[nt] = j;
				nt++;
			}
		}
		if (nt != 0) {
			preidx = possibleChoices[GenerateRnd(nt)];
			goe = ItemPrefixes[preidx].PLGOE;
			prefixFound(ItemPrefixes[preidx]);
		}
	}
	if (allocateSuffix) {
		int nl = 0;
		for (int j = 0; ItemSuffixes[j].power.type != IPL_INVALID; j++) {
			if (IsSuffixValidForItemType(j, flgs, hellfireItem)
#if JWK_DONT_SKIP_DESIRABLE_ITEM_PROPERTIES
			    && (ItemSuffixes[j].PLDesirable || ItemSuffixes[j].PLMinLvl >= minlvl) && ItemSuffixes[j].PLMinLvl <= maxlvl
#else // original code
			    && ItemSuffixes[j].PLMinLvl >= minlvl && ItemSuffixes[j].PLMinLvl <= maxlvl
#endif
				&& !((goe == GOE_GOOD && ItemSuffixes[j].PLGOE == GOE_EVIL) || (goe == GOE_EVIL && ItemSuffixes[j].PLGOE == GOE_GOOD))
			    && (!onlygood || ItemSuffixes[j].PLOk)) {
				possibleChoices[nl] = j;
				nl++;
			}
		}
		if (nl != 0) {
			sufidx = possibleChoices[GenerateRnd(nl)];
			suffixFound(ItemSuffixes[sufidx]);
		}
	}
}

static void GenerateAffixesForItemWithoutSpellCharges(Item &item, int minlvl, int maxlvl, AffixItemType flgs, bool onlygood)
{
	const ItemAffixData *pPrefix = nullptr;
	const ItemAffixData *pSufix = nullptr;
	GenerateItemAffixes(
	    minlvl, maxlvl, flgs, onlygood, gbIsHellfire,
	    [&item, &pPrefix](const ItemAffixData &prefix) {
		    item._iMagical = ITEM_QUALITY_MAGIC;
		    AddAffixToItem(item, prefix);
		    item._iPrefixPower = prefix.power.type;
		    pPrefix = &prefix;
	    },
	    [&item, &pSufix](const ItemAffixData &suffix) {
		    item._iMagical = ITEM_QUALITY_MAGIC;
		    AddAffixToItem(item, suffix);
		    item._iSuffixPower = suffix.power.type;
		    pSufix = &suffix;
	    });

	CopyUtf8(item._iIName, GetIdentifiedItemName(item._iName, pPrefix, pSufix, false), sizeof(item._iIName));
	if (!StringInPanel(item._iIName)) {
		CopyUtf8(item._iIName, GetIdentifiedItemName(AllItemsList[item.IDidx].iSName, pPrefix, pSufix, false), sizeof(item._iIName));
	}
	if (pPrefix != nullptr || pSufix != nullptr)
		CalcItemValue(item);
}

static void AddSpellChargesToStaff(Item &item, int maxlvl, bool decreaseChanceOfHigherSpells)
{
	SpellID chosenSpell = SelectRandomSpell(maxlvl, false, decreaseChanceOfHigherSpells);

	int minc = GetSpellData(chosenSpell).sStaffMin;
	int maxc = GetSpellData(chosenSpell).sStaffMax - minc + 1;
	item._iSpell = chosenSpell;
	item._iCharges = minc + GenerateRnd(maxc);
	item._iMaxCharges = item._iCharges;

	item._iMinMag = GetSpellData(chosenSpell).minInt;
	int v = item._iCharges * GetSpellData(chosenSpell).staffCost() / 5;
#if JWK_INCREASE_VALUE_OF_STAFF_CHARGES_IF_SPELL_CANT_BE_LEARNED
	if (GetSpellBookLevel(item._iSpell, true) < 0) { // then spell can't be learned
		v *= 3;
	}
#endif
	item._ivalue += v;
	item._iIvalue += v;
}

static void SelectRandomOil(Item &item, int maxLvl)
{
	int cnt = 2;
	int8_t rnd[32] = { 5, 6 };

	if (!gbIsMultiplayer) {
		if (maxLvl == 0)
			maxLvl = 1;

		cnt = 0;
		for (size_t j = 0; j < sizeof(OilLevels) / sizeof(OilLevels[0]); j++) {
			if (OilLevels[j] <= maxLvl) {
				rnd[cnt] = j;
				cnt++;
			}
		}
	}

	int8_t t = rnd[GenerateRnd(cnt)];

	CopyUtf8(item._iName, OilNames[t], sizeof(item._iName));
	CopyUtf8(item._iIName, OilNames[t], sizeof(item._iIName));
	item._iMiscId = OilMagic[t];
	item._ivalue = OilValues[t];
	item._iIvalue = OilValues[t];
}

static void GenerateAffixesForItemOfAnyType(Item &item, int minlvl, int maxlvl, bool onlygood, bool allowspells)
{
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	minlvl = std::max(minlvl, 1);
	maxlvl = std::max(minlvl, maxlvl);
#else // original code
	if (minlvl > 25)
		minlvl = 25;
#endif
	switch (item._itype) {
	case ItemType::Sword:
	case ItemType::Axe:
	case ItemType::Mace:
		GenerateAffixesForItemWithoutSpellCharges(item, minlvl, maxlvl, AffixItemType::Weapon, onlygood);
		break;
	case ItemType::Bow:
		GenerateAffixesForItemWithoutSpellCharges(item, minlvl, maxlvl, AffixItemType::Bow, onlygood);
		break;
	case ItemType::Shield:
		GenerateAffixesForItemWithoutSpellCharges(item, minlvl, maxlvl, AffixItemType::Shield, onlygood);
		break;
	case ItemType::LightArmor:
	case ItemType::Helm:
	case ItemType::MediumArmor:
	case ItemType::HeavyArmor:
		GenerateAffixesForItemWithoutSpellCharges(item, minlvl, maxlvl, AffixItemType::Armor, onlygood);
		break;
	case ItemType::Staff:
		if (allowspells && (gbIsHellfire || !FlipCoin(4))) {
			AddSpellChargesToStaff(item, maxlvl, true);
			GenerateAffixesForStaffWithCharges(item, minlvl, maxlvl, onlygood);
		}
		else
			GenerateAffixesForItemWithoutSpellCharges(item, minlvl, maxlvl, AffixItemType::Staff, onlygood);
		break;
	case ItemType::Ring:
	case ItemType::Amulet:
		GenerateAffixesForItemWithoutSpellCharges(item, minlvl, maxlvl, AffixItemType::Misc, onlygood);
		break;
	case ItemType::None:
	case ItemType::Misc:
	case ItemType::Gold:
		break;
	}
}

bool IsUniqueAvailable(int i)
{
	return gbIsHellfire || i <= 89;
}

bool IsItemAvailable(int idx)
{
	if (idx < 0 || idx > IDI_LAST)
		return false;

	if (gbIsDemoGame) {
		if (idx >= 62 && idx <= 70)
			return false; // Medium and heavy armors
		if (IsAnyOf(idx, 105, 107, 108, 110, 111, 113))
			return false; // Unavailable scrolls
	}

	if (gbIsHellfire)
		return true;

	return (
	           idx != IDI_MAPOFDOOM                     // Cathedral Map
	           && idx != IDI_LGTFORGE                   // Bovine Plate
	           && (idx < IDI_OIL || idx > IDI_GREYSUIT) // Hellfire exclusive items
	           && (idx < 83 || idx > 86)                // Oils
	           && idx != 92                             // Scroll of Search
	           && (idx < 161 || idx > 165)              // Runes
	           && idx != IDI_SORCERER                   // Short Staff of Mana
	           )
	    || (
	        // Bard items are technically Hellfire-exclusive
	        // but are just normal items with adjusted stats.
	        *sgOptions.Gameplay.testBard && IsAnyOf(idx, IDI_BARDSWORD, IDI_BARDDAGGER));
}

static BaseItemIdx SelectRandomBaseItem(bool considerDropRate, tl::function_ref<bool(const BaseItemData &item)> isItemOkay)
{
	static std::array<BaseItemIdx, IDI_LAST * 2> choices;

	size_t numChoices = 0;
	for (std::underlying_type_t<BaseItemIdx> i = IDI_GOLD; i <= IDI_LAST; i++) {
		if (!IsItemAvailable(i))
			continue;
		const BaseItemData &item = AllItemsList[i];
		if (item.iRnd == IDROP_NEVER)
			continue;
		if (IsAnyOf(item.iSpell, SpellID::Resurrect, SpellID::HealOther) && !gbIsMultiplayer)
			continue;
		if (!isItemOkay(item))
			continue;
		choices[numChoices] = static_cast<BaseItemIdx>(i);
		numChoices++;
		if (item.iRnd == IDROP_DOUBLE && considerDropRate) {
			choices[numChoices] = static_cast<BaseItemIdx>(i);
			numChoices++;
		}
	}

	if (numChoices == 0) {
		return IDI_NONE;
	}
	return choices[GenerateRnd(static_cast<int>(numChoices))];
}

static BaseItemIdx SelectRandomBaseItemForHighQualityDrop()
{
	int itemMaxLevel = GetCurrentBaseItemLevelForDrops() + 8; // boost base item limit

	return SelectRandomBaseItem(false, [&itemMaxLevel](const BaseItemData &item) {
		if (item.itype == ItemType::Misc && item.iMiscId == IMISC_BOOK)
			return true;
		if (itemMaxLevel < item.iMinMLvl)
			return false;
#if 1 // jwk - include rings and amulets as good drops
		if (item.itype == ItemType::Misc && (item.iMiscId == IMISC_RING || item.iMiscId == IMISC_AMULET))
			return true;
#endif
		if (IsAnyOf(item.itype, ItemType::Gold, ItemType::Misc)) // don't drop gold, potions, scroll, etc.
			return false;
		return true;
	});
}

static BaseItemIdx SelectRandomBaseItemForDrop()
{
	if (GenerateRnd(100) > 25)
		return IDI_GOLD;

	int maxBaseItemLevel = GetCurrentBaseItemLevelForDrops();
	return SelectRandomBaseItem(true, [&maxBaseItemLevel](const BaseItemData &item) {
		return item.iMinMLvl <= maxBaseItemLevel;
	});
}

static BaseItemIdx SelectRandomBaseItemOfType(ItemType itemType, int iMiscId, int maxLevel)
{
	return SelectRandomBaseItem(false, [&maxLevel, &itemType, &iMiscId](const BaseItemData &item) {
		if (maxLevel < item.iMinMLvl)
			return false;
		if (item.itype != itemType)
			return false;
		if (iMiscId != -1 && item.iMiscId != iMiscId)
			return false;
		return true;
	});
}

static UniqueItemIdx SelectRandomUniqueItem(Item &item, int maxLevel, int percentChanceOfUnique, bool allowDuplicateUniqueItems)
{
	std::bitset<UniqueItemFlags.size()> choices = {};

	if (GenerateRnd(100) > percentChanceOfUnique)
		return UITEM_INVALID;

	int numChoices = 0;
	for (int j = 0; UniqueItems[j].UIItemId != UITYPE_INVALID; j++) {
		if (j >= UniqueItemFlags.size()) {
			assert(false && "use a larger array size");
			break;
		}
		if (!IsUniqueAvailable(j))
			break;
		if (UniqueItems[j].UIItemId == AllItemsList[item.IDidx].iItemId
		    && maxLevel >= UniqueItems[j].UIMinLvl
		    && (allowDuplicateUniqueItems || gbIsMultiplayer || !UniqueItemFlags[j])) {
			choices[j] = true;
			numChoices++;
		}
	}

	if (numChoices == 0)
		return UITEM_INVALID;

	DiscardRandomValues(1);
	uint8_t itemData = 0;
	while (numChoices > 0) {
		if (choices[itemData])
			numChoices--;
		if (numChoices > 0)
			itemData = (itemData + 1) % 128;
	}

	return (UniqueItemIdx)itemData;
}

static void ApplyUniqueItemPropertiesToItem(Item &item, UniqueItemIdx uid)
{
	UniqueItemFlags[uid] = true;

	for (auto power : UniqueItems[uid].powers) {
		if (power.type == IPL_INVALID)
			break;
		AddItemPowerToItem(item, power);
	}

	CopyUtf8(item._iIName, UniqueItems[uid].UIName, sizeof(item._iIName));
	item._iIvalue = UniqueItems[uid].UIValue;

	if (item._iMiscId == IMISC_UNIQUE)
		item._iSeed = uid;

	item._iUid = uid;
	item._iMagical = ITEM_QUALITY_UNIQUE;
	item._iCreateInfo |= CF_UNIQUE;
}

static void ApplyRandomDurability(Item &item)
{
	if (item._iDurability > 0 && item._iDurability != DUR_INDESTRUCTIBLE)
		item._iDurability = GenerateRnd(item._iMaxDur / 2) + (item._iMaxDur / 4) + 1;
}

// This is THE item generation function for all dungeon items, ie items not created in town (item._iCreateInfo & CF_TOWN) == 0.  This includes monster loot, chest loot, etc.
// 'chanceOfUnique' must be either 0, 1, or 15.
static void ConstructItemFromSeed(Item &item, BaseItemIdx idx, uint32_t iseed, int level, int chanceOfUnique, bool onlygood, bool allowDuplicateUniqueItems, bool pregen)
{
	item._iSeed = iseed;
	SetRndSeed(iseed);
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	level = clamp<int>(level, 1, CF_LEVEL); // make sure the level can be encoded into the bits allocated when we bit-pack the item
	// Note: There are two levels relevant when constructing an item:  The level of the base item, and the level of the affixes.  In this function, 'level' refers to level of the affixes.
	// The base item level is not relevant here because we already selected a base item (BaseItemIdx), and this base item will be encoded directly into the bit-packed item.  See struct TItem.
	int minBaseItemLevel = FlipCoin(3) ? 1 : (level + 1) / 2; // this allows all items while still producing higher quality items in higher zones
#else // original code:
	int minBaseItemLevel = level / 2;
#endif
	GenerateRandomPropertiesForBaseItem(item, idx, minBaseItemLevel); // here, minBaseItemLevel applies to spellbooks and oils.  This means Nova (sBookLvl=14) only drops from monsters level 28 and above.
	item._iCreateInfo = level;

	if (pregen)
		item._iCreateInfo |= CF_PREGEN;
	if (onlygood)
		item._iCreateInfo |= CF_ONLYGOOD;

	if (chanceOfUnique == 15)
		item._iCreateInfo |= CF_UPER15;
	else if (chanceOfUnique == 1)
		item._iCreateInfo |= CF_UPER1;

	if (item._iMiscId != IMISC_UNIQUE) {
		int iLevelForAffixes = -1; // This value doesn't need to be clamped to CF_LEVEL because it's a derived value regenerated from the seed
		if (GenerateRnd(100) <= 10
			|| GenerateRnd(100) <= level
			|| onlygood
			|| IsAnyOf(item._iMiscId, IMISC_STAFF, IMISC_RING, IMISC_AMULET)) {
			iLevelForAffixes = level;
		}
		if (chanceOfUnique == 15) {
			iLevelForAffixes = level + 4;
		}
		if (iLevelForAffixes != -1) {
			UniqueItemIdx uid = SelectRandomUniqueItem(item, iLevelForAffixes, chanceOfUnique, allowDuplicateUniqueItems);
			if (uid == UITEM_INVALID) {
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
				int minLevelForAffixes = FlipCoin(3) ? 1 : (iLevelForAffixes + 1) / 2; // this allows all items while still producing higher quality items in higher zones
#else // original code:
				int minLevelForAffixes = iLevelForAffixes / 2;
#endif
				GenerateAffixesForItemOfAnyType(item, minLevelForAffixes, iLevelForAffixes, onlygood, true);
			} else {
				ApplyUniqueItemPropertiesToItem(item, uid);
			}
		}
		if (item._iMagical != ITEM_QUALITY_UNIQUE)
			ApplyRandomDurability(item);
	} else {
		if (item._iLoc != ILOC_UNEQUIPABLE) {
			if (iseed > 109 || AllItemsList[static_cast<size_t>(idx)].iItemId != UniqueItems[iseed].UIItemId) { // jwk wtf is this?
				item.clear();
				return;
			}

			ApplyUniqueItemPropertiesToItem(item, (UniqueItemIdx)iseed); // uid is stored in iseed for uniques
		}
	}
	SetupItem(item);
}

// 'Useful' items are things like potions and scrolls
static void ConstructUsefulItemFromSeed(Item &item, uint32_t seed, int lvl)
{
	item._iSeed = seed;
	SetRndSeed(seed);

	BaseItemIdx idx;

	if (gbIsHellfire) {
		switch (GenerateRnd(7)) {
		case 0:
			idx = IDI_PORTAL;
			if (lvl <= 1)
				idx = IDI_HEAL;
			break;
		case 1:
		case 2:
			idx = IDI_HEAL;
			break;
		case 3:
			idx = IDI_PORTAL;
			if (lvl <= 1)
				idx = IDI_MANA;
			break;
		case 4:
		case 5:
			idx = IDI_MANA;
			break;
		default:
			idx = IDI_OIL;
			break;
		}
	} else {
		idx = PickRandomlyAmong({ IDI_MANA, IDI_HEAL });

		if (lvl > 1 && FlipCoin(3))
			idx = IDI_PORTAL;
	}

	GenerateRandomPropertiesForBaseItem(item, idx, lvl);
	item._iCreateInfo = (lvl & CF_LEVEL) | CF_USEFUL;
	SetupItem(item);
}

static uint8_t Char2int(uint8_t input)
{
	if (input >= '0' && input <= '9')
		return input - '0';
	if (input >= 'A' && input <= 'F')
		return input - 'A' + 10;
	return 0;
}

static void Hex2bin(const char *src, int bytes, uint8_t *target)
{
	for (int i = 0; i < bytes; i++, src += 2) {
		target[i] = (Char2int(src[0]) << 4) | Char2int(src[1]);
	}
}

static void SpawnRock()
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	const Object *stand = nullptr;
	for (int i = 0; i < ActiveObjectCount; i++) {
		const Object &object = Objects[ActiveObjects[i]];
		if (object._otype == OBJ_STAND) {
			stand = &object;
			break;
		}
	}

	if (stand == nullptr)
		return;

	int ii = AllocateItem();
	Item &item = Items[ii];

	item.position = stand->position;
	dItem[item.position.x][item.position.y] = ii + 1;
	int curlv = GetCurrentBaseItemLevelForDrops();
	GenerateRandomPropertiesForBaseItem(item, IDI_ROCK, curlv);
	SetupItem(item);
	item._iSelFlag = 2;
	item._iPostDraw = true;
	item.AnimInfo.currentFrame = 10;
	item._iCreateInfo |= CF_PREGEN;

	DeltaAddItem(ii);
}

static void PrintItemOil(char iDidx)
{
	switch (iDidx) {
	case IMISC_OILACC:
		AddPanelString(_("increases a weapon's"));
		AddPanelString(_("chance to hit"));
		break;
	case IMISC_OILMAST:
		AddPanelString(_("greatly increases a"));
		AddPanelString(_("weapon's chance to hit"));
		break;
	case IMISC_OILSHARP:
		AddPanelString(_("increases a weapon's"));
		AddPanelString(_("damage potential"));
		break;
	case IMISC_OILDEATH:
		AddPanelString(_("greatly increases a weapon's"));
		AddPanelString(_("damage potential - not bows"));
		break;
	case IMISC_OILSKILL:
		AddPanelString(_("reduces attributes needed"));
		AddPanelString(_("to use armor or weapons"));
		break;
	case IMISC_OILBSMTH:
		AddPanelString(/*xgettext:no-c-format*/ _("restores 20% of an"));
		AddPanelString(_("item's durability"));
		break;
	case IMISC_OILFORT:
		AddPanelString(_("increases an item's"));
		AddPanelString(_("current and max durability"));
		break;
	case IMISC_OILPERM:
		AddPanelString(_("makes an item indestructible"));
		break;
	case IMISC_OILHARD:
		AddPanelString(_("increases the armor class"));
		AddPanelString(_("of armor and shields"));
		break;
	case IMISC_OILIMP:
		AddPanelString(_("greatly increases the armor"));
		AddPanelString(_("class of armor and shields"));
		break;
	case IMISC_RUNEF:
		AddPanelString(_("sets fire trap"));
		break;
	case IMISC_RUNEL:
	case IMISC_GR_RUNEL:
		AddPanelString(_("sets lightning trap"));
		break;
	case IMISC_GR_RUNEF:
		AddPanelString(_("sets fire trap"));
		break;
	case IMISC_RUNES:
		AddPanelString(_("sets petrification trap"));
		break;
	case IMISC_FULLHEAL:
		AddPanelString(_("restore all life"));
		break;
	case IMISC_HEAL:
		AddPanelString(_("restore some life"));
		break;
	case IMISC_MANA:
		AddPanelString(_("restore some mana"));
		break;
	case IMISC_FULLMANA:
		AddPanelString(_("restore all mana"));
		break;
	case IMISC_ELIXSTR:
		AddPanelString(_("increase strength"));
		break;
	case IMISC_ELIXMAG:
		AddPanelString(_("increase magic"));
		break;
	case IMISC_ELIXDEX:
		AddPanelString(_("increase dexterity"));
		break;
	case IMISC_ELIXVIT:
		AddPanelString(_("increase vitality"));
		break;
	case IMISC_REJUV:
		AddPanelString(_("restore some life and mana"));
		break;
	case IMISC_FULLREJUV:
		AddPanelString(_("restore all life and mana"));
		break;
	case IMISC_ARENAPOT:
		AddPanelString(_("restore all life and mana"));
		AddPanelString(_("(works only in arenas)"));
		break;
	}
}

static void DrawUniqueInfoWindow(const Surface &out)
{
	ClxDraw(out, GetPanelPosition(UiPanels::Inventory, { 24 - SidePanelSize.width, 327 }), (*pSTextBoxCels)[0]);
	DrawHalfTransparentRectTo(out, GetRightPanel().position.x - SidePanelSize.width + 27, GetRightPanel().position.y + 28, 265, 297);
}

static void printItemMiscKBM(const Item &item, const bool isOil, const bool isCastOnTarget)
{
	if (item._iMiscId == IMISC_MAPOFDOOM) {
		AddPanelString(_("Right-click to view"));
	} else if (isOil) {
		PrintItemOil(item._iMiscId);
		AddPanelString(_("Right-click to use"));
	} else if (isCastOnTarget) {
		AddPanelString(_("Right-click to read, then\nleft-click to target"));
	} else if (IsAnyOf(item._iMiscId, IMISC_BOOK, IMISC_NOTE, IMISC_SCROLL, IMISC_SCROLLT)) {
		AddPanelString(_("Right-click to read"));
	}
}

static void printItemMiscGenericGamepad(const Item &item, const bool isOil, bool isCastOnTarget)
{
	if (item._iMiscId == IMISC_MAPOFDOOM) {
		AddPanelString(_("Activate to view"));
	} else if (isOil) {
		PrintItemOil(item._iMiscId);
		if (!invflag) {
			AddPanelString(_("Open inventory to use"));
		} else {
			AddPanelString(_("Activate to use"));
		}
	} else if (isCastOnTarget) {
		AddPanelString(_("Select from spell book, then\ncast spell to read"));
	} else if (IsAnyOf(item._iMiscId, IMISC_BOOK, IMISC_NOTE, IMISC_SCROLL, IMISC_SCROLLT)) {
		AddPanelString(_("Activate to read"));
	}
}

static void printItemMiscGamepad(const Item &item, bool isOil, bool isCastOnTarget)
{
	string_view activateButton;
	string_view castButton;
	switch (GamepadType) {
	case GamepadLayout::Generic:
		printItemMiscGenericGamepad(item, isOil, isCastOnTarget);
		return;
	case GamepadLayout::Xbox:
		activateButton = controller_button_icon::Xbox_Y;
		castButton = controller_button_icon::Xbox_X;
		break;
	case GamepadLayout::PlayStation:
		activateButton = controller_button_icon::Playstation_Triangle;
		castButton = controller_button_icon::Playstation_Square;
		break;
	case GamepadLayout::Nintendo:
		activateButton = controller_button_icon::Nintendo_X;
		castButton = controller_button_icon::Nintendo_Y;
		break;
	}

	if (item._iMiscId == IMISC_MAPOFDOOM) {
		AddPanelString(fmt::format(fmt::runtime(_("{} to view")), activateButton));
	} else if (isOil) {
		PrintItemOil(item._iMiscId);
		if (!invflag) {
			AddPanelString(_("Open inventory to use"));
		} else {
			AddPanelString(fmt::format(fmt::runtime(_("{} to use")), activateButton));
		}
	} else if (isCastOnTarget) {
		AddPanelString(fmt::format(fmt::runtime(_("Select from spell book,\nthen {} to read")), castButton));
	} else if (IsAnyOf(item._iMiscId, IMISC_BOOK, IMISC_NOTE, IMISC_SCROLL, IMISC_SCROLLT)) {
		AddPanelString(fmt::format(fmt::runtime(_("{} to read")), activateButton));
	}
}

static void PrintItemMisc(const Item &item)
{
	if (item._iMiscId == IMISC_EAR) {
		AddPanelString(fmt::format(fmt::runtime(pgettext("player", "Level: {:d}")), item._ivalue));
		return;
	}
	if (item._iMiscId == IMISC_AURIC) {
		AddPanelString(_("Doubles gold capacity"));
		return;
	}
	const bool isOil = (item._iMiscId >= IMISC_USEFIRST && item._iMiscId <= IMISC_USELAST)
	    || (item._iMiscId > IMISC_OILFIRST && item._iMiscId < IMISC_OILLAST)
	    || (item._iMiscId > IMISC_RUNEFIRST && item._iMiscId < IMISC_RUNELAST)
	    || item._iMiscId == IMISC_ARENAPOT;
	const bool mouseRequiresTarget = (item._iMiscId == IMISC_SCROLLT && item._iSpell != SpellID::Flash)
	    || (item._iMiscId == IMISC_SCROLL && IsAnyOf(item._iSpell, SpellID::TownPortal, SpellID::Identify));
	const bool gamepadRequiresTarget = item.isScroll() && TargetsMonster(item._iSpell);

	switch (ControlMode) {
	case ControlTypes::None:
		break;
	case ControlTypes::KeyboardAndMouse:
		printItemMiscKBM(item, isOil, mouseRequiresTarget);
		break;
	case ControlTypes::VirtualGamepad:
		printItemMiscGenericGamepad(item, isOil, gamepadRequiresTarget);
		break;
	case ControlTypes::Gamepad:
		printItemMiscGamepad(item, isOil, gamepadRequiresTarget);
		break;
	}
}

static void PrintItemInfo(const Item &item)
{
	PrintItemMisc(item);
	uint8_t str = item._iMinStr;
	uint8_t dex = item._iMinDex;
	uint8_t mag = item._iMinMag;
	if (str != 0 || mag != 0 || dex != 0) {
		std::string text = std::string(_("Required:"));
		if (str != 0)
			text.append(fmt::format(fmt::runtime(_(" {:d} Str")), str));
		if (mag != 0)
			text.append(fmt::format(fmt::runtime(_(" {:d} Mag")), mag));
		if (dex != 0)
			text.append(fmt::format(fmt::runtime(_(" {:d} Dex")), dex));
		AddPanelString(text);
	}
}

uint8_t GetOutlineColor(const Item &item, bool checkReq)
{
	if (checkReq && !item._iStatFlag)
		return ICOL_RED;
	if (item._itype == ItemType::Gold)
		return ICOL_YELLOW;
	if (item._iMagical == ITEM_QUALITY_MAGIC)
		return ICOL_BLUE;
	if (item._iMagical == ITEM_QUALITY_UNIQUE)
		return ICOL_YELLOW;

	return ICOL_WHITE;
}

void ClearUniqueItemFlags()
{
	for (int i = 0; i < UniqueItemFlags.size(); i++)
		UniqueItemFlags[i] = false;
}

void InitItemGFX()
{
	char arglist[64];

	int itemTypes = gbIsHellfire ? ITEMTYPES : 35;
	for (int i = 0; i < itemTypes; i++) {
		*BufCopy(arglist, "items\\", ItemDropNames[i]) = '\0';
		itemanims[i] = LoadCel(arglist, ItemAnimWidth);
	}
}

void InitItems()
{
	ActiveItemCount = 0;
	memset(dItem, 0, sizeof(dItem));

	for (auto &item : Items) {
		item.clear();
		item.position = { 0, 0 };
		item._iAnimFlag = false;
		item._iSelFlag = 0;
		item._iIdentified = false;
		item._iPostDraw = false;
	}

	for (uint8_t i = 0; i < MAXITEMS; i++) {
		ActiveItems[i] = i;
	}

	if (!setlevel) {
		DiscardRandomValues(1);
		if (Quests[Q_ROCK].IsAvailable())
			SpawnRock();
		if (Quests[Q_ANVIL].IsAvailable())
			SpawnQuestItem(IDI_ANVIL, SetPiece.position.megaToWorld() + Displacement { 11, 11 }, 0, 1, false);
		if (sgGameInitInfo.bCowQuest != 0 && currlevel == 20)
			SpawnQuestItem(IDI_BROWNSUIT, { 25, 25 }, 3, 1, false);
		if (sgGameInitInfo.bCowQuest != 0 && currlevel == 19)
			SpawnQuestItem(IDI_GREYSUIT, { 25, 25 }, 3, 1, false);
		// In multiplayer items spawn during level generation to avoid desyncs
		if (gbIsMultiplayer) {
			if (Quests[Q_MUSHROOM].IsAvailable())
				SpawnQuestItem(IDI_FUNGALTM, { 0, 0 }, 5, 1, false);
			if (currlevel == Quests[Q_VEIL]._qlevel + 1 && Quests[Q_VEIL]._qactive != QUEST_NOTAVAIL)
				SpawnQuestItem(IDI_GLDNELIX, { 0, 0 }, 5, 1, false);
		}
		if (currlevel > 0 && currlevel < 16)
			AddInitItems();
		if (currlevel >= 21 && currlevel <= 23)
			SpawnNote();
	}

	ShowUniqueItemInfoBox = false;

	initItemGetRecords();
}

void CalcPlayerPowerFromItems(Player &player, bool loadgfx)
{
	int mind = 0; // min damage
	int maxd = 0; // max damage
	int tac = 0;  // accuracy

	int bdam = 0;   // bonus damage
	int btohit = 0; // bonus chance to hit
	int bac = 0;    // bonus accuracy

	ItemSpecialEffect iflgs = ItemSpecialEffect::None; // item_special_effect flags

	ItemSpecialEffectHf pDamAcFlags = ItemSpecialEffectHf::None;

	int sadd = 0; // added strength
	int madd = 0; // added magic
	int dadd = 0; // added dexterity
	int vadd = 0; // added vitality

	uint64_t spl = 0; // bitarray for all enabled/active spells

	int fr = 0; // fire resistance
	int lr = 0; // lightning resistance
	int mr = 0; // magic resistance

	int dmod = 0; // bonus damage mod
	int ghit = 0; // increased damage from enemies

	int lrad = 10; // light radius

	int ihp = 0;   // increased HP
	int imana = 0; // increased mana
	int manaCostMod = 0;

	int spllvladd = 0; // increased spell level
	int enac = 0;      // enhanced accuracy (armor penetration)

	int fmin = 0; // minimum fire damage
	int fmax = 0; // maximum fire damage
	int lmin = 0; // minimum lightning damage
	int lmax = 0; // maximum lightning damage

	for (auto &item : player.InvBody) {
		if (!item.isEmpty() && item._iStatFlag) {

			mind += item._iMinDam;
			maxd += item._iMaxDam;
			tac += item._iAC;

			if (IsValidSpell(item._iSpell)) {
				spl |= GetSpellBitmask(item._iSpell);
			}

			if (item._iMagical == ITEM_QUALITY_NORMAL || item._iIdentified) {
				bdam += item._iPLDam;
				btohit += item._iPLToHit;
				if (item._iPLAC != 0) {
					int tmpac = item._iAC;
					tmpac *= item._iPLAC;
					tmpac /= 100;
					if (tmpac == 0)
						tmpac = math::Sign(item._iPLAC);
					bac += tmpac;
				}
				iflgs |= item._iFlags;
				pDamAcFlags |= item._iDamAcFlags;
				sadd += item._iPLStr;
				madd += item._iPLMag;
				dadd += item._iPLDex;
				vadd += item._iPLVit;
				fr += item._iPLFR;
				lr += item._iPLLR;
				mr += item._iPLMR;
				dmod += item._iPLDamMod;
				ghit += item._iPLGetHit;
				lrad += item._iPLLight;
				manaCostMod += item._iPLManaCostMod;
				ihp += item._iPLHP;
				imana += item._iPLMana;
				spllvladd += item._iSplLvlAdd;
				enac += item._iPLArmorPierce;
				fmin += item._iFMinDam;
				fmax += item._iFMaxDam;
				lmin += item._iLMinDam;
				lmax += item._iLMaxDam;
			}
		}
	}
	sadd += JWK_GOD_MODE_ADJUST_STR_BY_AMOUNT;
	madd += JWK_GOD_MODE_ADJUST_MAG_BY_AMOUNT;
	dadd += JWK_GOD_MODE_ADJUST_DEX_BY_AMOUNT;
	vadd += JWK_GOD_MODE_ADJUST_VIT_BY_AMOUNT;

	if (mind == 0 && maxd == 0) {
		mind = 1;
		maxd = 1;

		if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Shield && player.InvBody[INVLOC_HAND_LEFT]._iStatFlag) {
			maxd = 3;
		}

		if (player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Shield && player.InvBody[INVLOC_HAND_RIGHT]._iStatFlag) {
			maxd = 3;
		}

		if (player._pHeroClass == HeroClass::Monk) {
			mind = std::max(mind, player._pLevel / 2);
			maxd = std::max(maxd, (int)player._pLevel);
		}
	}

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive)) {
		sadd += 2 * player._pLevel;
		dadd += player._pLevel + player._pLevel / 2;
		vadd += 2 * player._pLevel;
	}
	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageCooldown)) {
		sadd -= 2 * player._pLevel;
		dadd -= player._pLevel + player._pLevel / 2;
		vadd -= 2 * player._pLevel;
	}

	player._pIMinDam = mind;
	player._pIMaxDam = maxd;
	player._pIAC = tac;
	player._pIBonusDam = bdam;
	player._pIBonusToHit = btohit;
	player._pIBonusAC = bac;
	player._pIFlags = iflgs;
	player.pDamAcFlags = pDamAcFlags;
	player._pIBonusDamMod = dmod;
	player._pIGetHit = ghit;
	player._pManaCostMod = clamp(manaCostMod, -100, 100);

	if (player.pSneak) {
		// Players can use sneak + rings of the night (-20% light radius) or rings of the dark (-40% light radius) to reach the minimum light radius of 2.
		if (player._pHeroClass == HeroClass::Rogue)
			lrad -= 8;
		else
			lrad -= 4;
	}
	lrad = clamp(lrad, 2, 15);
	if (player._pLightRad != lrad) {
		ChangeVisionRadius(player.getId(), lrad);
#if !JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
		ChangeLightRadius(player.lightId, lrad);
#endif
		player._pLightRad = lrad;
	}

	player._pStrength = std::max(0, sadd + player._pBaseStr);
	player._pMagic = std::max(0, madd + player._pBaseMag);
	player._pDexterity = std::max(0, dadd + player._pBaseDex);
	player._pVitality = std::max(0, vadd + player._pBaseVit);

#if JWK_USE_CONSISTENT_MELEE_AND_RANGED_DAMAGE // Use the same damage formula for all classes.  Each class already gets a huge benefit when using their preferred weapon because of hit chance and swing speed
	bool okForMonk = false;
	if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Bow || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Bow) {
		player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 200;
	}
	else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Staff || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Staff) {
		player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 150;
		okForMonk = true;
	}
	else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Axe || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Axe) {
		player._pDamageMod = player._pLevel * player._pStrength / 100;
	}
	else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Sword || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Sword) { // note: swords do 150%x to animals, 66% to undead
		player._pDamageMod = player._pLevel * player._pStrength / 125 + player._pLevel * player._pDexterity / 250;
	}
	else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Mace || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Mace) { // note: maces do 66% to animals, 150% to undead
		player._pDamageMod = player._pLevel * player._pStrength / 125 + player._pLevel * player._pDexterity / 250;
	}
	else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Shield || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Shield) { // unarmed with shield
		player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 200;
	}
	else { // completely unarmed
		if (player._pHeroClass == HeroClass::Monk) {
			player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 200;
		} else {
			player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 300;
		}
		okForMonk = true;
	}
	
	if (player._pHeroClass == HeroClass::Monk && !okForMonk) {
		// Monks do less damage unless they're unarmed or using a staff
		player._pDamageMod /= 2;
	}
	else if (player._pHeroClass == HeroClass::Barbarian) {
		// Barbarians get natural armor but they get less benefit from armor on shields
		if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Shield)
			player._pIAC -= player.InvBody[INVLOC_HAND_LEFT]._iAC / 2;
		else if (player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Shield)
			player._pIAC -= player.InvBody[INVLOC_HAND_RIGHT]._iAC / 2;
		player._pIAC += player._pLevel / 4;
	}
#else // original code:
	if (player._pHeroClass == HeroClass::Rogue) {
		player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 200;
	} else if (player._pHeroClass == HeroClass::Monk) {
		player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 150;
		if ((!player.InvBody[INVLOC_HAND_LEFT].isEmpty() && player.InvBody[INVLOC_HAND_LEFT]._itype != ItemType::Staff) || (!player.InvBody[INVLOC_HAND_RIGHT].isEmpty() && player.InvBody[INVLOC_HAND_RIGHT]._itype != ItemType::Staff))
			player._pDamageMod /= 2; // Monks get half the normal damage bonus if they're holding a non-staff weapon
	} else if (player._pHeroClass == HeroClass::Bard) {
		if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Sword || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Sword)
			player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 150;
		else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Bow || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Bow) {
			player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 250;
		} else {
			player._pDamageMod = player._pLevel * player._pStrength / 100;
		}
	} else if (player._pHeroClass == HeroClass::Barbarian) {
		if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Axe || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Axe) {
			player._pDamageMod = player._pLevel * player._pStrength / 75;
		} else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Mace || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Mace) {
			player._pDamageMod = player._pLevel * player._pStrength / 75;
		} else if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Bow || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Bow) {
			player._pDamageMod = player._pLevel * player._pStrength / 300;
		} else {
			player._pDamageMod = player._pLevel * player._pStrength / 100;
		}
		if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Shield || player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Shield) {
			if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Shield)
				player._pIAC -= player.InvBody[INVLOC_HAND_LEFT]._iAC / 2;
			else if (player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Shield)
				player._pIAC -= player.InvBody[INVLOC_HAND_RIGHT]._iAC / 2;
		} else if (IsNoneOf(player.InvBody[INVLOC_HAND_LEFT]._itype, ItemType::Staff, ItemType::Bow) && IsNoneOf(player.InvBody[INVLOC_HAND_RIGHT]._itype, ItemType::Staff, ItemType::Bow)) {
			player._pDamageMod += player._pLevel * player._pVitality / 100;
		}
		player._pIAC += player._pLevel / 4;
	} else if (player._pHeroClass == HeroClass::Warrior) {
		player._pDamageMod = player._pLevel * player._pStrength / 100;
	} else { // HeroClass::Sorcerer
		// original code: player._pDamageMod = player._pLevel * player._pStrength / 100;
		player._pDamageMod = player._pLevel * (player._pStrength + player._pDexterity) / 200;
	}
#endif

	player._pISpells = spl;

	EnsureValidReadiedSpell(player);

	player._pISplLvlAdd = spllvladd;
	player._pArmorPierce = enac;

	if (player._pHeroClass == HeroClass::Barbarian) {
		mr += player._pLevel;
		fr += player._pLevel;
		lr += player._pLevel;
	}

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageCooldown)) {
		mr -= player._pLevel;
		fr -= player._pLevel;
		lr -= player._pLevel;
	}

	if (HasAnyOf(iflgs, ItemSpecialEffect::ZeroResistance)) {
		// reset resistances to zero if the respective special effect is active
		mr = 0;
		fr = 0;
		lr = 0;
	}

	player._pMagResist = clamp(mr, 0, MaxResistance);
	player._pFireResist = clamp(fr, 0, MaxResistance);
	player._pLghtResist = clamp(lr, 0, MaxResistance);

	vadd = (vadd * PlayersData[static_cast<size_t>(player._pHeroClass)].itmLife) >> 6;
	ihp += (vadd << 6); // BUGFIX: blood boil can cause negative shifts here (see line 757)

	madd = (madd * PlayersData[static_cast<size_t>(player._pHeroClass)].itmMana) >> 6;
	imana += (madd << 6);

	player._pMaxHP = ihp + player._pMaxHPBase;
	player._pHitPoints = std::min(ihp + player._pHPBase, player._pMaxHP);

	if (&player == MyPlayer && (player._pHitPoints >> 6) <= 0) {
		SetPlayerHitPoints(player, 0);
	}

	player._pMaxMana = imana + player._pMaxManaBase;
	player._pMana = std::min(imana + player._pManaBase, player._pMaxMana);

	player._pIFMinDam = fmin;
	player._pIFMaxDam = fmax;
	player._pILMinDam = lmin;
	player._pILMaxDam = lmax;

	player._pBlockFlag = false;
	if (player._pHeroClass == HeroClass::Monk) {
		if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Staff && player.InvBody[INVLOC_HAND_LEFT]._iStatFlag) {
			player._pBlockFlag = true;
			player._pIFlags |= ItemSpecialEffect::FastBlock;
		}
		if (player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Staff && player.InvBody[INVLOC_HAND_RIGHT]._iStatFlag) {
			player._pBlockFlag = true;
			player._pIFlags |= ItemSpecialEffect::FastBlock;
		}
		if (player.InvBody[INVLOC_HAND_LEFT].isEmpty() && player.InvBody[INVLOC_HAND_RIGHT].isEmpty())
			player._pBlockFlag = true;
		if (player.InvBody[INVLOC_HAND_LEFT]._iClass == ICLASS_WEAPON && player.GetItemLocation(player.InvBody[INVLOC_HAND_LEFT]) != ILOC_TWOHAND && player.InvBody[INVLOC_HAND_RIGHT].isEmpty())
			player._pBlockFlag = true;
		if (player.InvBody[INVLOC_HAND_RIGHT]._iClass == ICLASS_WEAPON && player.GetItemLocation(player.InvBody[INVLOC_HAND_RIGHT]) != ILOC_TWOHAND && player.InvBody[INVLOC_HAND_LEFT].isEmpty())
			player._pBlockFlag = true;
	}

	ItemType weaponItemType = ItemType::None;
	bool holdsShield = false;
	if (!player.InvBody[INVLOC_HAND_LEFT].isEmpty()
	    && player.InvBody[INVLOC_HAND_LEFT]._iClass == ICLASS_WEAPON
	    && player.InvBody[INVLOC_HAND_LEFT]._iStatFlag) {
		weaponItemType = player.InvBody[INVLOC_HAND_LEFT]._itype;
	}

	if (!player.InvBody[INVLOC_HAND_RIGHT].isEmpty()
	    && player.InvBody[INVLOC_HAND_RIGHT]._iClass == ICLASS_WEAPON
	    && player.InvBody[INVLOC_HAND_RIGHT]._iStatFlag) {
		weaponItemType = player.InvBody[INVLOC_HAND_RIGHT]._itype;
	}

	if (player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Shield && player.InvBody[INVLOC_HAND_LEFT]._iStatFlag) {
		player._pBlockFlag = true;
		holdsShield = true;
	}
	if (player.InvBody[INVLOC_HAND_RIGHT]._itype == ItemType::Shield && player.InvBody[INVLOC_HAND_RIGHT]._iStatFlag) {
		player._pBlockFlag = true;
		holdsShield = true;
	}

	PlayerWeaponGraphic animWeaponId = holdsShield ? PlayerWeaponGraphic::UnarmedShield : PlayerWeaponGraphic::Unarmed;
	switch (weaponItemType) {
	case ItemType::Sword:
		animWeaponId = holdsShield ? PlayerWeaponGraphic::SwordShield : PlayerWeaponGraphic::Sword;
		break;
	case ItemType::Axe:
		animWeaponId = PlayerWeaponGraphic::Axe;
		break;
	case ItemType::Bow:
		animWeaponId = PlayerWeaponGraphic::Bow;
		break;
	case ItemType::Mace:
		animWeaponId = holdsShield ? PlayerWeaponGraphic::MaceShield : PlayerWeaponGraphic::Mace;
		break;
	case ItemType::Staff:
		animWeaponId = PlayerWeaponGraphic::Staff;
		break;
	default:
		break;
	}

	PlayerArmorGraphic animArmorId = PlayerArmorGraphic::Light;
	if (player.InvBody[INVLOC_CHEST]._itype == ItemType::HeavyArmor && player.InvBody[INVLOC_CHEST]._iStatFlag) {
		if (player._pHeroClass == HeroClass::Monk && player.InvBody[INVLOC_CHEST]._iMagical == ITEM_QUALITY_UNIQUE)
			player._pIAC += player._pLevel / 2;
		animArmorId = PlayerArmorGraphic::Heavy;
	} else if (player.InvBody[INVLOC_CHEST]._itype == ItemType::MediumArmor && player.InvBody[INVLOC_CHEST]._iStatFlag) {
		if (player._pHeroClass == HeroClass::Monk) {
			if (player.InvBody[INVLOC_CHEST]._iMagical == ITEM_QUALITY_UNIQUE)
				player._pIAC += player._pLevel * 2;
			else
				player._pIAC += player._pLevel / 2;
		}
		animArmorId = PlayerArmorGraphic::Medium;
	} else if (player._pHeroClass == HeroClass::Monk) {
		player._pIAC += player._pLevel * 2;
	}

	const uint8_t gfxNum = static_cast<uint8_t>(animWeaponId) | static_cast<uint8_t>(animArmorId);
	if (player._pgfxnum != gfxNum && loadgfx) {
		player._pgfxnum = gfxNum;
		ResetPlayerGFX(player);
		SetPlrAnims(player);
		player.previewCelSprite = std::nullopt;
		player_graphic graphic = player.getGraphic();
		int8_t numberOfFrames;
		int8_t ticksPerFrame;
		player.getAnimationFramesAndTicksPerFrame(graphic, numberOfFrames, ticksPerFrame);
		LoadPlrGFX(player, graphic);
		OptionalClxSpriteList sprites;
		if (!HeadlessMode) {
			auto& animData = player.AnimationData[static_cast<size_t>(graphic)];
			if (animData.sprites.has_value()) {
				sprites = animData.spritesForDirection(player._pdir);
			} else {
				// In multiplayer games, a remote player can unequip their shield while that player is blocking an attack on the host.
				// This results in a nonexistant animation state on the host where the remote player must block with no shield equipped.
				// ie, (graphic == player_graphic::Block && !player._pBlockFlag) requests warrior animation "whnbl"
				// Attempting to load the nonexistant animation crashes the host.
				// This could also happen when unequipping a weapon during an attack, etc.
				// To avoid the crash, we can set the remote player into a standing animation before updating the items on their sprite.
				graphic = player_graphic::Stand;
				NewPlrAnim(player, graphic, player._pdir);
				player.getAnimationFramesAndTicksPerFrame(graphic, numberOfFrames, ticksPerFrame);
				sprites = player.AnimationData[static_cast<size_t>(graphic)].spritesForDirection(player._pdir);
			}
		}
		player.AnimInfo.changeAnimationData(sprites, numberOfFrames, ticksPerFrame);
	} else {
		player._pgfxnum = gfxNum;
	}

	if (&player == MyPlayer) {
		if (player.InvBody[INVLOC_AMULET].isEmpty() || player.InvBody[INVLOC_AMULET].IDidx != IDI_AURIC) {
			int half = MaxGold;
			MaxGold = GOLD_MAX_LIMIT;

			if (half != MaxGold)
				StripTopGold(player); // force gold into ordinary-sized stacks
		} else {
			MaxGold = GOLD_MAX_LIMIT * 2; // player is wearing amulet which allows double gold size stacks
		}
	}

	RedrawComponent(PanelDrawComponent::Mana);
	RedrawComponent(PanelDrawComponent::Health);
}

// This is called after processing network packets when a remote player (or the local player) changes gear
void CalcPlayerInventory(Player &player, bool loadgfx)
{
	// Determine the players current stats, this updates the statFlag on all equipped items that became unusable after a change in equipment.
	UnequipGearWhichCantBeWorn(player);

	// Ensure we don't load graphics for players that aren't on our level
	if (&player != MyPlayer && !player.isOnActiveLevel()) {
		loadgfx = false;
	}
	// Determine the current item bonuses gained from usable equipped items
	CalcPlayerPowerFromItems(player, loadgfx);

	if (&player == MyPlayer) {
		// Now that stat gains from equipped items have been calculated, mark unusable scrolls etc
		for (Item &item : InventoryAndBeltPlayerItemsRange { player }) {
			item.updateRequiredStatsCacheForPlayer(player);
		}
		player.CalcScrolls();
		CalcPlrStaff(player);
		if (IsStashOpen) {
			// If stash is open, ensure the items are displayed correctly
			Stash.RefreshItemStatFlags();
		}
		if (!player.HoldItem.isEmpty())
			player.HoldItem.updateRequiredStatsCacheForPlayer(player);
	}
}

// Like a constructor, initialize item with default values (no random values)
void InitializeItemToDefaultValues(Item &item, BaseItemIdx itemIdx)
{
	auto &pAllItem = AllItemsList[static_cast<size_t>(itemIdx)];

	// zero-initialize struct
	item = {};

	item._itype = pAllItem.itype;
	item._iCurs = pAllItem.iCurs;
	CopyUtf8(item._iName, pAllItem.iName, sizeof(item._iName));
	CopyUtf8(item._iIName, pAllItem.iName, sizeof(item._iIName));
	item._iLoc = pAllItem.iLoc;
	item._iClass = pAllItem.iClass;
	item._iMinDam = pAllItem.iMinDam;
	item._iMaxDam = pAllItem.iMaxDam;
	item._iAC = pAllItem.iMinAC;
	item._iMiscId = pAllItem.iMiscId;
	item._iSpell = pAllItem.iSpell;

	if (pAllItem.iMiscId == IMISC_STAFF) {
		item._iCharges = gbIsHellfire ? 18 : 40;
	}

	item._iMaxCharges = item._iCharges;
	item._iDurability = pAllItem.iDurability;
	item._iMaxDur = pAllItem.iDurability;
	item._iMinStr = pAllItem.iMinStr;
	item._iMinMag = pAllItem.iMinMag;
	item._iMinDex = pAllItem.iMinDex;
	item._ivalue = pAllItem.iValue;
	item._iIvalue = pAllItem.iValue;
	item._iPrefixPower = IPL_INVALID;
	item._iSuffixPower = IPL_INVALID;
	item._iMagical = ITEM_QUALITY_NORMAL;
	item.IDidx = itemIdx;
	if (gbIsHellfire)
		item.dwBuff |= CF_HELLFIRE;
}

void GenerateNewSeed(Item &item)
{
	item._iSeed = AdvanceRndSeed();
}

int GetGoldCursor(int value)
{
	if (value >= GOLD_MEDIUM_LIMIT)
		return ICURS_GOLD_LARGE;

	if (value <= GOLD_SMALL_LIMIT)
		return ICURS_GOLD_SMALL;

	return ICURS_GOLD_MEDIUM;
}

void SetPlrHandGoldCurs(Item &gold)
{
	gold._iCurs = GetGoldCursor(gold._ivalue);
}

// Called when a new character is created to give them default items for their class
void CreateNewPlayerItems(Player &player)
{
	for (auto &item : player.InvBody) {
		item.clear();
	}

	// converting this to a for loop creates a `rep stosd` instruction,
	// so this probably actually was a memset
	memset(&player.InvGrid, 0, sizeof(player.InvGrid));

	for (auto &item : player.InvList) {
		item.clear();
	}

	player._pNumInv = 0;

	for (auto &item : player.SpdList) {
		item.clear();
	}

	switch (player._pHeroClass) {
	case HeroClass::Warrior:
		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_LEFT], IDI_WARRIOR);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_LEFT]);

		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_RIGHT], IDI_WARRSHLD);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_RIGHT]);

		{
			Item club;
			InitializeItemToDefaultValues(club, IDI_WARRCLUB);
			GenerateNewSeed(club);
			AutoPlaceItemInInventorySlot(player, 10, club, true);
		}

		InitializeItemToDefaultValues(player.SpdList[0], IDI_HEAL);
		GenerateNewSeed(player.SpdList[0]);

		InitializeItemToDefaultValues(player.SpdList[1], IDI_HEAL);
		GenerateNewSeed(player.SpdList[1]);
		break;
	case HeroClass::Rogue:
		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_LEFT], IDI_ROGUE);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_LEFT]);

		InitializeItemToDefaultValues(player.SpdList[0], IDI_HEAL);
		GenerateNewSeed(player.SpdList[0]);

		InitializeItemToDefaultValues(player.SpdList[1], IDI_HEAL);
		GenerateNewSeed(player.SpdList[1]);
		break;
	case HeroClass::Sorcerer:
		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_LEFT], gbIsHellfire ? IDI_SORCERER : IDI_SORCERER_DIABLO);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_LEFT]);

		InitializeItemToDefaultValues(player.SpdList[0], gbIsHellfire ? IDI_HEAL : IDI_MANA);
		GenerateNewSeed(player.SpdList[0]);

		InitializeItemToDefaultValues(player.SpdList[1], gbIsHellfire ? IDI_HEAL : IDI_MANA);
		GenerateNewSeed(player.SpdList[1]);
		break;

	case HeroClass::Monk:
		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_LEFT], IDI_SHORTSTAFF);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_LEFT]);
		InitializeItemToDefaultValues(player.SpdList[0], IDI_HEAL);
		GenerateNewSeed(player.SpdList[0]);

		InitializeItemToDefaultValues(player.SpdList[1], IDI_HEAL);
		GenerateNewSeed(player.SpdList[1]);
		break;
	case HeroClass::Bard:
		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_LEFT], IDI_BARDSWORD);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_LEFT]);

		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_RIGHT], IDI_BARDDAGGER);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_RIGHT]);
		InitializeItemToDefaultValues(player.SpdList[0], IDI_HEAL);
		GenerateNewSeed(player.SpdList[0]);

		InitializeItemToDefaultValues(player.SpdList[1], IDI_HEAL);
		GenerateNewSeed(player.SpdList[1]);
		break;
	case HeroClass::Barbarian:
		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_LEFT], IDI_BARBARIAN);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_LEFT]);

		InitializeItemToDefaultValues(player.InvBody[INVLOC_HAND_RIGHT], IDI_WARRSHLD);
		GenerateNewSeed(player.InvBody[INVLOC_HAND_RIGHT]);
		InitializeItemToDefaultValues(player.SpdList[0], IDI_HEAL);
		GenerateNewSeed(player.SpdList[0]);

		InitializeItemToDefaultValues(player.SpdList[1], IDI_HEAL);
		GenerateNewSeed(player.SpdList[1]);
		break;
	}

	Item &goldItem = player.InvList[player._pNumInv];
	MakeGoldStackForInventory(goldItem, 100);

	player._pNumInv++;
	player.InvGrid[0] = player._pNumInv;

	player._pGold = goldItem._ivalue;

	CalcPlayerPowerFromItems(player, false);
}

int AllocateItem()
{
	assert(ActiveItemCount < MAXITEMS);

	int inum = ActiveItems[ActiveItemCount];
	ActiveItemCount++;

	Items[inum] = {};

	return inum;
}

uint8_t PlaceItemInWorld(Item &&item, WorldTilePosition position)
{
	assert(ActiveItemCount < MAXITEMS);

	uint8_t ii = ActiveItems[ActiveItemCount];
	ActiveItemCount++;

	dItem[position.x][position.y] = ii + 1;
	auto &item_ = Items[ii];
	item_ = std::move(item);
	item_.position = position;
	RespawnItem(item_, true);

	if (CornerStone.isAvailable() && position == CornerStone.position) {
		CornerStone.item = item_;
		InitQTextMsg(TEXT_CORNSTN);
		Quests[Q_CORNSTN]._qactive = QUEST_DONE;
	}

	return ii;
}

bool IsItemSpaceOk(Point position)
{
	if (!InDungeonBounds(position)) {
		return false;
	}

	if (IsTileSolid(position)) {
		return false;
	}

	if (dItem[position.x][position.y] != 0) {
		return false;
	}

	if (dMonster[position.x][position.y] != 0) {
		return false;
	}

	if (dPlayer[position.x][position.y] != 0) {
		return false;
	}

	if (IsItemBlockingObjectAtPosition(position)) {
		return false;
	}

	return true;
}

Point GetSuperItemLoc(Point position)
{
	std::optional<Point> itemPosition = FindClosestValidPosition(IsItemSpaceOk, position, 1, 50);

	return itemPosition.value_or(Point { 0, 0 }); // TODO handle no space for dropping items
}

void GenerateRandomPropertiesForBaseItem(Item &item, BaseItemIdx itemIndex, int maxLevel)
{
	auto &baseItemData = AllItemsList[static_cast<size_t>(itemIndex)];
	item._itype = baseItemData.itype;
	item._iCurs = baseItemData.iCurs;
	CopyUtf8(item._iName, baseItemData.iName, sizeof(item._iName));
	CopyUtf8(item._iIName, baseItemData.iName, sizeof(item._iIName));
	item._iLoc = baseItemData.iLoc;
	item._iClass = baseItemData.iClass;
	item._iMinDam = baseItemData.iMinDam;
	item._iMaxDam = baseItemData.iMaxDam;
	item._iAC = baseItemData.iMinAC + GenerateRnd(baseItemData.iMaxAC - baseItemData.iMinAC + 1);
	item._iFlags = baseItemData.iFlags;
	item._iMiscId = baseItemData.iMiscId;
	item._iSpell = baseItemData.iSpell;
	item._iMagical = ITEM_QUALITY_NORMAL;
	item._ivalue = baseItemData.iValue;
	item._iIvalue = baseItemData.iValue;
	item._iDurability = baseItemData.iDurability;
	item._iMaxDur = baseItemData.iDurability;
	item._iMinStr = baseItemData.iMinStr;
	item._iMinMag = baseItemData.iMinMag;
	item._iMinDex = baseItemData.iMinDex;
	item.IDidx = itemIndex;
	if (gbIsHellfire)
		item.dwBuff |= CF_HELLFIRE;
	item._iPrefixPower = IPL_INVALID;
	item._iSuffixPower = IPL_INVALID;

	if (item._iMiscId == IMISC_BOOK) {
		SelectRandomSpellBook(item, maxLevel, true);
	}
	else if (item._iMiscId == IMISC_OILOF && gbIsHellfire) {
		SelectRandomOil(item, maxLevel);
	}
	else if (item._itype == ItemType::Gold) {
		// Determine how much gold is dropped.  This branch of code only executes when dropping gold the first time (not when recreating items from seed).
		// Recreating items of ItemType::Gold is handled by RecreateItem() - the gold value is loaded from the bit-packed structure.
		int goldValue;
		int baseItemlevel = GetCurrentLevelForDrops();
		switch (sgGameInitInfo.nDifficulty) {
		case DIFF_NORMAL:
			goldValue = 5 * baseItemlevel + GenerateRnd(10 * baseItemlevel);
			break;
		case DIFF_NIGHTMARE:
			goldValue = 5 * (baseItemlevel + 16) + GenerateRnd(10 * (baseItemlevel + 16));
			break;
		case DIFF_HELL:
			goldValue = 5 * (baseItemlevel + 32) + GenerateRnd(10 * (baseItemlevel + 32));
			break;
		}
		if (leveltype == DTYPE_HELL) // extra gold bonus in hell
			goldValue += goldValue / 8;

		item._ivalue = std::min(goldValue, GOLD_MAX_LIMIT);
		SetPlrHandGoldCurs(item);
	}
}

void SetupItem(Item &item)
{
	item.setNewAnimation(MyPlayer != nullptr && MyPlayer->pLvlLoad == 0);
	item._iIdentified = false;
}

Item *CreateUniqueItem(UniqueItemIdx uid, Point position, std::optional<int> ilevel /*= std::nullopt*/, bool sendmsg /*= true*/, bool exactPosition /*= false*/)
{
	if (ActiveItemCount >= MAXITEMS)
		return nullptr;

	int ii = AllocateItem();
	auto &item = Items[ii];
	if (exactPosition && CanPut(position)) {
		item.position = position;
		dItem[position.x][position.y] = ii + 1;
	} else {
		GetSuperItemSpace(position, ii);
	}
	int baseItemLevel = GetCurrentBaseItemLevelForDrops();

	std::underlying_type_t<BaseItemIdx> idx = 0;
	while (AllItemsList[idx].iItemId != UniqueItems[uid].UIItemId)
		idx++;

	if (sgGameInitInfo.nDifficulty == DIFF_NORMAL) {
		GenerateRandomPropertiesForBaseItem(item, static_cast<BaseItemIdx>(idx), baseItemLevel);
		ApplyUniqueItemPropertiesToItem(item, uid);
		SetupItem(item);
	} else {
		int affixLevel = ilevel ? *ilevel : GetCurrentAffixLevelForDrops();
		const BaseItemData &uniqueItemData = AllItemsList[idx];
		BaseItemIdx idx = SelectRandomBaseItem(false, [&uniqueItemData](const BaseItemData &item) {
			return item.itype == uniqueItemData.itype;
		});
		ConstructItemFromSeed(item, idx, AdvanceRndSeed(), affixLevel, 15, true, false, false);
	}

	if (sendmsg)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);

	return &item;
}

void CreateItemFromMonster(Monster &monster, Point position, bool sendmsg, bool spawn)
{
	BaseItemIdx idx;
	bool onlygood = true;

	bool dropsSpecialTreasure = (monster.data().treasure & T_UNIQ) != 0;
	bool dropBrain = Quests[Q_MUSHROOM]._qactive == QUEST_ACTIVE && Quests[Q_MUSHROOM]._qvar1 == QS_MUSHGIVEN;

	if (dropsSpecialTreasure && !UseMultiplayerQuests()) {
		Item *uniqueItem = CreateUniqueItem(static_cast<UniqueItemIdx>(monster.data().treasure & T_MASK), position, std::nullopt, false);
		if (uniqueItem != nullptr && sendmsg)
			NetSendCmdPItem(false, CMD_DROPITEM, uniqueItem->position, *uniqueItem);
		return;
	} else if (monster.isUnique() || dropsSpecialTreasure) {
		// Unqiue monster is killed => use better item base (for example no gold)
		idx = SelectRandomBaseItemForHighQualityDrop();
	} else if (dropBrain && !gbIsMultiplayer) {
		// Normal monster is killed => need to drop brain to progress the quest
		Quests[Q_MUSHROOM]._qvar1 = QS_BRAINSPAWNED;
		NetSendCmdQuest(true, Quests[Q_MUSHROOM]);
		// brain replaces normal drop
		idx = IDI_BRAIN;
	} else {
		if (dropBrain && gbIsMultiplayer && sendmsg) {
			Quests[Q_MUSHROOM]._qvar1 = QS_BRAINSPAWNED;
			NetSendCmdQuest(true, Quests[Q_MUSHROOM]);
			// Drop the brain as extra item to ensure that all clients see the brain drop
			// When executing CreateItemFromMonster is not reliable, cause another client can already have the quest state updated before CreateItemFromMonster is executed
			Point posBrain = GetSuperItemLoc(position);
			SpawnQuestItem(IDI_BRAIN, posBrain, false, false, true);
		}
		// Normal monster
		if (GenerateRnd(100) > 40 || (monster.data().treasure & T_NODROP))
			return; // no loot
		onlygood = false;
		idx = SelectRandomBaseItemForDrop();
	}

	if (idx == IDI_NONE)
		return;

	if (ActiveItemCount >= MAXITEMS)
		return;

	int ii = AllocateItem();
	auto &item = Items[ii];
	GetSuperItemSpace(position, ii);

#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	int8_t mLevel = monster.level(sgGameInitInfo.nDifficulty);
#else
	int8_t mLevel = monster.data().level; // omitting the difficulty here seems like a huge bug which prevents monsters from dropping loot with affixes > lvl30
	if (!gbIsHellfire && monster.type().type == MT_DIABLO)
		mLevel -= 15;
#endif

	ConstructItemFromSeed(item, idx, AdvanceRndSeed(), mLevel, monster.isUnique() ? 15 : 1, onlygood, false, false);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (spawn)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

void CreateGold(Point position, int value, bool sendmsg, bool delta, bool spawn)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	int ii = AllocateItem();
	auto &item = Items[ii];
	item = {};
	
	if (value <= 0) { // choose a random value as if gold was dropped as monster loot
		int iLevelForLootDrop = GetCurrentBaseItemLevelForDrops();
		ConstructItemFromSeed(item, IDI_GOLD, AdvanceRndSeed(), iLevelForLootDrop, 1, false, false, delta);

		item._iSeed = AdvanceRndSeed();
		SetRndSeed(item._iSeed);
		GenerateRandomPropertiesForBaseItem(item, IDI_GOLD, iLevelForLootDrop / 2);
		SetupItem(item);
	} else {
		return;
		// clamp value
		//InitializeItemToDefaultValues(item, IDI_GOLD);
		//GenerateNewSeed(goldItem);
		//goldItem._iStatFlag = true;
		//goldItem._ivalue = value;
	}
	
	GetSuperItemSpace(position, ii);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (delta)
		DeltaAddItem(ii);
	if (spawn)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

void CreateRandomItemOfTypeOrMisc(Point position, ItemType itemType, int iMiscId, bool onlygood, bool sendmsg, bool delta, bool spawn)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	if (itemType == ItemType::Gold) {
		return CreateGold(position, 0, sendmsg, delta, spawn); // use more efficient code that avoids SelectRandomBaseItemOfType()
	}

	int maxBaseLevel = GetCurrentBaseItemLevelForDrops();
	BaseItemIdx baseItemIdx = SelectRandomBaseItemOfType(itemType, iMiscId, maxBaseLevel);
	if (baseItemIdx == BaseItemIdx::IDI_NONE) {
		return;
	}

	int ii = AllocateItem();
	auto &item = Items[ii];
	item = {};

	int maxAffixLevel = GetCurrentAffixLevelForDrops();
	ConstructItemFromSeed(item, baseItemIdx, AdvanceRndSeed(), maxAffixLevel, 1, onlygood, false, delta);
	
	GetSuperItemSpace(position, ii);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (delta)
		DeltaAddItem(ii);
	if (spawn)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

void CreateRandomItemSpecificBase(Point position, BaseItemIdx idx, bool onlygood, bool sendmsg, bool delta, bool spawn)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	int ii = AllocateItem();
	auto &item = Items[ii];
	GetSuperItemSpace(position, ii);

	int maxAffixLevel = GetCurrentAffixLevelForDrops();
	ConstructItemFromSeed(item, idx, AdvanceRndSeed(), maxAffixLevel, 1, onlygood, false, delta);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (delta)
		DeltaAddItem(ii);
	if (spawn)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

void CreateRandomItem(Point position, bool onlygood, bool sendmsg, bool delta)
{
	BaseItemIdx idx = onlygood ? SelectRandomBaseItemForHighQualityDrop() : SelectRandomBaseItemForDrop();
	CreateRandomItemSpecificBase(position, idx, onlygood, sendmsg, delta, false);
}
void CreateRandomItemOfType(Point position, ItemType itemType, bool onlygood, bool sendmsg, bool delta)
{
	int iMiscId = IMISC_NONE;
	if (itemType == ItemType::Staff) {
		iMiscId = IMISC_STAFF;
	} else if (itemType == ItemType::Ring) {
		iMiscId = IMISC_RING;
	} else if (itemType == ItemType::Amulet) {
		iMiscId = IMISC_AMULET;
	}
	CreateRandomItemOfTypeOrMisc(position, itemType, iMiscId, onlygood, sendmsg, delta, false);
}

// Spawn a random potion, scroll, etc
void CreateRandomUsefulItem(Point position, bool sendmsg)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	int ii = AllocateItem();
	auto &item = Items[ii];
	GetSuperItemSpace(position, ii);
	int curlv = GetCurrentBaseItemLevelForDrops();

	ConstructUsefulItemFromSeed(item, AdvanceRndSeed(), curlv);
	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
}

void CreateSpellBook(Point position, SpellID spellID, bool sendmsg, bool delta)
{
	if (ActiveItemCount >= MAXITEMS) {
		return;
	}
	if (spellID == SpellID::Null) {
		int curlv = GetCurrentBaseItemLevelForDrops();
		spellID = SelectRandomSpell(curlv, true, true);
	}

#if JWK_EDIT_SPELLBOOK_DROPS
	int lvl = GetSpellBookLevel(spellID, true);
	BaseItemIdx idx = IDI_BOOK1; // There's no difference between IDI_BOOK1, IDI_BOOK2, IDI_BOOK3, IDI_BOOK4 so just pick one
#else // original code
	int lvl = currlevel;
	if (gbIsHellfire) {
		lvl = GetSpellBookLevel(spellID, true) + 1;
		if (lvl < 1) {
			return;
		}
	}
	BaseItemIdx idx = SelectRandomBaseItemOfType(ItemType::Misc, IMISC_BOOK, 2*lvl);
#endif
	int ii = AllocateItem();
	auto &item = Items[ii];
	while (true) {
		item = {};
		ConstructItemFromSeed(item, idx, AdvanceRndSeed(), 2 * lvl, 1, true, false, delta);
		if (item._iMiscId == IMISC_BOOK && item._iSpell == spellID)
			break;
	}

	GetSuperItemSpace(position, ii);

	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
	if (delta)
		DeltaAddItem(ii);
}

void CornerstoneSave()
{
	if (!CornerStone.activated)
		return;
	if (!CornerStone.item.isEmpty()) {
		ItemPack id;
		PackItem(id, CornerStone.item, (CornerStone.item.dwBuff & CF_HELLFIRE) != 0);
		const auto *buffer = reinterpret_cast<uint8_t *>(&id);
		for (size_t i = 0; i < sizeof(ItemPack); i++) {
			fmt::format_to(&sgOptions.Hellfire.szItem[i * 2], "{:02X}", buffer[i]);
		}
		sgOptions.Hellfire.szItem[sizeof(sgOptions.Hellfire.szItem) - 1] = '\0';
	} else {
		sgOptions.Hellfire.szItem[0] = '\0';
	}
}

void CornerstoneLoad(Point position)
{
	ItemPack pkSItem;

	if (CornerStone.activated || position.x == 0 || position.y == 0) {
		return;
	}

	CornerStone.item.clear();
	CornerStone.activated = true;
	if (dItem[position.x][position.y] != 0) {
		int ii = dItem[position.x][position.y] - 1;
		for (int i = 0; i < ActiveItemCount; i++) {
			if (ActiveItems[i] == ii) {
				DeleteItem(i);
				break;
			}
		}
		dItem[position.x][position.y] = 0;
	}

	if (strlen(sgOptions.Hellfire.szItem) < sizeof(ItemPack) * 2)
		return;

	Hex2bin(sgOptions.Hellfire.szItem, sizeof(ItemPack), reinterpret_cast<uint8_t *>(&pkSItem));

	int ii = AllocateItem();
	auto &item = Items[ii];

	dItem[position.x][position.y] = ii + 1;

	UnPackItem(pkSItem, item, (pkSItem.dwBuff & CF_HELLFIRE) != 0);
	item.position = position;
	RespawnItem(item, false);
	CornerStone.item = item;
}

void SpawnQuestItem(BaseItemIdx itemid, Point position, int randarea, int selflag, bool sendmsg)
{
	if (randarea > 0) {
		int tries = 0;
		while (true) {
			tries++;
			if (tries > 1000 && randarea > 1)
				randarea--;

			position.x = GenerateRnd(MAXDUNX);
			position.y = GenerateRnd(MAXDUNY);

			bool failed = false;
			for (int i = 0; i < randarea && !failed; i++) {
				for (int j = 0; j < randarea && !failed; j++) {
					failed = !IsItemSpaceOk(position + Displacement { i, j });
				}
			}
			if (!failed)
				break;
		}
	}

	if (ActiveItemCount >= MAXITEMS)
		return;

	int ii = AllocateItem();
	auto &item = Items[ii];

	item.position = position;

	dItem[position.x][position.y] = ii + 1;

	int curlv = GetCurrentBaseItemLevelForDrops();
	GenerateRandomPropertiesForBaseItem(item, itemid, curlv);

	SetupItem(item);
	item._iSeed = AdvanceRndSeed();
	SetRndSeed(item._iSeed);
	item._iPostDraw = true;
	if (selflag != 0) {
		item._iSelFlag = selflag;
		item.AnimInfo.currentFrame = item.AnimInfo.numberOfFrames - 1;
		item._iAnimFlag = false;
	}

	if (sendmsg)
		NetSendCmdPItem(true, CMD_SPAWNITEM, item.position, item);
	else {
		item._iCreateInfo |= CF_PREGEN;
		DeltaAddItem(ii);
	}
}

void SpawnRewardItem(BaseItemIdx itemid, Point position, bool sendmsg)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	int ii = AllocateItem();
	auto &item = Items[ii];

	item.position = position;
	dItem[position.x][position.y] = ii + 1;
	int curlv = GetCurrentBaseItemLevelForDrops();
	GenerateRandomPropertiesForBaseItem(item, itemid, curlv);
	item.setNewAnimation(true);
	item._iSelFlag = 2;
	item._iPostDraw = true;
	item._iIdentified = true;
	GenerateNewSeed(item);

	if (sendmsg) {
		NetSendCmdPItem(true, CMD_SPAWNITEM, item.position, item);
	}
}

void SpawnMapOfDoom(Point position, bool sendmsg)
{
	SpawnRewardItem(IDI_MAPOFDOOM, position, sendmsg);
}

void SpawnRuneBomb(Point position, bool sendmsg)
{
	SpawnRewardItem(IDI_RUNEBOMB, position, sendmsg);
}

void SpawnTheodore(Point position, bool sendmsg)
{
	SpawnRewardItem(IDI_THEODORE, position, sendmsg);
}

void RespawnItem(Item &item, bool flipFlag)
{
	int it = ItemCAnimTbl[item._iCurs];
	item.setNewAnimation(flipFlag);
	item._iRequest = false;

	if (IsAnyOf(item._iCurs, ICURS_MAGIC_ROCK, ICURS_TAVERN_SIGN, ICURS_ANVIL_OF_FURY))
		item._iSelFlag = 1;
	else if (IsAnyOf(item._iCurs, ICURS_MAP_OF_THE_STARS, ICURS_RUNE_BOMB, ICURS_THEODORE, ICURS_AURIC_AMULET))
		item._iSelFlag = 2;

	if (item._iCurs == ICURS_MAGIC_ROCK) {
		PlaySfxLoc(ItemDropSnds[it], item.position);
	}
}

void DeleteItem(int i)
{
	if (ActiveItemCount > 0)
		ActiveItemCount--;

	assert(i >= 0 && i < MAXITEMS && ActiveItemCount < MAXITEMS);

	if (pcursitem == ActiveItems[i]) // Unselect item if player has it highlighted
		pcursitem = -1;

	if (i < ActiveItemCount) {
		// If the deleted item was not already at the end of the active list, swap the indexes around to make the next item allocation simpler.
		std::swap(ActiveItems[i], ActiveItems[ActiveItemCount]);
	}
}

void RemoveItemsOnFloorThatShouldNotExist()
{
	if (!gbIsMultiplayer)
		return;

	// To reduce performance cost, only search one line of floor tiles (y=const) every game tick
	static int idoppely = 16;

	for (int idoppelx = 16; idoppelx < 96; idoppelx++) {
		if (dItem[idoppelx][idoppely] != 0) {
			Item *i = &Items[dItem[idoppelx][idoppely] - 1];
			if (i->position.x != idoppelx || i->position.y != idoppely)
				dItem[idoppelx][idoppely] = 0;
		}
	}

	idoppely++;
	if (idoppely == 96)
		idoppely = 16;
}

void ProcessItems()
{
	for (int i = 0; i < ActiveItemCount; i++) {
		int ii = ActiveItems[i];
		auto &item = Items[ii];
		if (!item._iAnimFlag)
			continue;
		item.AnimInfo.processAnimation();
		if (item._iCurs == ICURS_MAGIC_ROCK) {
			if (item._iSelFlag == 1 && item.AnimInfo.currentFrame == 10)
				item.AnimInfo.currentFrame = 0;
			if (item._iSelFlag == 2 && item.AnimInfo.currentFrame == 20)
				item.AnimInfo.currentFrame = 10;
		} else {
			if (item.AnimInfo.currentFrame == (item.AnimInfo.numberOfFrames - 1) / 2)
				PlaySfxLoc(ItemDropSnds[ItemCAnimTbl[item._iCurs]], item.position);

			if (item.AnimInfo.isLastFrame()) {
				item.AnimInfo.currentFrame = item.AnimInfo.numberOfFrames - 1;
				item._iAnimFlag = false;
				item._iSelFlag = 1;
			}
		}
	}
	RemoveItemsOnFloorThatShouldNotExist();
}

void FreeItemGFX()
{
	for (auto &itemanim : itemanims) {
		itemanim = std::nullopt;
	}
}

void GetItemFrm(Item &item)
{
	int it = ItemCAnimTbl[item._iCurs];
	if (itemanims[it])
		item.AnimInfo.sprites.emplace(*itemanims[it]);
}

void GetItemStr(Item &item)
{
	if (item._itype != ItemType::Gold) {
		InfoString = item.getName();
		InfoColor = item.getTextColor();
	} else {
		int nGold = item._ivalue;
		InfoString = fmt::format(fmt::runtime(ngettext("{:s} gold piece", "{:s} gold pieces", nGold)), FormatInteger(nGold));
	}
}

void CheckIdentify(Player &player, int cii)
{
	Item *pi;

	if (cii >= NUM_INVLOC)
		pi = &player.InvList[cii - NUM_INVLOC];
	else
		pi = &player.InvBody[cii];

	pi->_iIdentified = true;
	CalcPlayerInventory(player, true);
}

void DoRepair(Player &player, int cii)
{
	Item *pi;

	PlaySfxLoc(IS_REPAIR, player.position.tile);

	if (cii >= NUM_INVLOC) {
		pi = &player.InvList[cii - NUM_INVLOC];
	} else {
		pi = &player.InvBody[cii];
	}

	RepairItem(*pi, player);
	CalcPlayerInventory(player, true);
}

void DoRecharge(Player &player, int cii)
{
	Item *pi;

	if (cii >= NUM_INVLOC) {
		pi = &player.InvList[cii - NUM_INVLOC];
	} else {
		pi = &player.InvBody[cii];
	}

	RechargeItem(*pi, player);
	CalcPlayerInventory(player, true);
}

bool DoOil(Player &player, int cii)
{
	Item *pi;
	if (cii >= NUM_INVLOC) {
		pi = &player.InvList[cii - NUM_INVLOC];
	} else {
		pi = &player.InvBody[cii];
	}
	if (!ApplyOilToItem(*pi, player))
		return false;
	CalcPlayerInventory(player, true);
	return true;
}

[[nodiscard]] StringOrView PrintItemPower(char plidx, const Item &item)
{
	switch (plidx) {
	case IPL_TOHIT:
	case IPL_TOHIT_CURSE:
		return fmt::format(fmt::runtime(_("chance to hit: {:+d}%")), item._iPLToHit);
	case IPL_DAMP:
	case IPL_DAMP_CURSE:
		return fmt::format(fmt::runtime(_(/*xgettext:no-c-format*/ "{:+d}% damage")), item._iPLDam);
	case IPL_TOHIT_DAMP:
	case IPL_TOHIT_DAMP_CURSE:
		return fmt::format(fmt::runtime(_("to hit: {:+d}%, {:+d}% damage")), item._iPLToHit, item._iPLDam);
	case IPL_ACP:
	case IPL_ACP_CURSE:
		return fmt::format(fmt::runtime(_(/*xgettext:no-c-format*/ "{:+d}% armor")), item._iPLAC);
	case IPL_SETAC:
	case IPL_AC_CURSE:
		return fmt::format(fmt::runtime(_("armor class: {:d}")), item._iAC);
	case IPL_FIRERES:
	case IPL_FIRERES_CURSE:
		if (item._iPLFR < MaxResistance)
			return fmt::format(fmt::runtime(_("Resist Fire: {:+d}%")), item._iPLFR);
		else
			return fmt::format(fmt::runtime(_("Resist Fire: {:+d}% MAX")), MaxResistance);
	case IPL_LIGHTRES:
	case IPL_LIGHTRES_CURSE:
		if (item._iPLLR < MaxResistance)
			return fmt::format(fmt::runtime(_("Resist Lightning: {:+d}%")), item._iPLLR);
		else
			return fmt::format(fmt::runtime(_("Resist Lightning: {:+d}% MAX")), MaxResistance);
	case IPL_MAGICRES:
	case IPL_MAGICRES_CURSE:
		if (item._iPLMR < MaxResistance)
			return fmt::format(fmt::runtime(_("Resist Magic: {:+d}%")), item._iPLMR);
		else
			return fmt::format(fmt::runtime(_("Resist Magic: {:+d}% MAX")), MaxResistance);
	case IPL_ALLRES:
		if (item._iPLFR < MaxResistance)
			return fmt::format(fmt::runtime(_("Resist All: {:+d}%")), item._iPLFR);
		else
			return fmt::format(fmt::runtime(_("Resist All: {:+d}% MAX")), MaxResistance);
	case IPL_SPLLVLADD:
		if (item._iSplLvlAdd > 0)
			return fmt::format(fmt::runtime(ngettext("spells are increased {:d} level", "spells are increased {:d} levels", item._iSplLvlAdd)), item._iSplLvlAdd);
		else if (item._iSplLvlAdd < 0)
			return fmt::format(fmt::runtime(ngettext("spells are decreased {:d} level", "spells are decreased {:d} levels", -item._iSplLvlAdd)), -item._iSplLvlAdd);
		else
			return _("spell levels unchanged (?)");
	case IPL_CHARGES:
		return _("Extra charges");
	case IPL_SPELL:
		return fmt::format(fmt::runtime(ngettext("{:d} {:s} charge", "{:d} {:s} charges", item._iMaxCharges)), item._iMaxCharges, pgettext("spell", GetSpellData(item._iSpell).sNameText));
	case IPL_FIREDAM:
		if (item._iFMinDam == item._iFMaxDam)
			return fmt::format(fmt::runtime(_("Fire hit damage: {:d}")), item._iFMinDam);
		else
			return fmt::format(fmt::runtime(_("Fire hit damage: {:d}-{:d}")), item._iFMinDam, item._iFMaxDam);
	case IPL_LIGHTDAM:
		if (item._iLMinDam == item._iLMaxDam)
			return fmt::format(fmt::runtime(_("Lightning hit damage: {:d}")), item._iLMinDam);
		else
			return fmt::format(fmt::runtime(_("Lightning hit damage: {:d}-{:d}")), item._iLMinDam, item._iLMaxDam);
	case IPL_STR:
	case IPL_STR_CURSE:
		return fmt::format(fmt::runtime(_("{:+d} to strength")), item._iPLStr);
	case IPL_MAG:
	case IPL_MAG_CURSE:
		return fmt::format(fmt::runtime(_("{:+d} to magic")), item._iPLMag);
	case IPL_DEX:
	case IPL_DEX_CURSE:
		return fmt::format(fmt::runtime(_("{:+d} to dexterity")), item._iPLDex);
	case IPL_VIT:
	case IPL_VIT_CURSE:
		return fmt::format(fmt::runtime(_("{:+d} to vitality")), item._iPLVit);
	case IPL_ATTRIBS:
	case IPL_ATTRIBS_CURSE:
		return fmt::format(fmt::runtime(_("{:+d} to all attributes")), item._iPLStr);
	case IPL_GETHIT_CURSE:
	case IPL_GETHIT:
		return fmt::format(fmt::runtime(_("{:+d} damage from enemies")), item._iPLGetHit);
	case IPL_LIFE:
	case IPL_LIFE_CURSE:
		return fmt::format(fmt::runtime(_("Hit Points: {:+d}")), item._iPLHP >> 6);
	case IPL_MANA:
	case IPL_MANA_CURSE:
		return fmt::format(fmt::runtime(_("Mana: {:+d}")), item._iPLMana >> 6);
	case IPL_DUR:
		return _("high durability");
	case IPL_DUR_CURSE:
		return _("decreased durability");
	case IPL_INDESTRUCTIBLE:
		return _("indestructible");
	case IPL_LIGHT:
		return fmt::format(fmt::runtime(_(/*xgettext:no-c-format*/ "+{:d}% light radius")), 10 * item._iPLLight);
	case IPL_LIGHT_CURSE:
		return fmt::format(fmt::runtime(_(/*xgettext:no-c-format*/ "-{:d}% light radius")), -10 * item._iPLLight);
	case IPL_MULT_ARROWS:
		return _("multiple arrows per shot");
	case IPL_FIRE_ARROWS:
		if (item._iFMinDam == item._iFMaxDam)
			return fmt::format(fmt::runtime(_("fire arrows damage: {:d}")), item._iFMinDam);
		else
			return fmt::format(fmt::runtime(_("fire arrows damage: {:d}-{:d}")), item._iFMinDam, item._iFMaxDam);
	case IPL_LIGHT_ARROWS:
		if (item._iLMinDam == item._iLMaxDam)
			return fmt::format(fmt::runtime(_("lightning arrows damage {:d}")), item._iLMinDam);
		else
			return fmt::format(fmt::runtime(_("lightning arrows damage {:d}-{:d}")), item._iLMinDam, item._iLMaxDam);
	case IPL_FIREBALL:
		if (item._iFMinDam == item._iFMaxDam)
			return fmt::format(fmt::runtime(_("fireball damage: {:d}")), item._iFMinDam);
		else
			return fmt::format(fmt::runtime(_("fireball damage: {:d}-{:d}")), item._iFMinDam, item._iFMaxDam);
	case IPL_THORNS:
		return _("attacker takes 1-3 damage");
	case IPL_NOMANA:
		return _("user loses all mana");
	case IPL_ABSHALFTRAP:
		return _("absorbs half of trap damage");
	case IPL_KNOCKBACK:
		return _("knocks target back");
	case IPL_3XDAMVDEM:
		return _(/*xgettext:no-c-format*/ "+200% damage vs. demons");
	case IPL_ALLRESZERO:
		return _("All Resistance equals 0");
	case IPL_STEALMANA:
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealMana3))
			return _(/*xgettext:no-c-format*/ "hit steals 3% mana");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealMana5))
			return _(/*xgettext:no-c-format*/ "hit steals 5% mana");
		return {};
	case IPL_STEALLIFE:
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealLife3))
			return _(/*xgettext:no-c-format*/ "hit steals 3% life");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::StealLife5))
			return _(/*xgettext:no-c-format*/ "hit steals 5% life");
		return {};
	case IPL_TARGAC:
		return _("penetrates target's armor");
	case IPL_FASTATTACK:
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::QuickAttack))
			return _("quick attack");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastAttack))
			return _("fast attack");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FasterAttack))
			return _("faster attack");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastestAttack))
			return _("fastest attack");
		return _("Another ability (NW)");
	case IPL_FASTRECOVER:
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastHitRecovery))
			return _("fast hit recovery");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FasterHitRecovery))
			return _("faster hit recovery");
		if (HasAnyOf(item._iFlags, ItemSpecialEffect::FastestHitRecovery))
			return _("fastest hit recovery");
		return _("Another ability (NW)");
	case IPL_FASTBLOCK:
		return _("fast block");
#if JWK_ALLOW_FASTER_CASTING
	case IPL_FASTCAST:
		return _("fast cast");
#endif
#if JWK_ALLOW_MANA_COST_MODIFIER
	case IPL_MANA_COST:
	case IPL_MANA_COST_CURSE:
		if (item._iPLManaCostMod <= 0) {
			return fmt::format(fmt::runtime(_("{:d}% chance of free spell")), -item._iPLManaCostMod);
		} else {
			return fmt::format(fmt::runtime(_("{:d}% chance of costly spell")), item._iPLManaCostMod);
		}
#endif
	case IPL_DAMMOD:
		return fmt::format(fmt::runtime(ngettext("adds {:d} point to damage", "adds {:d} points to damage", item._iPLDamMod)), item._iPLDamMod);
	case IPL_RNDARROWVEL:
		return _("fires random speed arrows");
	case IPL_SETDAM:
		return _("unusual item damage");
	case IPL_SETDUR:
		return _("altered durability");
	case IPL_ONEHAND:
		return _("one handed sword");
	case IPL_DRAINLIFE:
		return _("constantly lose hit points");
	case IPL_RNDSTEALLIFE:
		return _("life stealing");
	case IPL_NOMINSTR:
		return _("no strength requirement");
	case IPL_INVCURS:
		return { string_view(" ") };
	case IPL_ADDACLIFE:
		if (item._iFMinDam == item._iFMaxDam)
			return fmt::format(fmt::runtime(_("lightning damage: {:d}")), item._iFMinDam);
		else
			return fmt::format(fmt::runtime(_("lightning damage: {:d}-{:d}")), item._iFMinDam, item._iFMaxDam);
	case IPL_ADDMANAAC:
		return _("charged bolts on hits");
	case IPL_DEVASTATION:
		return _("occasional triple damage");
	case IPL_DECAY:
		return fmt::format(fmt::runtime(_(/*xgettext:no-c-format*/ "decaying {:+d}% damage")), item._iPLDam);
	case IPL_PERIL:
		return _("2x dmg to monst, 1x to you");
	case IPL_JESTERS:
		return std::string(_(/*xgettext:no-c-format*/ "Random 0 - 600% damage"));
	case IPL_CRYSTALLINE:
		return fmt::format(fmt::runtime(_(/*xgettext:no-c-format*/ "low dur, {:+d}% damage")), item._iPLDam);
	case IPL_DOPPELGANGER:
		return fmt::format(fmt::runtime(_("to hit: {:+d}%, {:+d}% damage")), item._iPLToHit, item._iPLDam);
	case IPL_ACDEMON:
		return _("extra AC vs demons");
	case IPL_ACUNDEAD:
		return _("extra AC vs undead");
	case IPL_MANATOLIFE:
		return _("50% Mana moved to Health");
	case IPL_LIFETOMANA:
		return _("40% Health moved to Mana");
	default:
		return _("Another ability (NW)");
	}
}

void DrawUniqueInfo(const Surface &out)
{
	const Point position = GetRightPanel().position - Displacement { SidePanelSize.width, 0 };
	if (IsLeftPanelOpen() && GetLeftPanel().contains(position)) {
		return;
	}

	DrawUniqueInfoWindow(out);

	Rectangle rect { position + Displacement { 32, 56 }, { 257, 0 } };
	const UniqueItem &uitem = UniqueItems[curruitem._iUid];
	DrawString(out, _(uitem.UIName), rect, { UiFlags::AlignCenter });

	const Rectangle dividerLineRect { position + Displacement { 26, 25 }, { 267, 3 } };
	out.BlitFrom(out, MakeSdlRect(dividerLineRect), dividerLineRect.position + Displacement { 0, 5 * 12 + 13 });

	rect.position.y += (10 - uitem.UINumPL) * 12;
	assert(uitem.UINumPL <= sizeof(uitem.powers) / sizeof(*uitem.powers));
	for (const auto &power : uitem.powers) {
		if (power.type == IPL_INVALID)
			break;
		rect.position.y += 2 * 12;
		DrawString(out, PrintItemPower(power.type, curruitem), rect, { UiFlags::ColorWhite | UiFlags::AlignCenter });
	}
}

void PrintItemDetails(const Item &item)
{
	if (HeadlessMode)
		return;

	if (item._iClass == ICLASS_WEAPON) {
		if (item._iMinDam == item._iMaxDam) {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddPanelString(fmt::format(fmt::runtime(_("damage: {:d}  Indestructible")), item._iMinDam));
			else
				AddPanelString(fmt::format(fmt::runtime(_(/* TRANSLATORS: Dur: is durability */ "damage: {:d}  Dur: {:d}/{:d}")), item._iMinDam, item._iDurability, item._iMaxDur));
		} else {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddPanelString(fmt::format(fmt::runtime(_("damage: {:d}-{:d}  Indestructible")), item._iMinDam, item._iMaxDam));
			else
				AddPanelString(fmt::format(fmt::runtime(_(/* TRANSLATORS: Dur: is durability */ "damage: {:d}-{:d}  Dur: {:d}/{:d}")), item._iMinDam, item._iMaxDam, item._iDurability, item._iMaxDur));
		}
	}
	if (item._iClass == ICLASS_ARMOR) {
		if (item._iMaxDur == DUR_INDESTRUCTIBLE)
			AddPanelString(fmt::format(fmt::runtime(_("armor: {:d}  Indestructible")), item._iAC));
		else
			AddPanelString(fmt::format(fmt::runtime(_(/* TRANSLATORS: Dur: is durability */ "armor: {:d}  Dur: {:d}/{:d}")), item._iAC, item._iDurability, item._iMaxDur));
	}
	if (item._iMiscId == IMISC_STAFF && item._iMaxCharges != 0) {
		AddPanelString(fmt::format(fmt::runtime(_("Charges: {:d}/{:d}")), item._iCharges, item._iMaxCharges));
	}
	if (item._iPrefixPower != -1) {
		AddPanelString(PrintItemPower(item._iPrefixPower, item));
	}
	if (item._iSuffixPower != -1) {
		AddPanelString(PrintItemPower(item._iSuffixPower, item));
	}
	if (item._iMagical == ITEM_QUALITY_UNIQUE) {
		AddPanelString(_("unique item"));
		ShowUniqueItemInfoBox = true;
		curruitem = item;
	}
	PrintItemInfo(item);
}

void PrintItemDur(const Item &item)
{
	if (HeadlessMode)
		return;

	if (item._iClass == ICLASS_WEAPON) {
		if (item._iMinDam == item._iMaxDam) {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddPanelString(fmt::format(fmt::runtime(_("damage: {:d}  Indestructible")), item._iMinDam));
			else
				AddPanelString(fmt::format(fmt::runtime(_("damage: {:d}  Dur: {:d}/{:d}")), item._iMinDam, item._iDurability, item._iMaxDur));
		} else {
			if (item._iMaxDur == DUR_INDESTRUCTIBLE)
				AddPanelString(fmt::format(fmt::runtime(_("damage: {:d}-{:d}  Indestructible")), item._iMinDam, item._iMaxDam));
			else
				AddPanelString(fmt::format(fmt::runtime(_("damage: {:d}-{:d}  Dur: {:d}/{:d}")), item._iMinDam, item._iMaxDam, item._iDurability, item._iMaxDur));
		}
		if (item._iMiscId == IMISC_STAFF && item._iMaxCharges > 0) {
			AddPanelString(fmt::format(fmt::runtime(_("Charges: {:d}/{:d}")), item._iCharges, item._iMaxCharges));
		}
		if (item._iMagical != ITEM_QUALITY_NORMAL)
			AddPanelString(_("Not Identified"));
	}
	if (item._iClass == ICLASS_ARMOR) {
		if (item._iMaxDur == DUR_INDESTRUCTIBLE)
			AddPanelString(fmt::format(fmt::runtime(_("armor: {:d}  Indestructible")), item._iAC));
		else
			AddPanelString(fmt::format(fmt::runtime(_("armor: {:d}  Dur: {:d}/{:d}")), item._iAC, item._iDurability, item._iMaxDur));
		if (item._iMagical != ITEM_QUALITY_NORMAL)
			AddPanelString(_("Not Identified"));
		if (item._iMiscId == IMISC_STAFF && item._iMaxCharges > 0) {
			AddPanelString(fmt::format(fmt::runtime(_("Charges: {:d}/{:d}")), item._iCharges, item._iMaxCharges));
		}
	}
	if (IsAnyOf(item._itype, ItemType::Ring, ItemType::Amulet))
		AddPanelString(_("Not Identified"));
	PrintItemInfo(item);
}

void UseItem(size_t pnum, item_misc_id mid, SpellID spellID, int spellFrom)
{
	Player &player = Players[pnum];
	std::optional<SpellID> prepareSpellID;

	switch (mid) {
	case IMISC_HEAL:
		player.RestorePartialLife();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
		}
		break;
	case IMISC_FULLHEAL:
		player.RestoreFullLife();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
		}
		break;
	case IMISC_MANA:
		player.RestorePartialMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Mana);
		}
		break;
	case IMISC_FULLMANA:
		player.RestoreFullMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Mana);
		}
		break;
	case IMISC_ELIXSTR:
		ModifyPlrStr(player, 1);
		break;
	case IMISC_ELIXMAG:
		ModifyPlrMag(player, 1);
		if (gbIsHellfire) {
			player.RestoreFullMana();
			if (&player == MyPlayer) {
				RedrawComponent(PanelDrawComponent::Mana);
			}
		}
		break;
	case IMISC_ELIXDEX:
		ModifyPlrDex(player, 1);
		break;
	case IMISC_ELIXVIT:
		ModifyPlrVit(player, 1);
		if (gbIsHellfire) {
			player.RestoreFullLife();
			if (&player == MyPlayer) {
				RedrawComponent(PanelDrawComponent::Health);
			}
		}
		break;
	case IMISC_REJUV: {
		player.RestorePartialLife();
		player.RestorePartialMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
			RedrawComponent(PanelDrawComponent::Mana);
		}
	} break;
	case IMISC_FULLREJUV:
	case IMISC_ARENAPOT:
		player.RestoreFullLife();
		player.RestoreFullMana();
		if (&player == MyPlayer) {
			RedrawComponent(PanelDrawComponent::Health);
			RedrawComponent(PanelDrawComponent::Mana);
		}
		break;
	case IMISC_SCROLL:
	case IMISC_SCROLLT:
		if (ControlMode == ControlTypes::KeyboardAndMouse && GetSpellData(spellID).isTargeted()) {
			prepareSpellID = spellID;
		} else {
			const int spellLevel = player.GetSpellLevel(spellID);
			// Find a valid target for the spell because tile coords
			// will be validated when processing the network message
			Point target = cursPosition;
			if (!InDungeonBounds(target))
				target = player.position.future + Displacement(player._pdir);
			// Use CMD_SPELLXY because it's the same behavior as normal casting
			assert(IsValidSpellFrom(spellFrom));
			NetSendCmdLocParam3(true, CMD_SPELLXY, target, static_cast<int8_t>(spellID), static_cast<uint8_t>(SpellType::Scroll), static_cast<uint16_t>(spellFrom));
		}
		break;
	case IMISC_BOOK: {
		uint8_t newSpellLevel = player._pSplLvl[static_cast<int8_t>(spellID)] + 1;
		if (newSpellLevel <= MaxSpellLevel) {
			player._pSplLvl[static_cast<int8_t>(spellID)] = newSpellLevel;
			NetSendCmdParam2(true, CMD_CHANGE_SPELL_LEVEL, static_cast<uint16_t>(spellID), newSpellLevel);
		}
		if (HasNoneOf(player._pIFlags, ItemSpecialEffect::NoMana)) {
			player._pMana += GetSpellData(spellID).sManaCost << 6;
			player._pMana = std::min(player._pMana, player._pMaxMana);
			player._pManaBase += GetSpellData(spellID).sManaCost << 6;
			player._pManaBase = std::min(player._pManaBase, player._pMaxManaBase);
		}
		if (&player == MyPlayer) {
			for (Item &item : InventoryPlayerItemsRange { player }) {
				item.updateRequiredStatsCacheForPlayer(player);
			}
			if (IsStashOpen) {
				Stash.RefreshItemStatFlags();
			}
		}
		RedrawComponent(PanelDrawComponent::Mana);
	} break;
	case IMISC_MAPOFDOOM:
		doom_init();
		break;
	case IMISC_OILACC:
	case IMISC_OILMAST:
	case IMISC_OILSHARP:
	case IMISC_OILDEATH:
	case IMISC_OILSKILL:
	case IMISC_OILBSMTH:
	case IMISC_OILFORT:
	case IMISC_OILPERM:
	case IMISC_OILHARD:
	case IMISC_OILIMP:
		player._pOilType = mid;
		if (&player != MyPlayer) {
			return;
		}
		if (sbookflag) {
			sbookflag = false;
		}
		if (!invflag) {
			invflag = true;
		}
		NewCursor(CURSOR_OIL);
		break;
	case IMISC_SPECELIX:
		ModifyPlrStr(player, 3);
		ModifyPlrMag(player, 3);
		ModifyPlrDex(player, 3);
		ModifyPlrVit(player, 3);
		break;
	case IMISC_RUNEF:
		prepareSpellID = SpellID::RuneOfFire;
		break;
	case IMISC_RUNEL:
		prepareSpellID = SpellID::RuneOfLight;
		break;
	case IMISC_GR_RUNEL:
		prepareSpellID = SpellID::RuneOfNova;
		break;
	case IMISC_GR_RUNEF:
		prepareSpellID = SpellID::RuneOfImmolation;
		break;
	case IMISC_RUNES:
		prepareSpellID = SpellID::RuneOfStone;
		break;
	default:
		break;
	}

	if (prepareSpellID) {
		assert(IsValidSpellFrom(spellFrom));
		player.inventorySpell = *prepareSpellID;
		player.spellFrom = spellFrom;
		if (&player == MyPlayer)
			NewCursor(CURSOR_TELEPORT);
	}
}

bool UseItemOpensHive(const Item &item, Point position)
{
	if (item.IDidx != IDI_RUNEBOMB)
		return false;
	for (auto dir : PathDirs) {
		Point adjacentPosition = position + dir;
		if (OpensHive(adjacentPosition))
			return true;
	}
	return false;
}

bool UseItemOpensGrave(const Item &item, Point position)
{
	if (item.IDidx != IDI_MAPOFDOOM)
		return false;
	for (auto dir : PathDirs) {
		Point adjacentPosition = position + dir;
		if (OpensGrave(adjacentPosition))
			return true;
	}
	return false;
}

template <bool (*Ok)(const BaseItemData &), bool ConsiderDropRate = false>
static BaseItemIdx SelectRandomBaseItemForVendor(int minlvl, int maxlvl)
{
	return SelectRandomBaseItem(ConsiderDropRate, [&minlvl, &maxlvl](const BaseItemData &item) {
		if (!Ok(item))
			return false;
		if (item.iMinMLvl < minlvl || item.iMinMLvl > maxlvl)
			return false;
		return true;
	});
}

static void SortVendor(Item *itemList)
{
	int count = 1;
	while (!itemList[count].isEmpty())
		count++;

	auto cmp = [](const Item &a, const Item &b) {
		return a.IDidx < b.IDidx;
	};

	std::sort(itemList, itemList + count, cmp);
}

static bool IsSmithPremiumItemOk(const BaseItemData &item)
{
	if (item.itype == ItemType::Misc)
		return false;
	if (item.itype == ItemType::Gold)
		return false;
	if (!gbIsHellfire && item.itype == ItemType::Staff)
		return false;

	if (gbIsMultiplayer) {
		if (item.iMiscId == IMISC_OILOF)
			return false;
		if (item.itype == ItemType::Ring)
			return false;
		if (item.itype == ItemType::Amulet)
			return false;
	}

	return true;
}

static void ConstructSmithPremiumItemFromSeed(Item &premiumItem, uint32_t seed, int itemLevel)
{
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	int minLevelForBaseItem = FlipCoin(3) ? 1 : std::min(10, (itemLevel + 1) / 4); // itemLevel could be high so we need to make sure there are base items which exist
	int minLevelForAffixes = FlipCoin(3) ? 1 : (itemLevel + 1) / 2;
#else // original code
	int minLevelForBaseItem = itemLevel / 4;
	int minLevelForAffixes = itemLevel / 2;
#endif
	premiumItem._iSeed = seed;
	premiumItem._iCreateInfo = (itemLevel & CF_LEVEL) | CF_SMITH_PREMIUM;
	premiumItem._iIdentified = true;
	BaseItemIdx itemType = SelectRandomBaseItemForVendor<IsSmithPremiumItemOk>(minLevelForBaseItem, itemLevel);
	GenerateRandomPropertiesForBaseItem(premiumItem, itemType, itemLevel);
	GenerateAffixesForItemOfAnyType(premiumItem, minLevelForAffixes, itemLevel, true, false);
}

static void SpawnOnePremiumItemForSmith(Item &premiumItem, int itemLevel, const Player &player)
{
	int strength = std::max(player.GetMaximumAttributeValue(CharacterAttribute::Strength), player._pStrength);
	int dexterity = std::max(player.GetMaximumAttributeValue(CharacterAttribute::Dexterity), player._pDexterity);
	int magic = std::max(player.GetMaximumAttributeValue(CharacterAttribute::Magic), player._pMagic);
	strength += strength / 5;
	dexterity += dexterity / 5;
	magic += magic / 5;

	itemLevel = clamp(itemLevel, 1, 30); // Smith shouldn't sell the most valuable items

	int maxCount = 150;
	const bool unlimited = !gbIsHellfire; // TODO: This could lead to an infinite loop if a suitable item can never be generated
	// jwk - We shouldn't get an infinite loop as long as there are level 1 items, level 1 affixes, and we use a high enough MaxVendorValue so items don't get rejected
	
	for (int count = 0; unlimited || count < maxCount; count++) {
		premiumItem = {};
		uint32_t seed = AdvanceRndSeed();
		SetRndSeed(seed);
		ConstructSmithPremiumItemFromSeed(premiumItem, seed, itemLevel);

		if (!gbIsHellfire) { // in vanilla diablo, sell any item that's not too expensive
			if (premiumItem._iIvalue <= MaxVendorValue) {
				break;
			}
		} else { // In hellfire, try to select items more useful to the player's class
			int itemValue = 0;
			switch (premiumItem._itype) {
			case ItemType::LightArmor:
			case ItemType::MediumArmor:
			case ItemType::HeavyArmor: {
				const auto *const mostValuablePlayerArmor = player.GetMostValuableItem(
				    [](const Item &item) {
					    return IsAnyOf(item._itype, ItemType::LightArmor, ItemType::MediumArmor, ItemType::HeavyArmor);
				    });

				itemValue = mostValuablePlayerArmor == nullptr ? 0 : mostValuablePlayerArmor->_iIvalue;
				break;
			}
			case ItemType::Shield:
			case ItemType::Axe:
			case ItemType::Bow:
			case ItemType::Mace:
			case ItemType::Sword:
			case ItemType::Helm:
			case ItemType::Staff:
			case ItemType::Ring:
			case ItemType::Amulet: {
				const auto *const mostValuablePlayerItem = player.GetMostValuableItem(
				    [filterType = premiumItem._itype](const Item &item) { return item._itype == filterType; });

				itemValue = mostValuablePlayerItem == nullptr ? 0 : mostValuablePlayerItem->_iIvalue;
				break;
			}
			default:
				itemValue = 0;
				break;
			}
			itemValue = itemValue * 4 / 5;
			if (premiumItem._iIvalue <= MaxVendorValueHf
			    && premiumItem._iMinStr <= strength
			    && premiumItem._iMinMag <= magic
			    && premiumItem._iMinDex <= dexterity
			    && premiumItem._iIvalue >= itemValue) {
				break;
			}
		}
	}
	premiumItem._iStatFlag = player.CanUseItem(premiumItem);
}

void SpawnPremiumItemsForSmith(const Player &player)
{
#if JWK_SMITH_SELLS_MORE_ITEMS
	bool spawnMoreItems = true;
#else // original code
	bool spawnMoreItems = gbIsHellfire;
#endif
	int maxItems = spawnMoreItems ? SMITH_PREMIUM_ITEMS : 6;
	// Populate the shop with items
	if (gNumSmithPremiumItems < maxItems) {
		for (int i = 0; i < maxItems; i++) {
			if (gSmithPremiumItems[i].isEmpty()) {
				int level = gSmithPremiumItemLevel + (spawnMoreItems ? premiumLvlAddHellfire[i] : premiumlvladd[i]);
				SpawnOnePremiumItemForSmith(gSmithPremiumItems[i], level, player);
			}
		}
		gNumSmithPremiumItems = maxItems;
	}
	// If player levels up, toss out the lowest level items in the shop and replace with new ones
	while (gSmithPremiumItemLevel < player._pLevel) {
		gSmithPremiumItemLevel++;
		if (spawnMoreItems) {
			// Discard first 3 items and shift next 10
			std::move(&gSmithPremiumItems[3], &gSmithPremiumItems[12] + 1, &gSmithPremiumItems[0]);
			SpawnOnePremiumItemForSmith(gSmithPremiumItems[10], gSmithPremiumItemLevel + premiumLvlAddHellfire[10], player);
			gSmithPremiumItems[11] = gSmithPremiumItems[13];
			SpawnOnePremiumItemForSmith(gSmithPremiumItems[12], gSmithPremiumItemLevel + premiumLvlAddHellfire[12], player);
			gSmithPremiumItems[13] = gSmithPremiumItems[14];
			SpawnOnePremiumItemForSmith(gSmithPremiumItems[14], gSmithPremiumItemLevel + premiumLvlAddHellfire[14], player);
		} else {
			// Discard first 2 items and shift next 3
			std::move(&gSmithPremiumItems[2], &gSmithPremiumItems[4] + 1, &gSmithPremiumItems[0]);
			SpawnOnePremiumItemForSmith(gSmithPremiumItems[3], gSmithPremiumItemLevel + premiumlvladd[3], player);
			gSmithPremiumItems[4] = gSmithPremiumItems[5];
			SpawnOnePremiumItemForSmith(gSmithPremiumItems[5], gSmithPremiumItemLevel + premiumlvladd[5], player);
		}
	}
}

static bool IsSmithBasicItemOk(const BaseItemData &item)
{
	if (item.itype == ItemType::Misc)
		return false;
	if (item.itype == ItemType::Gold)
		return false;
	if (item.itype == ItemType::Staff && (!gbIsHellfire || IsValidSpell(item.iSpell)))
		return false;
	if (item.itype == ItemType::Ring)
		return false;
	if (item.itype == ItemType::Amulet)
		return false;

	return true;
}

static void ConstructSmithBasicItemFromSeed(Item &item, uint32_t seed, int itemLevel)
{
	BaseItemIdx baseItemIdx = SelectRandomBaseItemForVendor<IsSmithBasicItemOk, true>(0, itemLevel);
	GenerateRandomPropertiesForBaseItem(item, baseItemIdx, itemLevel);
	item._iSeed = seed;
	item._iCreateInfo = (itemLevel & CF_LEVEL) | CF_SMITH_BASIC;
	item._iIdentified = true;
}

void SpawnBasicItemsForSmith(int dungeonLevelUpTo16)
{
	constexpr int PinnedItemCount = 0;

	int maxValue = MaxVendorValue;
	int maxItems = JWK_SMITH_SELLS_MORE_ITEMS ? 25 : 20;
	if (gbIsHellfire) {
		maxValue = MaxVendorValueHf;
		maxItems = 25;
	}

	int iCnt = GenerateRnd(maxItems - 10) + 10;
	for (int i = 0; i < iCnt; i++) {
		Item &newItem = gSmithBasicItems[i];
		do {
			newItem = {};
			int seed = AdvanceRndSeed();
			SetRndSeed(seed);
			ConstructSmithBasicItemFromSeed(newItem, seed, dungeonLevelUpTo16);
		} while (newItem._iIvalue > maxValue);
	}
	for (int i = iCnt; i < SMITH_ITEMS; i++)
		gSmithBasicItems[i].clear();

	SortVendor(gSmithBasicItems + PinnedItemCount);
}

static bool IsWitchItemOk(const BaseItemData &item)
{
	if (IsNoneOf(item.itype, ItemType::Misc, ItemType::Staff))
		return false;
	if (item.iMiscId == IMISC_MANA)
		return false;
	if (item.iMiscId == IMISC_FULLMANA) // Don't include mana potions because these are explicitly added to her shop.
		return false;
	if (item.iSpell == SpellID::TownPortal)
		return false;
	if (item.iMiscId == IMISC_FULLHEAL)
		return false;
	if (item.iMiscId == IMISC_HEAL)
		return false;
#if JWK_HEALER_SELLS_VITALITY_ELIXIRS // don't allow witch to sell vitality elixers.  The reason she didn't sell them in the vanilla game was because they were level 20 items and her shop only sells up to level 16.
	if (item.iMiscId == IMISC_ELIXVIT)
		return false;
#endif
#if JWK_USE_MODIFIED_HELLFIRE_WITCH
	if (item.iMiscId == IMISC_BOOK)
		return false; // Books are explicitly added to her shop so they shouldn't appear as random items
#endif
	if (item.iMiscId > IMISC_OILFIRST && item.iMiscId < IMISC_OILLAST)
		return false;
	if (item.iSpell == SpellID::Resurrect && !gbIsMultiplayer)
		return false;
	if (item.iSpell == SpellID::HealOther && !gbIsMultiplayer)
		return false;

	return true;
}

static void ConstructWitchItemWithAffixes(Item &item, int dungeonLevelUpTo16)
{
	BaseItemIdx baseItemIdx = SelectRandomBaseItemForVendor<IsWitchItemOk>(0, dungeonLevelUpTo16);
	GenerateRandomPropertiesForBaseItem(item, baseItemIdx, dungeonLevelUpTo16);
	int iLevelForAffixes = -1;
	int minLevelForAffixes = -1;
#if JWK_USE_MODIFIED_HELLFIRE_WITCH
	if (item._iMiscId == IMISC_STAFF) {
		iLevelForAffixes = 2 * dungeonLevelUpTo16;
		minLevelForAffixes = FlipCoin(3) ? 1 : (iLevelForAffixes + 1) / 2;
	}
#else // original code
	if (GenerateRnd(100) <= 5) {
		iLevelForAffixes = 2 * dungeonLevelUpTo16; // I'm not sure what the point of this is because the only items the witch sells which have affixes are staves
	}
	if (iLevelForAffixes == -1 && item._iMiscId == IMISC_STAFF) {
		iLevelForAffixes = 2 * dungeonLevelUpTo16;
	}
	minLevelForAffixes = iLevelForAffixes / 2;
#endif
	if (iLevelForAffixes != -1) {
		GenerateAffixesForItemOfAnyType(item, minLevelForAffixes, iLevelForAffixes, true, true);
	}
}

static void RecreateWitchItemFromSeed(Item &item, BaseItemIdx idx, int dungeonLevelUpTo16, int iseed) // must match SpawnItemsForWitch()
{
	if (IsAnyOf(idx, IDI_MANA, IDI_FULLMANA, IDI_PORTAL)) {
		GenerateRandomPropertiesForBaseItem(item, idx, dungeonLevelUpTo16);
	} else if ((JWK_USE_MODIFIED_HELLFIRE_WITCH || gbIsHellfire) && idx >= IDI_BOOK1 && idx <= IDI_BOOK4) {
		// original code (only needed for vanilla compatability): DiscardRandomValues(1);
		GenerateRandomPropertiesForBaseItem(item, idx, dungeonLevelUpTo16);
	} else {
		ConstructWitchItemWithAffixes(item, dungeonLevelUpTo16);
	}
	item._iSeed = iseed;
	item._iCreateInfo = (dungeonLevelUpTo16 & CF_LEVEL) | CF_WITCH;
	item._iIdentified = true;
}

void SpawnItemsForWitch(int dungeonLevelUpTo16) // must match RecreateWitchItemFromSeed()
{
	constexpr std::array<BaseItemIdx, 3> PinnedItemTypes = { IDI_MANA, IDI_FULLMANA, IDI_PORTAL };
	constexpr std::array<BaseItemIdx, 4> PinnedBookTypes = { IDI_BOOK1, IDI_BOOK2, IDI_BOOK3, IDI_BOOK4 };

	int bookCount = 0;
	bool useHellfireWitch = JWK_USE_MODIFIED_HELLFIRE_WITCH || gbIsHellfire;
	const int pinnedBookCount = useHellfireWitch ? GenerateRnd(PinnedBookTypes.size()) : 0;
	const int reservedItems = useHellfireWitch ? 10 : 17;
	const int itemCount = GenerateRnd(WITCH_ITEMS - reservedItems) + 10;
	const int maxValue = useHellfireWitch ? MaxVendorValueHf : MaxVendorValue;

	for (int i = 0; i < WITCH_ITEMS; i++) {
		Item &item = gWitchItems[i];
		item = {};

		if (i < PinnedItemTypes.size()) {
			item._iSeed = AdvanceRndSeed();
			SetRndSeed(item._iSeed);
			GenerateRandomPropertiesForBaseItem(item, PinnedItemTypes[i], 1);
			item._iCreateInfo = dungeonLevelUpTo16 & CF_LEVEL;
			item._iStatFlag = true;
			continue;
		}

		if (useHellfireWitch) {
			// Always include a few spellbooks in the witch's shop.  Number of spellbooks increases with dungeonLevelUpTo16.  IDI_BOOK4 has iMinMLvl=20 which is always > dungeonLevelUpTo16 so witch will sell a maximum of 3 books at a time.
			if (i < PinnedItemTypes.size() + PinnedBookTypes.size() && bookCount < pinnedBookCount) {
				BaseItemIdx bookType = PinnedBookTypes[i - PinnedItemTypes.size()];
				if (dungeonLevelUpTo16 >= AllItemsList[bookType].iMinMLvl) {
					item._iSeed = AdvanceRndSeed();
					SetRndSeed(item._iSeed);
					// original code (only needed for vanilla compatability): DiscardRandomValues(1);
					GenerateRandomPropertiesForBaseItem(item, bookType, dungeonLevelUpTo16);
					item._iCreateInfo = (dungeonLevelUpTo16 & CF_LEVEL) | CF_WITCH;
					item._iIdentified = true;
					bookCount++;
					continue;
				}
			}
		}

		if (i >= itemCount) {
			item.clear(); // fill the remaining gWitchItems[] entries with empty items because SortVendor() expects it to be a null-item terminated list
			continue;
		}

		// Generate an random item that isn't too expensive
		do {
			item = {};
			item._iSeed = AdvanceRndSeed();
			SetRndSeed(item._iSeed);
			ConstructWitchItemWithAffixes(item, dungeonLevelUpTo16);
		} while (item._iIvalue > maxValue);

		item._iCreateInfo = (dungeonLevelUpTo16 & CF_LEVEL) | CF_WITCH;
		item._iIdentified = true;
	}
	SortVendor(gWitchItems + PinnedItemTypes.size());
}

static void ConstructBoyItemFromSeed(Item &item, uint32_t seed, int itemLevel)
{
	itemLevel = clamp<int>(itemLevel, 1, std::min((int)CF_LEVEL, MaxCharacterLevel));
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	int minLevelForBaseItem = FlipCoin() ? 1 : std::min(10, (itemLevel + 1) / 4); // itemLevel could be high so we need to make sure there are base items which exist
	int minLevelForAffixes = FlipCoin(4) ? 1 : std::min(25, (itemLevel + 1) / 2);
#else // original code
	int minLevelForBaseItem = 0; // This is why wirt often sells things like "Godly rags of the whale"
	int minLevelForAffixes = std::min(25, itemLevel);
#endif
	BaseItemIdx itype = SelectRandomBaseItemForVendor<IsSmithPremiumItemOk>(minLevelForBaseItem, itemLevel);
	GenerateRandomPropertiesForBaseItem(item, itype, itemLevel);
	GenerateAffixesForItemOfAnyType(item, minLevelForAffixes, itemLevel, true, true);
	item._iSeed = seed;
	item._iCreateInfo = itemLevel | CF_BOY;
	item._iIdentified = true;
}

void SpawnItemsForBoy(int playerLevel)
{
	int ivalue = 0;
	bool keepgoing = false;
	int count = 0;

	Player &myPlayer = *MyPlayer;

	int strength = std::max(myPlayer.GetMaximumAttributeValue(CharacterAttribute::Strength), myPlayer._pStrength);
	int dexterity = std::max(myPlayer.GetMaximumAttributeValue(CharacterAttribute::Dexterity), myPlayer._pDexterity);
	int magic = std::max(myPlayer.GetMaximumAttributeValue(CharacterAttribute::Magic), myPlayer._pMagic);
	strength += strength / 5;
	dexterity += dexterity / 5;
	magic += magic / 5;

	if (gPlayerLevelForBoyItem >= playerLevel && !gBoyItem.isEmpty()) // Don't create a new boy item if the current one is good enough (it might not be good enough if the player levels up)
		return;
	do {
		keepgoing = false;
		gBoyItem = {};
		int seed = AdvanceRndSeed();
		SetRndSeed(seed);
		ConstructBoyItemFromSeed(gBoyItem, seed, 2 * playerLevel);

		if (!gbIsHellfire) {
			if (gBoyItem._iIvalue > MaxBoyValue) {
				keepgoing = true; // prevent breaking the do/while loop too early by failing hellfire's condition in while
				continue;
			}
			break; // vanilla diablo takes the first item generated, allowing all possibilities for all characters
		}

		// Hellfire code:
		ivalue = 0;
		ItemType itemType = gBoyItem._itype;

		switch (itemType) {
		case ItemType::LightArmor:
		case ItemType::MediumArmor:
		case ItemType::HeavyArmor: {
			const auto *const mostValuablePlayerArmor = myPlayer.GetMostValuableItem(
			    [](const Item &item) {
				    return IsAnyOf(item._itype, ItemType::LightArmor, ItemType::MediumArmor, ItemType::HeavyArmor);
			    });

			ivalue = mostValuablePlayerArmor == nullptr ? 0 : mostValuablePlayerArmor->_iIvalue;
			break;
		}
		case ItemType::Shield:
		case ItemType::Axe:
		case ItemType::Bow:
		case ItemType::Mace:
		case ItemType::Sword:
		case ItemType::Helm:
		case ItemType::Staff:
		case ItemType::Ring:
		case ItemType::Amulet: {
			const auto *const mostValuablePlayerItem = myPlayer.GetMostValuableItem(
			    [itemType](const Item &item) { return item._itype == itemType; });

			ivalue = mostValuablePlayerItem == nullptr ? 0 : mostValuablePlayerItem->_iIvalue;
			break;
		}
		default:
			app_fatal("Invalid item spawn");
		}
		ivalue = ivalue * 4 / 5; // avoids forced int > float > int conversion

		count++;

		if (count < 200) {
			switch (myPlayer._pHeroClass) {
			case HeroClass::Warrior:
				if (IsAnyOf(itemType, ItemType::Bow, ItemType::Staff))
					ivalue = INT_MAX;
				break;
			case HeroClass::Rogue:
				if (IsAnyOf(itemType, ItemType::Sword, ItemType::Staff, ItemType::Axe, ItemType::Mace, ItemType::Shield))
					ivalue = INT_MAX;
				break;
			case HeroClass::Sorcerer:
				if (IsAnyOf(itemType, ItemType::Staff, ItemType::Axe, ItemType::Bow, ItemType::Mace))
					ivalue = INT_MAX;
				break;
			case HeroClass::Monk:
				if (IsAnyOf(itemType, ItemType::Bow, ItemType::MediumArmor, ItemType::Shield, ItemType::Mace))
					ivalue = INT_MAX;
				break;
			case HeroClass::Bard:
				if (IsAnyOf(itemType, ItemType::Axe, ItemType::Mace, ItemType::Staff))
					ivalue = INT_MAX;
				break;
			case HeroClass::Barbarian:
				if (IsAnyOf(itemType, ItemType::Bow, ItemType::Staff))
					ivalue = INT_MAX;
				break;
			}
		}
	} while (keepgoing
	    || ((
	            gBoyItem._iIvalue > MaxBoyValueHf
	            || gBoyItem._iMinStr > strength
	            || gBoyItem._iMinMag > magic
	            || gBoyItem._iMinDex > dexterity
	            || gBoyItem._iIvalue < ivalue)
	        && count < 250));
	gPlayerLevelForBoyItem = playerLevel;
}

static bool IsHealerItemOk(const BaseItemData &item)
{
	if (item.itype != ItemType::Misc)
		return false;
	if (item.iMiscId == IMISC_SCROLL)
		return item.iSpell == SpellID::Healing;
	if (item.iMiscId == IMISC_SCROLLT)
		return item.iSpell == SpellID::HealOther && gbIsMultiplayer;
#if 0 // original code (No longer valid because I removed "const Player &player" from the function arguments.  Item functions shouldn't depend on the player)
	if (!gbIsMultiplayer) {
		if (item.iMiscId == IMISC_ELIXSTR)
			return !gbIsHellfire || player._pBaseStr < player.GetMaximumAttributeValue(CharacterAttribute::Strength);
		if (item.iMiscId == IMISC_ELIXMAG)
			return !gbIsHellfire || player._pBaseMag < player.GetMaximumAttributeValue(CharacterAttribute::Magic);
		if (item.iMiscId == IMISC_ELIXDEX)
			return !gbIsHellfire || player._pBaseDex < player.GetMaximumAttributeValue(CharacterAttribute::Dexterity);
		if (item.iMiscId == IMISC_ELIXVIT)
			return !gbIsHellfire || player._pBaseVit < player.GetMaximumAttributeValue(CharacterAttribute::Vitality); // Note: This doesn't actually allow vitality elixirs because town item generation is capped at level 16, and vitality elixir has iMinMLvl=20
	}
#endif
	if (item.iMiscId == IMISC_REJUV)
		return true;
	if (item.iMiscId == IMISC_FULLREJUV)
		return true;

	return false;
}

static void RecreateHealerItemFromSeed(Item &item, BaseItemIdx idx, int iseed, int lvl) // must match SpawnItemsForHealer()
{
#if JWK_HEALER_SELLS_VITALITY_ELIXIRS
	if (IsAnyOf(idx, IDI_HEAL, IDI_FULLHEAL, IDI_RESURRECT, IDI_ELIXVIT)) {
#else // original code
	if (IsAnyOf(idx, IDI_HEAL, IDI_FULLHEAL, IDI_RESURRECT)) {
#endif
		GenerateRandomPropertiesForBaseItem(item, idx, lvl);
	} else {
		BaseItemIdx itype = SelectRandomBaseItemForVendor<IsHealerItemOk>(0, lvl);
		GenerateRandomPropertiesForBaseItem(item, itype, lvl);
	}
	item._iSeed = iseed;
	item._iCreateInfo = (lvl & CF_LEVEL) | CF_HEALER;
	item._iIdentified = true;
}

void SpawnItemsForHealer(int dungeonLevelUpTo16) // must match RecreateHealerItemFromSeed()
{
	constexpr int PinnedItemCount = 2;
	constexpr std::array<BaseItemIdx, PinnedItemCount + 1> PinnedItemTypes = { IDI_HEAL, IDI_FULLHEAL, IDI_RESURRECT };
	const int itemCount = GenerateRnd(gbIsHellfire ? 10 : 8) + 10;

	bool shouldSellVitality = false;
#if JWK_HEALER_SELLS_VITALITY_ELIXIRS
	if (MyPlayer->_pLevel >= 30 && FlipCoin(3)) {
		shouldSellVitality = true;
	}
#endif

	for (int i = 0; i < 20; i++) {
		Item &item = gHealerItems[i];
		item = {};

		if (i < PinnedItemCount || (gbIsMultiplayer && i == PinnedItemCount)) {
			item._iSeed = AdvanceRndSeed();
			GenerateRandomPropertiesForBaseItem(item, PinnedItemTypes[i], 1);
			item._iCreateInfo = dungeonLevelUpTo16 & CF_LEVEL;
			item._iStatFlag = true;
			continue;
		}
		
		if (i >= itemCount) {
			item.clear(); // gHealerItems[] is a null-item terminated list
			continue;
		}

		if (shouldSellVitality) {
			shouldSellVitality = false; // Only allow one elixer to be sold at a time
			BaseItemIdx itype = IDI_ELIXVIT;
			GenerateRandomPropertiesForBaseItem(item, itype, dungeonLevelUpTo16);
			item._iCreateInfo = (dungeonLevelUpTo16 & CF_LEVEL) | CF_HEALER;
			item._iIdentified = true;
			continue;
		}

		item._iSeed = AdvanceRndSeed();
		SetRndSeed(item._iSeed);
		BaseItemIdx itype = SelectRandomBaseItemForVendor<IsHealerItemOk>(0, dungeonLevelUpTo16);
		GenerateRandomPropertiesForBaseItem(item, itype, dungeonLevelUpTo16);
		item._iCreateInfo = (dungeonLevelUpTo16 & CF_LEVEL) | CF_HEALER;
		item._iIdentified = true;
	}

	SortVendor(gHealerItems + PinnedItemCount);
}

static void RecreateTownItem(Item &item, BaseItemIdx idx, uint16_t icreateinfo, int iseed)
{
	SetRndSeed(iseed);
	if ((icreateinfo & CF_SMITH_BASIC) != 0) {
		ConstructSmithBasicItemFromSeed(item, iseed, icreateinfo & CF_LEVEL);
	} else if ((icreateinfo & CF_SMITH_PREMIUM) != 0) {
		ConstructSmithPremiumItemFromSeed(item, iseed, icreateinfo & CF_LEVEL);
	} else if ((icreateinfo & CF_BOY) != 0) {
		ConstructBoyItemFromSeed(item, iseed, icreateinfo & CF_LEVEL);
	} else if ((icreateinfo & CF_WITCH) != 0) {
		RecreateWitchItemFromSeed(item, idx, icreateinfo & CF_LEVEL, iseed);
	} else if ((icreateinfo & CF_HEALER) != 0) {
		RecreateHealerItemFromSeed(item, idx, iseed, icreateinfo & CF_LEVEL);
	} else {
		app_fatal("It should be impossible for an item to have CF_TOWN flag but none of the vendor flags");
	}
}

// This is called to recreate items from their bit-packed format (items dropped on the ground in multiplayer, etc).
// See void RecreateItem(const Player &player, const TItem &messageItem, Item &item) in msg.cpp
void RecreateItem(Item &item, BaseItemIdx idx, uint16_t icreateinfo, uint32_t iseed, int ivalue, bool isHellfire)
{
	bool tmpIsHellfire = gbIsHellfire;
	gbIsHellfire = isHellfire;

	if (idx == IDI_GOLD) {
		InitializeItemToDefaultValues(item, IDI_GOLD);
		item._iSeed = iseed;
		item._iCreateInfo = icreateinfo;
		item._ivalue = ivalue;
		SetPlrHandGoldCurs(item);
		gbIsHellfire = tmpIsHellfire;
		return;
	}

	if (icreateinfo == 0) {
		InitializeItemToDefaultValues(item, idx);
		item._iSeed = iseed;
		gbIsHellfire = tmpIsHellfire;
		return;
	}

	if ((icreateinfo & CF_UNIQUE) == 0) {
		if ((icreateinfo & CF_TOWN) != 0) {
			RecreateTownItem(item, idx, icreateinfo, iseed);
			gbIsHellfire = tmpIsHellfire;
			return;
		}

		if ((icreateinfo & CF_USEFUL) == CF_USEFUL) {
			ConstructUsefulItemFromSeed(item, iseed, icreateinfo & CF_LEVEL);
			gbIsHellfire = tmpIsHellfire;
			return;
		}
	}

	int level = icreateinfo & CF_LEVEL;

	int chanceOfUnique = 0;
	if ((icreateinfo & CF_UPER1) != 0)
		chanceOfUnique = 1;
	if ((icreateinfo & CF_UPER15) != 0)
		chanceOfUnique = 15;

	bool onlygood = (icreateinfo & CF_ONLYGOOD) != 0;
	bool recreate = (icreateinfo & CF_UNIQUE) != 0;
	bool pregen = (icreateinfo & CF_PREGEN) != 0;

	ConstructItemFromSeed(item, idx, iseed, level, chanceOfUnique, onlygood, recreate, pregen);
	gbIsHellfire = tmpIsHellfire;
}

void RecreateEar(Item &item, uint16_t ic, uint32_t iseed, uint8_t bCursval, string_view heroName)
{
	InitializeItemToDefaultValues(item, IDI_EAR);

	std::string itemName = fmt::format(fmt::runtime("Ear of {:s}"), heroName);

	CopyUtf8(item._iName, itemName, sizeof(item._iName));
	CopyUtf8(item._iIName, heroName, sizeof(item._iIName));

	item._iCurs = ((bCursval >> 6) & 3) + ICURS_EAR_SORCERER;
	item._ivalue = bCursval & 0x3F;
	item._iCreateInfo = ic;
	item._iSeed = iseed;
}

void MakeGoldStackForInventory(Item &goldItem, int value)
{
	InitializeItemToDefaultValues(goldItem, IDI_GOLD);
	GenerateNewSeed(goldItem);
	goldItem._iStatFlag = true;
	goldItem._ivalue = value;
	SetPlrHandGoldCurs(goldItem);
}

// Disable the item flip animation when an item is tossed onto the floor
int ItemNoFlippy()
{
	int r = ActiveItems[ActiveItemCount - 1];
	Items[r].AnimInfo.currentFrame = Items[r].AnimInfo.numberOfFrames - 1;
	Items[r]._iAnimFlag = false;
	Items[r]._iSelFlag = 1;
	return r;
}

void NextItemRecord(int i)
{
	gnNumGetRecords--;

	if (gnNumGetRecords == 0) {
		return;
	}

	itemrecord[i].dwTimestamp = itemrecord[gnNumGetRecords].dwTimestamp;
	itemrecord[i].nSeed = itemrecord[gnNumGetRecords].nSeed;
	itemrecord[i].wCI = itemrecord[gnNumGetRecords].wCI;
	itemrecord[i].nIndex = itemrecord[gnNumGetRecords].nIndex;
}

bool GetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex)
{
	uint32_t ticks = SDL_GetTicks();

	for (int i = 0; i < gnNumGetRecords; i++) {
		if (ticks - itemrecord[i].dwTimestamp > 6000) {
			// BUGFIX: loot actions for multiple quest items with same seed (e.g. blood stone) performed within less than 6 seconds will be ignored.
			NextItemRecord(i);
			i--;
		} else if (nSeed == itemrecord[i].nSeed && wCI == itemrecord[i].wCI && nIndex == itemrecord[i].nIndex) {
			return false;
		}
	}

	return true;
}

void SetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex)
{
	uint32_t ticks = SDL_GetTicks();

	if (gnNumGetRecords == MAXITEMS) {
		return;
	}

	itemrecord[gnNumGetRecords].dwTimestamp = ticks;
	itemrecord[gnNumGetRecords].nSeed = nSeed;
	itemrecord[gnNumGetRecords].wCI = wCI;
	itemrecord[gnNumGetRecords].nIndex = nIndex;
	gnNumGetRecords++;
}

void PutItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex)
{
	uint32_t ticks = SDL_GetTicks();

	for (int i = 0; i < gnNumGetRecords; i++) {
		if (ticks - itemrecord[i].dwTimestamp > 6000) {
			NextItemRecord(i);
			i--;
		} else if (nSeed == itemrecord[i].nSeed && wCI == itemrecord[i].wCI && nIndex == itemrecord[i].nIndex) {
			NextItemRecord(i);
			break;
		}
	}
}

#ifdef _DEBUG
std::mt19937 RngForItemGen; // use a RNG with good statistical properties to seed the diablo RNG
#if JWK_USE_BETTER_RANDOM_NUMBERS
std::uniform_int_distribution<uint32_t> DistributionForItemGen;
#else // original code
std::uniform_int_distribution<int32_t> DistributionForItemGen(0, INT_MAX);
#endif
std::string DebugSpawnItem(std::string itemName)
{
	if (ActiveItemCount >= MAXITEMS)
		return "No space to generate the item!";

	const int max_time = 5000;
	const int max_iter = 5000000;

	AsciiStrToLower(itemName);

	Item testItem;

	uint32_t begin = SDL_GetTicks();
	int i = 0;
	for (;; i++) {
		bool onlyGood = DistributionForItemGen(RngForItemGen, decltype(DistributionForItemGen)::param_type{0, 1});
		SetRndSeed(DistributionForItemGen(RngForItemGen));
		if (SDL_GetTicks() - begin > max_time)
			return StrCat("Item not found in ", max_time / 1000, " seconds!");

		if (i > max_iter)
			return StrCat("Item not found in ", max_iter, " tries!");

		const int8_t monsterLevel = DistributionForItemGen(RngForItemGen) % CF_LEVEL + 1;
		//if (!DoesMonsterLevelExist(monsterLevel, false, gbIsHellfire)) {
		//	continue;
		//}
		BaseItemIdx idx = SelectRandomBaseItem(false, [&monsterLevel](const BaseItemData &item) {
			return item.iMinMLvl <= monsterLevel;
		});
		if (IsAnyOf(idx, IDI_NONE, IDI_GOLD))
			continue;

		testItem = {};
		int32_t seed = AdvanceRndSeed();
		ConstructItemFromSeed(testItem, idx, seed, monsterLevel, 1, onlyGood, false, false);

		std::string tmp = AsciiStrToLower(testItem._iIName);
		if (tmp.find(itemName) != std::string::npos) {
			// Set breakpoint here to see how the item was generated:
			//testItem = {};
			//ConstructItemFromSeed(testItem, idx, seed, monsterLevel, 1, false, false, false);
			break;
		}
	}

	int ii = AllocateItem();
	auto &item = Items[ii];
	item = testItem.pop();
	item._iIdentified = true;
	Point pos = MyPlayer->position.tile;
	GetSuperItemSpace(pos, ii);
	NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
	return StrCat("Item generated successfully - iterations: ", i);
}

std::string DebugSpawnUniqueItem(std::string itemName)
{
	if (ActiveItemCount >= MAXITEMS)
		return "No space to generate the item!";

	AsciiStrToLower(itemName);
	UniqueItem uniqueItem;
	bool foundUnique = false;
	int uniqueIndex = 0;
	for (int j = 0; UniqueItems[j].UIItemId != UITYPE_INVALID; j++) {
		if (!IsUniqueAvailable(j))
			break;

		const std::string tmp = AsciiStrToLower(UniqueItems[j].UIName);
		if (tmp.find(itemName) != std::string::npos) {
			itemName = tmp;
			uniqueItem = UniqueItems[j];
			uniqueIndex = j;
			foundUnique = true;
			break;
		}
	}
	if (!foundUnique)
		return "No unique found!";

	BaseItemIdx uniqueBaseIndex = IDI_GOLD;
	for (std::underlying_type_t<BaseItemIdx> j = IDI_GOLD; j <= IDI_LAST; j++) {
		if (!IsItemAvailable(j))
			continue;
		if (AllItemsList[j].iItemId == uniqueItem.UIItemId) {
			uniqueBaseIndex = static_cast<BaseItemIdx>(j);
			break;
		}
	}

	if (uniqueBaseIndex == IDI_GOLD)
		return "Base item not available";

	auto &baseItemData = AllItemsList[static_cast<size_t>(uniqueBaseIndex)];

	Item testItem;

	int i = 0;
	for (uint32_t begin = SDL_GetTicks();; i++) {
		constexpr int max_time = 3000;
		if (SDL_GetTicks() - begin > max_time)
			return StrCat("Item not found in ", max_time / 1000, " seconds!");

		constexpr int max_iter = 1000000;
		if (i > max_iter)
			return StrCat("Item not found in ", max_iter, " tries!");

		testItem = {};
		testItem._iMiscId = baseItemData.iMiscId;
		SetRndSeed(DistributionForItemGen(RngForItemGen));
		for (auto &flag : UniqueItemFlags)
			flag = true;
		UniqueItemFlags[uniqueIndex] = false;
		ConstructItemFromSeed(testItem, uniqueBaseIndex, testItem._iMiscId == IMISC_UNIQUE ? uniqueIndex : AdvanceRndSeed(), uniqueItem.UIMinLvl, 1, false, false, false);
		for (auto &flag : UniqueItemFlags)
			flag = false;

		if (testItem._iMagical != ITEM_QUALITY_UNIQUE)
			continue;

		const std::string tmp = AsciiStrToLower(testItem._iIName);
		if (tmp.find(itemName) != std::string::npos)
			break;
		return "Impossible to generate!";
	}

	int ii = AllocateItem();
	auto &item = Items[ii];
	item = testItem.pop();
	Point pos = MyPlayer->position.tile;
	GetSuperItemSpace(pos, ii);
	item._iIdentified = true;
	NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
	return StrCat("Item generated successfully - iterations: ", i);
}
#endif

bool Item::isUsable() const
{
	if (IDidx == IDI_SPECELIX && Quests[Q_MUSHROOM]._qactive != QUEST_DONE)
		return false;
	return AllItemsList[IDidx].iUsable;
}

void Item::setNewAnimation(bool showAnimation)
{
	int8_t it = ItemCAnimTbl[_iCurs];
	int8_t numberOfFrames = ItemAnimLs[it];
	OptionalClxSpriteList sprite = itemanims[it] ? OptionalClxSpriteList { *itemanims[static_cast<size_t>(it)] } : std::nullopt;
	if (_iCurs != ICURS_MAGIC_ROCK)
		AnimInfo.setNewAnimation(sprite, numberOfFrames, 1, AnimationDistributionFlags::ProcessAnimationPending, 0, numberOfFrames);
	else
		AnimInfo.setNewAnimation(sprite, numberOfFrames, 1);
	_iPostDraw = false;
	_iRequest = false;
	if (showAnimation) {
		_iAnimFlag = true;
		_iSelFlag = 0;
	} else {
		AnimInfo.currentFrame = AnimInfo.numberOfFrames - 1;
		_iAnimFlag = false;
		_iSelFlag = 1;
	}
}

void Item::updateRequiredStatsCacheForPlayer(const Player &player)
{
	if (_itype == ItemType::Misc && _iMiscId == IMISC_BOOK) {
		_iMinMag = GetSpellData(_iSpell).minInt;
		int8_t spellLevel = player._pSplLvl[static_cast<int8_t>(_iSpell)];
		while (spellLevel != 0) {
			_iMinMag += 20 * _iMinMag / 100;
			spellLevel--;
			if (_iMinMag + 20 * _iMinMag / 100 > 255) {
				_iMinMag = 255;
				spellLevel = 0;
			}
		}
	}
	_iStatFlag = player.CanUseItem(*this);
}

StringOrView Item::getName() const
{
	// JWK_DISABLE_ITEM_NAME_TRANSLATION - The original code included a lot of logic here for translating item names into different languages which involved re-creating the item using the random seed.
	// I deleted this code because it was yet another failure point for random-seed code to become inconsistent.  Now, we just return the English name stored in the item data structure itself.  Much easier!
	if (isEmpty()) {
		return string_view("");
	} else if (!_iIdentified) {
		return _(_iName);
	} else if (_iMagical == ITEM_QUALITY_UNIQUE) {
		return _(UniqueItems[_iUid].UIName);
	} else {
		return _(_iIName);
	}
}

bool CornerStoneStruct::isAvailable()
{
	return currlevel == 21 && !gbIsMultiplayer;
}

void initItemGetRecords()
{
	memset(itemrecord, 0, sizeof(itemrecord));
	gnNumGetRecords = 0;
}

void RepairItem(Item &item, Player &player)
{
	if (item._iDurability == item._iMaxDur) {
		return;
	}

	if (item._iMaxDur <= 0) {
		item.clear();
		return;
	}

#if JWK_EDIT_PLAYER_SKILLS
	Uint64 now = SDL_GetTicks64(); // This is a real-time tick that doesn't pause during debugger breaks
	if (now >= player._timeOfMostRecentSkillUse + player.GetSkillCooldownMilliseconds()) {
		player._timeOfMostRecentSkillUse = now;
		item._iDurability = item._iMaxDur;
	}
	return;
#else // original code (repairs to full but reduces max durability of item)
	int rep = 0;
	do {
		rep += player._pLevel + GenerateRnd(player._pLevel);
		item._iMaxDur -= std::max(item._iMaxDur / (player._pLevel + 9), 1);
		if (item._iMaxDur == 0) {
			item.clear();
			return;
		}
	} while (rep + item._iDurability < item._iMaxDur);
	item._iDurability = std::min<int>(item._iDurability + rep, item._iMaxDur);
#endif
}

void RechargeItem(Item &item, Player &player)
{
	if (item._itype != ItemType::Staff || !IsValidSpell(item._iSpell))
		return;

	if (item._iCharges == item._iMaxCharges)
		return;

#if JWK_EDIT_PLAYER_SKILLS
	Uint64 now = SDL_GetTicks64(); // This is a real-time tick that doesn't pause during debugger breaks
	if (now >= player._timeOfMostRecentSkillUse + player.GetSkillCooldownMilliseconds()) {
		player._timeOfMostRecentSkillUse = now;
		item._iCharges = item._iMaxCharges;
	} else {
		return;
	}
#else // original code (recharges to full but reduces max charges of staff)
	int r = GetSpellStaffLevel(item._iSpell);
	r = GenerateRnd(player._pLevel / r) + 1;
	do {
		item._iMaxCharges--;
		if (item._iMaxCharges == 0) {
			return;
		}
		item._iCharges += r;
	} while (item._iCharges < item._iMaxCharges);
	item._iCharges = std::min(item._iCharges, item._iMaxCharges);
#endif

	if (&player != MyPlayer)
		return;
	if (&item == &player.InvBody[INVLOC_HAND_LEFT]) {
		NetSendCmdChItem(true, INVLOC_HAND_LEFT);
		return;
	}
	if (&item == &player.InvBody[INVLOC_HAND_RIGHT]) {
		NetSendCmdChItem(true, INVLOC_HAND_RIGHT);
		return;
	}
	for (int i = 0; i < player._pNumInv; i++) {
		if (&item == &player.InvList[i]) {
			NetSyncInvItem(player, i);
			break;
		}
	}
}

bool ApplyOilToItem(Item &item, Player &player)
{
	int r;

	if (item._iClass == ICLASS_MISC) {
		return false;
	}
	if (item._iClass == ICLASS_GOLD) {
		return false;
	}
	if (item._iClass == ICLASS_QUEST) {
		return false;
	}

	switch (player._pOilType) {
	case IMISC_OILACC:
	case IMISC_OILMAST:
	case IMISC_OILSHARP:
		if (item._iClass == ICLASS_ARMOR) {
			return false;
		}
		break;
	case IMISC_OILDEATH:
		if (item._iClass == ICLASS_ARMOR) {
			return false;
		}
		if (item._itype == ItemType::Bow) {
			return false;
		}
		break;
	case IMISC_OILHARD:
	case IMISC_OILIMP:
		if (item._iClass == ICLASS_WEAPON) {
			return false;
		}
		break;
	default:
		break;
	}

	switch (player._pOilType) {
	case IMISC_OILACC:
		if (item._iPLToHit < 50) {
			item._iPLToHit += GenerateRnd(2) + 1;
		}
		break;
	case IMISC_OILMAST:
		if (item._iPLToHit < 100) {
			item._iPLToHit += GenerateRnd(3) + 3;
		}
		break;
	case IMISC_OILSHARP:
		if (item._iMaxDam - item._iMinDam < 30 && item._iMaxDam < 255) {
			item._iMaxDam = item._iMaxDam + 1;
		}
		break;
	case IMISC_OILDEATH:
		if (item._iMaxDam - item._iMinDam < 30 && item._iMaxDam < 254) {
			item._iMinDam = item._iMinDam + 1;
			item._iMaxDam = item._iMaxDam + 2;
		}
		break;
	case IMISC_OILSKILL:
		r = GenerateRnd(6) + 5;
		item._iMinStr = std::max(0, item._iMinStr - r);
		item._iMinMag = std::max(0, item._iMinMag - r);
		item._iMinDex = std::max(0, item._iMinDex - r);
		break;
	case IMISC_OILBSMTH:
		if (item._iMaxDur == DUR_INDESTRUCTIBLE)
			return true;
		if (item._iDurability < item._iMaxDur) {
			item._iDurability = (item._iMaxDur + 4) / 5 + item._iDurability;
			item._iDurability = std::min<int>(item._iDurability, item._iMaxDur);
		} else {
			if (item._iMaxDur >= 100) {
				return true;
			}
			item._iMaxDur++;
			item._iDurability = item._iMaxDur;
		}
		break;
	case IMISC_OILFORT:
		if (item._iMaxDur != DUR_INDESTRUCTIBLE && item._iMaxDur < 200) {
			r = GenerateRnd(41) + 10;
			item._iMaxDur += r;
			item._iDurability += r;
		}
		break;
	case IMISC_OILPERM:
		item._iDurability = DUR_INDESTRUCTIBLE;
		item._iMaxDur = DUR_INDESTRUCTIBLE;
		break;
	case IMISC_OILHARD:
		if (item._iAC < 60) {
			item._iAC += GenerateRnd(2) + 1;
		}
		break;
	case IMISC_OILIMP:
		if (item._iAC < 120) {
			item._iAC += GenerateRnd(3) + 3;
		}
		break;
	default:
		return false;
	}
	return true;
}

void UpdateHellfireFlag(Item &item, const char *identifiedItemName)
{
#if JWK_DISABLE_ITEM_NAME_TRANSLATION
	// We don't need to do anything because we aren't planning to support loading vanilla (non DevilutionX) save files.
	// We assume any hellfire save file already has the DevilutionX-specific flag CF_HELLFIRE
#else // original code
	// DevilutionX support vanilla and hellfire items in one save file and for that introduced CF_HELLFIRE
	// But vanilla hellfire items don't have CF_HELLFIRE set in Item::dwBuff
	// This functions tries to set this flag for vanilla hellfire items based on the item name
	// This ensures that Item::getName() returns the correct translated item name
	if (item.dwBuff & CF_HELLFIRE)
		return; // Item is already a hellfire item
	if (item._iMagical != ITEM_QUALITY_MAGIC)
		return; // Only magic item's name can differ between diablo and hellfire
	if (gbIsMultiplayer)
		return; // Vanilla hellfire multiplayer is not supported in devilutionX, so there can't be items with missing dwBuff from there
	// We need to test both short and long name, cause StringInPanel can return a different result (other font and some bugfixes)
	std::string diabloItemNameShort = GetTranslatedItemNameMagical(item, false, false, false);
	if (diabloItemNameShort == identifiedItemName)
		return; // Diablo item name is identical => not a hellfire specific item
	std::string diabloItemNameLong = GetTranslatedItemNameMagical(item, false, false, true);
	if (diabloItemNameLong == identifiedItemName)
		return; // Diablo item name is identical => not a hellfire specific item
	std::string hellfireItemNameShort = GetTranslatedItemNameMagical(item, true, false, false);
	std::string hellfireItemNameLong = GetTranslatedItemNameMagical(item, true, false, true);
	if (hellfireItemNameShort == identifiedItemName || hellfireItemNameLong == identifiedItemName) {
		// This item should be a vanilla hellfire item that has CF_HELLFIRE missing, cause only then the item name matches
		item.dwBuff |= CF_HELLFIRE;
	}
#endif
}

} // namespace devilution
