/**
 * @file spells.cpp
 *
 * Implementation of functionality for casting player spells.
 */
#include "spells.h"

#include "control.h"
#include "cursor.h"
#ifdef _DEBUG
#include "debug.h"
#endif
#include "engine/backbuffer_state.hpp"
#include "engine/point.hpp"
#include "engine/random.hpp"
#include "gamemenu.h"
#include "inv.h"
#include "missiles.h"

namespace devilution {

namespace {

/**
 * @brief Gets a value indicating whether the player's current readied spell is a valid spell. Readied spells can be
 * invalidaded in a few scenarios where the spell comes from items, for example (like dropping the only scroll that
 * provided the spell).
 * @param player The player whose readied spell is to be checked.
 * @return 'true' when the readied spell is currently valid, and 'false' otherwise.
 */
bool IsReadiedSpellValid(const Player &player)
{
	switch (player._pRSplType) {
	case SpellType::Skill:
	case SpellType::Spell:
	case SpellType::Invalid:
		return true;

	case SpellType::Charges:
		return (player._pISpells & GetSpellBitmask(player._pRSpell)) != 0;

	case SpellType::Scroll:
		return (player._pScrlSpells & GetSpellBitmask(player._pRSpell)) != 0;

	default:
		return false;
	}
}

/**
 * @brief Clears the current player's readied spell selection.
 * @note Will force a UI redraw in case the values actually change, so that the new spell reflects on the bottom panel.
 * @param player The player whose readied spell is to be cleared.
 */
void ClearReadiedSpell(Player &player)
{
	if (player._pRSpell != SpellID::Invalid) {
		player._pRSpell = SpellID::Invalid;
		RedrawEverything();
	}

	if (player._pRSplType != SpellType::Invalid) {
		player._pRSplType = SpellType::Invalid;
		RedrawEverything();
	}
}

} // namespace

bool IsValidSpell(SpellID spl)
{
	return spl > SpellID::Null
	    && spl <= SpellID::LAST
	    && (spl <= SpellID::LastDiablo || gbIsHellfire);
}

bool IsValidSpellFrom(int spellFrom)
{
	if (spellFrom == 0)
		return true;
	if (spellFrom >= INVITEM_INV_FIRST && spellFrom <= INVITEM_INV_LAST)
		return true;
	if (spellFrom >= INVITEM_BELT_FIRST && spellFrom <= INVITEM_BELT_LAST)
		return true;
	return false;
}

bool IsWallSpell(SpellID spl)
{
	return spl == SpellID::FireWall || spl == SpellID::LightningWall;
}

bool TargetsMonster(SpellID id)
{
	return id == SpellID::Fireball
	    || id == SpellID::FireWall
	    || id == SpellID::Inferno
	    || id == SpellID::Lightning
	    || id == SpellID::StoneCurse
	    || id == SpellID::FlameWave;
}

int GetManaAmount(const Player &player, SpellID spellID)
{
	// spell level
	int spellLvlMinus1 = std::max(player.GetSpellLevel(spellID) - 1, 0);

	// mana adjust
	int adj = 0;
	if (spellLvlMinus1 > 0) {
		adj = spellLvlMinus1 * GetSpellData(spellID).sManaAdj;
	}
	if (spellID == SpellID::Firebolt) {
#if 1 // This reduces mana cost slower, resulting in a 2 mana cost bolt at lvl 15 and a 1 mana cost bolt at lvl 16
		adj /= 3;
#else // original code
		adj /= 2;
#endif
	}

	int ma; // mana amount
	if (spellID == SpellID::Healing || spellID == SpellID::HealOther) {
#if 1 // jwk - heal spell mana cost
		ma = std::max(5, 5 + player._pLevel - std::max(0, spellLvlMinus1));
#else // original code
		ma = (GetSpellData(SpellID::Healing).sManaCost + 2 * player._pLevel - adj);
#endif
#if JWK_EDIT_MANA_SHIELD // make mana shield expensive to cast, proportional to your base mana
	} else if (spellID == SpellID::ManaShield) {
		int percentOfBaseMana = 80 - 2 * spellLvlMinus1;
		ma = std::max(1, (percentOfBaseMana * player._pMaxManaBase / 100) >> 6);
#endif
	} else if (GetSpellData(spellID).sManaCost == 255) {
		ma = (player._pMaxManaBase >> 6) - adj;
	} else {
		ma = (GetSpellData(spellID).sManaCost - adj);
	}

	ma = std::max(ma, 0);
	ma <<= 6;

#if 0 // jwk - don't give specific classes spell mana cost discounts
	if (gbIsHellfire && player._pHeroClass == HeroClass::Sorcerer) {
		ma /= 2;
	} else if (player._pHeroClass == HeroClass::Rogue || player._pHeroClass == HeroClass::Monk || player._pHeroClass == HeroClass::Bard) {
		ma -= ma / 4;
	}
#endif

	if (GetSpellData(spellID).sMinMana > ma >> 6) {
		ma = GetSpellData(spellID).sMinMana << 6;
	}

	return ma;
}

void ConsumeSpell(Player &player, SpellID spellID, int spllvl, int manaCostMultiplier)
{
	if (JWK_GOD_MODE_SPELLS_COST_NOTHING) { return; }

	switch (player.executedSpell.spellType) {
	case SpellType::Skill:
	case SpellType::Invalid:
		break;
	case SpellType::Scroll:
		ConsumeScroll(player);
		break;
	case SpellType::Charges:
		ConsumeStaffCharge(player);
		break;
	case SpellType::Spell:
#ifdef _DEBUG
		if (DebugGodMode)
			break;
#endif
		if (manaCostMultiplier != 0) {
			int ma = manaCostMultiplier * GetManaAmount(player, spellID);
			player._pMana -= ma;
			player._pManaBase -= ma;
			RedrawComponent(PanelDrawComponent::Mana);
		}
		break;
	}
	// JWK_FIX_NETWORK_SYNC_AND_AUTHORITY - We don't need to check &player == MyPlayer here because the casting player has already sent a network command to cast the spell, which is like sending the command "I've taken damage."
	// Assuming mana shield is in sync... all players should be able to resolve the same result.
	if (manaCostMultiplier != 0) {
		if (spellID == SpellID::BloodStar) {
			int healthCost = JWK_EDIT_BLOOD_STAR ? std::max<int>(1, 4 - spllvl / 5) : 5;
			ApplyPlrDamage(DamageType::Physical, player, healthCost * manaCostMultiplier, 0, 0, 100, player.getId(), DeathReason::MonsterOrTrap);
		}
		if (spellID == SpellID::BoneSpirit) {
			int healthCost = JWK_EDIT_BONE_SPIRIT ? 10 : 6;
			ApplyPlrDamage(DamageType::Physical, player, healthCost * manaCostMultiplier, 0, 0, 100, player.getId(), DeathReason::MonsterOrTrap);
		}
	}
}

void EnsureValidReadiedSpell(Player &player)
{
	if (!IsReadiedSpellValid(player)) {
		ClearReadiedSpell(player);
	}
}

SpellCheckResult CheckSpell(const Player &player, SpellID spellID, SpellType st, bool manaonly)
{
#if JWK_GOD_MODE_SPELLS_COST_NOTHING
	return SpellCheckResult::Success;
#endif
#ifdef _DEBUG
	if (DebugGodMode)
		return SpellCheckResult::Success;
#endif

	if (!manaonly && pcurs != CURSOR_HAND) {
		return SpellCheckResult::Fail_Busy;
	}

	if (st == SpellType::Skill) {
		return SpellCheckResult::Success;
	}

	if (player.GetSpellLevel(spellID) <= 0) {
		return SpellCheckResult::Fail_Level0;
	}

	if (st != SpellType::Spell) {
		assert(false); // jwk - can this ever happen?
	}

#if JWK_EDIT_GOLEM // Allow removing golem for free (no mana cost)
	if (spellID == SpellID::Golem && &player == MyPlayer && Monsters[MyPlayerId].position.tile != GolemHoldingCell) {
		return SpellCheckResult::Success;
	}
#endif

	if (!JWK_ALLOW_MANA_COST_MODIFIER || player._pManaCostMod >= 0 || st != SpellType::Spell) { // Only fail for lack of mana if there's no chance of free spell
		if (player._pMana < GetManaAmount(player, spellID)) {
			return SpellCheckResult::Fail_NoMana;
		}
	}

	return SpellCheckResult::Success;
}

void CastSpell(int playerID, SpellID spellID, int sx, int sy, int dx, int dy, int spllvl)
{
	Player &player = Players[playerID];
	Direction dir = player._pdir;
	if (IsWallSpell(spellID)) {
		dir = player.tempDirection;
	}

	int manaCostMultiplier = 1;
	if (JWK_ALLOW_MANA_COST_MODIFIER && player._pManaCostMod != 0 && player.executedSpell.spellType == SpellType::Spell)
	{
		if (player._pManaCostMod < 0) {
			if (GenerateRnd(100) < -player._pManaCostMod) {
				manaCostMultiplier = 0; // free spell
			}
		} else {
			if (GenerateRnd(100) < player._pManaCostMod) {
				manaCostMultiplier = 2; // costly spell
			}
		}
		if (manaCostMultiplier != 0 && player._pMana < manaCostMultiplier * GetManaAmount(player, spellID)) {
			if (MyPlayer == &player) {
				MyPlayer->Say(HeroSpeech::NotEnoughMana);
			}
			return;
		}
	}

	bool fizzled = false;
	if (spellID == SpellID::ChargedBolt) {
		int numBolts = GetNumberOfChargedBolts(spllvl);
		for (int i = numBolts; i > 0; i--) {
			Missile *missile = AddMissile({ sx, sy }, { dx, dy }, dir, MissileID::ChargedBolt, TARGET_MONSTERS, playerID, 0, spllvl);
			fizzled |= (missile == nullptr);
		}
#if JWK_EDIT_GOLEM // Allow removing golem for free (no mana cost)
	} else if (spellID == SpellID::Golem && Monsters[playerID].position.tile != GolemHoldingCell) {
		KillPlayerGolem(playerID);
		fizzled = true;
#endif	
	} else {
		const SpellData &spellData = GetSpellData(spellID);
		Missile* missile = nullptr;
		for (size_t i = 0; i < sizeof(spellData.sMissiles) / sizeof(spellData.sMissiles[0]) && spellData.sMissiles[i] != MissileID::Null; i++) {
			missile = AddMissile({ sx, sy }, { dx, dy }, dir, spellData.sMissiles[i], TARGET_MONSTERS, playerID, 0, spllvl, missile);
			fizzled |= (missile == nullptr);
		}
	}

	if (!fizzled) {
		ConsumeSpell(player, spellID, spllvl, manaCostMultiplier);
	}
}

void DoResurrect(Player &player, Player &target)
{
	AddMissile(target.position.tile, target.position.tile, Direction::South, MissileID::ResurrectBeam, TARGET_MONSTERS, player.getId(), 0, 0);

	if (target._pHitPoints != 0)
		return;

	if (&target == MyPlayer) {
		MyPlayerIsDead = false;
		gamemenu_off();
		RedrawComponent(PanelDrawComponent::Health);
		RedrawComponent(PanelDrawComponent::Mana);
	}

	ClrPlrPath(target);
	target.destAction = ACTION_NONE;
	target._pInvincible = false;
	SyncInitPlrPos(target);

	int hp = 10 << 6;
	if (target._pMaxHPBase < (10 << 6)) {
		hp = target._pMaxHPBase;
	}
	SetPlayerHitPoints(target, hp);

	target._pHPBase = target._pHitPoints + (target._pMaxHPBase - target._pMaxHP); // CODEFIX: does the same stuff as SetPlayerHitPoints above, can be removed
	target._pMana = 0;
	target._pManaBase = target._pMana + (target._pMaxManaBase - target._pMaxMana);

	target._pmode = PM_STAND;

	CalcPlayerInventory(target, true);

	if (target.isOnActiveLevel()) {
		StartStand(target, target._pdir);
	}
}

void DoHealOther(const Player &caster, Player &target)
{
	if ((target._pHitPoints >> 6) <= 0) {
		return;
	}

#if 1 // jwk - be consistent with heal other defined in missiles.cpp
	int hp = CalcHealOtherAmount(caster, target, caster.GetSpellLevel(SpellID::HealOther)) << 6;
#else // original code, identical to missiles.cpp except monk is *3 instead of *2
	int hp = (GenerateRnd(10) + 1) << 6;
	for (int i = 0; i < caster._pLevel; i++) {
		hp += (GenerateRnd(4) + 1) << 6;
	}
	for (int i = 0; i < caster.GetSpellLevel(SpellID::HealOther); i++) {
		hp += (GenerateRnd(6) + 1) << 6;
	}

	if (caster._pHeroClass == HeroClass::Warrior || caster._pHeroClass == HeroClass::Barbarian) {
		hp *= 2;
	} else if (caster._pHeroClass == HeroClass::Rogue || caster._pHeroClass == HeroClass::Bard) {
		hp += hp / 2;
	} else if (caster._pHeroClass == HeroClass::Monk) {
		hp *= 3;
	}
#endif
	target._pHitPoints = std::min(target._pHitPoints + hp, target._pMaxHP);
	target._pHPBase = std::min(target._pHPBase + hp, target._pMaxHPBase);

	if (&target == MyPlayer) {
		RedrawComponent(PanelDrawComponent::Health);
	}
}

int GetSpellBookLevel(SpellID s, bool onlyLearnable)
{
	if (gbIsDemoGame) {
		switch (s) {
		case SpellID::StoneCurse:
		case SpellID::Guardian:
		case SpellID::Golem:
		case SpellID::Elemental:
		case SpellID::BloodStar:
		case SpellID::BoneSpirit:
			return -1;
		default:
			break;
		}
	}

	if (!gbIsHellfire) {
		if (s == SpellID::Nova) {
			if (onlyLearnable && !JWK_EDIT_NOVA) {
				return -1;
			}
		} else if (s == SpellID::Apocalypse) {
			if (onlyLearnable) {
				return -1;
			}
		} else if (s > SpellID::LastDiablo) {
			return -1;
		}
	}

	return GetSpellData(s).sBookLvl;
}

} // namespace devilution
