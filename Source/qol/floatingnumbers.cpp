#include "floatingnumbers.h"

#include <cstdint>
#include <ctime>
#include <deque>
#include <fmt/format.h>
#include <string>

#include "engine/render/text_render.hpp"
#include "options.h"
#include "diablo.h" // for gnTotalGameLogicStepsExecuted
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr int DelayAfterFinalMerge = 2000; // measured in milliseconds.  Delay before removing the final merged damage number from the screen.

constexpr bool gPrintHitChance = true;

constexpr bool gDebugAttackRate = false;
static uint32_t gMostRecentPlayerAttack = 0; // used for gDebugAttackRate

struct FloatingNumber {
	FloatingNumber(Point inStartPos, Displacement inStartOffset, Displacement inEndOffset, uint32_t inExpiryTime, uint32_t inMostRecentMergeTick, uint32_t inLastMergeTickAllowed, UiFlags inStyle, DamageType inType, int inValue, int inIndex, int inHitChance, bool inNumberFloatsDown) {
		startPos = inStartPos;
		startOffset = inStartOffset;
		endOffset = inEndOffset;
		expiryTime = inExpiryTime;
		mostRecentMergeTick = inMostRecentMergeTick;
		lastMergeTickAllowed = inLastMergeTickAllowed;
		style = inStyle;
		type = inType;
		value = inValue;
		index = inIndex;
		hitChance = inHitChance;
		numberFloatsDown = inNumberFloatsDown;
	}

	Point startPos;
	Displacement startOffset;
	Displacement endOffset;
	std::string text;
	uint32_t expiryTime;
	uint32_t mostRecentMergeTick;
	uint32_t lastMergeTickAllowed;
	UiFlags style;
	DamageType type;
	int value;
	int index;
	int hitChance; // allows player to see the hit chance calculation in real time (useful for debugging)
	bool numberFloatsDown;
};

std::deque<FloatingNumber> FloatingQueue;

void ClearExpiredNumbers()
{
	while (!FloatingQueue.empty()) {
		FloatingNumber &num = FloatingQueue.front();
		if (num.expiryTime > SDL_GetTicks())
			break;

		FloatingQueue.pop_front();
	}
}

GameFontTables GetGameFontSizeByDamage(int value)
{
	value >>= 6;
	if (value >= 300)
		return GameFont30;
	if (value >= 100)
		return GameFont24;
	return GameFont12;
}

UiFlags GetFontSizeByDamage(int value)
{
	value >>= 6;
	if (value >= 300)
		return UiFlags::FontSize30;
	if (value >= 100)
		return UiFlags::FontSize24;
	return UiFlags::FontSize12;
}

void UpdateFloatingText(FloatingNumber &num)
{
	if (num.value > 0 && num.value < 64) {
		//num.text = fmt::format("{:.2f}", num.value / 64.0);
		num.text = ""; // don't show damage numbers less than 1
	} else {
		if (gPrintHitChance && num.hitChance > 0 && num.hitChance < 100) {
			// could use UiFlags::ColorGold
			num.text = StrCat(num.value >> 6, " (", num.hitChance, "%)");
		} else {
			num.text = StrCat(num.value >> 6);
		}
		if (gDebugAttackRate && !num.numberFloatsDown) {
			int ticksBetweenAttacks = num.mostRecentMergeTick - gMostRecentPlayerAttack;
			gMostRecentPlayerAttack = num.mostRecentMergeTick;
			num.text = StrCat(num.text, " (", ticksBetweenAttacks, " ticks)");
		}
	}

	num.style &= ~(UiFlags::FontSize12 | UiFlags::FontSize24 | UiFlags::FontSize30);
	num.style |= GetFontSizeByDamage(num.value);

	// ColorDialogWhite is tan
	// ColorUiGoldDark is dark red, looks kinda bad
	// ColorBlack is super black, hard to read because outline is also black
	// ColorGold and ColorWhitegold are the same

	switch (num.type) {
	case DamageType::Physical:
		num.style |= UiFlags::ColorWhite;//::ColorGold;
		break;
	case DamageType::Fire:
		num.style |= UiFlags::ColorUiSilver; // UiSilver appears dark red in game
		break;
	case DamageType::Lightning:
		num.style |= UiFlags::ColorBlue;
		break;
	case DamageType::Magic:
	case DamageType::Acid:
		num.style |= UiFlags::ColorYellow;//::ColorOrange;
		break;
	}
}

void AddFloatingNumber(Point pos, Displacement offset, DamageType type, int value, int index, bool numberFloatsDown, int hitChance)
{
	// 45 deg angles to avoid jitter caused by px alignment
	Displacement goodAngles[] = {
		{ 0, -140 },
		{ 100, -100 },
		{ -100, -100 },
	};

	Displacement endOffset;
	if (*sgOptions.Gameplay.enableFloatingNumbers == FloatingNumbers::Random) {
		endOffset = goodAngles[rand() % 3];
	} else if (*sgOptions.Gameplay.enableFloatingNumbers == FloatingNumbers::Vertical) {
		endOffset = goodAngles[0];
	}

	Uint32 nowTicks = SDL_GetTicks(); // This isn't the game tick.  It's a real-time tick that doesn't pause during debugger breaks

	if (numberFloatsDown) {
		endOffset = -endOffset;
	}

	if (!gDebugAttackRate) {
		for (auto &num : FloatingQueue) {
			//if (num.numberFloatsDown == numberFloatsDown && num.type == type && num.index == index && (nowTicks - num.lastMergeTime) <= 100) {
			if (num.numberFloatsDown == numberFloatsDown && num.type == type && num.hitChance == hitChance && num.index == index && (gnTotalGameLogicStepsExecuted - num.mostRecentMergeTick) <= 1 && gnTotalGameLogicStepsExecuted < num.lastMergeTickAllowed) {
				num.value += value;
				//num.lastMergeTick = nowTicks;
				num.mostRecentMergeTick = gnTotalGameLogicStepsExecuted;
				UpdateFloatingText(num);
				return;
			}
		}
	}
	int TextDuration = MaxMergeTicksForDamageNumbers * gnTickDelay + DelayAfterFinalMerge;
	FloatingNumber num(pos, offset, endOffset, nowTicks + TextDuration, gnTotalGameLogicStepsExecuted, gnTotalGameLogicStepsExecuted + MaxMergeTicksForDamageNumbers, UiFlags::Outlined, type, value, index, hitChance, numberFloatsDown);
	UpdateFloatingText(num);
	FloatingQueue.push_back(num);
}

} // namespace

void AddFloatingNumber(DamageType damageType, const Monster &monster, int damage, int hitChance)
{
	if (*sgOptions.Gameplay.enableFloatingNumbers == FloatingNumbers::Off)
		return;

	Displacement offset = {};
	if (monster.isWalking()) {
		offset = GetOffsetForWalking(monster.animInfo, monster.direction);
		if (monster.mode == MonsterMode::MoveSideways) {
			if (monster.direction == Direction::West)
				offset -= Displacement { 64, 0 };
			else
				offset += Displacement { 64, 0 };
		}
	}
	if (monster.animInfo.sprites) {
		const ClxSprite sprite = monster.animInfo.currentSprite();
		offset.deltaY -= sprite.height() / 2;
	}

	AddFloatingNumber(monster.position.tile, offset, damageType, damage, monster.getId(), monster.isPlayerMinion(), hitChance);
}

void AddFloatingNumber(DamageType damageType, const Player &player, int damage, int hitChance)
{
	if (*sgOptions.Gameplay.enableFloatingNumbers == FloatingNumbers::Off)
		return;

	Displacement offset = {};
	if (player.isWalking()) {
		offset = GetOffsetForWalking(player.AnimInfo, player._pdir);
		if (player._pmode == PM_WALK_SIDEWAYS) {
			if (player._pdir == Direction::West)
				offset -= Displacement { 64, 0 };
			else
				offset += Displacement { 64, 0 };
		}
	}

	AddFloatingNumber(player.position.tile, offset, damageType, damage, player.getId(), true, hitChance);
}

void DrawFloatingNumbers(const Surface &out, Point viewPosition, Displacement offset)
{
	if (*sgOptions.Gameplay.enableFloatingNumbers == FloatingNumbers::Off)
		return;

	for (auto &floatingNum : FloatingQueue) {
		Displacement worldOffset = viewPosition - floatingNum.startPos;
		worldOffset = worldOffset.worldToScreen() + offset + Displacement { TILE_WIDTH / 2, -TILE_HEIGHT / 2 } + floatingNum.startOffset;

		if (*sgOptions.Graphics.zoom) {
			worldOffset *= 2;
		}

		Point screenPosition { worldOffset.deltaX, worldOffset.deltaY };

		int lineWidth = GetLineWidth(floatingNum.text, GetGameFontSizeByDamage(floatingNum.value));
		screenPosition.x -= lineWidth / 2;
		uint32_t timeLeft = floatingNum.expiryTime - SDL_GetTicks();
		int TextDuration = MaxMergeTicksForDamageNumbers * gnTickDelay + DelayAfterFinalMerge;
		float mul = 1 - (timeLeft / (float)TextDuration);
		screenPosition += floatingNum.endOffset * mul;

		DrawString(out, floatingNum.text, Rectangle { screenPosition, { lineWidth, 0 } }, { floatingNum.style });
	}

	ClearExpiredNumbers();
}

void ClearFloatingNumbers()
{
	srand(time(nullptr));

	FloatingQueue.clear();
}

} // namespace devilution
