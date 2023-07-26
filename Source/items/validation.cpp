/**
 * @file items/validation.cpp
 *
 * Implementation of functions for validation of player and item data.
 */

#include "items/validation.h"

#include <cstdint>

#include "items.h"
#include "monstdat.h"
#include "player.h"
#include "spells.h"

namespace devilution {

namespace {

bool hasMultipleFlags(uint16_t flags)
{
	return (flags & (flags - 1)) > 0;
}

} // namespace

bool IsCreationFlagComboValid(uint16_t iCreateInfo)
{
	iCreateInfo = iCreateInfo & ~CF_LEVEL;
	const bool isTownItem = (iCreateInfo & CF_TOWN) != 0;
	const bool isPregenItem = (iCreateInfo & CF_PREGEN) != 0;
	const bool isUsefulItem = (iCreateInfo & CF_USEFUL) == CF_USEFUL;

	if (isPregenItem) {
		// Pregen flags are discarded when an item is picked up, therefore impossible to have in the inventory
		return false;
	}
	if (isUsefulItem && (iCreateInfo & ~CF_USEFUL) != 0)
		return false;
	if (isTownItem && hasMultipleFlags(iCreateInfo)) {
		// Items from town can only have 1 towner flag
		return false;
	}
	return true;
}

bool IsTownItemValid(uint16_t iCreateInfo, const Player &player)
{
	const uint8_t level = iCreateInfo & CF_LEVEL;
	const bool isBoyItem = (iCreateInfo & CF_BOY) != 0;
	const uint8_t maxTownItemLevel = 30;

	// Wirt items in multiplayer are equal to the level of the player, therefore they cannot exceed the max character level
	if (isBoyItem && level <= player.getMaxCharacterLevel())
		return true;

	return level <= maxTownItemLevel;
}

bool IsShopPriceValid(const Item &item)
{
	const int boyPriceLimit = MaxBoyValue;
	if (!gbIsHellfire && (item._iCreateInfo & CF_BOY) != 0 && item._iIvalue > boyPriceLimit)
		return false;

	const int premiumPriceLimit = MaxVendorValue;
	if (!gbIsHellfire && (item._iCreateInfo & CF_SMITH_PREMIUM) != 0 && item._iIvalue > premiumPriceLimit)
		return false;

	const uint16_t smithOrWitch = CF_SMITH_BASIC | CF_WITCH;
	const int smithAndWitchPriceLimit = gbIsHellfire ? MaxVendorValueHf : MaxVendorValue;
	if ((item._iCreateInfo & smithOrWitch) != 0 && item._iIvalue > smithAndWitchPriceLimit)
		return false;

	return true;
}

bool DoesMonsterLevelExist(uint8_t monsterLevel, bool uniqueMonster, bool isHellfire)
{
	static bool monsterLevelExists[128];
	static bool uniqueLevelExists[128];
	static bool tableIsBuilt = false;
	if (!tableIsBuilt) {
		for (int i = 0; i < 128; i++) {
			monsterLevelExists[i] = false;
			uniqueLevelExists[i] = false;
		}
	
		// Build a table of all normal monster levels
		for (int16_t i = 0; i < static_cast<int16_t>(NUM_MTYPES); i++) {
			const MonsterData& monsterData = MonstersData[i];
			uint8_t mlvl = static_cast<uint8_t>(monsterData.level);

			if (i != MT_DIABLO && monsterData.availability == MonsterAvailability::Never) {
				// Skip monsters that are unable to appear in the game
				continue;
			}

			if (!isHellfire) {
				if (i == MT_DIABLO) {
					mlvl -= 15; // Diablo is level 30 in vanilla game
				} else if (monsterData.minDunLvl > 16) {
					// Vanilla game only has 16 dungeon levels
					continue;
				}
			}

			monsterLevelExists[std::min<uint8_t>(127, mlvl)] = true;
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY // Account for monsters in hell/nightmare
			monsterLevelExists[std::min<uint8_t>(127, mlvl + 15)] = true;
			monsterLevelExists[std::min<uint8_t>(127, mlvl + 30)] = true;
#endif
		}

		// Build a table of all unique monster levels
		for (int i = 0; UniqueMonstersData[i].mName != nullptr; i++) {
			const UniqueMonsterData& uniqueMonsterData = UniqueMonstersData[i];
			uint8_t mlvl = static_cast<uint8_t>(Monster::GetLevelForUnique(static_cast<UniqueMonsterType>(i)));

			if (IsAnyOf(uniqueMonsterData.mtype, MT_DEFILER, MT_NAKRUL, MT_HORKDMN)) {
				// These monsters don't use their mlvl for item generation
				continue;
			}
			
			uniqueLevelExists[std::min<uint8_t>(127, mlvl)] = true;
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY // Account for monsters in hell/nightmare
			uniqueLevelExists[std::min<uint8_t>(127, mlvl + 15)] = true;
			uniqueLevelExists[std::min<uint8_t>(127, mlvl + 30)] = true;
#endif
		}
	}

	if (uniqueMonster) {
		return monsterLevel < 128 && uniqueLevelExists[monsterLevel];
	} else {
		return monsterLevel < 128 && monsterLevelExists[monsterLevel];
	}
}

bool IsUniqueMonsterItemValid(uint16_t iCreateInfo, uint32_t dwBuff)
{
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	return true; // Don't bother checking monster level because Diablo could be above the bit storage limit of CF_LEVEL
#else // original code
	// Check all unique monster levels to see if they match the item level
	const uint8_t level = iCreateInfo & CF_LEVEL;
	const bool isHellfireItem = (dwBuff & CF_HELLFIRE) != 0;
	return DoesMonsterLevelExist(level, true, isHellfireItem);
#endif
}

bool IsDungeonItemValid(uint16_t iCreateInfo, uint32_t dwBuff)
{
#if JWK_LOOT_QUALITY_DEPENDS_ON_DIFFICULTY
	return true; // Don't bother checking monster level because Diablo could be above the bit storage limit of CF_LEVEL
#else // original code
	const uint8_t level = iCreateInfo & CF_LEVEL;
	const bool isHellfireItem = (dwBuff & CF_HELLFIRE) != 0;

	// Check all monster levels to see if they match the item level
	if (DoesMonsterLevelExist(level, false, isHellfireItem)) {
		return true;
	}

	if (isHellfireItem) {
		uint8_t hellfireMaxDungeonLevel = 24;

		// Hellfire adjusts the currlevel minus 7 in dungeon levels 20-24 for generating items
		hellfireMaxDungeonLevel -= 7;
		return level <= (hellfireMaxDungeonLevel * 2);
	}

	uint8_t diabloMaxDungeonLevel = 16;

	// Diablo doesn't have containers that drop items in dungeon level 16, therefore we decrement by 1
	diabloMaxDungeonLevel--;
	return level <= (diabloMaxDungeonLevel * 2);
#endif
}

#if JWK_EDIT_SPELLBOOK_DROPS
bool IsDungeonSpellBookValid(const Item &spellBook)
{
	int spellBookLevel = GetSpellBookLevel(spellBook._iSpell, true);
	if (spellBookLevel < 0) {
		return false;
	}
	return IsDungeonItemValid(spellBook._iCreateInfo, spellBook.dwBuff);
}
#else // original code
bool IsHellfireSpellBookValid(const Item &spellBook)
{
	// ilevel needs to match CreateSpellBook()
	int spellBookLevel = GetSpellBookLevel(spellBook._iSpell, true) + 1;

	if (spellBookLevel >= 1 && (spellBook._iCreateInfo & CF_LEVEL) == spellBookLevel * 2) {
		// The ilvl matches the result for a spell book drop, so we confirm the item is legitimate
		return true;
	}

	return IsDungeonItemValid(spellBook._iCreateInfo, spellBook.dwBuff);
}
#endif

bool IsItemValid(const Player &player, const Item &item)
{
	if (!gbIsMultiplayer)
		return true;

	if (item.IDidx == IDI_EAR)
		return true;
	if (item.IDidx != IDI_GOLD && !IsCreationFlagComboValid(item._iCreateInfo))
		return false;
	if ((item._iCreateInfo & CF_TOWN) != 0)
		return IsTownItemValid(item._iCreateInfo, player) && IsShopPriceValid(item);
	if ((item._iCreateInfo & CF_USEFUL) == CF_UPER15)
		return IsUniqueMonsterItemValid(item._iCreateInfo, item.dwBuff);
#if JWK_EDIT_SPELLBOOK_DROPS
	if (AllItemsList[item.IDidx].iMiscId == IMISC_BOOK)
		return IsDungeonSpellBookValid(item);
#else // original code
	if ((item.dwBuff & CF_HELLFIRE) != 0 && AllItemsList[item.IDidx].iMiscId == IMISC_BOOK)
		return IsHellfireSpellBookValid(item);
#endif
	return IsDungeonItemValid(item._iCreateInfo, item.dwBuff);
}

} // namespace devilution
