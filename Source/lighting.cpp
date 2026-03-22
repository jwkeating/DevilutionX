/**
 * @file lighting.cpp
 *
 * Implementation of light and vision.
 */
#include "lighting.h"

#include <algorithm>
#include <cstdint>
#include <numeric>

#include "automap.h"
#include "diablo.h"
#include "engine/load_file.hpp"
#include "engine/points_in_rectangle_range.hpp"
#include "player.h"

namespace devilution {

#define JWK_DEBUG_SET_LIGHTING_EQUAL_VISION 0 // For debugging vision.  For each tile, apply 100% light if player has vision.  Otherwise apply 100% darkness.

// One vision per player.  Vision is shared across all players.
std::array<Light, MAX_PLAYERS> VisionList;
std::array<bool, MAX_PLAYERS> VisionActive;

// Lights are client-side (only relevant to local player)
std::array<Light, MAXLIGHTS> Lights;
std::array<uint8_t, MAXLIGHTS> ActiveLights;
int ActiveLightCount;
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
std::array<Light, MAX_PLAYERS> PlayerLights; // use a separate array for player lights so they're always available instead of being used by fireball or something
#endif

std::array<std::array<uint8_t, 256>, NumLightingLevels> LightTables;
std::array<uint8_t, 256> InfravisionTable;
std::array<uint8_t, 256> StoneTable;
std::array<uint8_t, 256> PauseTable;
#ifdef _DEBUG
bool DisableLighting;
#endif
bool UpdateLighting;
bool UpdateVision;

namespace {

/*
 * XY points of vision rays are cast to trace the visibility of the
 * surrounding environment. The table represents N rays of M points in
 * one quadrant (0°-90°) of a circle, so rays for other quadrants will
 * be created by mirroring. Zero points at the end will be trimmed and
 * ignored. A similar table can be recreated using Bresenham's line
 * drawing algorithm, which is suitable for integer arithmetic:
 * https://en.wikipedia.org/wiki/Bresenham's_line_algorithm
 */
static const DisplacementOf<int8_t> VisionRays[23][15] = {
	// clang-format off
	{ { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 5, 0 }, { 6, 0 }, { 7, 0 }, { 8, 0 }, { 9, 0 }, { 10,  0 }, { 11,  0 }, { 12,  0 }, { 13,  0 }, { 14,  0 }, { 15,  0 } },
	{ { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 5, 0 }, { 6, 0 }, { 7, 0 }, { 8, 1 }, { 9, 1 }, { 10,  1 }, { 11,  1 }, { 12,  1 }, { 13,  1 }, { 14,  1 }, { 15,  1 } },
	{ { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 1 }, { 5, 1 }, { 6, 1 }, { 7, 1 }, { 8, 1 }, { 9, 1 }, { 10,  1 }, { 11,  1 }, { 12,  2 }, { 13,  2 }, { 14,  2 }, { 15,  2 } },
	{ { 1, 0 }, { 2, 0 }, { 3, 1 }, { 4, 1 }, { 5, 1 }, { 6, 1 }, { 7, 1 }, { 8, 2 }, { 9, 2 }, { 10,  2 }, { 11,  2 }, { 12,  2 }, { 13,  3 }, { 14,  3 }, { 15,  3 } },
	{ { 1, 0 }, { 2, 1 }, { 3, 1 }, { 4, 1 }, { 5, 1 }, { 6, 2 }, { 7, 2 }, { 8, 2 }, { 9, 3 }, { 10,  3 }, { 11,  3 }, { 12,  3 }, { 13,  4 }, { 14,  4 }, {  0,  0 } },
	{ { 1, 0 }, { 2, 1 }, { 3, 1 }, { 4, 1 }, { 5, 2 }, { 6, 2 }, { 7, 3 }, { 8, 3 }, { 9, 3 }, { 10,  4 }, { 11,  4 }, { 12,  4 }, { 13,  5 }, { 14,  5 }, {  0,  0 } },
	{ { 1, 0 }, { 2, 1 }, { 3, 1 }, { 4, 2 }, { 5, 2 }, { 6, 3 }, { 7, 3 }, { 8, 3 }, { 9, 4 }, { 10,  4 }, { 11,  5 }, { 12,  5 }, { 13,  6 }, { 14,  6 }, {  0,  0 } },
	{ { 1, 1 }, { 2, 1 }, { 3, 2 }, { 4, 2 }, { 5, 3 }, { 6, 3 }, { 7, 4 }, { 8, 4 }, { 9, 5 }, { 10,  5 }, { 11,  6 }, { 12,  6 }, { 13,  7 }, {  0,  0 }, {  0,  0 } },
	{ { 1, 1 }, { 2, 1 }, { 3, 2 }, { 4, 2 }, { 5, 3 }, { 6, 4 }, { 7, 4 }, { 8, 5 }, { 9, 6 }, { 10,  6 }, { 11,  7 }, { 12,  7 }, { 12,  8 }, { 13,  8 }, {  0,  0 } },
	{ { 1, 1 }, { 2, 2 }, { 3, 2 }, { 4, 3 }, { 5, 4 }, { 6, 5 }, { 7, 5 }, { 8, 6 }, { 9, 7 }, { 10,  7 }, { 10,  8 }, { 11,  8 }, { 12,  9 }, {  0,  0 }, {  0,  0 } },
	{ { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 5 }, { 7, 6 }, { 8, 7 }, { 9, 8 }, { 10,  9 }, { 11,  9 }, { 11, 10 }, {  0,  0 }, {  0,  0 }, {  0,  0 } },
	{ { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 6 }, { 7, 7 }, { 8, 8 }, { 9, 9 }, { 10, 10 }, { 11, 11 }, {  0,  0 }, {  0,  0 }, {  0,  0 }, {  0,  0 } },
	{ { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 }, { 5, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 }, { 8, 9 }, {  9, 10 }, {  9, 11 }, { 10, 11 }, {  0,  0 }, {  0,  0 }, {  0,  0 } },
	{ { 1, 1 }, { 2, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 5, 7 }, { 6, 8 }, { 7, 9 }, {  7, 10 }, {  8, 10 }, {  8, 11 }, {  9, 12 }, {  0,  0 }, {  0,  0 } },
	{ { 1, 1 }, { 1, 2 }, { 2, 3 }, { 2, 4 }, { 3, 5 }, { 4, 6 }, { 4, 7 }, { 5, 8 }, { 6, 9 }, {  6, 10 }, {  7, 11 }, {  7, 12 }, {  8, 12 }, {  8, 13 }, {  0,  0 } },
	{ { 1, 1 }, { 1, 2 }, { 2, 3 }, { 2, 4 }, { 3, 5 }, { 3, 6 }, { 4, 7 }, { 4, 8 }, { 5, 9 }, {  5, 10 }, {  6, 11 }, {  6, 12 }, {  7, 13 }, {  0,  0 }, {  0,  0 } },
	{ { 0, 1 }, { 1, 2 }, { 1, 3 }, { 2, 4 }, { 2, 5 }, { 3, 6 }, { 3, 7 }, { 3, 8 }, { 4, 9 }, {  4, 10 }, {  5, 11 }, {  5, 12 }, {  6, 13 }, {  6, 14 }, {  0,  0 } },
	{ { 0, 1 }, { 1, 2 }, { 1, 3 }, { 1, 4 }, { 2, 5 }, { 2, 6 }, { 3, 7 }, { 3, 8 }, { 3, 9 }, {  4, 10 }, {  4, 11 }, {  4, 12 }, {  5, 13 }, {  5, 14 }, {  0,  0 } },
	{ { 0, 1 }, { 1, 2 }, { 1, 3 }, { 1, 4 }, { 1, 5 }, { 2, 6 }, { 2, 7 }, { 2, 8 }, { 3, 9 }, {  3, 10 }, {  3, 11 }, {  3, 12 }, {  4, 13 }, {  4, 14 }, {  0,  0 } },
	{ { 0, 1 }, { 0, 2 }, { 1, 3 }, { 1, 4 }, { 1, 5 }, { 1, 6 }, { 1, 7 }, { 2, 8 }, { 2, 9 }, {  2, 10 }, {  2, 11 }, {  2, 12 }, {  3, 13 }, {  3, 14 }, {  3, 15 } },
	{ { 0, 1 }, { 0, 2 }, { 0, 3 }, { 1, 4 }, { 1, 5 }, { 1, 6 }, { 1, 7 }, { 1, 8 }, { 1, 9 }, {  1, 10 }, {  1, 11 }, {  2, 12 }, {  2, 13 }, {  2, 14 }, {  2, 15 } },
	{ { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 0, 6 }, { 0, 7 }, { 1, 8 }, { 1, 9 }, {  1, 10 }, {  1, 11 }, {  1, 12 }, {  1, 13 }, {  1, 14 }, {  1, 15 } },
	{ { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 0, 6 }, { 0, 7 }, { 0, 8 }, { 0, 9 }, {  0, 10 }, {  0, 11 }, {  0, 12 }, {  0, 13 }, {  0, 14 }, {  0, 15 } },
	// clang-format on
};

constexpr size_t MaxLightRadius = 15; // There are 16 possible light radii because 0 is included
#if !JWK_FIX_LIGHTING
/** Falloff tables for the light cone */
uint8_t LightFalloffs[MaxLightRadius + 1][128];
/** interpolations of a 32x32 (16x16 mirrored for each quadrant) light circle moving between tiles in steps of 1/8 of a tile */
uint8_t LightDistanceInterpolations[8][8][16][16];
#endif

void RotateRadius(DisplacementOf<int8_t> &offset, DisplacementOf<int8_t> &dist, DisplacementOf<int8_t> &light, DisplacementOf<int8_t> &block)
{
	dist = { static_cast<int8_t>(7 - dist.deltaY), dist.deltaX };
	light = { static_cast<int8_t>(7 - light.deltaY), light.deltaX };
	offset = { static_cast<int8_t>(dist.deltaX - light.deltaX), static_cast<int8_t>(dist.deltaY - light.deltaY) };

	block.deltaX = 0;
	if (offset.deltaX < 0) {
		offset.deltaX += 8;
		block.deltaX = 1;
	}
	block.deltaY = 0;
	if (offset.deltaY < 0) {
		offset.deltaY += 8;
		block.deltaY = 1;
	}
}

void SetLight(Point position, uint8_t v)
{
	if (LoadingMapObjects)
		dPreLight[position.x][position.y] = v;
	else
		dLight[position.x][position.y] = v;
}

uint8_t GetLight(Point position)
{
	if (LoadingMapObjects)
		return dPreLight[position.x][position.y];

	return dLight[position.x][position.y];
}

bool CrawlFlipsX(Displacement mirrored, tl::function_ref<bool(Displacement)> function)
{
	for (const Displacement displacement : { mirrored.flipX(), mirrored }) {
		if (!function(displacement))
			return false;
	}
	return true;
}

bool CrawlFlipsY(Displacement mirrored, tl::function_ref<bool(Displacement)> function)
{
	for (const Displacement displacement : { mirrored, mirrored.flipY() }) {
		if (!function(displacement))
			return false;
	}
	return true;
}

bool CrawlFlipsXY(Displacement mirrored, tl::function_ref<bool(Displacement)> function)
{
	for (const Displacement displacement : { mirrored.flipX(), mirrored, mirrored.flipXY(), mirrored.flipY() }) {
		if (!function(displacement))
			return false;
	}
	return true;
}

bool TileAllowsLight(Point position)
{
	if (!InDungeonBounds(position))
		return false;
	return !TileHasAny(dPiece[position.x][position.y], TileProperties::BlockLight);
}

void DoVisionFlags(Point position, MapExplorationType doAutomap, bool visibleToLocalPlayer)
{
	if (doAutomap != MAP_EXP_NONE) {
		if (dFlags[position.x][position.y] != DungeonFlag::None)
			SetAutomapView(position, doAutomap);
		dFlags[position.x][position.y] |= DungeonFlag::Explored;
	}
	if (visibleToLocalPlayer) {
		dFlags[position.x][position.y] |= DungeonFlag::Lit;
	}
	dFlags[position.x][position.y] |= DungeonFlag::Visible;
}

} // namespace

bool DoCrawl(unsigned radius, tl::function_ref<bool(Displacement)> function)
{
	if (radius == 0)
		return function(Displacement { 0, 0 });

	if (!CrawlFlipsY({ 0, static_cast<int>(radius) }, function))
		return false;
	for (unsigned i = 1; i < radius; i++) {
		if (!CrawlFlipsXY({ static_cast<int>(i), static_cast<int>(radius) }, function))
			return false;
	}
	if (radius > 1) {
		if (!CrawlFlipsXY({ static_cast<int>(radius) - 1, static_cast<int>(radius) - 1 }, function))
			return false;
	}
	if (!CrawlFlipsX({ static_cast<int>(radius), 0 }, function))
		return false;
	for (unsigned i = 1; i < radius; i++) {
		if (!CrawlFlipsXY({ static_cast<int>(radius), static_cast<int>(i) }, function))
			return false;
	}
	return true;
}

bool DoCrawl(unsigned minRadius, unsigned maxRadius, tl::function_ref<bool(Displacement)> function)
{
	for (unsigned i = minRadius; i <= maxRadius; i++) {
		if (!DoCrawl(i, function))
			return false;
	}
	return true;
}

void DoUnLight(Point position, uint8_t radius)
{
	radius++;
	radius++; // If lights moved at a diagonal it can result in some extra tiles being lit

	auto searchArea = PointsInRectangle(WorldTileRectangle { position, radius });

	for (WorldTilePosition targetPosition : searchArea) {
		if (InDungeonBounds(targetPosition))
			dLight[targetPosition.x][targetPosition.y] = dPreLight[targetPosition.x][targetPosition.y];
	}
}

void DoLighting(Point position, uint8_t radius, DisplacementOf<int8_t> eighthOffset)
{
	assert(radius <= MaxLightRadius);
	assert(InDungeonBounds(position));

#if JWK_DEBUG_SET_LIGHTING_EQUAL_VISION
	for (int y = 0; y < MAXDUNY; y++) {
		for (int x = 0; x < MAXDUNY; x++) {
			SetLight(Point(x,y), HasAnyOf(dFlags[x][y], DungeonFlag::Visible) ? 0 : 15);
		}
	}
	return;
#endif

#if JWK_FIX_LIGHTING
	unsigned int radius16 = radius << 16; // light radius stored as fixed point
	constexpr unsigned int maxDarkness = 15;
	int minX = std::max<int>(-radius, -position.x);
	int minY = std::max<int>(-radius, -position.y);
	int maxX = std::min<int>( radius, -position.x + MAXDUNX - 1);
	int maxY = std::min<int>( radius, -position.y + MAXDUNY - 1);
	for (int y = minY; y <= maxY; y++) {
		for (int x = minX; x <= maxX; x++) {
			int x3 = 8 * x - eighthOffset.deltaX; // measured in eighth's of a tile (<< 3)
			int y3 = 8 * y - eighthOffset.deltaY; // measured in eighth's of a tile (<< 3)
			unsigned int distanceToLight16 = static_cast<unsigned int>(sqrtf(x3 * x3 + y3 * y3) * (1 << 13)); // Euclidean distance: sqrt() and float->int could be slow but likely not on modern hardware.  I didn't notice an FPS difference compared to original code.
			if (distanceToLight16 >= radius16)
				continue;
			Point p = position + Displacement{x, y};
			assert(InDungeonBounds(p));
			unsigned int darkness = (maxDarkness * distanceToLight16) / radius16;
			// pick the least dark light contribution
			if (darkness < GetLight(p))
				SetLight(p, darkness);
		}
	}
#else // original code
	DisplacementOf<int8_t> light = {};
	DisplacementOf<int8_t> block = {};

	if (eighthOffset.deltaX < 0) {
		eighthOffset.deltaX += 8;
		position -= { 1, 0 };
	}
	if (eighthOffset.deltaY < 0) {
		eighthOffset.deltaY += 8;
		position -= { 0, 1 };
	}

	DisplacementOf<int8_t> dist = eighthOffset;

	// Allow for dim lights in crypt and nest
	if (IsAnyOf(leveltype, DTYPE_NEST, DTYPE_CRYPT)) {
		if (GetLight(position) > LightFalloffs[radius][0])
			SetLight(position, LightFalloffs[radius][0]);
	} else {
		SetLight(position, 0);
	}

	int minX = 15;
	if (position.x - 15 < 0) {
		minX = position.x + 1;
	}
	int maxX = 15;
	if (position.x + 15 > MAXDUNX) {
		maxX = MAXDUNX - position.x;
	}
	int minY = 15;
	if (position.y - 15 < 0) {
		minY = position.y + 1;
	}
	int maxY = 15;
	if (position.y + 15 > MAXDUNY) {
		maxY = MAXDUNY - position.y;
	}

	for (int i = 0; i < 4; i++) {
		int yBound = i > 0 && i < 3 ? maxY : minY;
		int xBound = i < 2 ? maxX : minX;
		for (int y = 0; y < yBound; y++) {
			for (int x = 1; x < xBound; x++) {
				int distanceEuclidean = LightDistanceInterpolations[eighthOffset.deltaX][eighthOffset.deltaY][x + block.deltaX][y + block.deltaY];
				if (distanceEuclidean >= 128)
					continue;
				Point temp = position + (Displacement { x, y }).Rotate(-i);
				uint8_t v = LightFalloffs[radius][distanceEuclidean];
				if (!InDungeonBounds(temp))
					continue;
				if (v < GetLight(temp))
					SetLight(temp, v);
			}
		}
		RotateRadius(eighthOffset, dist, light, block);
	}
#endif // JWK_FIX_LIGHTING
}

void DoUnVision(Point position, uint8_t radius)
{
	radius++;
	radius++; // increasing the radius even further here prevents leaving stray vision tiles behind and doesn't seem to affect monster AI - applying new vision happens in the same tick

	auto searchArea = PointsInRectangle(WorldTileRectangle { position, radius });

	for (WorldTilePosition targetPosition : searchArea) {
		if (InDungeonBounds(targetPosition))
			dFlags[targetPosition.x][targetPosition.y] &= ~(DungeonFlag::Visible | DungeonFlag::Lit);
	}
}

void DoVision(Point position, uint8_t radius, MapExplorationType doAutomap, bool visibleToLocalPlayer)
{
#if 1 // jwk - Implement custom vision when radius = 2
	if (radius == 2) {
		// When radius=2, the raycast algorithm omits tiles directly touching your character:
		//         .                               .
		//         .                             . . .					         . . .
		//     . . x . .                       . . x . .				         . x .  
		//         .                             . . .					         . . .
		//         .                               .					 
		// Raycast algorithm        Raycast with missing tiles added         Square vision
		//
		// Use square vision as depicted above
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				Point p = position + Displacement(x, y);
				if (InDungeonBounds(p))
					DoVisionFlags(p, doAutomap, visibleToLocalPlayer);
			}
		}
		return;
	}
#endif
	assert(radius <= MaxLightRadius);
	DoVisionFlags(position, doAutomap, visibleToLocalPlayer);

	// A brute force method would check every point in each quadrant using (x,y) traversal order like this:
	// for (y = 0; y < radius; y++)
	//     for (x = 0; x < radius; x++)
	//         DoStuff()
	// But the brute force method would need to perform a line of sight check for every point.  Instead, we can traverse all points in the quadrant using ray casts.
	// This is kind of like searching in polar coords instead of cartesian coords.  This allows us to traverse each raycast and ignore remaining points along the radial direction once we find a blocking tile.

	// Adjustment to a ray length to ensure all rays lie on an accurate circle
	static const uint8_t rayLenAdj[23] = { 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 4, 3, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0 };
	static_assert(std::size(rayLenAdj) == std::size(VisionRays));

	// Four quadrants on a circle
	static const Displacement quadrants[] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };

	// Loop over quadrants and mirror rays for each one
	for (const auto &quadrant : quadrants) {
		// Cast a ray for a quadrant
		for (unsigned int j = 0; j < std::size(VisionRays); j++) { // the angular coordinate
			int rayLen = radius - rayLenAdj[j];
			for (int k = 0; k < rayLen; k++) { // the radial coordinate
				const auto &relRayPoint = VisionRays[j][k];
				// Calculate the next point on a ray in the quadrant
				Point rayPoint = position + relRayPoint * quadrant;
				if (!InDungeonBounds(rayPoint))
					break;

				// We've cast an approximated ray on an integer 2D
				// grid, so we need to check if a ray can pass through
				// the diagonally adjacent tiles. For example, consider
				// this case:
				//
				//        #?
				//       ↗ #
				//     x
				//
				// The ray is cast from the observer 'x', and reaches
				// the '?', but diagonally adjacent tiles '#' do not
				// pass the light, so the '?' should not be visible
				// for the 2D observer.
				//
				// The trick is to perform two additional visibility
				// checks for the diagonally adjacent tiles, but only
				// for the rays that are not parallel to the X or Y
				// coordinate lines. Parallel rays, which have a 0 in
				// one of their coordinate components, do not require
				// any additional adjacent visibility checks, and the
				// tile, hit by the ray, is always considered visible.
				//
				if (relRayPoint.deltaX > 0 && relRayPoint.deltaY > 0) {
					Displacement adjacent1 = { -quadrant.deltaX, 0 };
					Displacement adjacent2 = { 0, -quadrant.deltaY };

					bool passesLight = (TileAllowsLight(rayPoint + adjacent1) || TileAllowsLight(rayPoint + adjacent2));
					if (!passesLight)
						// Diagonally adjacent tiles do not pass the
						// light further, we are done with this ray
						break;
				}
				DoVisionFlags(rayPoint, doAutomap, visibleToLocalPlayer);

				bool passesLight = TileAllowsLight(rayPoint);
				if (!passesLight)
					// Tile does not pass the light further, we are
					// done with this ray
					break;

				int8_t trans = dTransVal[rayPoint.x][rayPoint.y];
				if (trans != 0)
					TransList[trans] = true;
			}
		}
	}
}

void MakeLightTable()
{
	// Generate 16 gradually darker translation tables for doing lighting
	uint8_t shade = 0;
	constexpr uint8_t black = 0;
	constexpr uint8_t white = 255;
	for (auto &lightTable : LightTables) {
		uint8_t colorIndex = 0;
		for (uint8_t steps : { 16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 8, 8, 16, 16, 16, 16, 16, 16 }) {
			const uint8_t shading = shade * steps / 16;
			const uint8_t shadeStart = colorIndex;
			const uint8_t shadeEnd = shadeStart + steps - 1;
			for (uint8_t step = 0; step < steps; step++) {
				if (colorIndex == black) {
					lightTable[colorIndex++] = black;
					continue;
				}
				int color = shadeStart + step + shading;
				if (color > shadeEnd || color == white)
					color = black;
				lightTable[colorIndex++] = color;
			}
		}
		shade++;
	}

	LightTables[15] = {}; // Make last shade pitch black

	if (leveltype == DTYPE_HELL) {
		// Blood wall lighting
		const int shades = LightTables.size() - 1;
		for (int i = 0; i < shades; i++) {
			auto &lightTable = LightTables[i];
			constexpr int range = 16;
			for (int j = 0; j < range; j++) {
				uint8_t color = ((range - 1) << 4) / shades * (shades - i) / range * (j + 1);
				color = 1 + (color >> 4);
				lightTable[j + 1] = color;
				lightTable[31 - j] = color;
			}
		}
	} else if (IsAnyOf(leveltype, DTYPE_NEST, DTYPE_CRYPT)) {
		// Make the lava fully bright
		for (auto &lightTable : LightTables)
			std::iota(lightTable.begin(), lightTable.begin() + 16, 0);
		LightTables[15][0] = 0;
		std::fill_n(LightTables[15].begin() + 1, 15, 1);
	}

	LoadFileInMem("plrgfx\\infra.trn", InfravisionTable);
	LoadFileInMem("plrgfx\\stone.trn", StoneTable);
	LoadFileInMem("gendata\\pause.trn", PauseTable);
#if !JWK_FIX_LIGHTING
	// Generate light falloffs ranges
	const float maxDarkness = 15;
	const float maxBrightness = 0;
	for (size_t radius = 0; radius <= MaxLightRadius; radius++) {
		size_t maxDistance = (radius + 1) * 8;
		for (size_t distance = 0; distance < 128; distance++) {
			if (distance > maxDistance) {
				LightFalloffs[radius][distance] = 15;
			} else {
				const float factor = static_cast<float>(distance) / maxDistance;
				float scaled;
				if (IsAnyOf(leveltype, DTYPE_NEST, DTYPE_CRYPT)) {
					// quardratic falloff with over exposure
					const float brightness = radius * 1.25;
					scaled = factor * factor * brightness + (maxDarkness - brightness);
					scaled = std::max(maxBrightness, scaled);
				} else {
					// Linear falloff
					scaled = factor * maxDarkness;
				}
				LightFalloffs[radius][distance] = static_cast<uint8_t>(scaled + 0.5F); // round instead of truncate
			}
		}
	}

	// Generate a precomputed table of distances for every 1/8 fraction of a tile in the x or y direction.
	// LightDistanceInterpolations[offsetX][offsetY][x][y] is the Euclidean distance from tile position (0,0) to tile position (x - offsetX/8, y - offsetY/8)
	for (int offsetY = 0; offsetY < 8; offsetY++) {
		for (int offsetX = 0; offsetX < 8; offsetX++) {
			for (int y = 0; y < 16; y++) {
				for (int x = 0; x < 16; x++) {
					int a = (8 * x - offsetX);
					int b = (8 * y - offsetY);
					LightDistanceInterpolations[offsetX][offsetY][x][y] = static_cast<uint8_t>(sqrtf(a * a + b * b));
				}
			}
		}
	}
#endif
}

#ifdef _DEBUG
void ToggleLighting()
{
	DisableLighting = !DisableLighting;

	if (DisableLighting) {
		memset(dLight, 0, sizeof(dLight));
		return;
	}

	memcpy(dLight, dPreLight, sizeof(dLight));
	for (const Player &player : Players) {
		if (player.plractive && player.isOnActiveLevel()) {
			DoLighting(player.position.tile, player._pLightRad, {});
		}
	}
}
#endif

void InitLighting()
{
	ActiveLightCount = 0;
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
	for (int i = 0; i < MAX_PLAYERS; i++)
		PlayerLights[i].isInvalid = true;
#endif
	UpdateLighting = false;
	UpdateVision = false;
#ifdef _DEBUG
	DisableLighting = false;
#endif

	std::iota(ActiveLights.begin(), ActiveLights.end(), 0);
	VisionActive = {};
	TransList = {};
}

#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
static bool CanSeePlayerLight(const Player& player)
{
	//return player.friendlyMode;
	return true;
}

void AddPlayerLight(const Player& player, Point position, uint8_t radius)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	Light &light = PlayerLights[player.getId()];
	light.position.tile = position;
	light.radius = radius;
	light.position.offset = { 0, 0 };
	light.isInvalid = false;
	light.hasChanged = false;
	if (&player == MyPlayer || CanSeePlayerLight(player))
		UpdateLighting = true;
}

void AddPlayerUnLight(const Player& player)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	// isInvalid + hasChanged indicates this light needs to be unlit
	Light &light = PlayerLights[player.getId()];
	light.isInvalid = true;
	light.hasChanged = true;
	UpdateLighting = true;
}

static void ChangePlayerLightRadius(const Player& player, uint8_t radius)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	Light &light = PlayerLights[player.getId()];
	light.hasChanged = true;
	light.position.old = light.position.tile;
	light.oldRadius = light.radius;
	light.radius = radius;
	if (&player == MyPlayer || CanSeePlayerLight(player))
		UpdateLighting = true;
}

void ChangePlayerLightXY(const Player& player, Point position)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	Light &light = PlayerLights[player.getId()];
	light.hasChanged = true;
	light.position.old = light.position.tile;
	light.oldRadius = light.radius;
	light.position.tile = position;
	if (&player == MyPlayer || CanSeePlayerLight(player))
		UpdateLighting = true;
}

void ChangePlayerLightOffset(const Player& player, DisplacementOf<int8_t> offset)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	Light &light = PlayerLights[player.getId()];
	if (light.position.offset == offset)
		return;
	light.hasChanged = true;
	light.position.old = light.position.tile;
	light.oldRadius = light.radius;
	light.position.offset = offset;
	if (&player == MyPlayer || CanSeePlayerLight(player))
		UpdateLighting = true;
}

void ChangePlayerLightFriendly(const Player& player)
{
	if (&player == MyPlayer)
		return;

	if (CanSeePlayerLight(player)) {
		PlayerLights[player.getId()].hasChanged = true;
	} else {
		AddPlayerUnLight(player);
	}
	UpdateVision = true;
	UpdateLighting = true;
}
#endif // JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER

int AddLight(Point position, uint8_t radius)
{
#ifdef _DEBUG
	if (DisableLighting)
		return NO_LIGHT;
#endif
	if (ActiveLightCount >= MAXLIGHTS)
		return NO_LIGHT;

	int lid = ActiveLights[ActiveLightCount++];
	Light &light = Lights[lid];
	light.position.tile = position;
	light.radius = radius;
	light.position.offset = { 0, 0 };
	light.isInvalid = false;
	light.hasChanged = false;
	UpdateLighting = true;
	return lid;
}

void AddUnLight(int i)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	if (i == NO_LIGHT)
		return;

	Lights[i].isInvalid = true;
	UpdateLighting = true;
}

void ChangeLightRadius(int i, uint8_t radius)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	if (i == NO_LIGHT)
		return;

	Light &light = Lights[i];
	light.hasChanged = true;
	light.position.old = light.position.tile;
	light.oldRadius = light.radius;
	light.radius = radius;
	UpdateLighting = true;
}

void ChangeLightXY(int i, Point position)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	if (i == NO_LIGHT)
		return;

	Light &light = Lights[i];
	light.hasChanged = true;
	light.position.old = light.position.tile;
	light.oldRadius = light.radius;
	light.position.tile = position;
	UpdateLighting = true;
}

void ChangeLightOffset(int i, DisplacementOf<int8_t> offset)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	if (i == NO_LIGHT)
		return;

	Light &light = Lights[i];
	if (light.position.offset == offset)
		return;

	light.hasChanged = true;
	light.position.old = light.position.tile;
	light.oldRadius = light.radius;
	light.position.offset = offset;
	UpdateLighting = true;
}

void ChangeLight(int i, Point position, uint8_t radius)
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	if (i == NO_LIGHT)
		return;

	Light &light = Lights[i];
	light.hasChanged = true;
	light.position.old = light.position.tile;
	light.oldRadius = light.radius;
	light.position.tile = position;
	light.radius = radius;
	UpdateLighting = true;
}

void ProcessLightList()
{
#ifdef _DEBUG
	if (DisableLighting)
		return;
#endif
	if (!UpdateLighting)
		return;

	for (int i = 0; i < ActiveLightCount; i++) {
		Light &light = Lights[ActiveLights[i]];
		if (light.isInvalid) {
			DoUnLight(light.position.tile, light.radius);
		}
		if (light.hasChanged) {
			DoUnLight(light.position.old, light.oldRadius);
			light.hasChanged = false;
		}
	}
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
	for (int i = 0; i < MAX_PLAYERS; i++) {
		Light &light = PlayerLights[i];
		if (light.hasChanged) {
			if (light.isInvalid) {
				DoUnLight(light.position.tile, light.radius);
			} else {
				DoUnLight(light.position.old, light.oldRadius);
			}
			light.hasChanged = false;
		}
	}
	for (int i = 0; i < MAX_PLAYERS; i++) {
		if (i == MyPlayerId || CanSeePlayerLight(Players[i])) {
			Light &light = PlayerLights[i];
			if (!light.isInvalid) {
				DoLighting(light.position.tile, light.radius, light.position.offset);
			}
		}
	}
#endif
	for (int i = 0; i < ActiveLightCount; i++) {
		const Light &light = Lights[ActiveLights[i]];
		if (light.isInvalid) {
			ActiveLightCount--;
			std::swap(ActiveLights[ActiveLightCount], ActiveLights[i]);
			i--;
			continue;
		}
		if (TileHasAny(dPiece[light.position.tile.x][light.position.tile.y], TileProperties::Solid))
			continue; // Monster hidden in a wall, don't spoil the surprise
		DoLighting(light.position.tile, light.radius, light.position.offset);
	}

	UpdateLighting = false;
}

void SavePreLighting()
{
	memcpy(dPreLight, dLight, sizeof(dPreLight));
}

void ActivateVision(int playerId, Point position, int r)
{
	auto &vision = VisionList[playerId];
	vision.position.tile = position;
	vision.radius = r;
	vision.isInvalid = false;
	vision.hasChanged = false;
	VisionActive[playerId] = true;
	UpdateVision = true;
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
	AddPlayerLight(Players[playerId], position, r);
#endif
}

void ChangeVisionRadius(int playerId, int r)
{
	auto &vision = VisionList[playerId];
	vision.hasChanged = true;
	vision.position.old = vision.position.tile;
	vision.oldRadius = vision.radius;
	vision.radius = r;
	UpdateVision = true;
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
	ChangePlayerLightRadius(Players[playerId], r);
#endif
}

void ChangeVisionXY(int playerId, Point position, Point futurePosition)
{
	auto &vision = VisionList[playerId];
	vision.hasChanged = true;
	vision.position.old = vision.position.tile;
	vision.oldRadius = vision.radius;
#if JWK_FIX_LIGHTING
	// Immediately update player vision to the destination tile instead of waiting until the player finishes moving there.
	// It feels better to see where you're going instead of where you came from.
	vision.position.tile = futurePosition;
#else // original code:
	vision.position.tile = position;
#endif
	UpdateVision = true;
	// Note: We can't call ChangePlayerLightXY() here because it messes up light interpolation while walking.
}

void ProcessVisionList()
{
#if JWK_DEBUG_SET_LIGHTING_EQUAL_VISION
	UpdateLighting = true;
	UpdateVision = true;
#endif
	if (!UpdateVision)
		return;

	TransList = {};

	for (const Player &player : Players) {
		int id = player.getId();
		if (!VisionActive[id])
			continue;
		Light &vision = VisionList[id];
		if (!player.plractive || !player.isOnActiveLevel()) {
			DoUnVision(vision.position.tile, vision.radius);
#if JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER
			AddPlayerUnLight(player);
#endif
			VisionActive[id] = false;
			continue;
		}
		if (vision.hasChanged) {
			DoUnVision(vision.position.old, vision.oldRadius);
			vision.hasChanged = false;
		}
	}
	for (const Player &player : Players) {
		int id = player.getId();
		if (!VisionActive[id])
			continue;
		Light &vision = VisionList[id];
		MapExplorationType doautomap = MAP_EXP_SELF;
		if (&player != MyPlayer)
			doautomap = player.friendlyMode ? MAP_EXP_OTHERS : MAP_EXP_NONE;
		DoVision(
		    vision.position.tile,
		    vision.radius,
		    doautomap,
		    &player == MyPlayer || (JWK_ADD_PLAYER_LIGHTS_IN_MULTIPLAYER && CanSeePlayerLight(player)));
	}

	UpdateVision = false;
}

void lighting_color_cycling()
{
	for (auto &lightTable : LightTables) {
		// shift elements between indexes 1-31 to left
		std::rotate(lightTable.begin() + 1, lightTable.begin() + 2, lightTable.begin() + 32);
	}
}

} // namespace devilution
