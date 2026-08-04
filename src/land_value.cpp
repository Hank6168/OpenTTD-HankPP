/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file land_value.cpp Implementation of land-value calculations. */

#include "stdafx.h"

#include "core/math_func.hpp"
#include "clear_map.h"
#include "company_base.h"
#include "economy_func.h"
#include "genworld.h"
#include "house.h"
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

/** Clamp a development-demand value to its public fixed-point range. */
static uint16_t ClampTownDevelopmentDemand(int64_t demand)
{
	return static_cast<uint16_t>(std::clamp<int64_t>(demand, 0, TOWN_DEVELOPMENT_DEMAND_MAX));
}

/** Scale a full-strength factor's deviation from neutral by the configured influence. */
static uint16_t ScaleTownDevelopmentDemand(uint16_t full_demand, uint8_t influence_percent, bool enabled)
{
	if (!enabled || influence_percent == 0) return TOWN_DEVELOPMENT_DEMAND_NEUTRAL;

	const int64_t influence = std::min<uint32_t>(influence_percent, 100);
	const int64_t delta = static_cast<int64_t>(full_demand) - TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	return ClampTownDevelopmentDemand(TOWN_DEVELOPMENT_DEMAND_NEUTRAL + delta * influence / 100);
}

/** Return the frozen step curve value for a land-value score. */
static uint16_t GetTownDevelopmentCurveValue(LandValueScore score, const std::array<uint16_t, 7> &curve)
{
	const uint32_t value = ClampLandValueScore(score.base()).base();
	if (value < 150) return curve[0];
	if (value < 300) return curve[1];
	if (value < 600) return curve[2];
	if (value < 1200) return curve[3];
	if (value < 2500) return curve[4];
	if (value < 5000) return curve[5];
	return curve[6];
}

/** Apply a bounded, deliberately mild town-scale adjustment to a land-value tendency. */
static uint16_t ApplyTownDevelopmentMass(uint16_t land_tendency, TownDevelopmentMass mass)
{
	const int32_t bounded_mass = static_cast<int32_t>(std::min(mass.base(), TOWN_DEVELOPMENT_MASS_MAX.base()));
	const int32_t mass_adjustment = (bounded_mass - TOWN_DEVELOPMENT_DEMAND_NEUTRAL) / 3;
	return ClampTownDevelopmentDemand(static_cast<int64_t>(land_tendency) + mass_adjustment);
}

/** Calculate a stable, sub-linear development scale from already cached town data. */
TownDevelopmentMass CalculateTownDevelopmentMass(uint32_t population, uint32_t num_houses, bool larger_town)
{
	uint64_t mass = static_cast<uint64_t>(IntSqrt(population)) * 48;
	mass += static_cast<uint64_t>(IntSqrt(num_houses)) * 32;
	if (larger_town) mass += 1000;
	return TownDevelopmentMass{static_cast<uint32_t>(std::min<uint64_t>(mass, TOWN_DEVELOPMENT_MASS_MAX.base()))};
}

/** Calculate ordinary-development land affordability from the frozen land-value curve. */
uint16_t CalculateLandAffordability(LandValueScore score, uint8_t influence_percent, bool enabled)
{
	static constexpr std::array<uint16_t, 7> CURVE = {10500, 12500, 14000, 12000, 9000, 6500, 4000};
	return ScaleTownDevelopmentDemand(GetTownDevelopmentCurveValue(score, CURVE), influence_percent, enabled);
}

/** Calculate residential development demand without selecting or constructing houses. */
uint16_t CalculateResidentialDevelopmentDemand(TownDevelopmentMass mass, LandValueScore score, uint8_t influence_percent, bool enabled)
{
	static constexpr std::array<uint16_t, 7> CURVE = {8500, 11500, 13000, 12000, 10000, 8000, 6500};
	const uint16_t full_demand = ApplyTownDevelopmentMass(GetTownDevelopmentCurveValue(score, CURVE), mass);
	return ScaleTownDevelopmentDemand(full_demand, influence_percent, enabled);
}

/** Calculate commercial development demand without inspecting transport service or station data. */
uint16_t CalculateCommercialDevelopmentDemand(TownDevelopmentMass mass, LandValueScore score, uint8_t influence_percent, bool enabled)
{
	static constexpr std::array<uint16_t, 7> CURVE = {6000, 7500, 9500, 11500, 13500, 15000, 15000};
	const uint16_t full_demand = ApplyTownDevelopmentMass(GetTownDevelopmentCurveValue(score, CURVE), mass);
	return ScaleTownDevelopmentDemand(full_demand, influence_percent, enabled);
}

/** Calculate industrial development demand without generating or locating industries. */
uint16_t CalculateIndustrialDevelopmentDemand(TownDevelopmentMass mass, LandValueScore score, uint8_t influence_percent, bool enabled)
{
	static constexpr std::array<uint16_t, 7> CURVE = {8500, 12500, 13500, 11000, 7500, 4000, 2000};
	const uint16_t full_demand = ApplyTownDevelopmentMass(GetTownDevelopmentCurveValue(score, CURVE), mass);
	return ScaleTownDevelopmentDemand(full_demand, influence_percent, enabled);
}

/** Combine component demands using the frozen residential/commercial/industrial 5:3:2 weights. */
uint16_t CalculateOverallDevelopmentDemand(uint16_t residential, uint16_t commercial, uint16_t industrial, bool enabled)
{
	if (!enabled) return TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	const uint64_t weighted = static_cast<uint64_t>(std::min<uint16_t>(residential, TOWN_DEVELOPMENT_DEMAND_MAX)) * 5 +
			static_cast<uint64_t>(std::min<uint16_t>(commercial, TOWN_DEVELOPMENT_DEMAND_MAX)) * 3 +
			static_cast<uint64_t>(std::min<uint16_t>(industrial, TOWN_DEVELOPMENT_DEMAND_MAX)) * 2;
	return static_cast<uint16_t>(weighted / 10);
}

/** Calculate the complete authoritative derived demand cache for one town. */
TownDevelopmentDemandCache CalculateTownDevelopmentDemand(uint32_t population, uint32_t num_houses, bool larger_town,
		LandValueScore score, uint8_t influence_percent, bool enabled)
{
	TownDevelopmentDemandCache result{};
	result.mass = CalculateTownDevelopmentMass(population, num_houses, larger_town);
	result.active = enabled && influence_percent != 0;
	result.affordability = CalculateLandAffordability(score, influence_percent, enabled);
	result.residential_demand = CalculateResidentialDevelopmentDemand(result.mass, score, influence_percent, enabled);
	result.commercial_demand = CalculateCommercialDevelopmentDemand(result.mass, score, influence_percent, enabled);
	result.industrial_demand = CalculateIndustrialDevelopmentDemand(result.mass, score, influence_percent, enabled);
	result.overall_demand = CalculateOverallDevelopmentDemand(result.residential_demand, result.commercial_demand,
			result.industrial_demand, enabled);
	return result;
}

/** Map a demand index to a display-only level. */
TownDevelopmentDemandLevel GetTownDevelopmentDemandLevel(uint16_t demand)
{
	const uint16_t value = std::min<uint16_t>(demand, TOWN_DEVELOPMENT_DEMAND_MAX);
	if (value < 5000) return TownDevelopmentDemandLevel::VeryWeak;
	if (value < 8000) return TownDevelopmentDemandLevel::Weak;
	if (value < 12000) return TownDevelopmentDemandLevel::Average;
	if (value < 16000) return TownDevelopmentDemandLevel::Strong;
	return TownDevelopmentDemandLevel::VeryStrong;
}

/** Return an O(1), read-only copy of a town's demand cache with safe neutral disabled semantics. */
TownDevelopmentDemandCache GetTownDevelopmentDemand(const Town *town)
{
	if (town == nullptr) return {};
	TownDevelopmentDemandCache result = town->cache.development_demand;
	if (!IsLandValueEnabled() || _settings_game.economy.land_value_growth_percent == 0) {
		result.active = false;
		result.overall_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
		result.residential_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
		result.commercial_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
		result.industrial_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
		result.affordability = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	}
	return result;
}

/** Rebuild one town's NOSAVE development-demand cache without changing persistent state. */
void RebuildTownDevelopmentDemandCache(Town *town)
{
	assert(town != nullptr);
	town->cache.development_demand = CalculateTownDevelopmentDemand(town->cache.population, town->cache.num_houses,
			town->larger_town, ClampLandValueScore(town->land_value_score),
			_settings_game.economy.land_value_growth_percent, IsLandValueEnabled());
}

static std::vector<IntercityEconomicPair> _intercity_economic_pairs;

/** Calculate every independently bounded component of a town's potential intercity economic mass. */
TownEconomicMassBreakdown CalculateTownEconomicMassBreakdown(uint32_t population, uint32_t num_houses, bool larger_town,
		LandValueScore land_value_score, const TownDevelopmentDemandCache &demand)
{
	TownEconomicMassBreakdown result{};
	result.population_component = std::min<uint32_t>(IntSqrt(population) * 900, 300000);
	result.house_component = std::min<uint32_t>(IntSqrt(num_houses) * 650, 150000);
	result.development_component = std::min<uint32_t>(
			std::min(demand.mass.base(), TOWN_DEVELOPMENT_MASS_MAX.base()) * 8, 160000);
	result.land_value_component = std::min<uint32_t>(IntSqrt(ClampLandValueScore(land_value_score.base()).base()) * 800, 80000);

	const int32_t commercial = std::min<uint16_t>(demand.commercial_demand, TOWN_DEVELOPMENT_DEMAND_MAX);
	const int32_t industrial = std::min<uint16_t>(demand.industrial_demand, TOWN_DEVELOPMENT_DEMAND_MAX);
	const int32_t overall = std::min<uint16_t>(demand.overall_demand, TOWN_DEVELOPMENT_DEMAND_MAX);
	result.commercial_component = (commercial - TOWN_DEVELOPMENT_DEMAND_NEUTRAL) * 6;
	result.industrial_component = (industrial - TOWN_DEVELOPMENT_DEMAND_NEUTRAL) * 4;
	result.overall_component = (overall - TOWN_DEVELOPMENT_DEMAND_NEUTRAL) * 3;
	result.city_bonus = larger_town ? 70000 : 0;

	int64_t total = result.population_component;
	total += result.house_component;
	total += result.development_component;
	total += result.land_value_component;
	total += result.commercial_component;
	total += result.industrial_component;
	total += result.overall_component;
	total += result.city_bonus;
	result.total = TownEconomicMass{static_cast<uint32_t>(std::clamp<int64_t>(total, 0, TOWN_ECONOMIC_MASS_MAX.base()))};
	return result;
}

/** Return the bounded total from the authoritative economic-mass decomposition. */
TownEconomicMass CalculateTownEconomicMass(uint32_t population, uint32_t num_houses, bool larger_town,
		LandValueScore land_value_score, const TownDevelopmentDemandCache &demand)
{
	return CalculateTownEconomicMassBreakdown(population, num_houses, larger_town, land_value_score, demand).total;
}

/** Calculate a monotonic integer distance impedance with a safe non-zero base. */
uint32_t CalculateIntercityDistanceImpedance(uint32_t distance)
{
	const uint64_t impedance = 32 + static_cast<uint64_t>(distance) + static_cast<uint64_t>(distance) * distance / 256;
	return static_cast<uint32_t>(std::min<uint64_t>(impedance, std::numeric_limits<uint32_t>::max()));
}

/** Calculate a symmetric, saturating pair strength using frozen scale-before-multiply arithmetic. */
uint32_t CalculateIntercityPairStrength(TownEconomicMass mass_a, TownEconomicMass mass_b, uint32_t distance_impedance)
{
	const uint32_t bounded_a = std::min(mass_a.base(), TOWN_ECONOMIC_MASS_MAX.base());
	const uint32_t bounded_b = std::min(mass_b.base(), TOWN_ECONOMIC_MASS_MAX.base());
	if (bounded_a == 0 || bounded_b == 0) return 0;

	const uint64_t scaled_a = std::max<uint32_t>(1, bounded_a / 100);
	const uint64_t scaled_b = std::max<uint32_t>(1, bounded_b / 100);
	const uint64_t impedance = std::max<uint32_t>(distance_impedance, 1);
	const uint64_t strength = scaled_a * scaled_b * 1024 / impedance;
	return static_cast<uint32_t>(std::min<uint64_t>(strength, INTERCITY_PAIR_STRENGTH_MAX));
}

/** Compress a pair strength into a normalized potential-demand index, never actual passengers. */
IntercityPassengerDemand CalculateIntercityPassengerDemand(uint32_t pair_strength)
{
	const uint64_t bounded_strength = std::min(pair_strength, INTERCITY_PAIR_STRENGTH_MAX);
	const uint64_t demand = bounded_strength / 1000 + static_cast<uint64_t>(IntSqrt(bounded_strength)) * 20;
	return IntercityPassengerDemand{static_cast<uint32_t>(std::min<uint64_t>(demand, INTERCITY_PASSENGER_DEMAND_MAX.base()))};
}

/** Build one canonical TownID-ordered pair from already derived endpoint masses. */
IntercityEconomicPair CalculateIntercityEconomicPair(TownID town_a, TownID town_b, TownEconomicMass mass_a,
		TownEconomicMass mass_b, uint32_t distance)
{
	if (town_b < town_a) {
		std::swap(town_a, town_b);
		std::swap(mass_a, mass_b);
	}

	IntercityEconomicPair result{};
	result.town_a = town_a;
	result.town_b = town_b;
	result.mass_a = TownEconomicMass{std::min(mass_a.base(), TOWN_ECONOMIC_MASS_MAX.base())};
	result.mass_b = TownEconomicMass{std::min(mass_b.base(), TOWN_ECONOMIC_MASS_MAX.base())};
	result.distance = distance;
	result.distance_impedance = CalculateIntercityDistanceImpedance(distance);
	result.pair_strength = CalculateIntercityPairStrength(result.mass_a, result.mass_b, result.distance_impedance);
	result.potential_passenger_demand = CalculateIntercityPassengerDemand(result.pair_strength);
	return result;
}

/** Return the last globally rebuilt NOSAVE economic mass, or zero when unavailable or disabled. */
TownEconomicMass GetTownEconomicMass(const Town *town)
{
	if (!IsLandValueEnabled() || town == nullptr || !Town::IsValidID(town->index)) return TownEconomicMass{0};
	return town->cache.economic_mass;
}

/** Return all pairs between the deterministic Top-K endpoint candidates. */
std::span<const IntercityEconomicPair> GetIntercityEconomicPairs()
{
	return _intercity_economic_pairs;
}

/** Return at most the strongest 64 pairs without exposing a writable container. */
std::span<const IntercityEconomicPair> GetTopIntercityEconomicPairs()
{
	return std::span<const IntercityEconomicPair>{_intercity_economic_pairs}.first(
			std::min(_intercity_economic_pairs.size(), LAND_VALUE_INTERCITY_TOP_PAIRS));
}

/** Find a canonical pair while rejecting invalid or identical TownIDs. */
const IntercityEconomicPair *FindIntercityEconomicPair(TownID town_a, TownID town_b)
{
	if (town_a == town_b || !Town::IsValidID(town_a) || !Town::IsValidID(town_b)) return nullptr;
	if (town_b < town_a) std::swap(town_a, town_b);
	for (const IntercityEconomicPair &pair : _intercity_economic_pairs) {
		if (pair.town_a == town_a && pair.town_b == town_b) return &pair;
	}
	return nullptr;
}

/** Clear all global NOSAVE pair state, including before a TownPool bulk cleanup. */
void ClearIntercityEconomicGravityCache()
{
	_intercity_economic_pairs.clear();
}

/** Rebuild all economic masses and the bounded, deterministic Top-K pair cache exactly once. */
void RebuildIntercityEconomicGravityCache()
{
	ClearIntercityEconomicGravityCache();
	for (Town *town : Town::Iterate()) town->cache.economic_mass = TownEconomicMass{0};
	if (!IsLandValueEnabled()) return;

	struct Candidate {
		TownID town_id;
		TownEconomicMass mass;
		TileIndex xy;
	};
	std::vector<Candidate> candidates;
	candidates.reserve(Town::GetNumItems());

	for (Town *town : Town::Iterate()) {
		const TownDevelopmentDemandCache demand = GetTownDevelopmentDemand(town);
		town->cache.economic_mass = CalculateTownEconomicMass(town->cache.population, town->cache.num_houses,
				town->larger_town, ClampLandValueScore(town->land_value_score), demand);
		if (town->cache.economic_mass.base() != 0) candidates.push_back({town->index, town->cache.economic_mass, town->xy});
	}

	std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
		if (a.mass != b.mass) return a.mass > b.mass;
		return a.town_id < b.town_id;
	});
	if (candidates.size() > LAND_VALUE_INTERCITY_TOP_TOWNS) candidates.resize(LAND_VALUE_INTERCITY_TOP_TOWNS);

	_intercity_economic_pairs.reserve(candidates.size() * (candidates.size() - (candidates.empty() ? 0 : 1)) / 2);
	for (size_t i = 0; i < candidates.size(); ++i) {
		for (size_t j = i + 1; j < candidates.size(); ++j) {
			const Candidate &a = candidates[i];
			const Candidate &b = candidates[j];
			_intercity_economic_pairs.push_back(CalculateIntercityEconomicPair(
					a.town_id, b.town_id, a.mass, b.mass, DistanceManhattan(a.xy, b.xy)));
		}
	}

	std::sort(_intercity_economic_pairs.begin(), _intercity_economic_pairs.end(), [](const IntercityEconomicPair &a, const IntercityEconomicPair &b) {
		if (a.potential_passenger_demand != b.potential_passenger_demand) return a.potential_passenger_demand > b.potential_passenger_demand;
		if (a.pair_strength != b.pair_strength) return a.pair_strength > b.pair_strength;
		if (a.town_a != b.town_a) return a.town_a < b.town_a;
		return a.town_b < b.town_b;
	});
	uint32_t rank = 1;
	for (IntercityEconomicPair &pair : _intercity_economic_pairs) pair.rank = rank++;
}

/** Clamp a signed land-use factor to its public range. */
static uint16_t ClampLandUseFactor(int64_t factor)
{
	return static_cast<uint16_t>(std::clamp<int64_t>(factor, LAND_USE_FACTOR_MIN, LAND_USE_FACTOR_MAX));
}

/** Scale a full-strength land-use factor around neutral by the saved server setting. */
static uint16_t ScaleLandUseFactor(uint16_t full_factor, uint16_t influence_percent, bool active)
{
	if (!active || influence_percent == 0) return LAND_USE_FACTOR_NEUTRAL;
	const int64_t influence = std::min<uint32_t>(influence_percent, 200);
	const int64_t delta = static_cast<int64_t>(full_factor) - LAND_USE_FACTOR_NEUTRAL;
	return ClampLandUseFactor(LAND_USE_FACTOR_NEUTRAL + delta * influence / 100);
}

/** Convert population per occupied tile to the frozen five-band density profile. */
HouseDensityClass ClassifyHouseDensity(uint32_t population_per_tile)
{
	if (population_per_tile < 8) return HouseDensityClass::VeryLow;
	if (population_per_tile < 20) return HouseDensityClass::Low;
	if (population_per_tile < 40) return HouseDensityClass::Medium;
	if (population_per_tile < 80) return HouseDensityClass::High;
	return HouseDensityClass::VeryHigh;
}

/** Derive a conservative, use-neutral profile without modifying HouseSpec or NewGRF state. */
HouseLandUseProfile CalculateHouseLandUseProfile(uint32_t population, uint8_t tile_count, uint8_t minimum_zone, bool special_building)
{
	HouseLandUseProfile result{};
	result.tile_count = std::max<uint8_t>(tile_count, 1);
	result.minimum_zone = std::min<uint8_t>(minimum_zone, 4);
	result.population_per_tile = ClampTo<uint16_t>(population / result.tile_count);
	result.density_class = ClassifyHouseDensity(result.population_per_tile);
	result.valid = true;
	result.special_building = special_building || population == 0;
	result.residential_like = !result.special_building && result.minimum_zone <= 2 &&
			result.density_class <= HouseDensityClass::Medium;
	/* HouseSpec has no stable residential/commercial purpose field across NewGRFs. */
	result.commercial_like = false;
	return result;
}

/** Derive a profile directly from the authoritative HouseSpec registry. */
HouseLandUseProfile GetHouseLandUseProfile(HouseID house)
{
	if (static_cast<size_t>(house) >= HouseSpec::Specs().size()) return {};
	const HouseSpec *hs = HouseSpec::Get(house);
	if (!hs->building_flags.Any(BUILDING_HAS_1_TILE)) return {};

	uint8_t tile_count = 1;
	if (hs->building_flags.Test(BuildingFlag::Size2x2)) {
		tile_count = 4;
	} else if (hs->building_flags.Any(BUILDING_HAS_2_TILES)) {
		tile_count = 2;
	}

	uint8_t minimum_zone = 0;
	bool has_zone = false;
	for (HouseZone zone = HouseZone::TownEdge; zone < HouseZone::TownEnd; zone++) {
		if (!hs->building_availability.Test(zone)) continue;
		minimum_zone = to_underlying(zone);
		has_zone = true;
		break;
	}
	if (!has_zone) return {};

	const bool special = hs->building_flags.Any({BuildingFlag::IsChurch, BuildingFlag::IsStadium});
	return CalculateHouseLandUseProfile(hs->population, tile_count, minimum_zone, special);
}

/** Build the local, read-only context once for one house-selection attempt. */
LandUseDevelopmentContext GetLandUseDevelopmentContext(const Town *town, TileIndex tile, uint8_t town_zone, bool selection_context)
{
	LandUseDevelopmentContext result{};
	result.town_zone = std::min<uint8_t>(town_zone, 4);
	result.density_influence_percent = std::min<uint16_t>(_settings_game.economy.land_value_density_percent, 200);
	if (town == nullptr || !IsValidTile(tile)) return result;

	const TownDevelopmentDemandCache demand = GetTownDevelopmentDemand(town);
	result.final_score = GetFinalLandValueScore(tile);
	result.town_mass = demand.mass;
	result.residential_demand = demand.residential_demand;
	result.commercial_demand = demand.commercial_demand;
	result.affordability = demand.affordability;
	result.town_population = town->cache.population;
	result.larger_town = town->larger_town;
	result.active = selection_context && IsLandValueEnabled() && result.density_influence_percent != 0;
	return result;
}

/** Calculate the bounded local density tendency before or after setting-strength scaling. */
uint16_t CalculateHouseDensityDemand(const LandUseDevelopmentContext &context, bool scaled)
{
	if (!context.active) return LAND_USE_FACTOR_NEUTRAL;

	static constexpr std::array<uint16_t, 7> SCORE_CURVE = {7000, 8500, 10000, 11500, 13500, 15000, 16000};
	const int64_t score_base = GetTownDevelopmentCurveValue(context.final_score, SCORE_CURVE);
	static constexpr std::array<int16_t, 5> ZONE_ADJUSTMENT = {-600, -300, 0, 400, 800};
	const int64_t mass_adjustment = std::clamp<int64_t>((static_cast<int64_t>(context.town_mass.base()) - 10000) / 5, -1500, 2000);
	const int64_t commercial_adjustment = std::clamp<int64_t>((static_cast<int64_t>(context.commercial_demand) - 10000) / 4, -750, 1500);
	const int64_t affordability_adjustment = std::clamp<int64_t>((10000 - static_cast<int64_t>(context.affordability)) / 5, -1000, 1200);

	int64_t full = score_base + ZONE_ADJUSTMENT[context.town_zone] + mass_adjustment + commercial_adjustment + affordability_adjustment;
	uint16_t town_cap = LAND_USE_FACTOR_MAX;
	if (context.town_mass.base() < 3000) {
		town_cap = 11000;
	} else if (context.town_mass.base() < 6000) {
		town_cap = 12500;
	} else if (context.town_mass.base() < 10000) {
		town_cap = 14000;
	}
	full = std::clamp<int64_t>(full, LAND_USE_FACTOR_MIN, town_cap);
	const uint16_t full_factor = static_cast<uint16_t>(full);
	return scaled ? ScaleLandUseFactor(full_factor, context.density_influence_percent, true) : full_factor;
}

/** Calculate the conservative ordinary-residential siting tendency for one profile. */
uint16_t CalculateResidentialHouseWeightModifier(const LandUseDevelopmentContext &context, const HouseLandUseProfile &profile)
{
	if (!context.active || !profile.valid || !profile.residential_like) return LAND_USE_FACTOR_NEUTRAL;

	const uint32_t score = ClampLandValueScore(context.final_score.base()).base();
	int64_t siting_adjustment = 0;
	if (score < 150) {
		siting_adjustment = context.town_mass.base() >= 10000 ? 300 : -300;
	} else if (score < 600) {
		siting_adjustment = context.town_mass.base() >= 10000 ? 1000 : (context.town_mass.base() >= 3000 ? 200 : 0);
	} else if (score < 1200) {
		siting_adjustment = context.town_mass.base() >= 10000 ? 500 : 0;
	} else if (score < 2500) {
		siting_adjustment = profile.density_class <= HouseDensityClass::Low ? -500 : 0;
	} else if (score < 5000) {
		siting_adjustment = profile.density_class <= HouseDensityClass::Low ? -2000 : -750;
	} else {
		siting_adjustment = profile.density_class <= HouseDensityClass::Low ? -3500 : -1500;
	}

	const int64_t demand_adjustment = std::clamp<int64_t>(static_cast<int64_t>(context.residential_demand) - 10000, -2500, 2500);
	const int64_t affordability_adjustment = std::clamp<int64_t>((static_cast<int64_t>(context.affordability) - 10000) / 2, -2000, 2000);
	const uint16_t full = ClampLandUseFactor(10000 + siting_adjustment + demand_adjustment + affordability_adjustment);
	return ScaleLandUseFactor(full, context.density_influence_percent, true);
}

/** Convert the local density target to a profile-specific relative weight. */
uint16_t CalculateDensityHouseWeightModifier(uint16_t density_factor, const HouseLandUseProfile &profile)
{
	if (!profile.valid || profile.special_building) return LAND_USE_FACTOR_NEUTRAL;
	const int64_t delta = static_cast<int64_t>(ClampLandUseFactor(density_factor)) - LAND_USE_FACTOR_NEUTRAL;
	int64_t adjusted_delta = 0;
	switch (profile.density_class) {
		case HouseDensityClass::VeryLow: adjusted_delta = -delta; break;
		case HouseDensityClass::Low: adjusted_delta = -delta * 3 / 4; break;
		case HouseDensityClass::Medium: adjusted_delta = delta / 4; break;
		case HouseDensityClass::High: adjusted_delta = delta * 3 / 4; break;
		case HouseDensityClass::VeryHigh: adjusted_delta = delta; break;
	}
	return ClampLandUseFactor(LAND_USE_FACTOR_NEUTRAL + adjusted_delta);
}

/** Combine residential siting and density matching into one bounded candidate modifier. */
uint16_t CalculateHouseLandUseWeightModifier(const LandUseDevelopmentContext &context, const HouseLandUseProfile &profile)
{
	if (!context.active || !profile.valid || profile.special_building) return LAND_USE_FACTOR_NEUTRAL;
	const uint16_t residential = CalculateResidentialHouseWeightModifier(context, profile);
	const uint16_t density = CalculateDensityHouseWeightModifier(CalculateHouseDensityDemand(context), profile);
	const uint64_t combined = static_cast<uint64_t>(residential) * density / LAND_USE_FACTOR_NEUTRAL;
	if (context.town_mass.base() < 3000 && ClampLandValueScore(context.final_score.base()).base() < 150 && profile.residential_like) {
		return static_cast<uint16_t>(std::min<uint64_t>(combined, LAND_USE_FACTOR_NEUTRAL));
	}
	return ClampLandUseFactor(combined);
}

/** Apply a bounded modifier to an existing probability without overflow or zeroing a legal candidate. */
uint32_t CalculateAdjustedHouseCandidateWeight(uint32_t original_weight, uint16_t land_use_modifier)
{
	if (original_weight == 0) return 0;
	const uint64_t adjusted = static_cast<uint64_t>(original_weight) * ClampLandUseFactor(land_use_modifier) / LAND_USE_FACTOR_NEUTRAL;
	return static_cast<uint32_t>(std::clamp<uint64_t>(adjusted, 1, std::numeric_limits<uint32_t>::max()));
}

/** Calculate a non-negative, saturating land-purchase surcharge using fixed-point integer arithmetic. */
Money CalculateLandPurchaseSurcharge(Money base_land_unit, LandValueScore final_score, uint16_t purchase_percent, bool enabled)
{
	if (!enabled || base_land_unit <= 0 || purchase_percent == 0) return 0;

	const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
	uint64_t surcharge = ApplyLandValueScore(static_cast<uint64_t>(base_land_unit.base()), final_score, limit);
	surcharge = ScaleValue(surcharge, purchase_percent, 100, limit);
	return Money{static_cast<int64_t>(surcharge)};
}

/** Return the existing, inflation-adjusted minimal landscape-clearing unit used as the land-price base. */
Money GetLandPurchaseBaseUnit()
{
	return std::max<Money>(_price[Price::ClearGrass], 0);
}

/** Return whether LandscapeClear would perform a meaningful change rather than clear an already bare tile. */
bool IsLandPurchaseClearRequired(TileIndex tile)
{
	if (!IsValidTile(tile)) return false;
	return !IsTileType(tile, TileType::Clear) || GetClearGround(tile) != ClearGround::Grass || GetClearDensity(tile) != 0 || IsSnowTile(tile);
}

/** Return a deterministic, read-only decomposition of a land operation. */
LandPurchaseCostBreakdown GetLandPurchaseCostBreakdown(TileIndex tile, Money original_cost, DoCommandFlags flags, bool force_purchase)
{
	LandPurchaseCostBreakdown result{};
	result.original_cost = original_cost;
	result.total_cost = original_cost;
	result.enabled = IsLandValueEnabled();
	result.purchase_percent = _settings_game.economy.land_value_purchase_percent;
	result.base_land_unit = GetLandPurchaseBaseUnit();
	result.clear_required = IsLandPurchaseClearRequired(tile);

	if (!IsValidTile(tile)) {
		result.status = LandPurchaseCostStatus::InvalidTile;
		return result;
	}

	result.final_score = GetFinalLandValueScore(tile);
	if (!result.enabled) {
		result.status = LandPurchaseCostStatus::Disabled;
	} else if (result.purchase_percent == 0) {
		result.status = LandPurchaseCostStatus::ZeroPercent;
	} else if (_game_mode == GameMode::Editor) {
		result.status = LandPurchaseCostStatus::Editor;
	} else if (_generating_world) {
		result.status = LandPurchaseCostStatus::WorldGeneration;
	} else if (flags.Test(DoCommandFlag::Town)) {
		result.status = LandPurchaseCostStatus::TownOperation;
	} else if (flags.Test(DoCommandFlag::Bankrupt)) {
		result.status = LandPurchaseCostStatus::Bankruptcy;
	} else if (!Company::IsValidID(_current_company)) {
		result.status = LandPurchaseCostStatus::NoCompany;
	} else if (!result.clear_required && !force_purchase) {
		result.status = LandPurchaseCostStatus::NoClearRequired;
	} else {
		result.status = LandPurchaseCostStatus::Chargeable;
		result.land_value_surcharge = CalculateLandPurchaseSurcharge(result.base_land_unit, result.final_score, result.purchase_percent, true);
		result.total_cost += result.land_value_surcharge;
	}

	return result;
}

/** Split a command total which already contains the surcharge using the same core calculation. */
LandPurchaseCostBreakdown GetLandPurchaseCostBreakdownFromTotal(TileIndex tile, Money total_cost, DoCommandFlags flags, bool force_purchase)
{
	LandPurchaseCostBreakdown result = GetLandPurchaseCostBreakdown(tile, 0, flags, force_purchase);
	result.total_cost = total_cost;
	result.original_cost = total_cost - result.land_value_surcharge;
	return result;
}

/** Return a stable debug name for a land-purchase charging status. */
const char *GetLandPurchaseCostStatusName(LandPurchaseCostStatus status)
{
	switch (status) {
		case LandPurchaseCostStatus::Chargeable:      return "chargeable";
		case LandPurchaseCostStatus::InvalidTile:     return "invalid_tile";
		case LandPurchaseCostStatus::Disabled:        return "disabled";
		case LandPurchaseCostStatus::ZeroPercent:     return "zero_percent";
		case LandPurchaseCostStatus::NoCompany:       return "no_company";
		case LandPurchaseCostStatus::Editor:          return "editor";
		case LandPurchaseCostStatus::WorldGeneration: return "world_generation";
		case LandPurchaseCostStatus::TownOperation:   return "town_operation";
		case LandPurchaseCostStatus::Bankruptcy:      return "bankruptcy";
		case LandPurchaseCostStatus::NoClearRequired: return "no_clear_required";
	}
	NOT_REACHED();
}

/** Calculate a non-negative, saturating infrastructure surcharge in the frozen score-percent-units order. */
Money CalculateLandInfrastructureSurcharge(Money base_unit, LandValueScore final_score, uint16_t infrastructure_percent, uint32_t units, bool enabled)
{
	if (!enabled || base_unit <= 0 || infrastructure_percent == 0 || units == 0) return 0;

	const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
	uint64_t surcharge = ApplyLandValueScore(static_cast<uint64_t>(base_unit.base()), final_score, limit);
	surcharge = ScaleValue(surcharge, infrastructure_percent, 100, limit);
	if (surcharge == 0) return 0;
	surcharge = units > limit / surcharge ? limit : surcharge * units;
	return Money{static_cast<int64_t>(surcharge)};
}

/** Return a conservative inflation-adjusted land unit without coupling to NewGRF construction prices. */
Money GetLandInfrastructureBaseUnit(LandInfrastructureType type)
{
	const Money clear_grass = std::max<Money>(_price[Price::ClearGrass], 0);
	switch (type) {
		case LandInfrastructureType::Rail:        return clear_grass * 2;
		case LandInfrastructureType::Road:        return clear_grass;
		case LandInfrastructureType::RailStation: return clear_grass * 3;
		case LandInfrastructureType::RoadStop:    return clear_grass * 2;
	}
	NOT_REACHED();
}

/** Return a deterministic, read-only infrastructure land-cost breakdown. */
LandInfrastructureCostBreakdown GetLandInfrastructureCostBreakdown(TileIndex tile, LandInfrastructureType type, uint32_t units, DoCommandFlags flags)
{
	LandInfrastructureCostBreakdown result{};
	result.type = type;
	result.units = units;
	result.enabled = IsLandValueEnabled();
	result.infrastructure_percent = _settings_game.economy.land_value_infrastructure_percent;
	result.base_unit = GetLandInfrastructureBaseUnit(type);

	if (!IsValidTile(tile)) {
		result.status = LandInfrastructureCostStatus::InvalidTile;
		return result;
	}
	result.final_score = GetFinalLandValueScore(tile);
	if (!result.enabled) {
		result.status = LandInfrastructureCostStatus::Disabled;
	} else if (result.infrastructure_percent == 0) {
		result.status = LandInfrastructureCostStatus::ZeroPercent;
	} else if (units == 0) {
		result.status = LandInfrastructureCostStatus::ZeroUnits;
	} else if (_game_mode == GameMode::Editor) {
		result.status = LandInfrastructureCostStatus::Editor;
	} else if (_generating_world) {
		result.status = LandInfrastructureCostStatus::WorldGeneration;
	} else if (flags.Test(DoCommandFlag::Town)) {
		result.status = LandInfrastructureCostStatus::TownOperation;
	} else if (flags.Test(DoCommandFlag::Bankrupt)) {
		result.status = LandInfrastructureCostStatus::Bankruptcy;
	} else if (!Company::IsValidID(_current_company)) {
		result.status = LandInfrastructureCostStatus::NoCompany;
	} else {
		result.status = LandInfrastructureCostStatus::Chargeable;
		result.land_value_surcharge = CalculateLandInfrastructureSurcharge(result.base_unit, result.final_score, result.infrastructure_percent, units, true);
	}
	return result;
}

const char *GetLandInfrastructureTypeName(LandInfrastructureType type)
{
	switch (type) {
		case LandInfrastructureType::Rail:        return "rail";
		case LandInfrastructureType::Road:        return "road";
		case LandInfrastructureType::RailStation: return "rail_station";
		case LandInfrastructureType::RoadStop:    return "road_stop";
	}
	NOT_REACHED();
}

const char *GetLandInfrastructureCostStatusName(LandInfrastructureCostStatus status)
{
	switch (status) {
		case LandInfrastructureCostStatus::Chargeable:      return "chargeable";
		case LandInfrastructureCostStatus::InvalidTile:     return "invalid_tile";
		case LandInfrastructureCostStatus::Disabled:        return "disabled";
		case LandInfrastructureCostStatus::ZeroPercent:     return "zero_percent";
		case LandInfrastructureCostStatus::ZeroUnits:       return "zero_units";
		case LandInfrastructureCostStatus::NoCompany:       return "no_company";
		case LandInfrastructureCostStatus::Editor:          return "editor";
		case LandInfrastructureCostStatus::WorldGeneration: return "world_generation";
		case LandInfrastructureCostStatus::TownOperation:   return "town_operation";
		case LandInfrastructureCostStatus::Bankruptcy:      return "bankruptcy";
	}
	NOT_REACHED();
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
	RebuildTownDevelopmentDemandCache(town);
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
	RebuildIntercityEconomicGravityCache();
}

/** Update persistent scores once per economy month, then rebuild derived caches and ranks. */
void LandValueMonthlyLoop()
{
	if (!IsLandValueEnabled()) {
		RebuildAllLandValueCaches();
		InvalidateWindowClassesData(WindowClass::LandInfo);
		InvalidateWindowClassesData(WindowClass::TownView);
		return;
	}

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
	RebuildIntercityEconomicGravityCache();
	InvalidateWindowClassesData(WindowClass::LandInfo);
	InvalidateWindowClassesData(WindowClass::TownView);
}
