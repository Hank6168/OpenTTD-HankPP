/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file land_value.cpp Implementation of land-value calculations. */

#include "stdafx.h"

#include "core/math_func.hpp"
#include "land_value.h"
#include "map_func.h"
#include "settings_type.h"
#include "town.h"
#include "window_func.h"

#include "safeguards.h"

/**
 * Multiply an unsigned value by a bounded numerator and divide it without overflowing.
 * The result is rounded down and saturated to the supplied limit.
 */
static uint64_t ScaleValue(uint64_t value, uint32_t numerator, uint32_t denominator, uint64_t limit)
{
	assert(denominator != 0);

	const uint64_t quotient = value / denominator;
	const uint64_t remainder = value % denominator;

	if (quotient != 0 && numerator > limit / quotient) return limit;

	const uint64_t scaled_quotient = quotient * numerator;
	const uint64_t scaled_remainder = remainder * numerator / denominator;
	if (scaled_remainder > limit - scaled_quotient) return limit;

	return scaled_quotient + scaled_remainder;
}

/** Clamp a raw value to the valid land-value score range. */
LandValueScore ClampLandValueScore(uint64_t score)
{
	return LandValueScore{static_cast<uint32_t>(std::min<uint64_t>(score, LAND_VALUE_MAX.base()))};
}

/** Clamp a raw value to the valid land-value modifier range. */
LandValueModifier ClampLandValueModifier(uint64_t modifier)
{
	return LandValueModifier{static_cast<uint32_t>(std::min<uint64_t>(modifier, LAND_VALUE_MODIFIER_MAX.base()))};
}

/** Apply a land-value score to an unsigned value using the score scale. */
uint64_t ApplyLandValueScore(uint64_t value, LandValueScore score, uint64_t limit)
{
	return ScaleValue(value, ClampLandValueScore(score.base()).base(), LAND_VALUE_BASE.base(), limit);
}

/** Apply a land-value modifier to an unsigned value using the modifier scale. */
uint64_t ApplyLandValueModifier(uint64_t value, LandValueModifier modifier, uint64_t limit)
{
	return ScaleValue(value, ClampLandValueModifier(modifier.base()).base(), LAND_VALUE_MODIFIER_BASE.base(), limit);
}

/** Combine a distance-adjusted score with a modifier and clamp the final score. */
LandValueScore CombineLandValueScoreAndModifier(LandValueScore score, LandValueModifier modifier)
{
	const uint64_t final_score = ApplyLandValueModifier(ClampLandValueScore(score.base()).base(), modifier, LAND_VALUE_MAX.base());
	return LandValueScore{static_cast<uint32_t>(final_score)};
}

/**
 * Calculate a town's target score from stable, already cached town data.
 * Population and house contributions are independently bounded, while cities receive a small fixed premium.
 */
LandValueScore CalculateLandValueTargetScore(uint32_t population, uint32_t num_houses, bool is_city)
{
	static constexpr uint32_t POPULATION_DIVISOR = 25;
	static constexpr uint32_t POPULATION_CONTRIBUTION_MAX = 6000;
	static constexpr uint32_t HOUSE_DIVISOR = 2;
	static constexpr uint32_t HOUSE_CONTRIBUTION_MAX = 3000;
	static constexpr uint32_t CITY_BONUS = 250;

	uint64_t target = LAND_VALUE_BASE.base();
	target += std::min(population / POPULATION_DIVISOR, POPULATION_CONTRIBUTION_MAX);
	target += std::min(num_houses / HOUSE_DIVISOR, HOUSE_CONTRIBUTION_MAX);
	if (is_city) target += CITY_BONUS;

	return ClampLandValueScore(target);
}

/** Smooth a score towards its target using integer percentage arithmetic. */
LandValueScore SmoothLandValueScore(LandValueScore old_score, LandValueScore target_score, uint8_t smoothing_percent)
{
	const uint32_t smoothing = std::min<uint32_t>(smoothing_percent, 100);
	const uint64_t old_value = ClampLandValueScore(old_score.base()).base();
	const uint64_t target_value = ClampLandValueScore(target_score.base()).base();
	const uint64_t smoothed = (old_value * (100 - smoothing) + target_value * smoothing) / 100;
	return ClampLandValueScore(smoothed);
}

/** Build the monotonic distance curve for a town's current centre score. */
LandValueDistanceScores BuildLandValueDistanceScores(LandValueScore center_score)
{
	static_assert(LAND_VALUE_DISTANCE_BAND_COUNT > 1);

	LandValueDistanceScores scores{};
	const uint32_t center = ClampLandValueScore(center_score.base()).base();
	const uint32_t edge = std::min(center, LAND_VALUE_BASE.base());
	const uint32_t decrease = center - edge;

	for (uint32_t band = 0; band < LAND_VALUE_DISTANCE_BAND_COUNT; ++band) {
		const uint32_t band_decrease = static_cast<uint32_t>(static_cast<uint64_t>(decrease) * band / (LAND_VALUE_DISTANCE_BAND_COUNT - 1));
		scores[band] = LandValueScore{center - band_decrease};
	}

	return scores;
}

/** Calculate a bounded influence radius from the largest existing town-zone radius and a percentage distance scale. */
uint32_t CalculateLandValueMaxDistanceSquared(uint32_t town_radius_squared, uint16_t distance_scale_percent)
{
	static constexpr uint32_t MIN_DISTANCE_SQUARED = 64;
	static constexpr uint32_t RADIUS_SCALE_SQUARED = 16;
	static constexpr uint32_t PERCENT_SQUARED = 100 * 100;

	const uint64_t base_distance_squared = std::max<uint64_t>(
			MIN_DISTANCE_SQUARED,
			std::min<uint64_t>(static_cast<uint64_t>(town_radius_squared) * RADIUS_SCALE_SQUARED, std::numeric_limits<uint32_t>::max()));
	const uint64_t distance_scale = std::max<uint32_t>(distance_scale_percent, 1);
	const uint64_t scaled_distance_squared = base_distance_squared * distance_scale * distance_scale / PERCENT_SQUARED;
	return static_cast<uint32_t>(std::clamp<uint64_t>(scaled_distance_squared, 1, std::numeric_limits<uint32_t>::max()));
}

/**
 * Map a squared distance to one of the cached distance bands.
 * Distances at or beyond the maximum are assigned to the final band.
 */
uint8_t GetLandValueDistanceBand(uint32_t distance_squared, uint32_t max_distance_squared)
{
	if (max_distance_squared == 0 || distance_squared == 0) return 0;
	if (distance_squared >= max_distance_squared) return LAND_VALUE_DISTANCE_BAND_COUNT - 1;

	return static_cast<uint8_t>(static_cast<uint64_t>(distance_squared) * (LAND_VALUE_DISTANCE_BAND_COUNT - 1) / max_distance_squared);
}

/** Map a score to a player-facing display level without changing any economic logic. */
LandValueLevel GetLandValueLevel(LandValueScore score)
{
	const uint32_t value = ClampLandValueScore(score.base()).base();
	if (value < 150) return LandValueLevel::VeryLow;
	if (value < 300) return LandValueLevel::Low;
	if (value < 600) return LandValueLevel::Average;
	if (value < 1200) return LandValueLevel::AboveAverage;
	if (value < 2500) return LandValueLevel::High;
	if (value < 5000) return LandValueLevel::VeryHigh;
	return LandValueLevel::Core;
}

/** Return the approximate radius represented by a squared distance using the existing integer square root. */
uint32_t CalculateLandValueInfluenceRadius(uint32_t max_distance_squared)
{
	return IntSqrt(max_distance_squared);
}

/** Return the distance-adjusted score for a tile using the nearest town's cached curve. */
bool IsLandValueEnabled()
{
	return _settings_game.economy.land_value_enabled;
}

/** Convert an untrusted numeric tile index into a valid map tile. */
std::optional<TileIndex> ResolveLandValueTileIndex(uint64_t raw_tile)
{
	if (raw_tile > std::numeric_limits<uint32_t>::max()) return std::nullopt;
	const TileIndex tile{static_cast<uint32_t>(raw_tile)};
	if (!IsValidTile(tile)) return std::nullopt;
	return tile;
}

/** Resolve player-facing map coordinates to a valid tile for read-only debug queries. */
std::optional<TileIndex> ResolveLandValueTileCoordinates(uint64_t raw_x, uint64_t raw_y)
{
	if (raw_x >= Map::SizeX() || raw_y >= Map::SizeY()) return std::nullopt;
	return TileXY(static_cast<uint>(raw_x), static_cast<uint>(raw_y));
}

/** Return a complete, internally consistent, read-only land-value query result. */
LandValueQueryResult GetLandValueQueryResult(TileIndex tile)
{
	LandValueQueryResult result{};
	result.enabled = IsLandValueEnabled();
	if (!IsValidTile(tile)) return result;

	const Town *town = CalcClosestTownFromTile(tile, UINT_MAX);
	if (town == nullptr) return result;

	const LandValueCache &cache = town->cache.land_value;
	result.town_id = town->index;
	result.town_score = ClampLandValueScore(town->land_value_score);
	result.center_score = cache.center_score;
	result.max_distance_squared = cache.max_distance_squared;
	result.distance_squared = DistanceSquare(tile, town->xy);
	result.distance_band = GetLandValueDistanceBand(result.distance_squared, result.max_distance_squared);
	result.distance_score = result.enabled ? cache.distance_score[result.distance_band] : LAND_VALUE_BASE;
	result.modifier = GetLandValueModifier(tile);
	result.final_score = CombineLandValueScoreAndModifier(result.distance_score, result.modifier);
	result.rank = cache.rank;
	result.monthly_change = cache.monthly_change;
	return result;
}

/** Return the distance-adjusted score for a tile using the nearest town's cached curve. */
LandValueScore GetLandValueScore(TileIndex tile)
{
	return GetLandValueQueryResult(tile).distance_score;
}

/** Return the combined land-value modifier for a tile. */
LandValueModifier GetLandValueModifier(TileIndex)
{
	return LAND_VALUE_MODIFIER_BASE;
}

/** Return the final land-value score for a tile. */
LandValueScore GetFinalLandValueScore(TileIndex tile)
{
	return CombineLandValueScoreAndModifier(GetLandValueScore(tile), GetLandValueModifier(tile));
}

/** Initialize a town's persistent land-value score to the neutral base value. */
void InitializeTownLandValue(Town *town)
{
	assert(town != nullptr);
	town->land_value_score = LAND_VALUE_BASE.base();
}

/**
 * Initialize land values missing from an older savegame, or validate values loaded from a current savegame.
 * Valid stored values are preserved exactly.
 */
void InitializeLoadedTownLandValues(bool has_saved_land_value)
{
	for (Town *town : Town::Iterate()) {
		if (has_saved_land_value) {
			town->land_value_score = ClampLandValueScore(town->land_value_score).base();
		} else {
			InitializeTownLandValue(town);
		}
	}

	RebuildAllLandValueCaches();
}

/** Rebuild one town's derived land-value distance cache without changing its persistent score. */
void RebuildLandValueCache(Town *town)
{
	assert(town != nullptr);

	const uint32_t town_radius_squared = *std::max_element(
			std::begin(town->cache.squared_town_zone_radius), std::end(town->cache.squared_town_zone_radius));
	LandValueCache &cache = town->cache.land_value;
	cache.center_score = ClampLandValueScore(town->land_value_score);
	cache.distance_score = BuildLandValueDistanceScores(cache.center_score);
	cache.max_distance_squared = CalculateLandValueMaxDistanceSquared(
			town_radius_squared, _settings_game.economy.land_value_distance_scale);
}

/** Assign deterministic one-based ranks by descending score and ascending TownID. */
static void UpdateLandValueRanks()
{
	std::vector<Town *> towns;
	for (Town *town : Town::Iterate()) towns.push_back(town);

	std::sort(std::begin(towns), std::end(towns), [](const Town *a, const Town *b) {
		if (a->land_value_score != b->land_value_score) return a->land_value_score > b->land_value_score;
		return a->index < b->index;
	});

	uint32_t rank = 1;
	for (Town *town : towns) town->cache.land_value.rank = rank++;
}

/** Rebuild land-value caches for every town. */
void RebuildAllLandValueCaches()
{
	for (Town *town : Town::Iterate()) {
		RebuildLandValueCache(town);
	}
	UpdateLandValueRanks();
}

/** Update persistent scores once per economy month, then rebuild derived caches and ranks. */
void LandValueMonthlyLoop()
{
	if (!IsLandValueEnabled()) return;

	for (Town *town : Town::Iterate()) {
		const LandValueScore old_score = ClampLandValueScore(town->land_value_score);
		const LandValueScore target_score = CalculateLandValueTargetScore(
				town->cache.population, town->cache.num_houses, town->larger_town);
		const LandValueScore new_score = SmoothLandValueScore(
				old_score, target_score, _settings_game.economy.land_value_smoothing_percent);

		town->land_value_score = new_score.base();
		town->cache.land_value.monthly_change = static_cast<int32_t>(new_score.base()) - static_cast<int32_t>(old_score.base());
		RebuildLandValueCache(town);
	}

	UpdateLandValueRanks();
	InvalidateWindowClassesData(WindowClass::LandInfo);
	InvalidateWindowClassesData(WindowClass::TownView);
}
