/**
 * @file misdat.h
 *
 * Interface of data related to missiles.
 */
#pragma once

#include <cstdint>
#include <type_traits>
#include <vector>

#include "effects.h"
#include "engine.h"
#include "engine/clx_sprite.hpp"
#include "spelldat.h"
#include "utils/enum_traits.h"
#include "utils/stdcompat/cstddef.hpp"
#include "utils/stdcompat/string_view.hpp"

#define JWK_PREVENT_DUPLICATE_MISSILE_HITS 1 // prevent multiple hits per tick from the same spell effect.  This makes damage more consistent instead of randomly doing double damage depending on micro-differences in character position.

namespace devilution {

enum mienemy_type : uint8_t {
	TARGET_MONSTERS,
	TARGET_PLAYERS,
	TARGET_BOTH,
};

enum class DamageType : uint8_t {
	Physical = 0,
	Fire = 1,
	Lightning = 2,
	Magic = 3,
	Acid = 4,
};

enum class MissileGraphicID : uint8_t {
	Arrow,
	Fireball,
	Guardian,
	Lightning,
	FireWall,
	MagmaBallExplosion,
	TownPortal,
	FlashBottom,
	FlashTop,
	ManaShield,
	BloodHit,
	BoneHit,
	MetalHit,
	FireArrow,
	DoomSerpents,
	Golem,
	Spurt,
	ApocalypseBoom,
	StoneCurseShatter,
	BigExplosion,
	Inferno,
	ThinLightning,
	BloodStar,
	BloodStarExplosion,
	MagmaBall,
	Krull,
	ChargedBolt,
	HolyBolt,
	HolyBoltExplosion,
	LightningArrow,
	FireArrowExplosion,
	Acid,
	AcidSplat,
	AcidPuddle,
	Etherealize,
	Elemental,
	Resurrect,
	BoneSpirit,
	RedPortal,
	DiabloApocalypseBoom,
	BloodStarBlue,
	BloodStarBlueExplosion,
	BloodStarYellow,
	BloodStarYellowExplosion,
	BloodStarRed,
	BloodStarRedExplosion,
	HorkSpawn,
	Reflect,
	OrangeFlare,
	BlueFlare,
	RedFlare,
	YellowFlare,
	Rune,
	YellowFlareExplosion,
	BlueFlareExplosion,
	RedFlareExplosion,
	BlueFlare2,
	OrangeFlareExplosion,
	BlueFlareExplosion2,
	None,
};

/**
 * @brief Specifies what if and how movement distribution is applied
 */
enum class MissileMovementDistribution : uint8_t {
	/**
	 * @brief No movement distribution is calculated. Normally this means the missile doesn't move at all.
	 */
	Disabled,
	/**
	 * @brief The missile moves and if it hits a enemey it stops (for example firebolt)
	 */
	Blockable,
	/**
	 * @brief The missile moves and even it hits a enemy it keeps moving (for example flame wave)
	 */
	Unblockable,
};

struct Missile;
struct AddMissileParameter;

enum class MissileDataFlags : uint8_t {
	// The lower 3 bytes are used to store DamageType.
	Physical = static_cast<uint8_t>(DamageType::Physical),
	Fire = static_cast<uint8_t>(DamageType::Fire),
	Lightning = static_cast<uint8_t>(DamageType::Lightning),
	Magic = static_cast<uint8_t>(DamageType::Magic),
	Acid = static_cast<uint8_t>(DamageType::Acid),
	Arrow = 1 << 4,
	Invisible = 1 << 5,
};
use_enum_as_flags(MissileDataFlags);

struct MissileData {
	void (*mAddProc)(Missile &, AddMissileParameter &);
	void (*mProc)(Missile &);
	void (*mRemoveProc)(Missile &);
	_sfx_id mlSFX;
	_sfx_id miSFX;
	MissileGraphicID mFileNum;
	MissileDataFlags flags;
	MissileMovementDistribution movementDistribution;

	[[nodiscard]] bool isDrawn() const
	{
		return !HasAnyOf(flags, MissileDataFlags::Invisible);
	}

	[[nodiscard]] bool isArrow() const
	{
		return HasAnyOf(flags, MissileDataFlags::Arrow);
	}

	[[nodiscard]] DamageType damageType() const
	{
		return static_cast<DamageType>(static_cast<std::underlying_type<MissileDataFlags>::type>(flags) & 0b111U);
	}
};

enum class MissileGraphicsFlags : uint8_t {
	// clang-format off
	None         = 0,
	MonsterOwned = 1 << 0,
	NotAnimated  = 1 << 1,
	// clang-format on
};

struct MissileFileData {
	OptionalOwnedClxSpriteListOrSheet sprites;
	uint16_t animWidth;
	int8_t animWidth2;
	char name[9];
	uint8_t animFAmt;
	MissileGraphicsFlags flags;
	uint8_t animDelayIdx;
	uint8_t animLenIdx;

	[[nodiscard]] uint8_t animDelay(uint8_t dir) const;
	[[nodiscard]] uint8_t animLen(uint8_t dir) const;

	void LoadGFX();

	void FreeGFX()
	{
		sprites = std::nullopt;
	}

	/**
	 * @brief Returns the sprite list for a given direction.
	 *
	 * @param direction One of the 16 directions. Valid range: [0, 15].
	 * @return OptionalClxSpriteList
	 */
	[[nodiscard]] OptionalClxSpriteList spritesForDirection(size_t direction) const
	{
		if (!sprites)
			return std::nullopt;
		return sprites->isSheet() ? sprites->sheet()[direction] : sprites->list();
	}
};

extern const MissileData MissilesData[];

inline const MissileData &GetMissileData(MissileID missileId)
{
	return MissilesData[static_cast<std::underlying_type<MissileID>::type>(missileId)];
}

extern MissileFileData MissileSpriteData[];

inline MissileFileData &GetMissileSpriteData(MissileGraphicID graphicId)
{
	return MissileSpriteData[static_cast<std::underlying_type<MissileGraphicID>::type>(graphicId)];
}

void InitMissileGFX(bool loadHellfireGraphics = false);
void FreeMissileGFX();

#if JWK_PREVENT_DUPLICATE_MISSILE_HITS
extern uint32_t gnTotalGameLogicStepsExecuted;
// Note: For the purpose of estimating required array sizes, spells like chain lightning are considered one missile group even though it contains many missiles.
struct MissileGroupList {
	// Array size should be the max number of spells (missile groups) a player could take damage from on a single tick.
	std::array<uint16_t, 16> entries;
	int numEntries = 0;
	void AddEntry(uint16_t missileGroupID) {
		if (numEntries < entries.size()) {
			entries[numEntries++] = missileGroupID;
		}
	}
	bool DoesEntryExist(uint16_t missileGroupID) {
		for (int i = 0; i < numEntries; i++) {
			if (missileGroupID == entries[i]) {
				return true; 
			}
		}
		return false;
	}
};
struct MissileGroupRingBuffer {
	// Array size should be the max number of spells (missile groups) a player could be hit with before these spells disappear from the world.
	std::array<std::pair<uint32_t, uint32_t>, 32> entries;
	int numEntries = 0;
	int oldestEntry = 0;
	void RemoveExpiredEntries() {
		while (numEntries > 0) {
			if (entries[oldestEntry].second < gnTotalGameLogicStepsExecuted) {
				oldestEntry = (oldestEntry + 1) % entries.size();
				numEntries--;
			} else {
				break;
			}
		}
	}
	void AddEntry(uint16_t missileGroupID) {
		if (numEntries < entries.size()) {
			uint32_t expiryTick = gnTotalGameLogicStepsExecuted + 100; // 20 ticks is 1 second at normal game speed.  All non-damage-over-time spells should be completed after 5 seconds
			int index = (oldestEntry + numEntries) % entries.size();
			entries[index] = std::pair<uint32_t, uint32_t>(missileGroupID, expiryTick);
			numEntries++;
		}
	}
	bool DoesEntryExist(uint16_t missileGroupID) {
		for (int i = 0; i < numEntries; i++) {
			int index = (oldestEntry + i) % entries.size();
			if (missileGroupID == entries[index].first) {
				return true; 
			}
		}
		return false;
	}
};
#endif // JWK_PREVENT_DUPLICATE_MISSILE_HITS

} // namespace devilution
