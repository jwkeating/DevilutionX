/**
 * @file lighting.h
 *
 * Interface of light and vision.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include <function_ref.hpp>

#include "player.h"
#include "automap.h"
#include "engine.h"
#include "engine/point.hpp"
#include "utils/attributes.h"

namespace devilution {

#define MAXLIGHTS 32
/** @brief Number of supported light levels */
constexpr size_t NumLightingLevels = 16;
#define NO_LIGHT -1

struct LightPosition {
	WorldTilePosition tile;
	/** Pixel offset from tile. */
	DisplacementOf<int8_t> offset;
	/** Previous position. */
	WorldTilePosition old;
};

struct Light {
	LightPosition position;
	uint8_t radius;
	uint8_t oldRadius;
	bool isInvalid;
	bool hasChanged;
};

extern std::array<Light, MAX_PLAYERS> VisionList;
extern std::array<bool, MAX_PLAYERS> VisionActive;
extern std::array<Light, MAXLIGHTS> Lights;
extern std::array<uint8_t, MAXLIGHTS> ActiveLights;
extern int ActiveLightCount;
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
extern std::array<Light, MAX_PLAYERS> PlayerLights;
#endif
constexpr char LightsMax = 15;
extern std::array<std::array<uint8_t, 256>, NumLightingLevels> LightTables;
extern std::array<uint8_t, 256> InfravisionTable;
extern std::array<uint8_t, 256> StoneTable;
extern std::array<uint8_t, 256> PauseTable;
#if defined(_DEBUG) || JWK_ALLOW_DEBUG_COMMANDS_IN_RELEASE
extern bool DisableLighting;
#endif
extern bool UpdateLighting;
extern bool UpdateVision;

void DoUnLight(Point position, uint8_t radius);
void DoLighting(Point position, uint8_t radius, DisplacementOf<int8_t> eighthOffset);
void DoUnVision(Point position, uint8_t radius);
void DoVision(Point position, uint8_t radius, MapExplorationType doAutomap, bool visibleToLocalPlayer);
void MakeLightTable();
#if defined(_DEBUG) || JWK_ALLOW_DEBUG_COMMANDS_IN_RELEASE
void ToggleLighting();
#endif
void InitLighting();
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
void AddPlayerLight(const Player& player, Point position, uint8_t radius);
void AddPlayerUnLight(const Player& player);
//void ChangePlayerLightRadius(const Player& player, uint8_t radius);
void ChangePlayerLightXY(const Player& player, Point position);
void ChangePlayerLightOffset(const Player& player, DisplacementOf<int8_t> offset);
void ChangePlayerLightFriendly(const Player& player);
#endif
int AddLight(Point position, uint8_t radius);
void AddUnLight(int i);
void ChangeLightRadius(int i, uint8_t radius);
void ChangeLightXY(int i, Point position);
void ChangeLightOffset(int i, DisplacementOf<int8_t> offset);
void ChangeLight(int i, Point position, uint8_t radius);
void ProcessLightList();
void SavePreLighting();
void ActivateVision(int playerId, Point position, int r);
void ChangeVisionRadius(int playerId, int r);
void ChangeVisionXY(int playerId, Point position, Point futurePosition);
void ProcessVisionList();
void lighting_color_cycling();

constexpr int MaxCrawlRadius = 18;

/**
 * CrawlTable specifies X- and Y-coordinate deltas from a missile target coordinate.
 *
 * n=4
 *
 *    y
 *    ^
 *    |  1
 *    | 3#4
 *    |  2
 *    +-----> x
 *
 * n=16
 *
 *    y
 *    ^
 *    |  314
 *    | B7 8C
 *    | F # G
 *    | D9 AE
 *    |  526
 *    +-------> x
 */

bool DoCrawl(unsigned radius, tl::function_ref<bool(Displacement)> function);
bool DoCrawl(unsigned minRadius, unsigned maxRadius, tl::function_ref<bool(Displacement)> function);

template <typename F>
auto Crawl(unsigned radius, F function) -> std::invoke_result_t<decltype(function), Displacement>
{
	std::invoke_result_t<decltype(function), Displacement> result;
	DoCrawl(radius, [&result, &function](Displacement displacement) -> bool {
		result = function(displacement);
		return !result;
	});
	return result;
}

template <typename F>
auto Crawl(unsigned minRadius, unsigned maxRadius, F function) -> std::invoke_result_t<decltype(function), Displacement>
{
	std::invoke_result_t<decltype(function), Displacement> result;
	DoCrawl(minRadius, maxRadius, [&result, &function](Displacement displacement) -> bool {
		result = function(displacement);
		return !result;
	});
	return result;
}

} // namespace devilution
