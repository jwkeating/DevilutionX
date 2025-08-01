/**
 * @file missiles.cpp
 *
 * Implementation of missile functionality.
 */
#include "missiles.h"

#include <climits>
#include <cstdint>

#include "control.h"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "dead.h"
#ifdef _DEBUG
#include "debug.h"
#endif
#include "engine/backbuffer_state.hpp"
#include "engine/load_file.hpp"
#include "engine/points_in_rectangle_range.hpp"
#include "engine/random.hpp"
#include "init.h"
#include "inv.h"
#include "levels/trigs.h"
#include "lighting.h"
#include "monster.h"
#include "spells.h"
#include "utils/str_cat.hpp"
#include "qol/floatingnumbers.h"

namespace devilution {

std::list<Missile> Missiles;
bool MissilePreFlag;

static unsigned int ScaleSpellEffect(unsigned int base, int spellLevel)
{
	// Compute base * (9/8)^spellLevel.  At spell level 15, this multiplies base by 5.85
	base <<= 8; // use fixed point to maintain precision
	for (int i = 0; i < spellLevel; i++) {
		base += base >> 3;
	}
	//base = base * (100 + playerLevel) / 100; // This corresponds roughly to the damage scaling of critical strike chance when using melee/ranged attacks
	base >>= 8; // drop the fractional bits
	return base;
}

static int GenerateRndSum(int range, int iterations)
{
	int value = 0;
	for (int i = 0; i < iterations; i++) {
		value += GenerateRnd(range);
	}

	return value;
}

// Note: Most player-casted spells are initiated from void CastSpell() in spells.cpp which calls AddMissile().  Other code can also call AddMissle() to spawn spell effects.
// AddMissile() calls the mAddProc for the given spell in the MissilesData table.  The mProc for the given spell is also called every tick until the missle goes away.
// In general, damage-per-tick spells use shifted integers (integer value 64 means 1 damage in game) but direct damage spells use ordinary integers (integer value 1 means 1 damage in game).
static int32_t GenerateRndMin(int32_t v) { return 0; }
static int32_t GenerateRndMax(int32_t v) { return std::max(0, v - 1); }
static int GenerateRndSumMin(int range, int iterations) { return 0; }
static int GenerateRndSumMax(int range, int iterations) { return iterations * GenerateRndMax(range); }

template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcFireBoltDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return ScaleSpellEffect(2 + generateRndFunc(player._pLevel / 4), spellLevel);
	// original code: return generateRndFunc(10) + (player._pMagic / 8) + spellLevel + 1;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcChargedBoltDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return ScaleSpellEffect(10 + generateRndSumFunc(player._pLevel / 2, 4) + 10, spellLevel);
	// original code: return generateRndFunc(player._pMagic / 4) + 1;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcHolyBoltDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return ScaleSpellEffect(((4 + player._pLevel) * (300 + player._pMagic) * (20 + generateRndSumFunc(10, 4))) >> 16, spellLevel);
	// original code: return generateRndFunc(10) + player._pLevel + 9;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcFireBallDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return ScaleSpellEffect(player._pLevel + generateRndSumFunc(10, 3) + 5, spellLevel);
	// original code: return ScaleSpellEffect(2 * (player._pLevel + generateRndSumFunc(10, 2)) + 4, spellLevel);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcElementalDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return CalcFireBallDamage(player, spellLevel, generateRndFunc, generateRndSumFunc);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcGuardianDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// The original code computes a firebolt with the guardian's spell level:
	return CalcFireBoltDamage(player, spellLevel, generateRndFunc, generateRndSumFunc);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcFlameWaveDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return ScaleSpellEffect(3 + player._pLevel + generateRndSumFunc(player._pLevel, 3), spellLevel) / 5;
	// original code: return generateRndFunc(10) + player._pLevel + 1;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcLightningDamageShifted(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// Note: 64 means 1 when shifted by 6.  Also note: lightning damage is scaled by CalcLightningLength() because it ticks every frame as the bolt passes through a target
	return 64 + ScaleSpellEffect(generateRndFunc(13 * player._pLevel), spellLevel);
	// original code: return (generateRndFunc(2) + generateRndFunc(player._pLevel) + 2) << 6;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcChainLightningDamageShifted(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// Note: Even with the samge damage per tick, chain lightning is weaker than lightning because CalcChainLightningLength() is much less than CalcLightningLength()
	return CalcLightningDamageShifted(player, spellLevel, generateRndFunc, generateRndSumFunc);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcFireWallDamageShifted(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// At normal game speed, damage ticks every 50ms
	return ScaleSpellEffect(generateRndSumFunc(8, 3) + 2 * player._pLevel, spellLevel);
	// original code: return (generateRndSumFunc(10, 2) + 2 + player._pLevel) * 8;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcLightningWallDamageShifted(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return CalcFireWallDamageShifted(player, spellLevel, generateRndFunc, generateRndSumFunc);
	// original code: return 16 * (generateRndSumFunc(10, 2) + player._pLevel + 2);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcInfernoDamageShifted(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// "return (N << 6);" does (20 to 30) * N total damage to the target depending on 1,2,3 tile distance
	return ScaleSpellEffect(generateRndSumFunc(20, 5) + player._pLevel, spellLevel) * 3;
	// original code: int i = generateRndFunc(player._pLevel) + generateRndFunc(2);
	// original code: return 8 * i + 16 + ((8 * i + 16) / 2);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcFlashDamageShifted(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// "return 270 << 6;" deals 5130 damage in game.  5130/270 == 19 so flash does 19 ticks
	return ScaleSpellEffect(player._pLevel + generateRndSumFunc(player._pLevel / 5, 4), spellLevel) * 6;
	// original code: int dmg = player._pLevel + 1;
	// original code: dmg += generateRndSumFunc(20, dmg);
	// original code: dmg = ScaleSpellEffect(dmg, spellLevel);
	// original code: dmg += dmg / 2;
	// original code: return dmg;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcNovaDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	return ScaleSpellEffect(8 + generateRndSumFunc(8, 2) + player._pLevel, spellLevel) / 2;
	// original code: return ScaleSpellEffect((generateRndSumFunc(6, 5) + player._pLevel + 5) / 2, spellLevel);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcBloodStarDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// note: player._pHitPoints is shifted << 6
	return ScaleSpellEffect(((50 + player._pLevel) * (300 + player._pMagic) * player._pHitPoints) >> 16, spellLevel) / 128;
	// original code: return 3 * spellLevel - (player._pMagic / 8) + (player._pMagic / 2);
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcApocalypseDamage(const Player &player, int spellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
	// For this formula, we assume players can't learn the spell so every spell level added makes a big difference.
	int base = 16 * player._pLevel + generateRndSumFunc(player._pLevel + 1, 4);
	return base * (4 + spellLevel) / 4; // 25% more damage per spell level
	// original code: return generateRndSumFunc(6, player._pLevel) + player._pLevel;
}
template <typename GenerateRndFunc, typename GenerateRndSumFunc> static int CalcHealingAmount(const Player &caster, const Player& target, int casterSpellLevel, GenerateRndFunc generateRndFunc, GenerateRndSumFunc generateRndSumFunc)
{
#if 1 // jwk - edit healing amount
	if (casterSpellLevel < 0) { casterSpellLevel = 0; }
	int healPercent = 20 + casterSpellLevel + generateRndSumFunc(9, 2 + casterSpellLevel / 2);
	healPercent *= caster._pLevel;
	healPercent /= target._pLevel;
	int hp = (healPercent * target._pMaxHP / 100) >> 6;
	return hp;
#else // original code
	int hp = generateRndFunc(10) + 1;
	hp += generateRndSumFunc(4, player._pLevel) + player._pLevel;
	hp += generateRndSumFunc(6, spellLevel) + spellLevel;
	if (player._pHeroClass == HeroClass::Warrior || player._pHeroClass == HeroClass::Barbarian || player._pHeroClass == HeroClass::Monk) {
		hp *= 2;
	} else if (player._pHeroClass == HeroClass::Rogue || player._pHeroClass == HeroClass::Bard) {
		hp += hp / 2;
	}
	return hp;
#endif
}
static int CalcLightningLength(int spellLevel)
{
	// Note: bolt length affects damage
	return (spellLevel / 3) + 5;
	// original code: return (spellLevel / 2) + 6;
}
static int CalcChainLightningLength(int spellLevel)
{
	// Note: bolt length affects damage
	// We want a short bolt so the bolt can be seen bouncing between targets
	return 5;
}

static void TagMonsterWithDamageType(Monster& monster, DamageType damageType)
{
#if JWK_REVEAL_RESISTANCES_WHEN_DAMAGED
	static_assert((int)DamageType::Fire == 1 && (int)DamageType::Lightning == 2 && (int)DamageType::Magic == 3, "Enum must match array index");
	if ((int)damageType >= 1 && (int)damageType <= 3) {
		MonsterID monsterType = monster.type().type;
		uint8_t& hitCount = MonsterHitWithDamage[monsterType][(int)damageType - 1];
		if (++hitCount == 0) { hitCount = 0xFF; }
	}
#endif
}

static constexpr Direction16 Direction16Flip(Direction16 x, Direction16 pivot)
{
	std::underlying_type_t<Direction16> ret = (2 * static_cast<std::underlying_type_t<Direction16>>(pivot) + 16 - static_cast<std::underlying_type_t<Direction16>>(x)) % 16;

	return static_cast<Direction16>(ret);
}

static void UpdateMissileVelocity(Missile &missile, Point destination, int velocityInPixels)
{
	missile.position.velocity = { 0, 0 };

	if (missile.position.tile == destination)
		return;

	// Get the normalized vector in isometric projection
	Displacement fixed16NormalVector = (missile.position.tile - destination).worldToNormalScreen();

	// Multiplying by the target velocity gives us a scaled velocity vector.
	missile.position.velocity = fixed16NormalVector * velocityInPixels;
}

/**
 * @brief Add the missile to the lookup tables
 * @param missile The missile to add
 */
static void PutMissile(Missile &missile)
{
	Point position = missile.position.tile;

	if (!InDungeonBounds(position))
		missile._miDelFlag = true;

	if (missile._miDelFlag) {
		return;
	}

	DungeonFlag &flags = dFlags[position.x][position.y];
	flags |= DungeonFlag::Missile;
	if (missile._mitype == MissileID::FireWallSingleTile)
		flags |= DungeonFlag::MissileFireWall;
	if (missile._mitype == MissileID::LightningWallSingleTile)
		flags |= DungeonFlag::MissileLightningWall;

	if (missile._miPreFlag)
		MissilePreFlag = true;
}

static void UpdateMissilePos(Missile &missile)
{
	Displacement pixelsTravelled = missile.position.traveled >> 16;

	Displacement tileOffset = pixelsTravelled.screenToMissile();
	missile.position.tile = missile.position.start + tileOffset;

	missile.position.offset = pixelsTravelled + tileOffset.worldToScreen();

	Displacement absoluteLightOffset = pixelsTravelled.screenToLight();
	ChangeLightOffset(missile._mlid, absoluteLightOffset - tileOffset * 8);
}

/**
 * @brief Dodgy hack used to correct the position for charging monsters.
 *
 * If the monster represented by this missile is *not* facing north in some way it gets shifted to the south.
 * This appears to compensate for some visual oddity or invalid calculation earlier in the ProcessRhino logic.
 * @param missile MissileStruct representing a charging monster.
 */
static void MoveMissilePos(Missile &missile)
{
	Direction moveDirection;

	switch (static_cast<Direction>(missile._mimfnum)) {
	case Direction::East:
		moveDirection = Direction::SouthEast;
		break;
	case Direction::West:
		moveDirection = Direction::SouthWest;
		break;
	case Direction::South:
	case Direction::SouthWest:
	case Direction::SouthEast:
		moveDirection = Direction::South;
		break;
	default:
		return;
	}

	auto target = missile.position.tile + moveDirection;
	if (IsTileAvailable(*missile.sourceMonster(), target)) {
		missile.position.tile = target;
		missile.position.offset += Displacement(moveDirection).worldToScreen();
	}
}

static int ScaleTrapDamage(int dmg)
{
	// TODO: remove this function and individually tune each trap
	return dmg * (sgGameInitInfo.nDifficulty + 1);
}
static int ProjectileTrapDamage(Missile &missile)
{
	return 3 * (15 + 30);
	// return currlevel + GenerateRnd(2 * currlevel);
	return ScaleTrapDamage(currlevel + GenerateRndSum(currlevel + 1, 2));
}
static void GetTrapArrowDamage(int& mind, int& maxd)
{
	// Deals [45 - 70] damage in hell/hell
	mind = (currlevel + 15 * sgGameInitInfo.nDifficulty) * 1 + 1;
	maxd = (currlevel + 15 * sgGameInitInfo.nDifficulty) * 3 / 2 + 3;
}
static void GetAddedTrapElementalArrowDamage(int& mind, int& maxd)
{
	// Note: An elemental arrow hits twice - once for physical damage and once for fire damage.
	// The total damage of an elemental arrow is GetTrapArrowDamage() + GetAddedTrapElementalArrowDamage()
	// Deals [68 - 140] damage in hell/hell (computed as [45 - 70] + [23 - 70])
	mind = (currlevel + 15 * sgGameInitInfo.nDifficulty) / 2 + 1;
	maxd = (currlevel + 15 * sgGameInitInfo.nDifficulty) * 3 / 2 + 3;
}
static int TrapFireboltDamage()
{
	// This trap fires from distant walls and trapped doors (not chests)
	// Deals [135 - 180] in hell/hell
	return (currlevel + 15 * sgGameInitInfo.nDifficulty) * (3 + GenerateRnd(2));
}
static int TrapLightningDamageShifted()
{
	// This trap fires from distant walls and trapped doors (not chests)
	// Deals [90 - 270] damage in hell/hell
	int damageInGame = (currlevel + 15 * sgGameInitInfo.nDifficulty) * (2 + GenerateRnd(5));
	return damageInGame * 8; // Assuming a length 8 bolt, we need to multiply by approx 8 to obtain the desired damage in game
}
static int TrapChainLightningDamageShifted()
{
	// Deals [135 - 225] damage in hell/hell
	int damageInGame = (currlevel + 15 * sgGameInitInfo.nDifficulty) * (3 + GenerateRnd(3));
	return damageInGame * 13; // Assuming a length 5 bolt, we need to multiply by approx 13 to obtain the desired damage in game
}
static int TrapDamageAcidPuddleShifted()
{
	// Deals 45 damage per second in hell/hell (Player is expected to run out of the acid puddle)
	int damagePerSecond = (currlevel + 15 * sgGameInitInfo.nDifficulty);
	return damagePerSecond * 16 / 5;
}
static int TrapFireballDamage()
{
	// Deals [180 - 270] damage in hell/hell
	return (currlevel + 15 * sgGameInitInfo.nDifficulty) * (4 + GenerateRnd(3));
}
static int TrapNovaDamage()
{
	// Deals [135 - 270] damage in hell/hell
	return (currlevel + 15 * sgGameInitInfo.nDifficulty) * (3 + GenerateRnd(5));
}
static int TrapFlashDamage() // Note: sparkling shrine causes flash
{
	// Deals [90 - 315] (avg 203) damage in hell/hell but you can avoid 90% of the damage if you immediately run away after opening the chest
	int damageInGame = (currlevel + 15 * sgGameInitInfo.nDifficulty) * (2 + GenerateRnd(6));
	return (damageInGame << 6) / 19;
	// original code: return currlevel / 2; // This does almost no damage
}
static int TrapApocalypseBoomDamage()
{
	// Deals [100-200] in normal, [150-300] in nightmare, [200-400] in hell/hell
	return (2 + sgGameInitInfo.nDifficulty) * (50 + GenerateRnd(51));
}
static int TrapRingOfFireDamageShifted()
{
	return 16 * (GenerateRndSum(10, 2) + currlevel + 2) / 2; // original code
}

static int ProjectileMonsterDamage(Missile &missile)
{
	const Monster &monster = *missile.sourceMonster();
	return monster.minDamage + GenerateRnd(monster.maxDamage - monster.minDamage + 1);
}

#if JWK_USE_CONSISTENT_HIT_CHANCE
static int ChanceToMissAtDistance(int distance)
{
	distance = std::max(0, distance - 5);
	return distance * distance / 2;
}
#endif

static void RotateBlockedMissile(Missile &missile)
{
	int rotation = PickRandomlyAmong({ -1, 1 });

	if (missile._miAnimType == MissileGraphicID::Arrow) {
		int dir = missile._miAnimFrame + rotation;
		missile._miAnimFrame = (dir + 15) % 16 + 1;
		return;
	}

	int dir = missile._mimfnum + rotation;
	int mAnimFAmt = GetMissileSpriteData(missile._miAnimType).animFAmt;
	if (dir < 0)
		dir = mAnimFAmt - 1;
	else if (dir >= mAnimFAmt)
		dir = 0;

	SetMissDir(missile, dir);
}

#if JWK_PREVENT_DUPLICATE_MISSILE_HITS
static uint16_t GenerateMissileGroup()
{
	static uint16_t id = 0;
	if (++id == 0) { ++id; }
	return id;
}
static bool RecordMissleGroupHit(const Missile& missile, MissileGroupList& missileGroupsToIgnoreThisTick, MissileGroupRingBuffer& missileGroupsToIgnoreForever)
{
	if (missile._missileGroup == 0) {
		return true; // missile doesn't require deduplication logic.
	}
	if (missile._missileGroupIgnoreForever) {
		if (missileGroupsToIgnoreForever.DoesEntryExist(missile._missileGroup)) {
			return false; // Missile group already hit this target.  Don't allow another hit.
		}
		missileGroupsToIgnoreForever.AddEntry(missile._missileGroup);
		return true;
	} else {
		if (missileGroupsToIgnoreThisTick.DoesEntryExist(missile._missileGroup)) {
			return false; // Missile group already hit this target this tick.  Don't allow another hit this tick.
		}
		missileGroupsToIgnoreThisTick.AddEntry(missile._missileGroup);
		return true;
	}
}
#endif


bool MonsterHitByMissileFromMonsterOrTrap(int monsterId, Monster* attacker, int mindam, int maxdam, int dist, MissileID missileID, DamageType damageType, bool shift)
{
	auto &monster = Monsters[monsterId];

	if (!monster.isPossibleToHit() || monster.isImmune(missileID, damageType))
		return false;

	int diceRollToAvoidHit = GenerateRnd(100);
	int hitChance;
#if JWK_USE_CONSISTENT_HIT_CHANCE
	if (shift) {
		hitChance = 100; // damage over time effects should hit every tick.  This makes floating damage numbers merge into a single value.
	} else {
		if (attacker) {
			const MissileData &missileData = GetMissileData(missileID);
			if (missileData.isArrow()) {
				hitChance = attacker->toHit(sgGameInitInfo.nDifficulty) - monster.armorClass;
			} else {
				hitChance = attacker->toHitWithMagic(sgGameInitInfo.nDifficulty);
			}
			hitChance += 2 * (attacker->level(sgGameInitInfo.nDifficulty) - monster.level(sgGameInitInfo.nDifficulty)) - ChanceToMissAtDistance(dist);
		} else { // monster hit by trap
			hitChance = 50;
		}
		hitChance = clamp(hitChance, 5, 95);
	}
#else // original code:
	hitChance = 90 - monster.armorClass - dist;
	hitChance = clamp(hitChance, 5, 95);
#endif
	if (monster.tryLiftGargoyle())
		return true;
	if (diceRollToAvoidHit >= hitChance && monster.mode != MonsterMode::Petrified) {
#ifdef _DEBUG
		if (!DebugGodMode)
#endif
			return false;
	}

	bool resist = monster.isResistant(missileID, damageType);
	int dam = mindam + GenerateRnd(maxdam - mindam + 1);
	if (!shift)
		dam <<= 6;
	if (resist)
		dam /= 4;
	ApplyMonsterDamage(damageType, monster, dam, monster.mode == MonsterMode::Petrified ? 100 : hitChance);
#ifdef _DEBUG
	if (DebugGodMode)
		monster.hitPoints = 0;
#endif
	if (monster.hitPoints >> 6 <= 0) {
		MonsterDeath(monster, monster.direction, true);
#if !JWK_RESISTANT_TARGETS_CAN_BE_STUNNED
	} else if (resist) {
		PlayEffect(monster, MonsterSound::Hit);
#endif
	} else if (monster.type().type != MT_GOLEM) {
		M_StartHit(monster, dam);
	}
	return true;
}

static bool MonsterHitByMissileFromPlayer(int pnum, int monsterId, int mindam, int maxdam, int dist, MissileID missileID, DamageType damageType, bool shift)
{
	auto &monster = Monsters[monsterId];
	const Player &player = Players[pnum];

	if (!monster.isPossibleToHit())
		return false;

	if (monster.isImmune(missileID, damageType)) {
		if (JWK_REVEAL_RESISTANCES_WHEN_DAMAGED && &player == MyPlayer) {
			TagMonsterWithDamageType(monster, damageType);
		}
		return false;
	}

	int diceRollToAvoidHit = GenerateRnd(100);
	int hitChance = 0;
	const MissileData &missileData = GetMissileData(missileID);

#if JWK_USE_CONSISTENT_HIT_CHANCE
	if (shift) {
		hitChance = 100; // damage over time effects should hit every tick.  This makes floating damage numbers merge into a single value.
	} else {
		if (missileData.isArrow()) {
			hitChance = player.GetRangedPiercingToHit() - player.CalculateArmorAfterPierce(monster.armorClass, false);
			hitChance += 2 * (player._pLevel - monster.level(sgGameInitInfo.nDifficulty)) - ChanceToMissAtDistance(dist);
		} else {
			// magic ignores armor but caster level doesn't factor in, and there are no "+hit with magic" bonuses
			hitChance = player.GetMagicToHit() - 2 * monster.level(sgGameInitInfo.nDifficulty) - ChanceToMissAtDistance(dist);
		}
		hitChance = clamp(hitChance, 5, 95);
	}
#else // original code:
	if (missileData.isArrow()) {
		hitChance = player.GetRangedPiercingToHit() - player.CalculateArmorAfterPierce(monster.armorClass, false);
		hitChance -= (dist * dist) / 2;
	} else {
		hitChance = player.GetMagicToHit() - 2 * monster.level(sgGameInitInfo.nDifficulty) - dist;
	}
	hitChance = clamp(hitChance, 5, 95);
#endif

	if (monster.tryLiftGargoyle())
		return true;

	if (diceRollToAvoidHit >= hitChance && monster.mode != MonsterMode::Petrified) {
#ifdef _DEBUG
		if (!DebugGodMode)
#endif
			return false;
	}

	int dam;
	if (missileID == MissileID::BoneSpirit) {
		shift = true; // produce a shifted << 6 damage.  Note: hitPoints is already a shifted value.
		dam = monster.hitPoints / 3;
	} else {
		dam = mindam + GenerateRnd(maxdam - mindam + 1);
	}

	if (missileData.isArrow() && damageType == DamageType::Physical) {
		assert(!shift);
#if JWK_USE_CONSISTENT_MELEE_AND_RANGED_DAMAGE
		dam += player._pIBonusDamMod + dam * player._pIBonusDam / 100 + player._pDamageMod;
#else // original code
		dam += player._pIBonusDamMod + dam * player._pIBonusDam / 100;
		if (player._pHeroClass == HeroClass::Rogue)
			dam += player._pDamageMod;
		else
			dam += player._pDamageMod / 2;
#endif
#if JWK_EDIT_CRITICAL_STRIKE
		if (player._pHeroClass == HeroClass::Rogue) {
			if (GenerateRnd(200) < 10 + player._pLevel) { // 5.5% - 30% chance at level 1 - 50
				dam *= 2;
			}
		}
#endif
		if (monster.data().monsterClass == MonsterClass::Demon && HasAnyOf(player._pIFlags, ItemSpecialEffect::TripleDemonDamage))
			dam *= 3;
	}
	if (!shift)
		dam <<= 6;
	bool resist = monster.isResistant(missileID, damageType);
#if JWK_EDIT_HOLY_BOLT_RESISTANCE
	if (resist) {
		if (missileID == MissileID::HolyBolt) { dam >>= 1; }
		else { dam >>= 2; }
	}
#else // original code:
	if (resist)
		dam >>= 2;
#endif
#if JWK_EDIT_APOCALYPSE
	if (missileID == MissileID::ApocalypseBoom) {
		// Apocalypse is 50% fire, 50% physical
		if ((monster.resistance & IMMUNE_FIRE) != 0) {
			dam = dam / 2; // 100% of the fire is resisted
		} else if ((monster.resistance & RESIST_FIRE) != 0) {
			dam = dam * 5 / 8; // 75% of the fire is resisted
		}
	}
#endif
	if (&player == MyPlayer) {
		ApplyMonsterDamage(damageType, monster, dam, monster.mode == MonsterMode::Petrified ? 100 : hitChance);
		if (JWK_REVEAL_RESISTANCES_WHEN_DAMAGED) {
			TagMonsterWithDamageType(monster, damageType);
		}
	}

	if (monster.hitPoints >> 6 <= 0) {
		M_StartKill(monster, player);
#if !JWK_RESISTANT_TARGETS_CAN_BE_STUNNED
	} else if (resist) {
		monster.tag(player);
		PlayEffect(monster, MonsterSound::Hit);
#endif
	} else {
		if (monster.mode != MonsterMode::Petrified && missileData.isArrow() && HasAnyOf(player._pIFlags, ItemSpecialEffect::Knockback))
			M_GetKnockback(monster);
		if (monster.type().type != MT_GOLEM)
			M_StartHit(monster, player, dam);
	}

	if (monster.activeForTicks == 0) {
		monster.activeForTicks = UINT8_MAX;
		monster.position.last = player.position.tile;
	}

	return true;
}

// Player hit by missile from monster or trap
bool PlayerHitByMissile(int pnum, Monster *monster, int dist, Point mStartPos, MissileID missileID, int mind, int maxd, DamageType damageType, bool shift, DeathReason deathReason, bool *blocked)
{
	*blocked = false;

	Player &player = Players[pnum];

	if (player._pHitPoints >> 6 <= 0) {
		return false;
	}

	if (player._pInvincible) {
		return false;
	}

	const MissileData &missileData = GetMissileData(missileID);

	if (HasAnyOf(player._pSpellFlags, SpellFlag::Etherealize) && missileData.isArrow()) {
		return false;
	}

#if JWK_USE_CONSISTENT_HIT_CHANCE
	int hitChance = 0;
	if (shift) {
		hitChance = 100; // damage over time effects should hit every tick.  This makes floating damage numbers merge into a single value.
	} else {
		if (missileData.isArrow()) {
			if (monster) {
				hitChance = monster->toHit(sgGameInitInfo.nDifficulty) - player.GetArmor() + 2 * (monster->level(sgGameInitInfo.nDifficulty) - player._pLevel) - ChanceToMissAtDistance(dist);
			} else { // arrow traps
				int trapToHit = 100 * (sgGameInitInfo.nDifficulty + 1);
				hitChance = trapToHit - player.GetArmor() - ChanceToMissAtDistance(dist);
			}
		} else if (monster) { // magic spell from monster
			hitChance = monster->toHitWithMagic(sgGameInitInfo.nDifficulty) + 2 * (monster->level(sgGameInitInfo.nDifficulty) - player._pLevel) - ChanceToMissAtDistance(dist);
		} else { // magical trap
			hitChance = 100;
		}
		hitChance = std::min(hitChance, 95);
	}
#else // original code:
	int hitChance = 40;
	if (missileData.isArrow()) {
		int tac = player.GetArmor();
		if (monster) {
			hitChance = monster->toHit(sgGameInitInfo.nDifficulty) + 2 * (monster->level(sgGameInitInfo.nDifficulty) - player._pLevel - dist) - tac;
		} else { // arrow traps
			hitChance = 100 - (tac / 2) - 2 * dist;
		}
	} else if (monster != nullptr) {
		hitChance += 2 * (monster->level(sgGameInitInfo.nDifficulty) - player._pLevel - dist);
	}
#endif

	int minhit = 10;
	if (currlevel == 14)
		minhit = 20;
	if (currlevel == 15)
		minhit = 25;
	if (currlevel == 16)
		minhit = 30;
	if (hitChance < minhit) {
		hitChance = minhit;
	}

	int diceRollToAvoidHit = GenerateRnd(100);
#ifdef _DEBUG
	if (DebugGodMode)
		diceRollToAvoidHit = 1000;
#endif
	if (diceRollToAvoidHit >= hitChance) {
		return false;
	}

	int dam;
	if (missileID == MissileID::BoneSpirit) {
		dam = player._pHitPoints / 3;
	} else {
		int trapDamageReduction = 1;
		if (monster == nullptr) {
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::HalfTrapDamage)) {
				trapDamageReduction *= 2;
			}
#if JWK_EDIT_PLAYER_SKILLS
			if (player._pHeroClass == HeroClass::Rogue && (SDL_GetTicks64() - player._timeOfMostRecentSkillUse < 1000)) {
				trapDamageReduction *= 2; // rogue takes reduced damage if she tried to disarm the trap
			}
#endif
		}
		if (!shift) {
			dam = (mind << 6) + GenerateRnd(((maxd - mind) << 6) + 1);
			if (monster == nullptr) {
				dam /= trapDamageReduction;
			}
			dam += player._pIGetHit * 64;
		} else {
			dam = mind + GenerateRnd(maxd - mind + 1);
			if (monster == nullptr) {
				dam /= trapDamageReduction;
			}
			dam += player._pIGetHit;
		}

		dam = std::max(dam, 64);
	}

	int8_t resper;
	switch (damageType) {
	case DamageType::Fire:
		resper = player._pFireResist;
		break;
	case DamageType::Lightning:
		resper = player._pLghtResist;
		break;
	case DamageType::Magic:
	case DamageType::Acid:
		resper = player._pMagResist;
		break;
	default:
		resper = 0;
		break;
	}
#if JWK_EDIT_APOCALYPSE
	if (missileID == MissileID::ApocalypseBoom || missileID == MissileID::DiabloApocalypseBoom) {
		resper = player._pFireResist / 2; // Apocalypse is 50% fire, 50% physical
	}
#endif

	int blockChance, blockDifficultyRoll;
	if (shift || !player._pBlockFlag || (player._pmode != PM_STAND && player._pmode != PM_ATTACK) || (JWK_EDIT_APOCALYPSE && (missileID == MissileID::ApocalypseBoom || missileID == MissileID::DiabloApocalypseBoom))) {
		blockChance = 0;
		blockDifficultyRoll = 100;
	} else {
		blockChance = monster ? player.GetBlockChance(monster->level(sgGameInitInfo.nDifficulty)) : player.GetBlockChance(player._pLevel);
		blockDifficultyRoll = GenerateRnd(100);
	}

#if JWK_RESISTANT_TARGETS_CAN_BLOCK
	if (blockChance > blockDifficultyRoll) {
#else // original code
	if ((resper <= 0 || gbIsHellfire) && blockChance > blockDifficultyRoll) {
#endif
		Direction dir = mStartPos != Point(0,0) ? GetDirection(player.position.tile, mStartPos) : monster? GetDirection(player.position.tile, monster->position.tile) : player._pdir;
		*blocked = true;
		StartPlrBlock(player, dir);
		return true;
	}

	// target takes damage
	if (resper > 0) {
		dam -= dam * resper / 100;
	}

	if (&player == MyPlayer) {
		ApplyPlrDamage(damageType, player, 0, 0, dam, hitChance, deathReason);
	}
	if ((JWK_RESISTANT_TARGETS_CAN_BE_STUNNED || resper == 0) && player._pHitPoints >> 6 > 0) {
		StartPlrHit(player, dam, false); // stun player if damage is large enough
	} else if (player._pHitPoints >> 6 > 0) {
		player.Say(HeroSpeech::ArghClang);
	}

	return true;
}

static bool PvPHitByMissile(int p, const Player &attacker, int dist, Point mStartPos, MissileID missileID, int mindam, int maxdam, DamageType damageType, bool shift, bool *blocked)
{
	Player &target = Players[p];

	if (sgGameInitInfo.bFriendlyFire == 0 && attacker.friendlyMode && (!JWK_GUARDIAN_TARGETS_HOSTILE_PLAYERS || missileID != MissileID::GuardianBolt))
		return false;

	*blocked = false;

	if (target.isOnArenaLevel() && target._pmode == PM_WALK_SIDEWAYS) // jwk - What is this?  Deliberate miss when walking sideways for pvp balance?
		return false;

	if (target._pInvincible) {
		return false;
	}

	if (missileID == MissileID::HolyBolt) {
		return false;
	}

	const MissileData &missileData = GetMissileData(missileID);

	if (HasAnyOf(target._pSpellFlags, SpellFlag::Etherealize) && missileData.isArrow()) {
		return false;
	}

	int diceRollToAvoidHit = GenerateRnd(100);

	int hitChance;
#if JWK_USE_CONSISTENT_HIT_CHANCE
	if (shift) {
		hitChance = 100; // damage over time effects should hit every tick.  This makes floating damage numbers merge into a single value.
	} else {
		if (missileData.isArrow()) {
			hitChance = attacker.GetRangedToHit() - target.GetArmor() + 2 * (attacker._pLevel - target._pLevel) - ChanceToMissAtDistance(dist);
		} else {
			// magic ignores armor but caster level doesn't factor in, and there are no "+hit with magic" bonuses
			hitChance = attacker.GetMagicToHit() - 2 * target._pLevel - ChanceToMissAtDistance(dist);
		}
		hitChance = clamp(hitChance, 5, 95);
	}
#else // original code:
	if (missileData.isArrow()) {
		hitChance = attacker.GetRangedToHit() - target.GetArmor() - (dist * dist) / 2;
	} else {
		hitChance = attacker.GetMagicToHit() - 2 * target._pLevel - dist;
	}
	hitChance = clamp(hitChance, 5, 95);
#endif

	if (diceRollToAvoidHit >= hitChance) {
		return false;
	}

	int dam;
	if (missileID == MissileID::BoneSpirit) {
		dam = target._pHitPoints / 3;
	} else {
		dam = mindam + GenerateRnd(maxdam - mindam + 1);
		if (missileData.isArrow() && damageType == DamageType::Physical) {
			dam += attacker._pIBonusDamMod + attacker._pDamageMod + dam * attacker._pIBonusDam / 100;
#if JWK_EDIT_CRITICAL_STRIKE
			if (attacker._pHeroClass == HeroClass::Rogue) {
				if (GenerateRnd(200) < 10 + attacker._pLevel) { // 5.5% - 30% chance at level 1 - 50
					dam *= 2;
				}
			}
#endif
		}
		if (!shift)
			dam <<= 6;
	}

	int8_t resper;
	switch (damageType) {
	case DamageType::Fire:
		resper = target._pFireResist;
		break;
	case DamageType::Lightning:
		resper = target._pLghtResist;
		break;
	case DamageType::Magic:
	case DamageType::Acid:
		resper = target._pMagResist;
		break;
	default:
		resper = 0;
		break;
	}
#if JWK_EDIT_APOCALYPSE
	if (missileID == MissileID::ApocalypseBoom || missileID == MissileID::DiabloApocalypseBoom) {
		resper = target._pFireResist / 2; // Apocalypse is 50% fire, 50% physical
		dam /= 2; // Reduce apocalypse damage in pvp (in addition to other damage reduction)
	}
#endif
	if (resper > 0) {
		dam -= (dam * resper) / 100;
	}
#if JWK_REDUCE_DAMAGE_IN_PVP
	dam /= 2;
#else // original code
	if (!missileData.isArrow())
		dam /= 2;
#endif
#if JWK_FIX_NETWORK_SYNC_AND_AUTHORITY
	// At this point, the local player thinks there's a hit.  Since it's not necessarily in sync with other players, let the owner of the missile have authority of the hit logic.
	// The best everyone else can do is mark the missile as a hit and wait for damage numbers/block result from the missile owner.
	// I think marking the missile as a miss might be worse because this is the less likely to match the authority, and then local player will see the missile continue and potentially hit something else.
	// Maybe we could explicitly sync the missile hit flag but the latency won't be instant so players might see the missile transition from miss <-> hit when there's no target at the missile location.
	// Another option would be to encode the missile's hit chance roll when the missile is created.  Then all players would compute the same miss/hit result versus a specific target.
	// The symptom of incorrect hit/miss prediction is either:
	//      - Missile owner sees the missile hit but other players see it pass through the target despite doing damage
	//      - Missile owner sees the missile miss (pass through target) but other players see it hit and do no damage
	if (&attacker != MyPlayer)
		return true;
#endif

	int blockChance, blockDifficultyRoll;
	if (shift || !target._pBlockFlag || (target._pmode != PM_STAND && target._pmode != PM_ATTACK) || (JWK_EDIT_APOCALYPSE && (missileID == MissileID::ApocalypseBoom || missileID == MissileID::DiabloApocalypseBoom))) {
		blockChance = 0;
		blockDifficultyRoll = 100;
	} else {
		blockChance = target.GetBlockChance(attacker._pLevel);
		blockDifficultyRoll = GenerateRnd(100);
	}

	if ((JWK_RESISTANT_TARGETS_CAN_BLOCK || resper == 0) && blockChance > blockDifficultyRoll) {
		Direction dir = mStartPos != Point(0,0) ? GetDirection(target.position.tile, mStartPos) : target._pdir;
		if (JWK_FIX_NETWORK_SYNC_AND_AUTHORITY) {
			NetSendCmdPvPDamage(true, p, static_cast<uint8_t>(dir), 0, damageType); // 0 hit chance informs the target the attack was blocked (otherwise defender won't see their own block)
		}
		StartPlrBlock(target, dir);
		*blocked = true;
	} else { // target takes damage
		if (&attacker == MyPlayer) {
			NetSendCmdPvPDamage(true, p, dam, hitChance, damageType);
			AddFloatingNumber(damageType, target, dam, hitChance);
		}
		if (JWK_RESISTANT_TARGETS_CAN_BE_STUNNED || resper == 0) {
			StartPlrHit(target, dam, false);
		} else {
			target.Say(HeroSpeech::ArghClang);
		}
	}

	return true;
}

static void CheckMissileCollision(Missile &missile, DamageType damageType, int minDamage, int maxDamage, bool isDamageShifted, Point position, bool dontDeleteOnCollision)
{
	if (!InDungeonBounds(position))
		return;

	int mx = position.x;
	int my = position.y;

	bool isMonsterHit = false;
	int mid = dMonster[mx][my];
	if (mid > 0 || (mid != 0 && Monsters[abs(mid) - 1].mode == MonsterMode::Petrified)) {
		mid = abs(mid) - 1;
#if JWK_PREVENT_DUPLICATE_MISSILE_HITS
		if (RecordMissleGroupHit(missile, Monsters[mid]._missileGroupsToIgnoreThisTick, Monsters[mid]._missileGroupsToIgnoreForever))
#endif
		{
			if (missile.IsTrap()) {
				isMonsterHit = MonsterHitByMissileFromMonsterOrTrap(mid, nullptr, minDamage, maxDamage, missile._midist, missile._mitype, damageType, isDamageShifted);
			} else if (missile._micaster == TARGET_PLAYERS) { // was fired by a monster
				Monster* attacker = &Monsters[missile._misource];
				if (Monsters[mid].isPlayerMinion() != Monsters[missile._misource].isPlayerMinion() //  the monsters are on opposing factions
					|| (Monsters[missile._misource].flags & MFLAG_BERSERK) != 0                    //  or the attacker is berserked
					|| (Monsters[mid].flags & MFLAG_BERSERK) != 0                                  //  or the target is berserked
				) {
					// then the monsters can attack each other
					assert(attacker != nullptr);
					isMonsterHit = MonsterHitByMissileFromMonsterOrTrap(mid, attacker, minDamage, maxDamage, missile._midist, missile._mitype, damageType, isDamageShifted);
				}
			} else { // fired from a player
				assert(IsAnyOf(missile._micaster, TARGET_BOTH, TARGET_MONSTERS));
				isMonsterHit = MonsterHitByMissileFromPlayer(missile._misource, mid, minDamage, maxDamage, missile._midist, missile._mitype, damageType, isDamageShifted);
			}
		}
	}

	if (isMonsterHit) {
		if (!dontDeleteOnCollision)
			missile._ticksUntilExpiry = 0;
		missile._miHitFlag = true;
	}

	bool isPlayerHit = false;
	bool blocked = false;
	int8_t pid = dPlayer[mx][my];
	if (pid > 0) {
#if JWK_PREVENT_DUPLICATE_MISSILE_HITS
		if (RecordMissleGroupHit(missile, Players[pid - 1]._missileGroupsToIgnoreThisTick, Players[pid - 1]._missileGroupsToIgnoreForever))
#endif
		{
			if (missile._micaster != TARGET_BOTH && !missile.IsTrap()) {
				if (missile._micaster == TARGET_MONSTERS) {
					if ((pid - 1) != missile._misource)
						isPlayerHit = PvPHitByMissile(pid - 1, Players[missile._misource], missile._midist, missile.position.start, missile._mitype, minDamage, maxDamage, damageType, isDamageShifted, &blocked);
				} else {
					Monster &monster = Monsters[missile._misource];
					isPlayerHit = PlayerHitByMissile(pid - 1, &monster, missile._midist, missile.position.start, missile._mitype, minDamage, maxDamage, damageType, isDamageShifted, DeathReason::MonsterOrTrap, &blocked);
				}
			} else {
				DeathReason deathReason = missile.sourceType() == MissileSource::Player ? DeathReason::Player : DeathReason::MonsterOrTrap;
				isPlayerHit = PlayerHitByMissile(pid - 1, nullptr, missile._midist, missile.position.start, missile._mitype, minDamage, maxDamage, damageType, isDamageShifted, deathReason, &blocked);
			}
		}
	}

	if (isPlayerHit) {
		if (gbIsHellfire && blocked) {
			RotateBlockedMissile(missile);
		} else if (!dontDeleteOnCollision) {
			missile._ticksUntilExpiry = 0;
		}
		missile._miHitFlag = true;
	}

	if (IsMissileBlockedByTile({ mx, my })) {
		Object *object = FindObjectAtPosition({ mx, my });
		if (object != nullptr && object->IsBreakable()) {
			BreakObjectMissile(missile.sourcePlayer(), *object);
		}

		if (!dontDeleteOnCollision)
			missile._ticksUntilExpiry = 0;
		missile._miHitFlag = false;
	}

	const MissileData &missileData = GetMissileData(missile._mitype);
	if (missile._ticksUntilExpiry == 0 && missileData.miSFX != -1)
		PlaySfxLoc(missileData.miSFX, missile.position.tile);
}

static bool MoveMissile(Missile &missile, tl::function_ref<bool(Point)> checkTile, bool ifCheckTileFailsDontMoveToTile = false)
{
	Point prevTile = missile.position.tile;
	missile.position.traveled += missile.position.velocity;
	UpdateMissilePos(missile);

	int possibleVisitTiles;
	if (missile.position.velocity.deltaX == 0 || missile.position.velocity.deltaY == 0)
		possibleVisitTiles = prevTile.WalkingDistance(missile.position.tile);
	else
		possibleVisitTiles = prevTile.ManhattanDistance(missile.position.tile);

	if (possibleVisitTiles == 0)
		return false;

	// Did the missile skip a tile?
	if (possibleVisitTiles > 1) {
		auto speed = abs(missile.position.velocity);
		float denominator = (2 * speed.deltaY >= speed.deltaX) ? 2 * speed.deltaY : speed.deltaX;
		auto incVelocity = missile.position.velocity * ((32 << 16) / denominator);
		auto traveled = missile.position.traveled - missile.position.velocity;
		// Adjust the traveled vector to start on the next smallest multiple of incVelocity
		if (incVelocity.deltaY != 0)
			traveled.deltaY = (traveled.deltaY / incVelocity.deltaY) * incVelocity.deltaY;
		if (incVelocity.deltaX != 0)
			traveled.deltaX = (traveled.deltaX / incVelocity.deltaX) * incVelocity.deltaX;
		do {
			auto initialDiff = missile.position.traveled - traveled;
			traveled += incVelocity;
			auto incDiff = missile.position.traveled - traveled;

			// we are at the original calculated position => resume with normal logic
			if ((initialDiff.deltaX < 0) != (incDiff.deltaX < 0))
				break;
			if ((initialDiff.deltaY < 0) != (incDiff.deltaY < 0))
				break;

			// calculate in-between tile
			Displacement pixelsTraveled = traveled >> 16;
			Displacement tileOffset = pixelsTraveled.screenToMissile();
			Point tile = missile.position.start + tileOffset;

			// we haven't quite reached the missile's current position,
			// but we can break early to avoid checking collisions in this tile twice
			if (tile == missile.position.tile)
				break;

			// skip collision logic if the missile is on a corner between tiles
			if (pixelsTraveled.deltaY % 16 == 0
			    && pixelsTraveled.deltaX % 32 == 0
			    && abs(pixelsTraveled.deltaY / 16) % 2 != abs(pixelsTraveled.deltaX / 32) % 2) {
				continue;
			}

			// don't call checkTile more than once for a tile
			if (prevTile == tile)
				continue;

			prevTile = tile;

			if (!checkTile(tile)) {
				missile.position.traveled = traveled;
				if (ifCheckTileFailsDontMoveToTile) {
					missile.position.traveled -= incVelocity;
					UpdateMissilePos(missile);
					missile.position.StopMissile();
				} else {
					UpdateMissilePos(missile);
				}
				return true;
			}

		} while (true);
	}

	if (!checkTile(missile.position.tile) && ifCheckTileFailsDontMoveToTile) {
		missile.position.traveled -= missile.position.velocity;
		UpdateMissilePos(missile);
		missile.position.StopMissile();
	}

	return true;
}

static void MoveMissileAndCheckMissileCol(Missile &missile, DamageType damageType, int mindam, int maxdam, bool ignoreStart, bool ifCollidesDontMoveToHitTile)
{
	auto checkTile = [&](Point tile) {
		if (ignoreStart && missile.position.start == tile)
			return true;

		CheckMissileCollision(missile, damageType, mindam, maxdam, false, tile, false);

		// Did missile hit anything?
		if (missile._ticksUntilExpiry != 0)
			return true;

		if (missile._miHitFlag && GetMissileData(missile._mitype).movementDistribution == MissileMovementDistribution::Blockable)
			return false;

		return !IsMissileBlockedByTile(tile);
	};

	bool tileChanged = MoveMissile(missile, checkTile, ifCollidesDontMoveToHitTile);

	int16_t tileTargetHash = dMonster[missile.position.tile.x][missile.position.tile.y] ^ dPlayer[missile.position.tile.x][missile.position.tile.y];

	// missile didn't change the tile... check that we perform CheckMissileCollision only once for any monster/player to avoid multiple hits for slow missiles
	if (!tileChanged && missile.lastCollisionTargetHash != tileTargetHash) {
		CheckMissileCollision(missile, damageType, mindam, maxdam, false, missile.position.tile, false);
	}

	// remember what target CheckMissileCollision was checked against
	missile.lastCollisionTargetHash = tileTargetHash;
}

static void SetMissAnim(Missile &missile, MissileGraphicID animtype)
{
	int dir = missile._mimfnum;

	if (animtype > MissileGraphicID::None) {
		animtype = MissileGraphicID::None;
	}

	const MissileFileData &missileData = GetMissileSpriteData(animtype);

	missile._miAnimType = animtype;
	missile._miAnimFlags = missileData.flags;
	if (!HeadlessMode) {
		missile._miAnimData = missileData.spritesForDirection(static_cast<size_t>(dir));
	}
	missile._miAnimDelay = missileData.animDelay(dir);
	missile._miAnimLen = missileData.animLen(dir);
	missile._miAnimWidth = missileData.animWidth;
	missile._miAnimWidth2 = missileData.animWidth2;
	missile._miAnimCnt = 0;
	missile._miAnimFrame = 1;
}

static void AddRune(Missile &missile, Point dst, MissileID missileID)
{
	if (LineClearMissile(missile.position.start, dst)) {
		std::optional<Point> runePosition = FindClosestValidPosition(
		    [](Point target) {
			    if (!InDungeonBounds(target)) {
				    return false;
			    }
			    if (IsObjectAtPosition(target)) {
				    return false;
			    }
			    if (TileContainsMissile(target)) {
				    return false;
			    }
			    if (TileHasAny(dPiece[target.x][target.y], TileProperties::Solid)) {
				    return false;
			    }
			    return true;
		    },
		    dst, 0, 9);

		if (runePosition) {
			missile.position.tile = *runePosition;
			missile.var1 = static_cast<int8_t>(missileID);
			missile._mlid = AddLight(missile.position.tile, 8);
			return;
		}
	}

	missile._miDelFlag = true;
}

static bool CheckIfTrig(Point position)
{
	for (int i = 0; i < numtrigs; i++) {
		if (trigs[i].position.WalkingDistance(position) < 2)
			return true;
	}
	return false;
}

/** @brief Sync missile position with parent missile */
static void SyncPositionWithParent(Missile &missile, const AddMissileParameter &parameter)
{
	const Missile *parent = parameter.pParent;
	if (parent == nullptr)
		return;

	missile.position.offset = parent->position.offset;
	missile.position.traveled = parent->position.traveled;
}

static void SpawnLightning(Missile &missile, int dam)
{
	missile._ticksUntilExpiry--;
	bool canMissileContinue = MoveMissile(
	    missile, [&](Point missilePos) {
		    assert(InDungeonBounds(missilePos));
		    int pn = dPiece[missilePos.x][missilePos.y];
		    assert(pn >= 0 && pn <= MAXTILES);

		    if (!missile.IsTrap() || missilePos != missile.position.start) { // <- Ignore blocking for the first tile of trap-spawned missiles otherwise the trap object blocks its own missile
			    if (TileHasAny(pn, TileProperties::BlockMissile)) {
				    missile._ticksUntilExpiry = 0;
				    return false;
			    }
		    }
#if JWK_EDIT_CHAIN_LIGHTNING
			if (missile.var3 > 0) { // then this is chain lightning as opposed to ordinary lightning
				int hitMonsterID = -1;
				int hitPlayerID = -1;
				if (missile._micaster == mienemy_type::TARGET_MONSTERS || missile._micaster == mienemy_type::TARGET_BOTH) {
					hitMonsterID = dMonster[missilePos.x][missilePos.y];
					// note: monster ID can be negative when the monster is moving between tiles (monster will occupy 2 tiles - positive ID on one tile and negative ID on the other tile)
					if (hitMonsterID > 0 || (hitMonsterID != 0 && Monsters[abs(hitMonsterID) - 1].mode == MonsterMode::Petrified)) {
						hitMonsterID = abs(dMonster[missilePos.x][missilePos.y]);
						if (missile.var6 < missile.monsterHistory.size()) { missile.monsterHistory[missile.var6++] = hitMonsterID; }
					}
				} else if (missile._micaster == mienemy_type::TARGET_PLAYERS || missile._micaster == mienemy_type::TARGET_BOTH) {
					if (dPlayer[missilePos.x][missilePos.y] > 0) { // don't check negative playerIDs (to be consistent with CheckMissileCollision)
						hitPlayerID = dPlayer[missilePos.x][missilePos.y];
						if (missile.var7 < missile.playerHistory.size()) { missile.playerHistory[missile.var7++] = hitPlayerID; }
					}
				}
				if (hitMonsterID > 0 || hitPlayerID > 0) {
					missile._ticksUntilExpiry = 0; // stop the existing bolt
					if (missile.var3 > 1) // if we have any bounces left (note: var3=1 means chain lightning with 0 bounces left, var3==0 means ordinary lightning)
					{
						int maxRadius = 10;
						int casterPosX = missile.var4;
						int casterPosY = missile.var5;
#if 1 // Select nearest target that hasn't already been targetted.  By design, monsters become immune after getting hit so they can only be hit once per chain lightning cast.
						std::optional<Point> targetPosition = FindClosestValidPosition(
							[&](Point target) {
								// search for a new target to bounce toward
								assert(target.x != missilePos.x || target.y != missilePos.y); // FindClosestValidPosition should never select the target we're bouncing from because we specified minimum radius=1

								// Exclude targets that are too far from the original caster, otherwise chain lightning could bounce its way through the whole dungeon
								// Note: We want to target monsters even if they aren't visible, similar to firing other spells into the dark.
								if (!InDungeonBounds(target)) { return false; }
								if (abs(casterPosX - target.x) > maxRadius || abs(casterPosY - target.y) > maxRadius) { return false; }

								int16_t monsterTargetID = -1;
								int16_t playerTargetID = -1;
								if (missile._micaster == mienemy_type::TARGET_MONSTERS) {
									if (dMonster[target.x][target.y] <= 0 || Monsters[dMonster[target.x][target.y] - 1].isPlayerMinion()) { return false; }
									monsterTargetID = dMonster[target.x][target.y];
								} else if (missile._micaster == mienemy_type::TARGET_PLAYERS) {
									if (dPlayer[target.x][target.y] <= 0 || dPlayer[target.x][target.y] == MyPlayerId) { return false; }
									playerTargetID = dPlayer[target.x][target.y];
								} else { // mienemy_type::TARGET_BOTH
									if (dMonster[target.x][target.y] > 0 && !Monsters[dMonster[target.x][target.y] - 1].isPlayerMinion()) { monsterTargetID = dMonster[target.x][target.y]; }
									else if (dPlayer[target.x][target.y] > 0 && dPlayer[target.x][target.y] != MyPlayerId) { playerTargetID = dPlayer[target.x][target.y]; }
									else { return false; }
								}

								// don't target a recently targetted monster or player
								if (monsterTargetID > 0) {
									for (int i = 0; i < missile.var6; i++) {
										if (monsterTargetID == missile.monsterHistory[i]) { return false; }
									}
								} else if (playerTargetID > 0) {
									for (int i = 0; i < missile.var7; i++) {
										if (playerTargetID == missile.playerHistory[i]) { return false; }
									}
								}

								// Exclude targets without a clear line of sight from the bounce point to the new target
								if (IsDirectPathBlocked(missilePos, target)) { return false; }
								return true;
							},
							missilePos, 1, maxRadius);
#else // alternate search method
						std::array<std::pair<int, Point>, 1> candidateTargets; // pair<distance, targetPoint>
						int numCandidateTargets = 0;
						int minx = std::max(casterPosX - maxRadius, 1);
						int maxx = std::min(casterPosX + maxRadius, MAXDUNX - 1);
						int miny = std::max(casterPosY - maxRadius, 1);
						int maxy = std::min(casterPosY + maxRadius, MAXDUNY - 1);
						for (int j = miny; j < maxy; j++) {
							for (int k = minx; k < maxx; k++) {
								Point target {k, j};
								if (target == missilePos) { continue; } // don't target the actor we're bouncing from
								...
								int bounceDistance = missilePos.ApproxDistance(target);
							}
						}
#endif
						if (targetPosition) {
							missile.var3 -= 1; // reduce number of bounces which will be passed on to the spawned missile
							Direction dir = GetDirection(missilePos, *targetPosition);
							Missile* newMissile = AddMissile(missilePos, *targetPosition, dir, MissileID::LightningControl, missile._micaster, missile._misource, dam, missile._mispllvl, &missile);
							// To avoid duplicate damage, the actor who got hit and spawns a new missile shouldn't be hit by the missile they spawn
							if (newMissile) {
								if (hitMonsterID > 0) {
									Monsters[hitMonsterID - 1]._missileGroupsToIgnoreForever.AddEntry(newMissile->_missileGroup);
								} else if (hitPlayerID > 0) {
									Players[hitPlayerID - 1]._missileGroupsToIgnoreForever.AddEntry(newMissile->_missileGroup);
								}
							}
						}
					}
					return false;
				}
			}
#endif // JWK_EDIT_CHAIN_LIGHTNING
		    return true;
	    }); // MoveMissile()

	if (canMissileContinue) {
		auto position = missile.position.tile;
		if (position != Point { missile.var1, missile.var2 } && InDungeonBounds(position)) {
			MissileID spawnType;
			if (missile.sourceType() == MissileSource::Monster && IsAnyOf(missile.sourceMonster()->type().type, MT_STORM, MT_RSTORM, MT_STORML, MT_MAEL)) {
				spawnType = MissileID::ThinLightning;
			} else {
				spawnType = MissileID::Lightning;
			}
			Missile* spawnedMissile = AddMissile(position, missile.position.start, Direction::South, spawnType, missile._micaster, missile._misource, dam, missile._mispllvl, &missile);
			missile.var1 = position.x;
			missile.var2 = position.y;
			if (spawnedMissile) {
				spawnedMissile->var3 = missile.var3; // spawned missile needs to distinguish between lightning and chain lightning for computing damage
			}
		}
	}

	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
	}
}

#ifdef BUILD_TESTING
void TestRotateBlockedMissile(Missile &missile)
{
	RotateBlockedMissile(missile);
}
#endif

bool IsMissileBlockedByTile(Point tile)
{
	if (!InDungeonBounds(tile)) {
		return true;
	}

	if (TileHasAny(dPiece[tile.x][tile.y], TileProperties::BlockMissile)) {
		return true;
	}

	Object *object = FindObjectAtPosition(tile);
	// _oMissFlag is true if the object allows missiles to pass through so we need to invert the check here...
	return object != nullptr && !object->_oMissFlag;
}

int GetNumberOfChargedBolts(int spellLevel)
{
#if JWK_EDIT_CHARGED_BOLT
	// jwk - reduce number of charged bolts per cast since they move slower and stay on screen longer, and each bolt allocates a light
	return 1;
#else // original code:
	return (spllvl / 2) + 4;
#endif
}

void GetSpellStatsForUI(const Player& player, SpellID spellID, int spellLevel, int *mind, int *maxd)
{
	assert(spellID >= SpellID::FIRST && spellID <= SpellID::LAST);
	switch (spellID) {
	case SpellID::Firebolt:
		*mind = CalcFireBoltDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcFireBoltDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::Healing:
	case SpellID::HealOther:
		*mind = CalcHealingAmount(player, player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcHealingAmount(player, player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::RuneOfLight:
	case SpellID::Lightning:
		*mind = CalcLightningDamageShifted(player, spellLevel, GenerateRndMin, GenerateRndSumMin) * CalcLightningLength(spellLevel) >> 6;
		*maxd = CalcLightningDamageShifted(player, spellLevel, GenerateRndMax, GenerateRndSumMax) * CalcLightningLength(spellLevel) >> 6;
		break;
	case SpellID::Flash:
		// Note: Flash is a damage-over-time effect that lasts 19 ticks
		*mind = CalcFlashDamageShifted(player, spellLevel, GenerateRndMin, GenerateRndSumMin) * 19 >> 6;
		*maxd = CalcFlashDamageShifted(player, spellLevel, GenerateRndMax, GenerateRndSumMax) * 19 >> 6;
		break;
	case SpellID::Identify:
	case SpellID::TownPortal:
	case SpellID::StoneCurse:
	case SpellID::Infravision:
	case SpellID::Phasing:
	case SpellID::ManaShield:
	case SpellID::DoomSerpents:
	case SpellID::BloodRitual:
	case SpellID::Invisibility:
	case SpellID::Rage:
	case SpellID::Teleport:
	case SpellID::Etherealize:
	case SpellID::ItemRepair:
	case SpellID::StaffRecharge:
	case SpellID::TrapDisarm:
	case SpellID::Resurrect:
	case SpellID::Telekinesis:
	case SpellID::BoneSpirit:
	case SpellID::Warp:
	case SpellID::Reflect:
	case SpellID::Berserk:
	case SpellID::Search:
	case SpellID::RuneOfStone:
		*mind = -1;
		*maxd = -1;
		break;
	case SpellID::FireWall:
	case SpellID::LightningWall:
	case SpellID::RingOfFire:
		*mind = CalcFireWallDamageShifted(player, spellLevel, GenerateRndMin, GenerateRndSumMin) * MaxMergeTicksForDamageNumbers >> 6;
		*maxd = CalcFireWallDamageShifted(player, spellLevel, GenerateRndMax, GenerateRndSumMax) * MaxMergeTicksForDamageNumbers >> 6;
		break;
	case SpellID::Fireball:
	case SpellID::RuneOfFire: {
		*mind = CalcFireBallDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcFireBallDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
	} break;
	case SpellID::Guardian: {
		*mind = CalcGuardianDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcGuardianDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
	} break;
	case SpellID::ChainLightning:
		*mind = CalcChainLightningDamageShifted(player, spellLevel, GenerateRndMin, GenerateRndSumMin) * CalcChainLightningLength(spellLevel) >> 6;
		*maxd = CalcChainLightningDamageShifted(player, spellLevel, GenerateRndMax, GenerateRndSumMax) * CalcChainLightningLength(spellLevel) >> 6;
		break;
	case SpellID::FlameWave:
		*mind = CalcFlameWaveDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcFlameWaveDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::Nova:
	case SpellID::Immolation:
	case SpellID::RuneOfImmolation:
	case SpellID::RuneOfNova:
		*mind = CalcNovaDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcNovaDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::Inferno:
		*mind = CalcInfernoDamageShifted(player, spellLevel, GenerateRndMin, GenerateRndSumMin) * 25 >> 6;
		*maxd = CalcInfernoDamageShifted(player, spellLevel, GenerateRndMax, GenerateRndSumMax) * 25 >> 6;
		break;
	case SpellID::Golem:
	{
		uint32_t golemMaxHP, golemArmor, golemHitChance, golemMinDamage, golemMaxDamage;
		player.GetGolemStats(spellLevel, golemMaxHP, golemArmor, golemHitChance, golemMinDamage, golemMaxDamage);
		*mind = golemMinDamage;
		*maxd = golemMaxDamage;
		break;
	}
	case SpellID::Apocalypse:
		*mind = CalcApocalypseDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcApocalypseDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::Elemental:
		*mind = CalcElementalDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcElementalDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::ChargedBolt:
		*mind = CalcChargedBoltDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcChargedBoltDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::HolyBolt:
		*mind = CalcHolyBoltDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcHolyBoltDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	case SpellID::BloodStar:
		*mind = CalcBloodStarDamage(player, spellLevel, GenerateRndMin, GenerateRndSumMin);
		*maxd = CalcBloodStarDamage(player, spellLevel, GenerateRndMax, GenerateRndSumMax);
		break;
	default:
		break;
	}
}

Direction16 GetDirection16(Point p1, Point p2)
{
	Displacement offset = p2 - p1;
	Displacement absolute = abs(offset);

	bool flipY = offset.deltaX != absolute.deltaX;
	bool flipX = offset.deltaY != absolute.deltaY;

	bool flipMedian = false;
	if (absolute.deltaX > absolute.deltaY) {
		std::swap(absolute.deltaX, absolute.deltaY);
		flipMedian = true;
	}

	Direction16 ret = Direction16::South;
	if (3 * absolute.deltaX <= (absolute.deltaY * 2)) { // mx/my <= 2/3, approximation of tan(33.75)
		if (5 * absolute.deltaX < absolute.deltaY)      // mx/my < 0.2, approximation of tan(11.25)
			ret = Direction16::SouthWest;
		else
			ret = Direction16::South_SouthWest;
	}

	Direction16 medianPivot = Direction16::South;
	if (flipY) {
		ret = Direction16Flip(ret, Direction16::SouthWest);
		medianPivot = Direction16Flip(medianPivot, Direction16::SouthWest);
	}
	if (flipX) {
		ret = Direction16Flip(ret, Direction16::SouthEast);
		medianPivot = Direction16Flip(medianPivot, Direction16::SouthEast);
	}
	if (flipMedian)
		ret = Direction16Flip(ret, medianPivot);
	return ret;
}

void SetMissDir(Missile &missile, int dir)
{
	missile._mimfnum = dir;
	SetMissAnim(missile, missile._miAnimType);
}

void InitMissiles()
{
	Player &myPlayer = *MyPlayer;

	AutoMapShowItems = false;
	myPlayer._pSpellFlags &= ~SpellFlag::Etherealize;
	myPlayer._pInfraFlag = false;

	if (HasAnyOf(myPlayer._pSpellFlags, SpellFlag::RageActive | SpellFlag::RageCooldown)) {
		myPlayer._pSpellFlags &= ~SpellFlag::RageActive;
		myPlayer._pSpellFlags &= ~SpellFlag::RageCooldown;
		for (auto &missile : Missiles) {
			if (missile._mitype == MissileID::Rage) {
				if (missile.sourcePlayer() == MyPlayer) {
					int missingHP = myPlayer._pMaxHP - myPlayer._pHitPoints;
					CalcPlayerPowerFromItems(myPlayer, true);
					ApplyPlrDamage(DamageType::Physical, myPlayer, 0, 1, missingHP + missile.var2, 100, DeathReason::MonsterOrTrap);
				}
			}
		}
	}

	Missiles.clear();
	for (int j = 0; j < MAXDUNY; j++) {
		for (int i = 0; i < MAXDUNX; i++) { // NOLINT(modernize-loop-convert)
			dFlags[i][j] &= ~(DungeonFlag::Missile | DungeonFlag::MissileFireWall | DungeonFlag::MissileLightningWall);
		}
	}
}

void AddOpenNest(Missile &missile, AddMissileParameter &parameter)
{
	for (int x : { 80, 81 }) {
		for (int y : { 62, 63 }) {
			AddMissile({ x, y }, { 80, 62 }, parameter.midir, MissileID::BigExplosion, missile._micaster, missile._misource, missile._midam, 0);
		}
	}
	missile._miDelFlag = true;
}

void AddRuneOfFire(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::BigExplosion);
}

void AddRuneOfLight(Missile &missile, AddMissileParameter &parameter)
{
	int lvl = (missile.sourceType() == MissileSource::Player) ? missile.sourcePlayer()->_pLevel : 0;
	int dmg = 16 * (GenerateRndSum(10, 2) + lvl + 2);
	missile._midam = dmg;
	AddRune(missile, parameter.dst, MissileID::LightningWallSingleTile);
}

void AddRuneOfNova(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::Nova);
}

void AddRuneOfImmolation(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::Immolation);
}

void AddRuneOfStone(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::StoneCurse);
}

void AddReflect(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;

	if (missile.sourceType() != MissileSource::Player)
		return;

	Player &player = *missile.sourcePlayer();

	int add = (missile._mispllvl != 0 ? missile._mispllvl : 2) * player._pLevel;
	if (player.wReflections + add >= std::numeric_limits<uint16_t>::max())
		add = 0;
	player.wReflections += add;
	if (&player == MyPlayer)
		NetSendCmdParam1(true, CMD_SETREFLECT, player.wReflections);
}

void AddBerserk(Missile &missile, AddMissileParameter &parameter)
{
	missile._miDelFlag = true;
	parameter.spellFizzled = true;

	if (missile.sourceType() == MissileSource::Trap)
		return;

	std::optional<Point> targetMonsterPosition = FindClosestValidPosition(
	    [](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }

		    int monsterId = abs(dMonster[target.x][target.y]) - 1;
		    if (monsterId < 0)
			    return false;

		    const Monster &monster = Monsters[monsterId];
		    if (monster.isPlayerMinion())
			    return false;
		    if ((monster.flags & MFLAG_BERSERK) != 0)
			    return false;
		    if (monster.isUnique() || monster.ai == MonsterAIID::Diablo)
			    return false;
		    if (IsAnyOf(monster.mode, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Charge))
			    return false;
		    if ((monster.resistance & IMMUNE_MAGIC) != 0)
			    return false;
		    if ((monster.resistance & RESIST_MAGIC) != 0 && ((monster.resistance & RESIST_MAGIC) != 1 || !FlipCoin()))
			    return false;

		    return true;
	    },
	    parameter.dst, 0, 5);

	if (targetMonsterPosition) {
		auto &monster = Monsters[abs(dMonster[targetMonsterPosition->x][targetMonsterPosition->y]) - 1];
		Player &player = *missile.sourcePlayer();
		const int slvl = player.GetSpellLevel(SpellID::Berserk);
		monster.flags |= MFLAG_BERSERK | MFLAG_GOLEM;
		monster.minDamage = (GenerateRnd(10) + 120) * monster.minDamage / 100 + slvl;
		monster.maxDamage = (GenerateRnd(10) + 120) * monster.maxDamage / 100 + slvl;
		monster.minDamageSpecial = (GenerateRnd(10) + 120) * monster.minDamageSpecial / 100 + slvl;
		monster.maxDamageSpecial = (GenerateRnd(10) + 120) * monster.maxDamageSpecial / 100 + slvl;
		int lightRadius = leveltype == DTYPE_NEST ? 9 : 3;
		monster.lightId = AddLight(monster.position.tile, lightRadius);
		parameter.spellFizzled = false;
	}
}

void AddHorkSpawn(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 8);
	missile._ticksUntilExpiry = 9;
	missile.var1 = static_cast<int32_t>(parameter.midir);
	PutMissile(missile);
}

void AddJester(Missile &missile, AddMissileParameter &parameter)
{
	MissileID spell = MissileID::Firebolt;
	switch (GenerateRnd(10)) {
	case 0:
	case 1:
		spell = MissileID::Firebolt;
		break;
	case 2:
		spell = MissileID::Fireball;
		break;
	case 3:
		spell = MissileID::FireWallControl;
		break;
	case 4:
		spell = MissileID::Guardian;
		break;
	case 5:
		spell = MissileID::ChainLightning;
		break;
	case 6:
		spell = MissileID::TownPortal;
		break;
	case 7:
		spell = MissileID::Teleport;
		break;
	case 8:
		spell = MissileID::Apocalypse;
		break;
	case 9:
		spell = MissileID::StoneCurse;
		break;
	}
	Missile *randomMissile = AddMissile(missile.position.start, parameter.dst, parameter.midir, spell, missile._micaster, missile._misource, 0, missile._mispllvl);
	parameter.spellFizzled = randomMissile == nullptr;
	missile._miDelFlag = true;
}

void AddStealPotions(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Crawl(0, 2, [&](Displacement displacement) {
		Point target = missile.position.start + displacement;
		if (!InDungeonBounds(target))
			return false;
		int8_t pnum = dPlayer[target.x][target.y];
		if (pnum == 0)
			return false;
		Player &player = Players[abs(pnum) - 1];

		bool hasPlayedSFX = false;
		for (int si = 0; si < MaxBeltItems; si++) {
			Item &beltItem = player.SpdList[si];
			BaseItemIdx ii = IDI_NONE;
			if (beltItem._itype == ItemType::Misc) {
				if (FlipCoin())
					continue;
				switch (beltItem._iMiscId) {
				case IMISC_FULLHEAL:
					ii = ItemMiscIdIdx(IMISC_HEAL);
					break;
				case IMISC_HEAL:
				case IMISC_MANA:
					player.RemoveSpdBarItem(si);
					break;
				case IMISC_FULLMANA:
					ii = ItemMiscIdIdx(IMISC_MANA);
					break;
				case IMISC_REJUV:
					ii = ItemMiscIdIdx(PickRandomlyAmong({ IMISC_HEAL, IMISC_MANA }));
					break;
				case IMISC_FULLREJUV:
					switch (GenerateRnd(3)) {
					case 0:
						ii = ItemMiscIdIdx(IMISC_FULLMANA);
						break;
					case 1:
						ii = ItemMiscIdIdx(IMISC_FULLHEAL);
						break;
					default:
						ii = ItemMiscIdIdx(IMISC_REJUV);
						break;
					}
					break;
				default:
					continue;
				}
			}
			if (ii != IDI_NONE) {
				auto seed = beltItem._iSeed;
				InitializeItemToDefaultValues(beltItem, ii);
				beltItem._iSeed = seed;
				beltItem._iStatFlag = true;
			}
			if (!hasPlayedSFX) {
				PlaySfxLoc(IS_POPPOP2, target);
				hasPlayedSFX = true;
			}
		}
		RedrawEverything();

		return false;
	});
	missile._miDelFlag = true;
}

void AddStealMana(Missile &missile, AddMissileParameter & /*parameter*/)
{
	std::optional<Point> trappedPlayerPosition = FindClosestValidPosition(
	    [](Point target) {
		    return InDungeonBounds(target) && dPlayer[target.x][target.y] != 0;
	    },
	    missile.position.start, 0, 2);

	if (trappedPlayerPosition) {
		Player &player = Players[abs(dPlayer[trappedPlayerPosition->x][trappedPlayerPosition->y]) - 1];

		player._pMana = 0;
		player._pManaBase = player._pMana + player._pMaxManaBase - player._pMaxMana;
		CalcPlayerInventory(player, false);
		RedrawComponent(PanelDrawComponent::Mana);
		PlaySfxLoc(TSFX_COW7, *trappedPlayerPosition);
	}

	missile._miDelFlag = true;
}

void AddSpectralArrow(Missile &missile, AddMissileParameter &parameter)
{
	int av = 0;

	if (missile.sourceType() == MissileSource::Player) {
		const Player &player = *missile.sourcePlayer();

		if (player._pHeroClass == HeroClass::Rogue)
			av += (player._pLevel - 1) / 4;
		else if (player._pHeroClass == HeroClass::Warrior || player._pHeroClass == HeroClass::Bard)
			av += (player._pLevel - 1) / 8;

		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::QuickAttack))
			av++;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastAttack))
			av += 2;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FasterAttack))
			av += 4;
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastestAttack))
			av += 8;
	}

	missile._ticksUntilExpiry = 1;
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile.var3 = av;
}

void AddWarp(Missile &missile, AddMissileParameter &parameter)
{
	int minDistanceSq = std::numeric_limits<int>::max();

	int id = missile._misource;
	Player &player = Players[id];
	Point tile = player.position.tile;

	for (int i = 0; i < numtrigs && i < MAXTRIGGERS; i++) {
		TriggerStruct *trg = &trigs[i];
		if (IsNoneOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABNEXTLVL, WM_DIABRTNLVL))
			continue;
		Point candidate = trg->position;
		auto getTriggerOffset = [](TriggerStruct *trg) {
			switch (leveltype) {
			case DTYPE_CATHEDRAL:
				if (setlevel && setlvlnum == SL_VILEBETRAYER)
					return Displacement { 1, 1 }; // Portal
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABRTNLVL))
					return Displacement { 1, 2 };
				return Displacement { 0, 1 }; // WM_DIABNEXTLVL
			case DTYPE_CATACOMBS:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL))
					return Displacement { 1, 1 };
				return Displacement { 0, 1 }; // WM_DIABRTNLVL, WM_DIABNEXTLVL
			case DTYPE_CAVES:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL))
					return Displacement { 0, 1 };
				return Displacement { 1, 0 }; // WM_DIABRTNLVL, WM_DIABNEXTLVL
			case DTYPE_HELL:
				return Displacement { 1, 0 };
			case DTYPE_NEST:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABRTNLVL))
					return Displacement { 0, 1 };
				return Displacement { 1, 0 }; // WM_DIABNEXTLVL
			case DTYPE_CRYPT:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABRTNLVL))
					return Displacement { 1, 1 };
				return Displacement { 0, 1 }; // WM_DIABNEXTLVL
			case DTYPE_TOWN:
				app_fatal("invalid leveltype: DTYPE_TOWN");
			case DTYPE_NONE:
				app_fatal("leveltype not set");
			}
			app_fatal(StrCat("invalid leveltype", static_cast<int>(leveltype)));
		};
		const Displacement triggerOffset = getTriggerOffset(trg);
		candidate += triggerOffset;
		const Displacement off = Point { player.position.tile } - candidate;
		const int distanceSq = off.deltaY * off.deltaY + off.deltaX * off.deltaX;
		if (distanceSq < minDistanceSq) {
			minDistanceSq = distanceSq;
			tile = candidate;
		}
	}
	missile._ticksUntilExpiry = 2;
	std::optional<Point> teleportDestination = FindClosestValidPosition(
	    [&player](Point target) {
		    for (int i = 0; i < numtrigs; i++) {
			    if (trigs[i].position == target)
				    return false;
		    }
		    return PosOkPlayer(player, target);
	    },
	    tile, 0, 5);

	if (teleportDestination) {
		missile.position.tile = *teleportDestination;
	} else {
		// No valid teleport destination found
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
	}
}

void AddBigExplosion(Missile &missile, AddMissileParameter & /*parameter*/)
{
	// MissileID::BigExplosion is only created by AddRuneOfFire() or AddOpenNest() in hellfire expansion
	if (missile.sourceType() == MissileSource::Player) {
		int dmg = 2 * (missile.sourcePlayer()->_pLevel + GenerateRndSum(10, 2)) + 4;
		dmg = ScaleSpellEffect(dmg, missile._mispllvl);

		missile._midam = dmg;

		const DamageType damageType = GetMissileData(missile._mitype).damageType();
		for (Point position : PointsInRectangleColMajor(Rectangle { missile.position.tile, 1 }))
			CheckMissileCollision(missile, damageType, dmg, dmg, false, position, true);
	}
	missile._mlid = AddLight(missile.position.start, 8);
	SetMissDir(missile, 0);
	missile._ticksUntilExpiry = missile._miAnimLen - 1;
}

void AddImmolation(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (missile._micaster == TARGET_MONSTERS) {
		sp += std::min(missile._mispllvl, 34);
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._ticksUntilExpiry = 256;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddLightningBow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 32);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._ticksUntilExpiry = 255;
	if (missile._misource < 0) {
		missile.var1 = missile.position.start.x;
		missile.var2 = missile.position.start.y;
	} else {
		missile.var1 = Players[missile._misource].position.tile.x;
		missile.var2 = Players[missile._misource].position.tile.y;
	}
	missile._midam <<= 6;
}

void AddMana(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	int manaAmount = (GenerateRnd(10) + 1) << 6;
	for (int i = 0; i < player._pLevel; i++) {
		manaAmount += (GenerateRnd(4) + 1) << 6;
	}
	for (int i = 0; i < missile._mispllvl; i++) {
		manaAmount += (GenerateRnd(6) + 1) << 6;
	}
	if (player._pHeroClass == HeroClass::Sorcerer)
		manaAmount *= 2;
	if (player._pHeroClass == HeroClass::Rogue || player._pHeroClass == HeroClass::Bard)
		manaAmount += manaAmount / 2;
	player._pMana += manaAmount;
	if (player._pMana > player._pMaxMana)
		player._pMana = player._pMaxMana;
	player._pManaBase += manaAmount;
	if (player._pManaBase > player._pMaxManaBase)
		player._pManaBase = player._pMaxManaBase;
	missile._miDelFlag = true;
	RedrawComponent(PanelDrawComponent::Mana);
}

void AddMagi(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	player._pMana = player._pMaxMana;
	player._pManaBase = player._pMaxManaBase;
	missile._miDelFlag = true;
	RedrawComponent(PanelDrawComponent::Mana);
}

void AddRingOfFire(Missile &missile, AddMissileParameter& parameter)
{
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._ticksUntilExpiry = 7;
}

void AddSearch(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	if (&player == MyPlayer)
		AutoMapShowItems = true;
	int lvl = 2;
	if (missile._misource >= 0)
		lvl = player._pLevel * 2;
	missile._ticksUntilExpiry = lvl + 10 * missile._mispllvl + 245;

	for (auto &other : Missiles) {
		if (&other != &missile && missile.isSameSource(other) && other._mitype == MissileID::Search) {
			int r1 = missile._ticksUntilExpiry;
			int r2 = other._ticksUntilExpiry;
			if (r2 < INT_MAX - r1)
				other._ticksUntilExpiry = r1 + r2;
			missile._miDelFlag = true;
			break;
		}
	}
}

void AddChargedBoltBow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	missile._mirnd = GenerateRnd(15) + 1;
	if (missile._micaster != TARGET_MONSTERS) {
		missile._midam = 15;
	}

	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mlid = AddLight(missile.position.start, 5);
	UpdateMissileVelocity(missile, dst, 8);
	missile.var1 = 5;
	missile.var2 = static_cast<int32_t>(parameter.midir);
	missile._ticksUntilExpiry = 256;
}

void AddElementalArrow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int av = 32;
	if (missile._micaster == TARGET_MONSTERS) {
		const Player &player = Players[missile._misource];
		if (player._pHeroClass == HeroClass::Rogue)
			av += (player._pLevel) / 4;
		else if (IsAnyOf(player._pHeroClass, HeroClass::Warrior, HeroClass::Bard))
			av += (player._pLevel) / 8;

		if (gbIsHellfire) {
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::QuickAttack))
				av++;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastAttack))
				av += 2;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FasterAttack))
				av += 4;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastestAttack))
				av += 8;
		} else {
			if (IsAnyOf(player._pHeroClass, HeroClass::Rogue, HeroClass::Warrior, HeroClass::Bard))
				av -= 1;
		}
	}
	UpdateMissileVelocity(missile, dst, av);

	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._ticksUntilExpiry = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 5);
}

void AddArrow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int av = 32;
	if (missile._micaster == TARGET_MONSTERS) {
		const Player &player = Players[missile._misource];

		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::RandomArrowVelocity)) {
			av = GenerateRnd(32) + 16;
		}
		if (player._pHeroClass == HeroClass::Rogue)
			av += (player._pLevel - 1) / 4;
		else if (player._pHeroClass == HeroClass::Warrior || player._pHeroClass == HeroClass::Bard)
			av += (player._pLevel - 1) / 8;

		if (gbIsHellfire) {
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::QuickAttack))
				av++;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastAttack))
				av += 2;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FasterAttack))
				av += 4;
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::FastestAttack))
				av += 8;
		}
	}
	UpdateMissileVelocity(missile, dst, av);
	missile._miAnimFrame = static_cast<int>(GetDirection16(missile.position.start, dst)) + 1;
	missile._ticksUntilExpiry = 256;
}

void UpdateVileMissPos(Missile &missile, Point dst)
{
	for (int k = 1; k < 50; k++) {
		for (int j = -k; j <= k; j++) {
			int yy = j + dst.y;
			for (int i = -k; i <= k; i++) {
				int xx = i + dst.x;
				if (PosOkPlayer(*MyPlayer, { xx, yy })) {
					missile.position.tile = { xx, yy };
					return;
				}
			}
		}
	}
}

void AddPhasing(Missile &missile, AddMissileParameter &parameter)
{
	missile._ticksUntilExpiry = 2;

	Player &player = Players[missile._misource];

	if (missile._micaster == TARGET_BOTH) {
		missile.position.tile = parameter.dst;
		if (!PosOkPlayer(player, parameter.dst))
			UpdateVileMissPos(missile, parameter.dst);
		return;
	}

	std::array<Point, 4 * 9> targets;

	int count = 0;
	for (int y = -6; y <= 6; y++) {
		for (int x = -6; x <= 6; x++) {
			if ((x >= -3 && x <= 3) || (y >= -3 && y <= 3))
				continue; // Skip center

			Point target = missile.position.start + Displacement { x, y };
			if (!PosOkPlayer(player, target))
				continue;

			targets[count] = target;
			count++;
		}
	}

	if (count == 0) {
		missile._miDelFlag = true;
		return;
	}

	missile.position.tile = targets[std::max<int32_t>(GenerateRnd(count), 0)];
}

void AddFirebolt(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 26;
	if (missile._micaster == TARGET_MONSTERS) {
		sp = 16;
		if (!missile.IsTrap()) {
			sp += std::min(missile._mispllvl * 2, 47); // faster fire bolt
		}
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._ticksUntilExpiry = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player: {
			const Player &player = *missile.sourcePlayer();
			missile._midam = CalcFireBoltDamage(player, missile._mispllvl, GenerateRnd, GenerateRndSum);
		} break;

		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = TrapFireboltDamage();
			break;
		}
	}
}

void AddMagmaBall(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile.position.traveled.deltaX += 3 * missile.position.velocity.deltaX;
	missile.position.traveled.deltaY += 3 * missile.position.velocity.deltaY;
	UpdateMissilePos(missile);
	if (!gbIsHellfire || (missile.position.velocity.deltaX & 0xFFFF0000) != 0 || (missile.position.velocity.deltaY & 0xFFFF0000) != 0)
		missile._ticksUntilExpiry = 256;
	else
		missile._ticksUntilExpiry = 1;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player:
			// Not typically created by Players
			break;
		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = ProjectileTrapDamage(missile);
			break;
		}
	}
}

void AddTeleport(Missile &missile, AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];

	std::optional<Point> teleportDestination = FindClosestValidPosition(
	    [&player](Point target) {
		    return PosOkPlayer(player, target);
	    },
	    parameter.dst, 0, 5);

	if (teleportDestination) {
		missile.position.tile = *teleportDestination;
		missile.position.start = *teleportDestination;
		missile._ticksUntilExpiry = 2;
	} else {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
	}
}

void AddNovaBall(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._ticksUntilExpiry = 255;
	const Point position { missile._misource < 0 ? missile.position.start : Point(Players[missile._misource].position.tile) };
	missile.var1 = position.x;
	missile.var2 = position.y;
}

void AddFireball(Missile &missile, AddMissileParameter &parameter)
{
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (missile._micaster == TARGET_MONSTERS) {
		sp += std::min(missile._mispllvl * 2, 34);
		Player &player = Players[missile._misource];
		missile._midam = CalcFireBallDamage(player, missile._mispllvl, GenerateRnd, GenerateRndSum);
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._ticksUntilExpiry = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddLightningControl(Missile &missile, AddMissileParameter &parameter)
{
	if (parameter.pParent && (parameter.pParent->_mitype == MissileID::ChainLightning || parameter.pParent->_mitype == MissileID::LightningControl)) {
		missile.var3 = parameter.pParent->var3;
#if JWK_EDIT_CHAIN_LIGHTNING
		missile.var4 = parameter.pParent->var4;
		missile.var5 = parameter.pParent->var5;
		missile.var6 = parameter.pParent->var6;
		missile.var7 = parameter.pParent->var7;
		missile.monsterHistory = parameter.pParent->monsterHistory;
		missile.playerHistory = parameter.pParent->playerHistory;
#endif
	} else {
		missile.var3 = 0; // mark this lightning as ordinary lightning (not chain lightning) for computing damage and such
		missile._missileGroup = GenerateMissileGroup();
	}
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	UpdateMissileVelocity(missile, parameter.dst, 32);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._ticksUntilExpiry = 256;
}

void AddLightning(Missile &missile, AddMissileParameter &parameter)
{
	if (parameter.pParent && parameter.pParent->_mitype == MissileID::LightningControl) {
		missile.var3 = parameter.pParent->var3;
	}
	missile.position.start = parameter.dst;

	SyncPositionWithParent(missile, parameter);

	missile._miAnimFrame = GenerateRnd(8) + 1;

	if (missile._micaster == TARGET_PLAYERS || missile.IsTrap()) {
		if (missile.var3 > 0) { // chain lightning
			missile._ticksUntilExpiry = 5; // length of the bolt
		} else {
			if (missile.IsTrap() || Monsters[missile._misource].type().type == MT_FAMILIAR)
				missile._ticksUntilExpiry = 8;
			else
				missile._ticksUntilExpiry = 10;
		}
	} else {
		// This determines the length of the lightning bolt measured in damage ticks.
		// A 1 length bolt will hit once when passing through a monster but there's no visible graphic in game.
		// A 453 length bolt will hit 453 times when passing through a monster.  The bolt is extremely long but it's not 453 grid squares long.  It looks about half that.
		//missile._ticksUntilExpiry = (missile._mispllvl / 2) + 6;
		if (missile.var3 > 0) { // chain lightning
			missile._ticksUntilExpiry = CalcChainLightningLength(missile._mispllvl);
		} else { // ordinary lightning
			missile._ticksUntilExpiry = CalcLightningLength(missile._mispllvl);
		}
	}
	missile._mlid = AddLight(missile.position.tile, 4);
}

void AddMissileExplosion(Missile &missile, AddMissileParameter &parameter)
{
#if JWK_EDIT_NOVA
	if (parameter.pParent && parameter.pParent->_miAnimType == MissileGraphicID::BloodStarYellow) {
		SetMissAnim(missile, MissileGraphicID::BloodStarYellowExplosion);
	} else
#endif
	if (missile._micaster != TARGET_MONSTERS && missile._misource >= 0) {
		switch (Monsters[missile._misource].type().type) {
		case MT_SUCCUBUS:
			SetMissAnim(missile, MissileGraphicID::BloodStarExplosion);
			break;
		case MT_SNOWWICH:
			SetMissAnim(missile, MissileGraphicID::BloodStarBlueExplosion);
			break;
		case MT_HLSPWN:
			SetMissAnim(missile, MissileGraphicID::BloodStarRedExplosion);
			break;
		case MT_SOLBRNR:
			SetMissAnim(missile, MissileGraphicID::BloodStarYellowExplosion);
			break;
		default:
			break;
		}
	}

	assert(parameter.pParent != nullptr); // AddMissileExplosion will always be called with a parent associated to the missile.
	auto &parent = *parameter.pParent;
	missile.position.tile = parent.position.tile;
	missile.position.start = parent.position.start;
	missile.position.offset = parent.position.offset;
	missile.position.traveled = parent.position.traveled;
	missile._ticksUntilExpiry = missile._miAnimLen;
}

void AddWeaponExplosion(Missile &missile, AddMissileParameter &parameter)
{
	missile.var2 = parameter.dst.x;
	if (parameter.dst.x == 1)
		SetMissAnim(missile, MissileGraphicID::MagmaBallExplosion);
	else
		SetMissAnim(missile, MissileGraphicID::ChargedBolt);
	missile._ticksUntilExpiry = missile._miAnimLen - 1;
}

void AddTownPortal(Missile &missile, AddMissileParameter &parameter)
{
	if (leveltype == DTYPE_TOWN) {
		missile.position.tile = parameter.dst;
		missile.position.start = parameter.dst;
	} else {
		std::optional<Point> targetPosition = FindClosestValidPosition(
		    [](Point target) {
			    if (!InDungeonBounds(target)) {
				    return false;
			    }
			    if (IsObjectAtPosition(target)) {
				    return false;
			    }
			    if (dPlayer[target.x][target.y] != 0) {
				    return false;
			    }
			    if (TileContainsMissile(target)) {
				    return false;
			    }

			    int dp = dPiece[target.x][target.y];
			    if (TileHasAny(dp, TileProperties::Solid | TileProperties::BlockMissile)) {
				    return false;
			    }
			    return !CheckIfTrig(target);
		    },
		    parameter.dst, 0, 5);

		if (targetPosition) {
			missile.position.tile = *targetPosition;
			missile.position.start = *targetPosition;
			missile._miDelFlag = false;
		} else {
			missile._miDelFlag = true;
		}
	}

	missile._ticksUntilExpiry = 100;
	missile.var1 = missile._ticksUntilExpiry - missile._miAnimLen;
	for (auto &other : Missiles) {
		if (other._mitype == MissileID::TownPortal && &other != &missile && missile.isSameSource(other))
			other._ticksUntilExpiry = 0;
	}
	PutMissile(missile);
	if (missile.sourcePlayer() == MyPlayer && !missile._miDelFlag && leveltype != DTYPE_TOWN) {
		if (!setlevel) {
			NetSendCmdLocParam3(true, CMD_ACTIVATEPORTAL, missile.position.tile, currlevel, leveltype, 0);
		} else {
			NetSendCmdLocParam3(true, CMD_ACTIVATEPORTAL, missile.position.tile, setlvlnum, leveltype, 1);
		}
	}
}

void AddManaShield(Missile &missile, AddMissileParameter &parameter)
{
	missile._miDelFlag = true;

	Player &player = Players[missile._misource];

	if (player.pManaShield) {
		parameter.spellFizzled = true;
		return;
	}

	player.pManaShield = true;
	if (&player == MyPlayer)
		NetSendCmd(true, CMD_SETSHIELD);
}

#if JWK_EDIT_PLAYER_SKILLS
// Sneak is a toggleable ability (code-wise, it's similar to mana shield and infravision)
void AddSneak(Missile &missile, AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];
	player.pSneak = !player.pSneak;
	missile._miDelFlag = true;
	// note: Net commands send to everyone including local player.
	if (&player == MyPlayer) {
		if (player.pSneak)
			NetSendCmd(true, CMD_SETSNEAK);
		else
			NetSendCmd(true, CMD_REMSNEAK);
	}
}
#endif // JWK_EDIT_PLAYER_SKILLS

void AddFlameWave(Missile &missile, AddMissileParameter &parameter)
{
	//missile._midam = GenerateRnd(10) + Players[missile._misource]._pLevel + 1;
	missile._midam = CalcFlameWaveDamage(Players[missile._misource], missile._mispllvl, GenerateRnd, GenerateRndSum);
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._ticksUntilExpiry = 255;

	// Adjust missile's position for rendering
	missile.position.tile += Direction::South;
	missile.position.offset.deltaY -= 32;
}

void AddChainLightning(Missile &missile, AddMissileParameter &parameter)
{
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile.var3 = std::max<int>(3 + missile._mispllvl / 3, missile.monsterHistory.size()); // number of chain lightning bounces
	missile.var4 = missile.position.start.x;
	missile.var5 = missile.position.start.y;
	missile.var6 = 0; // history of most recent monster hits
	missile.var7 = 0; // history of most recent player hits
	missile._ticksUntilExpiry = 1;
}

static void InitMissileAnimationFromMonster(Missile &mis, Direction midir, const Monster &mon, MonsterGraphic graphic)
{
	const AnimStruct &anim = mon.type().getAnimData(graphic);
	mis._mimfnum = static_cast<int32_t>(midir);
	mis._miAnimFlags = MissileGraphicsFlags::None;
	ClxSpriteList sprites = *anim.spritesForDirection(midir);
	const uint16_t width = sprites[0].width();
	mis._miAnimData.emplace(sprites);
	mis._miAnimDelay = anim.rate;
	mis._miAnimLen = anim.frames;
	mis._miAnimWidth = width;
	mis._miAnimWidth2 = CalculateWidth2(width);
	mis._miAnimAdd = 1;
	mis.var1 = 0;
	mis.var2 = 0;
	mis._miLightFlag = true;
	mis._ticksUntilExpiry = 256;
}

void AddRhino(Missile &missile, AddMissileParameter &parameter)
{
	Monster &monster = Monsters[missile._misource];

	MonsterGraphic graphic = MonsterGraphic::Walk;
	if (IsAnyOf(monster.type().type, MT_HORNED, MT_MUDRUN, MT_FROSTC, MT_OBLORD)) {
		graphic = MonsterGraphic::Special;
	} else if (IsAnyOf(monster.type().type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE)) {
		graphic = MonsterGraphic::Attack;
	}
	UpdateMissileVelocity(missile, parameter.dst, 18);
	InitMissileAnimationFromMonster(missile, parameter.midir, monster, graphic);
	if (IsAnyOf(monster.type().type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE))
		missile._miAnimFrame = 7;
	if (monster.isUnique()) {
		missile._mlid = monster.lightId;
	}
	PutMissile(missile);
}

void AddGenericMagicMissile(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 16);
	missile._ticksUntilExpiry = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
#if JWK_EDIT_NOVA
	if (missile._micaster == TARGET_MONSTERS && parameter.pParent && parameter.pParent->_mitype == MissileID::Nova) {
		SetMissAnim(missile, MissileGraphicID::BloodStarYellow);
	} else
#endif	
	if (missile._micaster != TARGET_MONSTERS && missile._misource > 0) {
		auto &monster = Monsters[missile._misource];
		if (monster.type().type == MT_SUCCUBUS)
			SetMissAnim(missile, MissileGraphicID::BloodStar);
		if (monster.type().type == MT_SNOWWICH)
			SetMissAnim(missile, MissileGraphicID::BloodStarBlue);
		if (monster.type().type == MT_HLSPWN)
			SetMissAnim(missile, MissileGraphicID::BloodStarRed);
		if (monster.type().type == MT_SOLBRNR)
			SetMissAnim(missile, MissileGraphicID::BloodStarYellow);
	}

	if (GetMissileSpriteData(missile._miAnimType).animFAmt == 16) {
		SetMissDir(missile, GetDirection16(missile.position.start, dst));
	}

	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player: {
			const Player &player = *missile.sourcePlayer();
			//missile._midam = 3 * missile._mispllvl - (player._pMagic / 8) + (player._pMagic / 2);
			missile._midam = CalcBloodStarDamage(player, missile._mispllvl, GenerateRnd, GenerateRndSum);
			break;
		}
		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = ProjectileTrapDamage(missile);
			break;
		}
	}
}

void AddAcid(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	SetMissDir(missile, GetDirection16(missile.position.start, parameter.dst));
	if (!gbIsHellfire || (missile.position.velocity.deltaX & 0xFFFF0000) != 0 || (missile.position.velocity.deltaY & 0xFFFF0000) != 0)
		missile._ticksUntilExpiry = 5 * (Monsters[missile._misource].intelligence + 4);
	else
		missile._ticksUntilExpiry = 1;
	missile._mlid = NO_LIGHT;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player:
			// Not typically created by Players
			break;
		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = ProjectileTrapDamage(missile);
			break;
		}
	}
	PutMissile(missile);
}

void AddAcidPuddle(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile._miLightFlag = true;
	int monst = missile._misource;
	if (missile.IsTrap()) {
		missile._ticksUntilExpiry = 150;
	} else {
		missile._ticksUntilExpiry = GenerateRnd(15) + 40 * (Monsters[monst].intelligence + 1);
	}
	missile._miPreFlag = true;
}

void AddAcidPuddleHuge(Missile &missile, AddMissileParameter &parameter)
{
	missile._miDelFlag = true;
	int damage = TrapDamageAcidPuddleShifted();

	if (missile.limitReached)
		return;

	Crawl(1, 2, [&](Displacement displacement) {
		Point target = missile.position.start + displacement;
		if (!InDungeonBounds(target))
			return false;
		int dp = dPiece[target.x][target.y];
		if (TileHasAny(dp, TileProperties::Solid))
			return false;
		if (IsObjectAtPosition(target))
			return false;
		if (!LineClearMissile(missile.position.start, target))
			return false;
		if (TileHasAny(dp, TileProperties::BlockMissile)) {
			missile.limitReached = true;
			return true;
		}

		AddMissile(target, target, Direction::South, MissileID::AcidPuddle, TARGET_PLAYERS, missile._misource, damage, 0);
		return false;
	});
}

void AddStoneCurse(Missile &missile, AddMissileParameter &parameter)
{
	int maxRadius = JWK_EDIT_STONE_CURSE ? 2 : 5;
	std::optional<Point> targetMonsterPosition = FindClosestValidPosition(
	    [](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }

		    int monsterId = abs(dMonster[target.x][target.y]) - 1;
		    if (monsterId < 0) {
			    return false;
		    }

		    auto &monster = Monsters[monsterId];

		    if (IsAnyOf(monster.type().type, MT_GOLEM, MT_DIABLO, MT_NAKRUL)) {
			    return false;
		    }
		    if (IsAnyOf(monster.mode, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Charge)) {
			    return false;
		    }
			if (JWK_EDIT_STONE_CURSE && monster.mode == MonsterMode::Petrified) {
				// There's no point allowing the player to curse an already-cursed target since you can't refresh the spell duration.
				// Excluding already-cursed targets makes the spell feel much better instead of wasting the player's clicks and mana.
				return false;
			}

		    return true;
	    },
	    parameter.dst, 0, maxRadius);

	if (!targetMonsterPosition) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	// Petrify the targeted monster
	int monsterId = abs(dMonster[targetMonsterPosition->x][targetMonsterPosition->y]) - 1;
	auto &monster = Monsters[monsterId];

	if (monster.mode == MonsterMode::Petrified) {
		// Monster is already petrified and StoneCurse doesn't stack
		missile._miDelFlag = true;
		return;
	}

	missile.var1 = static_cast<int>(monster.mode);
	missile.var2 = monsterId;
	monster.petrify();

	// And set up the missile to unpetrify it in the future
	missile.position.tile = *targetMonsterPosition;
	missile.position.start = missile.position.tile;
#if JWK_EDIT_STONE_CURSE // edit stone curse duration
	missile._ticksUntilExpiry = missile._mispllvl + 6;
#else // original code (cap the duration)
	missile._ticksUntilExpiry = missile._mispllvl + 6;
	if (missile._ticksUntilExpiry > 15)
		missile._ticksUntilExpiry = 15;
#endif
	missile._ticksUntilExpiry <<= 4;
}

void AddGolem(Missile &missile, AddMissileParameter &parameter)
{
	missile._miDelFlag = true;

	int playerId = missile._misource;
	Player &player = Players[playerId];
	Monster &golem = Monsters[playerId];
#if !JWK_EDIT_GOLEM
	if (golem.position.tile != GolemHoldingCell && &player == MyPlayer)
		KillMyGolem();
#endif
	if (golem.position.tile == GolemHoldingCell) {
		std::optional<Point> spawnPosition = FindClosestValidPosition(
		    [start = missile.position.start](Point target) {
			    return !IsTileOccupied(target) && LineClearMissile(start, target);
		    },
		    parameter.dst, 0, 5);

		if (spawnPosition) {
			SpawnGolem(player, golem, *spawnPosition, missile);
		}
	}
}

void AddHealing(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	int hp = CalcHealingAmount(player, player, missile._mispllvl, GenerateRnd, GenerateRndSum) << 6;

	player._pHitPoints = std::min(player._pHitPoints + hp, player._pMaxHP);
	player._pHPBase = std::min(player._pHPBase + hp, player._pMaxHPBase);

	missile._miDelFlag = true;
	RedrawComponent(PanelDrawComponent::Health);
}

void AddHealOther(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		NewCursor(CURSOR_HEALOTHER);
		if (ControlMode != ControlTypes::KeyboardAndMouse)
			TryIconCurs();
	}
}

void AddElemental(Missile &missile, AddMissileParameter &parameter)
{
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}

	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}

	Player &player = Players[missile._misource];
	missile._midam = CalcElementalDamage(player, missile._mispllvl, GenerateRnd, GenerateRndSum);

	UpdateMissileVelocity(missile, dst, 16);
	SetMissDir(missile, GetDirection(missile.position.start, dst));
	missile._ticksUntilExpiry = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile.var4 = dst.x;
	missile.var5 = dst.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

extern void FocusOnInventory();

void AddIdentify(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_IDENTIFY);
	}
}

void AddWallControl(Missile &missile, AddMissileParameter &parameter)
{
	std::optional<Point> spreadPosition = FindClosestValidPosition(
	    [start = missile.position.start](Point target) {
		    return start != target && IsTileNotSolid(target) && !IsObjectAtPosition(target) && LineClearMissile(start, target);
	    },
	    parameter.dst, 0, 5);

	if (!spreadPosition) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	missile._miDelFlag = false;
	missile.var1 = spreadPosition->x;
	missile.var2 = spreadPosition->y;
	missile.var5 = spreadPosition->x;
	missile.var6 = spreadPosition->y;
	missile.var3 = static_cast<int>(Left(Left(parameter.midir)));
	missile.var4 = static_cast<int>(Right(Right(parameter.midir)));
	missile._ticksUntilExpiry = 7; // determines the width of the wall
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}
}

void AddFlameWaveControl(Missile &missile, AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile._ticksUntilExpiry = 1;
	missile._miAnimFrame = 4;
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
		missile._missileGroupIgnoreForever = true;
	}
}

void AddNova(Missile &missile, AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;

	if (!missile.IsTrap()) {
		Player &player = Players[missile._misource];
		missile._midam = CalcNovaDamage(player, missile._mispllvl, GenerateRnd, GenerateRndSum);
	} else {
		missile._midam = TrapNovaDamage();
	}

	missile._ticksUntilExpiry = 1;
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
		missile._missileGroupIgnoreForever = true;
	}
}

void AddRage(Missile &missile, AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive | SpellFlag::RageCooldown) || player._pHitPoints <= player._pLevel << 6) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	int tmp = 3 * player._pLevel;
	tmp <<= 7;
	player._pSpellFlags |= SpellFlag::RageActive;
	missile.var2 = tmp;
	int lvl = player._pLevel * 2;
	missile._ticksUntilExpiry = lvl + 10 * missile._mispllvl + 245;
	CalcPlayerPowerFromItems(player, true);
	RedrawEverything();
	player.Say(HeroSpeech::Aaaaargh);
}

void AddItemRepair(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_REPAIR);
	}
}

void AddStaffRecharge(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_RECHARGE);
	}
}

void AddTrapDisarm(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		NewCursor(CURSOR_DISARM);
		if (ControlMode != ControlTypes::KeyboardAndMouse) {
			if (ObjectUnderCursor != nullptr)
				NetSendCmdLoc(MyPlayerId, true, CMD_DISARMXY, cursPosition);
			else
				NewCursor(CURSOR_HAND);
		}
	}
}

void AddInfravision(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile._ticksUntilExpiry = ScaleSpellEffect(1584, missile._mispllvl);
}

void ProcessInfravision(Missile &missile)
{
	Player &player = Players[missile._misource];
	missile._ticksUntilExpiry--;
	player._pInfraFlag = true;
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		RemoveInfravision(missile);
	}
}

void RemoveInfravision(Missile &missile)
{
	Player &player = Players[missile._misource];
	player._pInfraFlag = false;
}

void AddApocalypseBoom(Missile &missile, AddMissileParameter &parameter)
{
	// This is used for player, trap, and Diablo's apocalypse boom
	if (missile.IsTrap()) {
		missile._midam = TrapApocalypseBoomDamage();
	}
	missile.position.tile = parameter.dst;
	missile.position.start = parameter.dst;
	missile._ticksUntilExpiry = missile._miAnimLen; // 15
#if JWK_EDIT_APOCALYPSE
	missile._mlid = AddLight(missile.position.start, 1);
#endif
}

#if JWK_EDIT_APOCALYPSE

// This is used for player, trap, and Diablo's apocalypse boom
void ProcessApocalypseBoom(Missile &missile)
{
	missile._ticksUntilExpiry--;
	// Animate a light explosion.  Looks cool.
	constexpr std::array<uint8_t, 15> animateRadius = { 2, 5, 8, 11, 14, 15, 14, 13, 12, 11, 10, 8, 6, 4, 2 };
	int animFrame = std::min(14, missile._miAnimLen - missile._ticksUntilExpiry);
	int lightRadius = animateRadius[animFrame];
	ChangeLightRadius(missile._mlid, lightRadius);
	if (missile.var1 == 0) {
		// var6 and var7 indicate we're locked onto a specific target.  If the target is moving, it will have negative ID and positive ID entries in the dMonster/dPlayer array.
		// The positive entry can be located at either position.tile or position.future depending on the direction the target is moving (southward, northward, sideways).
		// CheckMissileCollision() only checks for positive ID so we need to call it using the positive entry instead of the negative entry.
		// See RemoveAllMissilesForPlayer() which removes any apocalypse boom locked onto a player who leaves the dungeon.
		WorldTilePosition p = missile.position.tile;
		if (missile.var6 >= 0) { // target locked on specific monster
			Monster& monster = Monsters[missile.var6];
			p = monster.position.tile;
			if (dMonster[p.x][p.y] < 0) {
				p = monster.position.future;
			}
		} else if (missile.var7 >= 0) { // target locked on specific player
			Player& player = Players[missile.var7];
			p = player.position.tile;
			if (dPlayer[p.x][p.y] < 0) {
				p = player.position.future;
			}
		}
		missile.position.tile = p;
		CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, missile.position.tile, true);
		if (missile._miHitFlag) {
			missile.var1 = 1; // Stop checking for collisions (limit 1 hit per boom)
		}
	}
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

// This is the player apocalypse.  Diablo uses AddDiabloApocalypse() instead.
void AddApocalypse(Missile &missile, AddMissileParameter& parameter)
{
	Player &player = Players[missile._misource];

	std::pair<Monster*, Player*> target(nullptr, nullptr);

	auto CheckForTarget = [&player, &missile, &target](Point p) {
		Monster* targetMonster = nullptr;
		Player* targetPlayer = nullptr;
		if (!InDungeonBounds(p)) {
			return;
		}
		if (TileHasAny(dPiece[p.x][p.y], TileProperties::Solid)) {
			return;
		}
		int monsterId = abs(dMonster[p.x][p.y]) - 1;
		if (monsterId >= 0) {
			targetMonster = &Monsters[monsterId];
			if (player.friendlyMode && targetMonster->isPlayerMinion()) {
				targetMonster = nullptr;
			} else if (IsAnyOf(targetMonster->mode, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Charge)) {
				targetMonster = nullptr;
			}
		}
		if (!player.friendlyMode) {
			int playerId = abs(dPlayer[p.x][p.y]) - 1;
			if (playerId >= 0) {
				targetPlayer = &Players[playerId];
				if (targetPlayer == &player)
					targetPlayer = nullptr;
			}
		}
		if (!targetMonster && !targetPlayer) {
			return;
		}
		if (JWK_APOCALYPSE_NEEDS_LINE_OF_SIGHT && !LineClearMissile(missile.position.start, p)) {
			return;
		}
		target.first = targetMonster;
		target.second = targetPlayer;
	};

	Point targetPosition;
	bool foundTarget = false;
	constexpr std::array<Displacement, 9> PointsToCheck = {Displacement(0,0), {1,0}, {0,-1}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}};
	for (Displacement d : PointsToCheck) {
		CheckForTarget(parameter.dst + d);
		if (target.first) {
			targetPosition = target.first->position.future;
			foundTarget = true;
			break;
		}
		if (target.second) {
			targetPosition = target.second->position.future;
			foundTarget = true;
			break;
		}
	}

	if (!foundTarget) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	int pid = missile._misource;
	int damage = CalcApocalypseDamage(Players[pid], missile._mispllvl, GenerateRnd, GenerateRndSum);
	Missile* boom = AddMissile(missile.position.start, targetPosition, Players[pid]._pdir, MissileID::ApocalypseBoom, TARGET_MONSTERS, pid, damage, 0);
	if (boom) {
		boom->var6 = target.first ? target.first->getId() : -1;  // lock onto target monster
		boom->var7 = target.second ? target.second->getId() : -1;  // lock onto target player
	}
	missile._miDelFlag = true;
}
void ProcessApocalypse(Missile &missile) {}

#else // original code

// This is used for player, trap, and Diablo's apocalypse boom
void ProcessApocalypseBoom(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile.var1 == 0) {
		CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, missile.position.tile, true);
		if (missile._miHitFlag) {
			missile.var1 = 1; // Stop checking for collisions (limit 1 hit per boom)
		}
	}
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
	}
	PutMissile(missile);
}

// This is the player apocalypse.  Diablo uses AddDiabloApocalypse() instead.
void AddApocalypse(Missile &missile, AddMissileParameter& parameter)
{
	Player &player = Players[missile._misource];

	missile.var1 = 8; // radius of the effect
	missile.var2 = std::max(missile.position.start.y - 8, 1);
	missile.var3 = std::min(missile.position.start.y + 8, MAXDUNY - 1);
	missile.var4 = std::max(missile.position.start.x - 8, 1);
	missile.var5 = std::min(missile.position.start.x + 8, MAXDUNX - 1);
	missile.var6 = missile.var4;
	//int playerLevel = player._pLevel;
	//missile._midam = GenerateRndSum(6, playerLevel) + playerLevel;
	missile._midam = 0; // compute damage per target later
	missile._ticksUntilExpiry = 255;
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
		missile._missileGroupIgnoreForever = true;
	}
}

// This is used for player's apocalypse (not Diablo's apocalypse)
void ProcessApocalypse(Missile &missile)
{
	for (int j = missile.var2; j < missile.var3; j++) {
		for (int k = missile.var4; k < missile.var5; k++) {
			int mid = dMonster[k][j] - 1;
			if (mid < 0)
				continue;
			if (Monsters[mid].isPlayerMinion())
				continue;
			if (TileHasAny(dPiece[k][j], TileProperties::Solid))
				continue;
			if ((JWK_APOCALYPSE_NEEDS_LINE_OF_SIGHT || gbIsHellfire) && !LineClearMissile(missile.position.tile, { k, j }))
				continue;

			int pid = missile._misource;
			int damage = CalcApocalypseDamage(Players[pid], missile._mispllvl, GenerateRnd, GenerateRndSum);
			AddMissile({ k, j }, { k, j }, Players[pid]._pdir, MissileID::ApocalypseBoom, TARGET_MONSTERS, pid, damage, 0);
			missile.var2 = j;
			missile.var4 = k + 1;
			return;
		}
		missile.var4 = missile.var6;
	}
	missile._miDelFlag = true;
}
#endif // JWK_EDIT_APOCALYPSE

void AddDiabloApocalypse(Missile &missile, AddMissileParameter & /*parameter*/)
{
	for (const Player &player : Players) {
		if (!player.plractive)
			continue;
		if (!LineClearMissile(missile.position.start, player.position.future))
			continue;

		Missile* boom = AddMissile({ 0, 0 }, player.position.future, Direction::South, MissileID::DiabloApocalypseBoom, missile._micaster, missile._misource, missile._midam, 0);
#if JWK_EDIT_APOCALYPSE
		boom->var7 = player.getId(); // lock onto target
#endif
	}
	missile._miDelFlag = true;
}

void AddInferno(Missile &missile, AddMissileParameter &parameter)
{
	missile.var2 = 5 * missile._midam;
	missile.position.start = parameter.dst;

	SyncPositionWithParent(missile, parameter);

	missile._ticksUntilExpiry = missile.var2 + 20;
	missile._mlid = AddLight(missile.position.start, 1);
	if (missile._micaster == TARGET_MONSTERS) {
		//int i = GenerateRnd(Players[missile._misource]._pLevel) + GenerateRnd(2);
		//missile._midam = 8 * i + 16 + ((8 * i + 16) / 2);
		missile._midam = CalcInfernoDamageShifted(Players[missile._misource], missile._mispllvl, GenerateRnd, GenerateRndSum);
	} else {
		auto &monster = Monsters[missile._misource];
		missile._midam = monster.minDamage + GenerateRnd(monster.maxDamage - monster.minDamage + 1);
		missile._midam = (missile._midam >> 6) / 25; // jwk - buff monster inferno (note: _midam is assumed shifted for inferno so the damage per tick is _midam >> 6)
	}
}

void AddInfernoControl(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 32);
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._ticksUntilExpiry = 256;
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}
}

void ProcessChargedBolt(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._miAnimType != MissileGraphicID::Lightning) {
		if (missile.var3 == 0) {
			constexpr int BPath[16] = { -1, 0, 1, -1, 0, 1, -1, -1, 0, 0, 1, 1, 0, 1, -1, 0 };
			int rand3Minus1 = BPath[missile._mirnd % 0xF]; // similar to "GenerateRnd(3) - 1"
			missile._mirnd = missile._mirnd + 1;

			// randomly turn left, right, or go straight in the original direction of the spellcast
			auto md = static_cast<Direction>(missile.var2);
			switch (rand3Minus1) {
			case -1:
				md = Left(md);
				break;
			case 1:
				md = Right(md);
				break;
			}

#if JWK_EDIT_CHARGED_BOLT // make charged bolts move slower and branch more often
			int travelSpeed = GenerateRnd(4) + 2;
			UpdateMissileVelocity(missile, missile.position.tile + md, travelSpeed);
			missile.var3 = GenerateRnd(8); // number of ticks between direction switches
#else // original code
			UpdateMissileVelocity(missile, missile.position.tile + md, 8);
			missile.var3 = 16;
#endif
		} else {
			missile.var3--;
		}
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, false);
		if (missile._miHitFlag) {
			missile.var1 = 8;
			missile._mimfnum = 0;
			missile.position.offset = { 0, 0 };
			missile.position.velocity = {};
			SetMissAnim(missile, MissileGraphicID::Lightning);
			missile._ticksUntilExpiry = missile._miAnimLen;
		}
		ChangeLight(missile._mlid, missile.position.tile, missile.var1);
	}
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void AddChargedBolt(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	missile._mirnd = GenerateRnd(15) + 1;
	missile._midam = (missile._micaster == TARGET_MONSTERS) ? CalcChargedBoltDamage(Players[missile._misource], missile._mispllvl, GenerateRnd, GenerateRndSum) : ScaleTrapDamage(10);
	// original code: missile._midam = (missile._micaster == TARGET_MONSTERS) ? (GenerateRnd(Players[missile._misource]._pMagic / 4) + 1) : 15;

	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mlid = AddLight(missile.position.start, JWK_EDIT_CHARGED_BOLT ? 3 : 5);

	UpdateMissileVelocity(missile, dst, 8);
	missile.var1 = 5;
	missile.var2 = static_cast<int>(parameter.midir);
	missile._ticksUntilExpiry = 256;
}

void AddHolyBolt(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (!missile.IsTrap()) {
		sp += std::min(missile._mispllvl * 2, 47);
	}

	Player &player = Players[missile._misource];

	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._ticksUntilExpiry = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	missile._midam = CalcHolyBoltDamage(player, missile._mispllvl, GenerateRnd, GenerateRndSum);
}

void AddResurrect(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	if (&player == MyPlayer) {
		NewCursor(CURSOR_RESURRECT);
		if (ControlMode != ControlTypes::KeyboardAndMouse)
			TryIconCurs();
	}
	missile._miDelFlag = true;
}

void AddResurrectBeam(Missile &missile, AddMissileParameter &parameter)
{
	missile.position.tile = parameter.dst;
	missile.position.start = parameter.dst;
	missile._ticksUntilExpiry = GetMissileSpriteData(MissileGraphicID::Resurrect).animLen(0);
}

void AddTelekinesis(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer)
		NewCursor(CURSOR_TELEKINESIS);
}

void AddBoneSpirit(Missile &missile, AddMissileParameter &parameter)
{
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 16);
	SetMissDir(missile, GetDirection(missile.position.start, dst));
	missile._ticksUntilExpiry = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile.var4 = dst.x;
	missile.var5 = dst.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddRedPortal(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile._ticksUntilExpiry = 100;
	missile.var1 = 100 - missile._miAnimLen;
	PutMissile(missile);
}

Missile *AddMissile(Point src, Point dst, Direction midir, MissileID mitype,
    mienemy_type micaster, int sourceID, int midam, int spllvl,
    Missile *parent, std::optional<_sfx_id> lSFX)
{
	if (Missiles.size() >= Missiles.max_size()) {
		return nullptr;
	}

	Missiles.emplace_back(Missile {}); // <- This zero initializes the Missile as of c++14
	auto &missile = Missiles.back();

	const MissileData &missileData = GetMissileData(mitype);

	missile._mitype = mitype;
	missile._micaster = micaster;
	missile._misource = sourceID;
	missile._midam = midam;
	missile._mispllvl = spllvl;
	missile.position.tile = src;
	missile.position.start = src;
	missile._miAnimAdd = 1;
	missile._miAnimType = missileData.mFileNum;
	missile._miDrawFlag = missileData.isDrawn();
	missile._mlid = NO_LIGHT;
	missile.lastCollisionTargetHash = 0;
	if (parent) {
		missile._missileGroup = parent->_missileGroup;
		missile._missileGroupIgnoreForever = parent->_missileGroupIgnoreForever;
	} else { // assign defaults.  The defaults may be overridden in the mAddProc
		missile._missileGroup = 0;
		missile._missileGroupIgnoreForever = false;
	}

	if (!missile.IsTrap() && micaster == TARGET_PLAYERS) {
		Monster &monster = Monsters[sourceID];
		if (monster.isUnique()) {
			missile._miUniqTrans = monster.uniqTrans + 1;
		}
	}

	if (missile._miAnimType == MissileGraphicID::None || GetMissileSpriteData(missile._miAnimType).animFAmt < 8)
		SetMissDir(missile, 0);
	else
		SetMissDir(missile, midir);

	if (!lSFX) {
		lSFX = missileData.mlSFX;
	}

	if (*lSFX != SFX_NONE) {
		PlaySfxLoc(*lSFX, missile.position.start);
	}

	AddMissileParameter parameter = { dst, midir, parent, false };
	missileData.mAddProc(missile, parameter);
	if (parameter.spellFizzled) {
		return nullptr;
	}

	return &missile;
}

void ProcessElementalArrow(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._miAnimType == MissileGraphicID::ChargedBolt || missile._miAnimType == MissileGraphicID::MagmaBallExplosion) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame + 5);
	} else {
		int mind;
		int maxd;
		int p = missile._misource;
		missile._midist++;
		if (!missile.IsTrap()) {
			if (missile._micaster == TARGET_MONSTERS) {
				// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
				const Player &player = Players[p];
				mind = player._pIMinDam;
				maxd = player._pIMaxDam;
			} else {
				// BUGFIX: damage of missile should be encoded in missile struct; monster can be dead before missile arrives.
				Monster &monster = Monsters[p];
				mind = monster.minDamage;
				maxd = monster.maxDamage;
			}
		} else {
			GetTrapArrowDamage(mind, maxd);
		}
		MoveMissileAndCheckMissileCol(missile, DamageType::Physical, mind, maxd, true, false);
		if (missile._ticksUntilExpiry == 0) {
			missile._mimfnum = 0;
			missile._ticksUntilExpiry = missile._miAnimLen - 1;
			missile.position.StopMissile();

			int eMind;
			int eMaxd;
			MissileGraphicID eAnim;
			DamageType damageType;
			switch (missile._mitype) {
			case MissileID::LightningArrow:
				if (!missile.IsTrap()) {
					// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
					const Player &player = Players[p];
					eMind = player._pILMinDam;
					eMaxd = player._pILMaxDam;
				} else {
					GetAddedTrapElementalArrowDamage(eMind, eMaxd);
				}
				eAnim = MissileGraphicID::ChargedBolt;
				damageType = DamageType::Lightning;
				break;
			case MissileID::FireArrow:
				if (!missile.IsTrap()) {
					// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
					const Player &player = Players[p];
					eMind = player._pIFMinDam;
					eMaxd = player._pIFMaxDam;
				} else {
					GetAddedTrapElementalArrowDamage(eMind, eMaxd);
				}
				eAnim = MissileGraphicID::MagmaBallExplosion;
				damageType = DamageType::Fire;
				break;
			default:
				app_fatal(StrCat("wrong missile ID ", static_cast<int>(missile._mitype)));
				break;
			}
			SetMissAnim(missile, eAnim);
			CheckMissileCollision(missile, damageType, eMind, eMaxd, false, missile.position.tile, true);
		} else {
			if (missile.position.tile != Point { missile.var1, missile.var2 }) {
				missile.var1 = missile.position.tile.x;
				missile.var2 = missile.position.tile.y;
				ChangeLight(missile._mlid, missile.position.tile, 5);
			}
		}
	}
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessArrow(Missile &missile)
{
	missile._ticksUntilExpiry--;
	missile._midist++;

	int mind;
	int maxd;
	switch (missile.sourceType()) {
	case MissileSource::Player: {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		const Player &player = *missile.sourcePlayer();
		mind = player._pIMinDam;
		maxd = player._pIMaxDam;
	} break;
	case MissileSource::Monster: {
		// BUGFIX: damage of missile should be encoded in missile struct; monster can be dead before missile arrives.
		const Monster &monster = *missile.sourceMonster();
		mind = monster.minDamage;
		maxd = monster.maxDamage;
	} break;
	case MissileSource::Trap:
		GetTrapArrowDamage(mind, maxd);
		break;
	}
	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), mind, maxd, true, false);
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessGenericProjectile(Missile &missile)
{
	missile._ticksUntilExpiry--;

	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, true);
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		Point dst = { 0, 0 };
		auto dir = static_cast<Direction>(missile._mimfnum);
		switch (missile._mitype) {
		case MissileID::Firebolt:
		case MissileID::MagmaBall:
			AddMissile(missile.position.tile, dst, dir, MissileID::MagmaBallExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::BloodStar:
			AddMissile(missile.position.tile, dst, dir, MissileID::BloodStarExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::Acid:
			AddMissile(missile.position.tile, dst, dir, MissileID::AcidSplat, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::OrangeFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::OrangeExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::BlueFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::BlueExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::RedFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::RedExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::YellowFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::YellowExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::BlueFlare2:
			AddMissile(missile.position.tile, dst, dir, MissileID::BlueExplosion2, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		default:
			break;
		}
		AddUnLight(missile._mlid);
		PutMissile(missile);
	} else {
		if (missile.position.tile != Point { missile.var1, missile.var2 }) {
			missile.var1 = missile.position.tile.x;
			missile.var2 = missile.position.tile.y;
			ChangeLight(missile._mlid, missile.position.tile, 8);
		}
		PutMissile(missile);
	}
}

void ProcessNovaBall(Missile &missile)
{
	Point targetPosition = { missile.var1, missile.var2 };
	missile._ticksUntilExpiry--;
	int j = missile._ticksUntilExpiry;
	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, false);
	if (missile._miHitFlag)
		missile._ticksUntilExpiry = j;

	if (missile.position.tile == targetPosition) {
		Object *object = FindObjectAtPosition(targetPosition);
		if (object != nullptr && object->IsShrine()) {
			missile._ticksUntilExpiry = j;
		}
	}
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessAcidPuddle(Missile &missile)
{
	missile._ticksUntilExpiry--;
	int range = missile._ticksUntilExpiry;
	CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	missile._ticksUntilExpiry = range;
	if (range == 0) {
		if (missile._mimfnum != 0) {
			missile._miDelFlag = true;
		} else {
			SetMissDir(missile, 1);
			missile._ticksUntilExpiry = missile._miAnimLen;
		}
	}
	PutMissile(missile);
}

void ProcessLightningWallTile(Missile &missile)
{
	missile._ticksUntilExpiry--;
	int range = missile._ticksUntilExpiry;
	CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._miHitFlag)
		missile._ticksUntilExpiry = range;
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void AddLightningWallTile(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._ticksUntilExpiry = 255 * (missile._mispllvl + 1); // time duration of wall
	switch (missile.sourceType()) {
	case MissileSource::Trap:
		missile.var1 = missile.position.start.x;
		missile.var2 = missile.position.start.y;
		break;
	case MissileSource::Player: {
		Player &player = *missile.sourcePlayer();
		missile.var1 = player.position.tile.x;
		missile.var2 = player.position.tile.y;
	} break;
	case MissileSource::Monster:
		assert(missile.sourceType() != MissileSource::Monster);
		break;
	}
}

void ProcessFireWallTile(Missile &missile)
{
	constexpr int ExpLight[14] = { 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 12 };

	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == missile.var1) {
		SetMissDir(missile, 1);
		missile._miAnimFrame = GenerateRnd(11) + 1;
	}
	if (missile._ticksUntilExpiry == missile._miAnimLen - 1) {
		SetMissDir(missile, 0);
		missile._miAnimFrame = 13;
		missile._miAnimAdd = -1;
	}
	CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, true);
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile._mimfnum != 0 && missile._ticksUntilExpiry != 0 && missile._miAnimAdd != -1 && missile.var2 < 12) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, ExpLight[0]);
		ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var2]);
		missile.var2++;
	}
	PutMissile(missile);
}

void AddFireWallTile(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	int i = missile._mispllvl;
	missile._ticksUntilExpiry = 10; // time duration of wall
	if (i > 0)
		missile._ticksUntilExpiry *= i + 1;
	if (missile._micaster == TARGET_PLAYERS || missile._misource < 0)
		missile._ticksUntilExpiry += currlevel;
	missile._ticksUntilExpiry *= 16;
	missile.var1 = missile._ticksUntilExpiry - missile._miAnimLen;
}

void ProcessFireball(Missile &missile)
{
	missile._ticksUntilExpiry--;

	if (missile._miAnimType == MissileGraphicID::BigExplosion) {
		if (missile._ticksUntilExpiry == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	} else {
		int minDam = missile._midam;
		int maxDam = missile._midam;
		if (missile._micaster != TARGET_MONSTERS) {
			if (missile.IsTrap()) {
				minDam = TrapFireballDamage();
				maxDam = minDam;
			} else {
				auto &monster = Monsters[missile._misource];
				minDam = monster.minDamage;
				maxDam = monster.maxDamage;
			}
		}
		const DamageType damageType = GetMissileData(missile._mitype).damageType();
		MoveMissileAndCheckMissileCol(missile, damageType, minDam, maxDam, true, false);
		if (missile._ticksUntilExpiry == 0) {
			const Point missilePosition = missile.position.tile;
			ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);

			constexpr Direction Offsets[] = {
				Direction::NoDirection,
				Direction::SouthWest,
				Direction::NorthEast,
				Direction::SouthEast,
				Direction::East,
				Direction::South,
				Direction::NorthWest,
				Direction::West,
				Direction::North
			};
			for (Direction offset : Offsets) {
				if (!IsDirectPathBlocked(missile.position.start, missilePosition + offset))
#if JWK_FIREBALL_DOES_REDUCED_SPLASH_DAMAGE
					CheckMissileCollision(missile, damageType, (minDam + 1) / 2, (maxDam + 1) / 2, false, missilePosition + offset, true);
#else // original code - do full damage to adjacent targets
					CheckMissileCollision(missile, damageType, minDam, maxDam, false, missilePosition + offset, true);
#endif
			}

			if (!TransList[dTransVal[missilePosition.x][missilePosition.y]]
			    || (missile.position.velocity.deltaX < 0 && ((TransList[dTransVal[missilePosition.x][missilePosition.y + 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y + 1], TileProperties::Solid)) || (TransList[dTransVal[missilePosition.x][missilePosition.y - 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y - 1], TileProperties::Solid))))) {
				missile.position.tile += Displacement { 1, 1 };
				missile.position.offset.deltaY -= 32;
			}
			if (missile.position.velocity.deltaY > 0
			    && ((TransList[dTransVal[missilePosition.x + 1][missilePosition.y]] && TileHasAny(dPiece[missilePosition.x + 1][missilePosition.y], TileProperties::Solid))
			        || (TransList[dTransVal[missilePosition.x - 1][missilePosition.y]] && TileHasAny(dPiece[missilePosition.x - 1][missilePosition.y], TileProperties::Solid)))) {
				missile.position.offset.deltaY -= 32;
			}
			if (missile.position.velocity.deltaX > 0
			    && ((TransList[dTransVal[missilePosition.x][missilePosition.y + 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y + 1], TileProperties::Solid))
			        || (TransList[dTransVal[missilePosition.x][missilePosition.y - 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y - 1], TileProperties::Solid)))) {
				missile.position.offset.deltaX -= 32;
			}
			missile._mimfnum = 0;
			SetMissAnim(missile, MissileGraphicID::BigExplosion);
			missile._ticksUntilExpiry = missile._miAnimLen - 1;
			missile.position.velocity = {};
		} else if (missile.position.tile != Point { missile.var1, missile.var2 }) {
			missile.var1 = missile.position.tile.x;
			missile.var2 = missile.position.tile.y;
			ChangeLight(missile._mlid, missile.position.tile, 8);
		}
	}

	PutMissile(missile);
}

void ProcessHorkSpawn(Missile &missile)
{
	missile._ticksUntilExpiry--;
	CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), 0, 0, false, missile.position.tile, false);
	if (missile._ticksUntilExpiry <= 0) {
		missile._miDelFlag = true;

		std::optional<Point> spawnPosition = FindClosestValidPosition(
		    [](Point target) {
			    return !IsTileOccupied(target);
		    },
		    missile.position.tile, 0, 1);

		if (spawnPosition) {
			auto facing = static_cast<Direction>(missile.var1);
			Monster *monster = AddMonster(*spawnPosition, facing, 1, true);
			if (monster != nullptr) {
				M_StartStand(*monster, facing);
			}
		}
	} else {
		missile._midist++;
		missile.position.traveled += missile.position.velocity;
		UpdateMissilePos(missile);
	}
	PutMissile(missile);
}

void ProcessRune(Missile &missile)
{
	Point position = missile.position.tile;
	int mid = dMonster[position.x][position.y];
	int pid = dPlayer[position.x][position.y];
	if (mid != 0 || pid != 0) {
		Point targetPosition = mid != 0 ? Monsters[abs(mid) - 1].position.tile : Players[abs(pid) - 1].position.tile;
		Direction dir = GetDirection(position, targetPosition);

		missile._miDelFlag = true;
		AddUnLight(missile._mlid);

		AddMissile(position, position, dir, static_cast<MissileID>(missile.var1), TARGET_BOTH, missile._misource, missile._midam, missile._mispllvl);
	}

	PutMissile(missile);
}

void ProcessBigExplosion(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry <= 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessLightningBow(Missile &missile)
{
	SpawnLightning(missile, missile._midam);
}

void ProcessRingOfFire(Missile &missile)
{
	missile._miDelFlag = true;
	int damage;
	if (missile._micaster == TARGET_MONSTERS) {
		int damage = 16 * (GenerateRndSum(10, 2) + Players[missile._misource]._pLevel + 2) / 2;
	} else if (missile.IsTrap()) {
		damage = TrapRingOfFireDamageShifted();
	} else {
		damage = 0; // I don't believe monsters can cast ring of fire?
	}

	if (missile.limitReached)
		return;

	Crawl(3, [&](Displacement displacement) {
		Point target = Point { missile.var1, missile.var2 } + displacement;
		if (!InDungeonBounds(target))
			return false;
		int dp = dPiece[target.x][target.y];
		if (TileHasAny(dp, TileProperties::Solid))
			return false;
		if (IsObjectAtPosition(target))
			return false;
		if (!LineClearMissile(missile.position.tile, target))
			return false;
		if (TileHasAny(dp, TileProperties::BlockMissile)) {
			missile.limitReached = true;
			return true;
		}

		AddMissile(target, target, Direction::South, MissileID::FireWallSingleTile, TARGET_BOTH, missile._misource, damage, missile._mispllvl);
		return false;
	});
}

void ProcessSearch(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry != 0)
		return;

	const Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	PlaySfxLoc(IS_CAST7, player.position.tile);
	if (&player == MyPlayer)
		AutoMapShowItems = false;
}

void ProcessNovaCommon(Missile &missile, MissileID projectileType)
{
	int id = missile._misource;
	int dam = missile._midam;
	Point src = missile.position.tile;
	Direction dir = Direction::South;
	mienemy_type en = TARGET_PLAYERS;
	if (!missile.IsTrap()) {
		dir = Players[id]._pdir;
		en = TARGET_MONSTERS;
	}

	constexpr std::array<WorldTileDisplacement, 9> quarterRadius = { { { 4, 0 }, { 4, 1 }, { 4, 2 }, { 4, 3 }, { 4, 4 }, { 3, 4 }, { 2, 4 }, { 1, 4 }, { 0, 4 } } };
	for (WorldTileDisplacement quarterOffset : quarterRadius) {
		// This ends up with two missiles targeting offsets 4,0, 0,4, -4,0, 0,-4.
		std::array<WorldTileDisplacement, 4> offsets { quarterOffset, quarterOffset.flipXY(), quarterOffset.flipX(), quarterOffset.flipY() };
		for (WorldTileDisplacement offset : offsets)
			AddMissile(src, src + offset, dir, projectileType, en, id, dam, missile._mispllvl, &missile);
	}
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
}

void ProcessImmolation(Missile &missile)
{
	ProcessNovaCommon(missile, MissileID::FireballBow);
}

void ProcessNova(Missile &missile)
{
#if JWK_EDIT_NOVA
	if (!missile.IsTrap()) {
		ProcessNovaCommon(missile, MissileID::BloodStar);
		return;
	}
#endif
	ProcessNovaCommon(missile, MissileID::NovaBall);
}

void ProcessSpectralArrow(Missile &missile)
{
	int id = missile._misource;
	int dam = missile._midam;
	Point src = missile.position.tile;
	Point dst = { missile.var1, missile.var2 };
	int spllvl = missile.var3;
	MissileID mitype = MissileID::Arrow;
	Direction dir = Direction::South;
	mienemy_type micaster = TARGET_PLAYERS;
	if (!missile.IsTrap()) {
		const Player &player = Players[id];
		dir = player._pdir;
		micaster = TARGET_MONSTERS;

		switch (player._pILMinDam) {
		case 0:
			mitype = MissileID::FireballBow;
			break;
		case 1:
			mitype = MissileID::LightningBow;
			break;
		case 2:
			mitype = MissileID::ChargedBoltBow;
			break;
		case 3:
			mitype = MissileID::HolyBoltBow;
			break;
		}
	}
	AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
	if (mitype == MissileID::ChargedBoltBow) {
		AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
		AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
	}
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
}

// The lightning control missile only spawns other missles (of type MissileID::Lightning for player-casted spell) which are the actual lightning bolts
void ProcessLightningControl(Missile &missile)
{
	missile._ticksUntilExpiry--;

	int dam = 0; // Interpret this as a shifted value (64 means 1 damage in game) for all sources (trap, monster, and player).
	if (missile.IsTrap()) {
		// BUGFIX: damage of missile should be encoded in missile struct; monster can be dead before missile arrives.
		//dam = GenerateRnd(currlevel) + 2 * currlevel;
		if (missile.var3 > 0) {
			dam = TrapChainLightningDamageShifted();
		} else {
			dam = TrapLightningDamageShifted();
		}
	} else if (missile._micaster == TARGET_MONSTERS) {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		//dam = (GenerateRnd(2) + GenerateRnd(Players[missile._misource]._pLevel) + 2) << 6;
		if (missile.var3 > 0) {
			dam = CalcChainLightningDamageShifted(Players[missile._misource], missile._mispllvl, GenerateRnd, GenerateRndSum);
		} else {
			dam = CalcLightningDamageShifted(Players[missile._misource], missile._mispllvl, GenerateRnd, GenerateRndSum);
		}
	} else {
		auto &monster = Monsters[missile._misource];
		dam = 2 * (monster.minDamage + GenerateRnd(monster.maxDamage - monster.minDamage + 1));
	}

	SpawnLightning(missile, dam);
}

void ProcessLightning(Missile &missile)
{
	missile._ticksUntilExpiry--;
	int j = missile._ticksUntilExpiry;
	if (missile.position.tile != missile.position.start)
		CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._miHitFlag) // did we hit a player or a monster? (we want to penetrate for these hits but not for walls/objects)
		missile._ticksUntilExpiry = j; // don't delete missile
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessTownPortal(Missile &missile)
{
	int expLight[17] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

	if (missile._ticksUntilExpiry > 1)
		missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == missile.var1)
		SetMissDir(missile, 1);
	if (leveltype != DTYPE_TOWN && missile._mimfnum != 1 && missile._ticksUntilExpiry != 0) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, 1);
		ChangeLight(missile._mlid, missile.position.tile, expLight[missile.var2]);
		missile.var2++;
	}

	for (Player &player : Players) {
		if (player.plractive && player.isOnActiveLevel() && !player._pLvlChanging && player._pmode == PM_STAND && player.position.tile == missile.position.tile) {
			ClrPlrPath(player);
			if (&player == MyPlayer) {
				NetSendCmdParam1(true, CMD_WARP, missile._misource);
				player._pmode = PM_NEWLVL;
			}
		}
	}

	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void AddFlashBottom(Missile &missile, AddMissileParameter &parameter)
{
	if (!parameter.pParent) {
		missile._missileGroup = GenerateMissileGroup();
	}
	switch (missile.sourceType()) {
	case MissileSource::Player: {
		Player &player = *missile.sourcePlayer();
		missile._midam = CalcFlashDamageShifted(player, missile._mispllvl, GenerateRnd, GenerateRndSum);
	} break;
	case MissileSource::Monster:
		// original code: missile._midam = missile.sourceMonster()->level(sgGameInitInfo.nDifficulty) * 2;
		// jwk - buff monster flash.  Now, a level 60 advocate in hell difficulty will do 60*4*19 >> 6 = 71 damage in game
		// Note: When stun-locking a flash-casting monster, they sometimes do double damage (not sure why)
		missile._midam = missile.sourceMonster()->level(sgGameInitInfo.nDifficulty) * 4;
		break;
	case MissileSource::Trap:
		missile._midam = TrapFlashDamage();
		break;
	}

	missile._ticksUntilExpiry = 19;
}

void AddFlashTop(Missile &missile, AddMissileParameter &parameter)
{
	// FlashTop is the same effect as FlashBottom but for the North, NorthEast, East directions (possibly because in 2D rendering, the effect needs to be drawn under/over other objects.  See OperateShrineSparkling() which only spawns FlashBottom)
	// jwk - The original code neglected to compute a damage value for FlashTop (0 damage).  I fixed this by calling AddFlashBottom to guarantee FlashTop matches FlashBottom:
	AddFlashBottom(missile, parameter);
	missile._miPreFlag = true;

	// FlashTop should always come paired with FlashBottom because the bottom manages cleanup
	assert(parameter.pParent && parameter.pParent->_mitype == MissileID::FlashBottom);
}

void RemoveFlash(Missile &missile)
{
	if (missile._micaster == TARGET_MONSTERS && !missile.IsTrap())
		Players[missile._misource]._pInvincible = false;
}

void ProcessFlashBottom(Missile &missile)
{
	if (missile._micaster == TARGET_MONSTERS && !missile.IsTrap()) {
		Players[missile._misource]._pInvincible = true;
	}
	missile._ticksUntilExpiry--;

	constexpr Direction Offsets[] = {
		Direction::NorthWest,
		Direction::NoDirection,
		Direction::SouthEast,
		Direction::West,
		Direction::SouthWest,
		Direction::South
	};
	for (Direction offset : Offsets)
		CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile + offset, true);

	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		RemoveFlash(missile);
	}
	PutMissile(missile);
}

void ProcessFlashTop(Missile &missile)
{
	missile._ticksUntilExpiry--;

	constexpr Direction Offsets[] = {
		Direction::North,
		Direction::NorthEast,
		Direction::East
	};
	for (Direction offset : Offsets)
		CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile + offset, true);

	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
	}
	PutMissile(missile);
}

void ProcessFlameWave(Missile &missile)
{
	constexpr int ExpLight[14] = { 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 12 };

	// Adjust missile's position for processing
	missile.position.tile += Direction::North;
	missile.position.offset.deltaY += 32;

	missile.var1++;
	if (missile.var1 == missile._miAnimLen) {
		SetMissDir(missile, 1);
		missile._miAnimFrame = GenerateRnd(11) + 1;
	}
	int j = missile._ticksUntilExpiry;
	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, false);
	if (missile._miHitFlag)
		missile._ticksUntilExpiry = j;
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile._mimfnum != 0 || missile._ticksUntilExpiry == 0) {
		if (missile.position.tile != Point { missile.var3, missile.var4 }) {
			missile.var3 = missile.position.tile.x;
			missile.var4 = missile.position.tile.y;
			ChangeLight(missile._mlid, missile.position.tile, 8);
		}
	} else {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, ExpLight[0]);
		ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var2]);
		missile.var2++;
	}
	// Adjust missile's position for rendering
	missile.position.tile += Direction::South;
	missile.position.offset.deltaY -= 32;
	PutMissile(missile);
}

static bool GuardianTryFireAt(Missile &missile, Point target)
{
	Point position = missile.position.tile;

	if (!LineClearMissile(position, target))
		return false;

	Player &caster = Players[missile._misource];
#if JWK_GUARDIAN_TARGETS_HOSTILE_PLAYERS // include hostile players (those with pvp flag on) in the list of targets to shoot at
	bool foundMonster = false;
	int mid = dMonster[target.x][target.y] - 1;
	if (mid < 0 || Monsters[mid].isPlayerMinion() || (Monsters[mid].hitPoints >> 6) <= 0) {
		// No monster to target at this tile.  Look for a target player instead.
		// if (caster.friendlyMode) { return false; } <-- Could uncomment this line to avoid targetting players if the caster is not flagged pvp
		int pid = dPlayer[target.x][target.y] - 1;
		if (pid < 0 || pid == missile._misource || Players[pid].friendlyMode || (Players[pid]._pHitPoints >> 6) <= 0)
			return false;
	}
#else // original code (only shoot at monsters but any player who steps in the firebolt path can still be hit if the caster is flagged pvp)
	int mid = dMonster[target.x][target.y] - 1;
	if (mid < 0)
		return false;
	const Monster &monster = Monsters[mid];
	if (monster.isPlayerMinion())
		return false;
	if (monster.hitPoints >> 6 <= 0)
		return false;
#endif

	Direction dir = GetDirection(position, target);
	int boltDamage = CalcGuardianDamage(caster, missile._mispllvl, GenerateRnd, GenerateRndSum);
	AddMissile(position, target, dir, MissileID::GuardianBolt, TARGET_MONSTERS, missile._misource, boltDamage, missile.sourcePlayer()->GetSpellLevel(SpellID::Guardian), &missile);
	SetMissDir(missile, 2);
	missile.var2 = 3;

	return true;
}

void ProcessGuardian(Missile &missile)
{
	missile._ticksUntilExpiry--;

	if (missile.var2 > 0) {
		missile.var2--;
	}
	if (missile._ticksUntilExpiry == missile.var1 || (missile._mimfnum == 2 && missile.var2 == 0)) {
		SetMissDir(missile, 1);
	}

	Point position = missile.position.tile;

	if ((missile._ticksUntilExpiry % 16) == 0) {
		// Guardians pick a target by working backwards along lines originally based on VisionCrawlTable.
		// Because of their rather unique behaviour the points checked have been unrolled here
		constexpr std::array<WorldTileDisplacement, 48> guardianArc {
			{
			    // clang-format off
			    { 6, 0 }, { 5, 0 }, { 4, 0 }, { 3, 0 }, { 2, 0 }, { 1, 0 },
			    { 6, 1 }, { 5, 1 }, { 4, 1 }, { 3, 1 },
			    { 6, 2 }, { 2, 1 },
			    { 5, 2 },
			    { 6, 3 }, { 4, 2 },
			    { 5, 3 }, { 3, 2 }, { 1, 1 },
			    { 6, 4 },
			    { 6, 5 }, { 5, 4 }, { 4, 3 }, { 2, 2 },
			    { 5, 5 }, { 4, 4 }, { 3, 3 },
			    { 6, 6 }, { 5, 6 }, { 4, 5 }, { 3, 4 }, { 2, 3 },
			    { 4, 6 }, { 3, 5 }, { 2, 4 }, { 1, 2 },
			    { 3, 6 }, { 2, 5 }, { 1, 3 }, { 0, 1 },
			    { 2, 6 }, { 1, 4 },
			    { 1, 5 },
			    { 1, 6 },
			    { 0, 2 },
			    { 0, 3 },
			    { 0, 6 }, { 0, 5 }, { 0, 4 },
			    // clang-format on
			}
		};
		for (WorldTileDisplacement offset : guardianArc) {
			if (GuardianTryFireAt(missile, position + offset)
			    || GuardianTryFireAt(missile, position + offset.flipXY())
			    || GuardianTryFireAt(missile, position + offset.flipY())
			    || GuardianTryFireAt(missile, position + offset.flipX()))
				break;
		}
	}

	if (missile._ticksUntilExpiry == 14) {
		SetMissDir(missile, 0);
		missile._miAnimFrame = 15;
		missile._miAnimAdd = -1;
	}

	missile.var3 += missile._miAnimAdd;

	if (missile.var3 > 15) {
		missile.var3 = 15;
	} else if (missile.var3 > 0) {
		ChangeLight(missile._mlid, position, missile.var3);
	}

	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}

	PutMissile(missile);
}

void AddGuardian(Missile &missile, AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];

	std::optional<Point> spawnPosition = FindClosestValidPosition(
	    [start = missile.position.start](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }
		    if (dMonster[target.x][target.y] != 0) {
			    return false;
		    }
		    if (IsObjectAtPosition(target)) {
			    return false;
		    }
		    if (TileContainsMissile(target)) {
			    return false;
		    }

		    int dp = dPiece[target.x][target.y];
		    if (TileHasAny(dp, TileProperties::Solid | TileProperties::BlockMissile)) {
			    return false;
		    }

		    return LineClearMissile(start, target);
	    },
	    parameter.dst, 0, 5);

	if (!spawnPosition) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	missile._miDelFlag = false;
	missile.position.tile = *spawnPosition;
	missile.position.start = *spawnPosition;

	missile._mlid = AddLight(missile.position.tile, 1);
	missile._ticksUntilExpiry = missile._mispllvl + (player._pLevel / 2); // duration of guardian spell

	if (missile._ticksUntilExpiry > 30)
		missile._ticksUntilExpiry = 30;
	missile._ticksUntilExpiry <<= 4;
	if (missile._ticksUntilExpiry < 30)
		missile._ticksUntilExpiry = 30;

	missile.var1 = missile._ticksUntilExpiry - missile._miAnimLen;
	missile.var3 = 1;
}

void ProcessChainLightning(Missile &missile)
{
	Point position = missile.position.tile;
	Point dst { missile.var1, missile.var2 };
	Direction dir = GetDirection(position, dst);
	AddMissile(position, dst, dir, MissileID::LightningControl, missile._micaster, missile._misource, missile._midam, missile._mispllvl, &missile); // fire a lightning bolt in the direction the player clicks
#if !JWK_EDIT_CHAIN_LIGHTNING // original code:
	int rad = std::min<int>(missile._mispllvl + 3, MaxCrawlRadius);
	Crawl(1, rad, [&](Displacement displacement) {
		Point target = position + displacement;
		if (InDungeonBounds(target) && dMonster[target.x][target.y] > 0) {
			// Fire a lightning bolt at every target without checking if the monster is in light of sight.  To fix this, we could check LineClearMissile(position, target))
			dir = GetDirection(position, target);
			AddMissile(position, target, dir, MissileID::LightningControl, missile._micaster, missile._misource, missile._midam, missile._mispllvl, &missile);
		}
		return false;
	});
#endif
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
}

void ProcessWeaponExplosion(Missile &missile)
{
	constexpr int ExpLight[10] = { 9, 10, 11, 12, 11, 10, 8, 6, 4, 2 };

	missile._ticksUntilExpiry--;
	const Player &player = Players[missile._misource];
	int mind;
	int maxd;
	DamageType damageType;
	if (missile.var2 == 1) {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		mind = player._pIFMinDam;
		maxd = player._pIFMaxDam;
		damageType = DamageType::Fire;
	} else {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		mind = player._pILMinDam;
		maxd = player._pILMaxDam;
		damageType = DamageType::Lightning;
	}
	CheckMissileCollision(missile, damageType, mind, maxd, false, missile.position.tile, false);
	if (missile.var1 == 0) {
		missile._mlid = AddLight(missile.position.tile, 9);
	} else {
		if (missile._ticksUntilExpiry != 0)
			ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var1]);
	}
	missile.var1++;
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	} else {
		PutMissile(missile);
	}
}

void ProcessMissileExplosion(Missile &missile)
{
	constexpr int ExpLight[] = { 9, 10, 11, 12, 11, 10, 8, 6, 4, 2, 1, 0, 0, 0, 0 };

	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	} else {
		if (missile.var1 == 0)
			missile._mlid = AddLight(missile.position.tile, 9);
		else
			ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var1]);
		missile.var1++;
		PutMissile(missile);
	}
}

void ProcessAcidSplat(Missile &missile)
{
	if (missile._ticksUntilExpiry == missile._miAnimLen) {
		missile.position.tile += Displacement { 1, 1 };
		missile.position.offset.deltaY -= 32;
	}
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		int monst = missile._misource;
		int dam = (Monsters[monst].data().level >= 2 ? 2 : 1);
		AddMissile(missile.position.tile, { 0, 0 }, Direction::South, MissileID::AcidPuddle, TARGET_PLAYERS, monst, dam, missile._mispllvl);
	} else {
		PutMissile(missile);
	}
}

void ProcessTeleport(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry <= 0) {
		missile._miDelFlag = true;
		return;
	}

	int id = missile._misource;
	Player &player = Players[id];

	std::optional<Point> teleportDestination = FindClosestValidPosition(
	    [&player](Point target) {
		    return PosOkPlayer(player, target);
	    },
	    missile.position.tile, 0, 5);

	if (!teleportDestination)
		return;

	dPlayer[player.position.tile.x][player.position.tile.y] = 0;
	PlrClrTrans(player.position.tile);
	player.position.tile = *teleportDestination;
	player.position.future = player.position.tile;
	player.position.old = player.position.tile;
	PlrDoTrans(player.position.tile);
	missile.var1 = 1;
	dPlayer[player.position.tile.x][player.position.tile.y] = id + 1;
	if (leveltype != DTYPE_TOWN) {
		ChangeVisionXY(player.getId(), player.position.tile, player.position.future);
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
		ChangePlayerLightXY(player, player.position.tile);
#else
		ChangeLightXY(player.lightId, player.position.tile);
#endif
	}
	if (&player == MyPlayer) {
		ViewPosition = player.position.tile;
	}
}

void RemoveStoneCurse(Missile &missile)
{
	Monster& monster = Monsters[missile.var2];
	if (monster.hitPoints > 0) {
		monster.mode = static_cast<MonsterMode>(missile.var1);
		monster.animInfo.isPetrified = false;
	} else {
		AddCorpse(monster.position.tile, stonendx, monster.direction);
	}
}

void ProcessStoneCurse(Missile &missile)
{
	missile._ticksUntilExpiry--;
	Monster& monster = Monsters[missile.var2];
	if (monster.hitPoints == 0 && missile._miAnimType != MissileGraphicID::StoneCurseShatter) {
		missile._mimfnum = 0;
		missile._miDrawFlag = true;
		SetMissAnim(missile, MissileGraphicID::StoneCurseShatter);
		missile._ticksUntilExpiry = 11;
	}
#if JWK_GOD_MODE_INFINITE_PETRIFY
	if (missile._miAnimType != MissileGraphicID::StoneCurseShatter)
	{
		missile._ticksUntilExpiry = std::max(missile._ticksUntilExpiry, 1); // don't allow timer to reach zero
	}
#endif
	if (monster.mode != MonsterMode::Petrified) {
		missile._miDelFlag = true;
		return;
	}

	if (missile._ticksUntilExpiry == 0) {
		RemoveStoneCurse(missile);
		missile._miDelFlag = true;
	}
	if (missile._miAnimType == MissileGraphicID::StoneCurseShatter)
		PutMissile(missile);
}

void ProcessRhino(Missile &missile)
{
	int monst = missile._misource;
	auto &monster = Monsters[monst];
	if (monster.mode != MonsterMode::Charge) {
		missile._miDelFlag = true;
		return;
	}
	UpdateMissilePos(missile);
	Point prevPos = missile.position.tile;
	Point newPosSnake;
	dMonster[prevPos.x][prevPos.y] = 0;
	if (monster.ai == MonsterAIID::Snake) {
		missile.position.traveled += missile.position.velocity * 2;
		UpdateMissilePos(missile);
		newPosSnake = missile.position.tile;
		missile.position.traveled -= missile.position.velocity;
	} else {
		missile.position.traveled += missile.position.velocity;
	}
	UpdateMissilePos(missile);
	Point newPos = missile.position.tile;
	if (!IsTileAvailable(monster, newPos) || (monster.ai == MonsterAIID::Snake && !IsTileAvailable(monster, newPosSnake))) {
		MissileBecomesMonster(missile, prevPos);
		missile._miDelFlag = true;
		return;
	}
	monster.position.future = newPos;
	monster.position.old = newPos;
	monster.position.tile = newPos;
	dMonster[newPos.x][newPos.y] = -(monst + 1);
	if (monster.isUnique())
		ChangeLightXY(missile._mlid, newPos);
	MoveMissilePos(missile);
	PutMissile(missile);
}

static bool CanPlaceWall(Point position)
{
	if (!InDungeonBounds(position))
		return false;

	return !TileHasAny(dPiece[position.x][position.y], TileProperties::BlockMissile);
}

static Missile *PlaceWall(int id, MissileID type, Point position, Direction direction, int spellLevel, int damage)
{
#if 1 // jwk - don't allow walls to stack more than once on the same tile
	if (type != MissileID::FlameWave && HasAnyOf(dFlags[position.x][position.y], DungeonFlag::MissileFireWall | DungeonFlag::MissileLightningWall))
		return nullptr;
#endif
	return AddMissile(position, position + direction, direction, type, TARGET_BOTH, id, damage, spellLevel);
}

static bool TryGrowWall(int id, MissileID type, Point position, Direction direction, int spellLevel, int damage)
{
	if (!CanPlaceWall(position))
		return false;

	PlaceWall(id, type, position, direction, spellLevel, damage);
	return true;
}

static std::optional<Point> MoveWallToNextPosition(Point position, Direction growDirection)
{
	Point nextPosition = position + growDirection;
	if (!InDungeonBounds(nextPosition))
		return std::nullopt;
	return nextPosition;
}

void ProcessWallControl(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		return;
	}

	MissileID type;
	int dmg = 0;
	Player* caster = missile.sourcePlayer();

	switch (missile._mitype) {
	case MissileID::FireWallControl:
		type = MissileID::FireWallSingleTile;
		if (caster) {
			dmg = CalcFireWallDamageShifted(*caster, missile._mispllvl, GenerateRnd, GenerateRndSum);
		}
		break;
	case MissileID::LightningWallControl:
		type = MissileID::LightningWallSingleTile;
		if (caster) {
			dmg = CalcLightningWallDamageShifted(*caster, missile._mispllvl, GenerateRnd, GenerateRndSum);
		}
		break;
	default:
		app_fatal("ProcessWallControl: Invalid missile type for control missile");
	}

	const Point leftPosition = { missile.var1, missile.var2 };
	const Point rightPosition = { missile.var5, missile.var6 };
	const Direction leftDirection = static_cast<Direction>(missile.var3);
	const Direction rightDirection = static_cast<Direction>(missile.var4);

	bool isStart = leftPosition == rightPosition;

	std::optional<Point> nextLeftPosition = std::nullopt;
	std::optional<Point> nextRightPosition = std::nullopt;

	if (isStart) {
		if (!CanPlaceWall(leftPosition)) {
			missile._miDelFlag = true;
			return;
		}
		PlaceWall(missile._misource, type, leftPosition, leftDirection, missile._mispllvl, dmg);
		nextLeftPosition = MoveWallToNextPosition(leftPosition, leftDirection);
		nextRightPosition = MoveWallToNextPosition(rightPosition, rightDirection);
	} else {
		if (!missile.limitReached && TryGrowWall(missile._misource, type, leftPosition, Direction::South, missile._mispllvl, dmg)) {
			nextLeftPosition = MoveWallToNextPosition(leftPosition, leftDirection);
		}
		if (missile.var7 == 0 && TryGrowWall(missile._misource, type, rightPosition, Direction::South, missile._mispllvl, dmg)) {
			nextRightPosition = MoveWallToNextPosition(rightPosition, rightDirection);
		}
	}

	if (nextLeftPosition) {
		missile.var1 = nextLeftPosition->x;
		missile.var2 = nextLeftPosition->y;
	} else {
		missile.limitReached = true;
	}
	if (nextRightPosition) {
		missile.var5 = nextRightPosition->x;
		missile.var6 = nextRightPosition->y;
	} else {
		missile.var7 = 1; // use var7 as 'limitReached' in the right direction
	}
}

void ProcessFlameWaveControl(Missile &missile)
{
	const int id = missile._misource;
	const Direction pdir = Players[id]._pdir;
	const Point src = missile.position.tile;
	const Direction sd = GetDirection(src, { missile.var1, missile.var2 });
	const Point start = src + sd;
	if (CanPlaceWall(start)) {
		PlaceWall(id, MissileID::FlameWave, start, pdir, missile._mispllvl, 0);
		// original code: int segmentsToAdd = (missile._mispllvl / 2) + 2;
		int segmentsToAdd = 2; // totalWidth is wallWidth*2 + 1
		Point left = start;
		const Direction dirLeft = Left(Left(sd));
		for (int j = 0; j < segmentsToAdd; j++) {
			left += dirLeft;
			if (!TryGrowWall(id, MissileID::FlameWave, left, pdir, missile._mispllvl, 0))
				break;
		}
		Point right = start;
		const Direction dirRight = Right(Right(sd));
		for (int j = 0; j < segmentsToAdd; j++) {
			right += dirRight;
			if (!TryGrowWall(id, MissileID::FlameWave, right, pdir, missile._mispllvl, 0))
				break;
		}
	}

	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
}


void RemoveRage(Missile &missile)
{
	Player &player = Players[missile._misource];
	player._pSpellFlags &= ~SpellFlag::RageActive;
	player._pSpellFlags &= ~SpellFlag::RageCooldown;

	// The player isn't necessarily leaving the game.  They could just be leaving the zone.
	// We want to restore their stats but we don't want to apply damage if they leave the zone.
	CalcPlayerPowerFromItems(player, true);
}

void ProcessRage(Missile &missile)
{
	missile._ticksUntilExpiry--;

	if (missile._ticksUntilExpiry != 0) {
		return;
	}

	Player &player = Players[missile._misource];
	int hpdif = player._pMaxHP - player._pHitPoints;

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive)) {
		player._pSpellFlags &= ~SpellFlag::RageActive;
		player._pSpellFlags |= SpellFlag::RageCooldown;
		int lvl = player._pLevel * 2;
		missile._ticksUntilExpiry = lvl + 10 * missile._mispllvl + 245;
	} else {
		player._pSpellFlags &= ~SpellFlag::RageCooldown;
		missile._miDelFlag = true;
		hpdif += missile.var2;
	}

	CalcPlayerPowerFromItems(player, true);
	ApplyPlrDamage(DamageType::Physical, player, 0, 1, hpdif, 100, DeathReason::MonsterOrTrap);
	RedrawEverything();
	player.Say(HeroSpeech::HeavyBreathing);
}

void ProcessInferno(Missile &missile)
{
	missile._ticksUntilExpiry--;
	missile.var2--;
	int k = missile._ticksUntilExpiry;
	CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._ticksUntilExpiry == 0 && missile._miHitFlag)
		missile._ticksUntilExpiry = k;
	if (missile.var2 == 0)
		missile._miAnimFrame = 20;
	if (missile.var2 <= 0) {
		k = missile._miAnimFrame;
		if (k > 11)
			k = 24 - k;
		ChangeLight(missile._mlid, missile.position.tile, k);
	}
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile.var2 <= 0)
		PutMissile(missile);
}

void ProcessInfernoControl(Missile &missile)
{
	missile._ticksUntilExpiry--;
	missile.position.traveled += missile.position.velocity;
	UpdateMissilePos(missile);
	if (missile.position.tile != Point { missile.var1, missile.var2 }) {
		int id = dPiece[missile.position.tile.x][missile.position.tile.y];
		if (!TileHasAny(id, TileProperties::BlockMissile)) {
			AddMissile(
			    missile.position.tile,
			    missile.position.start,
			    Direction::South,
			    MissileID::Inferno,
			    missile._micaster,
			    missile._misource,
			    missile.var3, // damage factor increases from 0,1,2,3
			    missile._mispllvl,
			    &missile);
		} else {
			missile._ticksUntilExpiry = 0;
		}
		missile.var1 = missile.position.tile.x;
		missile.var2 = missile.position.tile.y;
		missile.var3++;
	}
	if (missile._ticksUntilExpiry == 0 || missile.var3 == 3)
		missile._miDelFlag = true;
}

void ProcessHolyBolt(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._miAnimType != MissileGraphicID::HolyBoltExplosion) {
		int dam = missile._midam;
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), dam, dam, true, true);
		if (missile._ticksUntilExpiry == 0) {
			missile._mimfnum = 0;
			SetMissAnim(missile, MissileGraphicID::HolyBoltExplosion);
			missile._ticksUntilExpiry = missile._miAnimLen - 1;
			missile.position.StopMissile();
		} else {
			if (missile.position.tile != Point { missile.var1, missile.var2 }) {
				missile.var1 = missile.position.tile.x;
				missile.var2 = missile.position.tile.y;
				ChangeLight(missile._mlid, missile.position.tile, 8);
			}
		}
	} else {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame + 7);
		if (missile._ticksUntilExpiry == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	}
	PutMissile(missile);
}

void ProcessElemental(Missile &missile)
{
#if JWK_EDIT_ELEMENTAL // use smaller explosion animation to indicate it's not AoE
	MissileGraphicID explosionType = MissileGraphicID::MagmaBallExplosion;
#else // original code
	MissileGraphicID explosionType = MissileGraphicID::BigExplosion;
#endif
	missile._ticksUntilExpiry--;
	int dam = missile._midam;
	const Point missilePosition = missile.position.tile;
	if (missile._miAnimType == explosionType) {
#if !JWK_EDIT_ELEMENTAL // remove the AoE explosion from Elemental.  Also, I believe the original code adds the AoE explosion at the wrong time here:
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);

		Point startPoint = missile.var3 == 2 ? Point { missile.var4, missile.var5 } : Point(missile.position.start);
		constexpr Direction Offsets[] = {
			Direction::NoDirection,
			Direction::SouthWest,
			Direction::NorthEast,
			Direction::SouthEast,
			Direction::East,
			Direction::South,
			Direction::NorthWest,
			Direction::West,
			Direction::North
		};
		for (Direction offset : Offsets) {
			if (!IsDirectPathBlocked(startPoint, missilePosition + offset))
				CheckMissileCollision(missile, GetMissileData(missile._mitype).damageType(), dam, dam, true, missilePosition + offset, true);
		}
#endif
		if (missile._ticksUntilExpiry == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	} else {
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), dam, dam, false, false);
		if (missile.var3 == 0 && missilePosition == Point { missile.var4, missile.var5 })
			missile.var3 = 1;
		if (missile.var3 == 1) {
			missile.var3 = 2;
			missile._ticksUntilExpiry = 255;
			int searchRadius = JWK_EDIT_ELEMENTAL ? 10 : 19; // otherwise elemental runs across the whole screen to target something behind you
			auto *nextMonster = FindClosestMonsterInSight(missilePosition, searchRadius);
			if (nextMonster != nullptr) {
				Direction sd = GetDirection(missilePosition, nextMonster->position.tile);
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, nextMonster->position.tile, 16);
			} else {
				Direction sd = Players[missile._misource]._pdir;
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, missilePosition + sd, 16);
			}
		}
		if (missilePosition != Point { missile.var1, missile.var2 }) {
			missile.var1 = missilePosition.x;
			missile.var2 = missilePosition.y;
			ChangeLight(missile._mlid, missilePosition, 8);
		}
		if (missile._ticksUntilExpiry == 0) {
			missile._mimfnum = 0;
			SetMissAnim(missile, explosionType);
			missile._ticksUntilExpiry = missile._miAnimLen - 1;
			missile.position.StopMissile();
		}
	}
	PutMissile(missile);
}

void ProcessBoneSpirit(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._mimfnum == 8) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);
		if (missile._ticksUntilExpiry == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
		PutMissile(missile);
	} else {
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), 0, 0, false, false);
		Point c = missile.position.tile;
		if (missile.var3 == 0 && c == Point { missile.var4, missile.var5 })
			missile.var3 = 1;
		if (missile.var3 == 1) {
			missile.var3 = 2;
			missile._ticksUntilExpiry = 255;

			bool foundTarget = false;
			int searchRadius = 19;
			if (missile._micaster == TARGET_PLAYERS) {
				Player* player = FindClosestPlayerInSight(c, searchRadius);
				if (player != nullptr) {
					SetMissDir(missile, GetDirection(c, player->position.tile));
					UpdateMissileVelocity(missile, player->position.tile, 16);
				}
			} else {
				Monster* monster = FindClosestMonsterInSight(c, searchRadius);
				if (monster != nullptr) {
					SetMissDir(missile, GetDirection(c, monster->position.tile));
					UpdateMissileVelocity(missile, monster->position.tile, 16);
				}
			}
			if (!foundTarget) {
				Direction dir;
				if (missile._misource > 0) {
					dir = Players[missile._misource]._pdir;
					SetMissDir(missile, dir);
					UpdateMissileVelocity(missile, c + dir, 16);
				} else {
					//dir = GetDirection(missile.position.start, c);
					//SetMissDir(missile, dir);
					//UpdateMissileVelocity(missile, c + dir, 16);
				}
			}
		}
		if (c != Point { missile.var1, missile.var2 }) {
			missile.var1 = c.x;
			missile.var2 = c.y;
			ChangeLight(missile._mlid, c, 8);
		}
		if (missile._ticksUntilExpiry == 0) {
			SetMissDir(missile, 8);
			missile.position.velocity = {};
			missile._ticksUntilExpiry = 7;
		}
		PutMissile(missile);
	}
}

void ProcessResurrectBeam(Missile &missile)
{
	missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessRedPortal(Missile &missile)
{
	int expLight[17] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

	if (missile._ticksUntilExpiry > 1)
		missile._ticksUntilExpiry--;
	if (missile._ticksUntilExpiry == missile.var1)
		SetMissDir(missile, 1);

	if (leveltype != DTYPE_TOWN && missile._mimfnum != 1 && missile._ticksUntilExpiry != 0) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, 1);
		ChangeLight(missile._mlid, missile.position.tile, expLight[missile.var2]);
		missile.var2++;
	}
	if (missile._ticksUntilExpiry == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void RemoveAllMissilesForPlayer(const Player& player)
{
	for (Missile &missile : Missiles) {
		if (missile.sourcePlayer() == &player) {
			const MissileData &missileData = GetMissileData(missile._mitype);
			if (missileData.mRemoveProc != nullptr) {
				missileData.mRemoveProc(missile);
			}
			AddUnLight(missile._mlid);
			missile._miDelFlag = true;
		}
#if JWK_EDIT_APOCALYPSE // remove any apocalypse boom's that are locked onto this player
		if (missile._mitype == MissileID::ApocalypseBoom && missile.var7 == player.getId()) {
			missile.var7 = -1;
		}
#endif
	}
}

static void DeleteMissiles()
{
	Missiles.remove_if([](Missile &missile) { return missile._miDelFlag; });
}

void ProcessManaShield()
{
	Player &myPlayer = *MyPlayer;
	if (myPlayer.pManaShield && myPlayer._pMana <= 0) {
		myPlayer.pManaShield = false;
		NetSendCmd(true, CMD_REMSHIELD);
	}
}

void ProcessMissiles()
{
	for (auto &missile : Missiles) {
		const auto &position = missile.position.tile;
		if (InDungeonBounds(position)) {
			// clear flags in advance of reassigning all the flags
			dFlags[position.x][position.y] &= ~(DungeonFlag::Missile | DungeonFlag::MissileFireWall | DungeonFlag::MissileLightningWall);
		} else {
			missile._miDelFlag = true;
		}
	}

	DeleteMissiles();

	MissilePreFlag = false;

	for (auto &missile : Missiles) {
		const MissileData &missileData = GetMissileData(missile._mitype);
		if (missileData.mProc != nullptr)
			missileData.mProc(missile);
		if (missile._miAnimFlags == MissileGraphicsFlags::NotAnimated)
			continue;

		missile._miAnimCnt++;
		if (missile._miAnimCnt < missile._miAnimDelay)
			continue;

		missile._miAnimCnt = 0;
		missile._miAnimFrame += missile._miAnimAdd;
		if (missile._miAnimFrame > missile._miAnimLen)
			missile._miAnimFrame = 1;
		else if (missile._miAnimFrame < 1)
			missile._miAnimFrame = missile._miAnimLen;
	}

	ProcessManaShield();
	DeleteMissiles();
}

void missiles_process_charge()
{
	for (auto &missile : Missiles) {
		missile._miAnimData = GetMissileSpriteData(missile._miAnimType).spritesForDirection(missile._mimfnum);
		if (missile._mitype != MissileID::Rhino)
			continue;

		const CMonster &mon = Monsters[missile._misource].type();

		MonsterGraphic graphic;
		if (IsAnyOf(mon.type, MT_HORNED, MT_MUDRUN, MT_FROSTC, MT_OBLORD)) {
			graphic = MonsterGraphic::Special;
		} else if (IsAnyOf(mon.type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE)) {
			graphic = MonsterGraphic::Attack;
		} else {
			graphic = MonsterGraphic::Walk;
		}
		missile._miAnimData = mon.getAnimData(graphic).spritesForDirection(static_cast<Direction>(missile._mimfnum));
	}
}

void RedoMissileFlags()
{
	for (auto &missile : Missiles) {
		PutMissile(missile);
	}
}

int CalcHealOtherAmount(const Player &caster, const Player &target, int spellLevel)
{
	return CalcHealingAmount(caster, target, spellLevel, GenerateRnd, GenerateRndSum);
}

} // namespace devilution
