/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file land_value.h Functions related to land value. */

#ifndef LAND_VALUE_H
#define LAND_VALUE_H

#include "land_value_type.h"
#include "command_type.h"
#include "economy_type.h"
#include "tile_type.h"
#include "town_type.h"

#include <limits>
#include <optional>

struct Town;

LandValueScore ClampLandValueScore(uint64_t score);
LandValueModifier ClampLandValueModifier(uint64_t modifier);

uint64_t ApplyLandValueScore(uint64_t value, LandValueScore score, uint64_t limit = std::numeric_limits<uint64_t>::max());
uint64_t ApplyLandValueModifier(uint64_t value, LandValueModifier modifier, uint64_t limit = std::numeric_limits<uint64_t>::max());
LandValueScore CombineLandValueScoreAndModifier(LandValueScore score, LandValueModifier modifier);

LandValueScore CalculateLandValueTargetScore(uint32_t population, uint32_t num_houses, bool is_city);
LandValueScore SmoothLandValueScore(LandValueScore old_score, LandValueScore target_score, uint8_t smoothing_percent);
LandValueDistanceScores BuildLandValueDistanceScores(LandValueScore center_score);
uint32_t CalculateLandValueMaxDistanceSquared(uint32_t town_radius_squared, uint16_t distance_scale_percent = 100);

uint8_t GetLandValueDistanceBand(uint32_t distance_squared, uint32_t max_distance_squared);

/** Player-facing classification of a land-value score. This is display-only and has no gameplay authority. */
enum class LandValueLevel : uint8_t {
	VeryLow,
	Low,
	Average,
	AboveAverage,
	High,
	VeryHigh,
	Core,
};

LandValueLevel GetLandValueLevel(LandValueScore score);
uint32_t CalculateLandValueInfluenceRadius(uint32_t max_distance_squared);

/** Complete read-only result of querying land value for one tile. */
struct LandValueQueryResult {
	TownID town_id = TownID::Invalid();
	LandValueScore town_score = LAND_VALUE_BASE;
	LandValueScore center_score = LAND_VALUE_BASE;
	uint32_t max_distance_squared = 0;
	uint32_t distance_squared = 0;
	uint8_t distance_band = 0;
	LandValueScore distance_score = LAND_VALUE_BASE;
	LandValueModifier modifier = LAND_VALUE_MODIFIER_BASE;
	LandValueScore final_score = LAND_VALUE_BASE;
	uint32_t rank = 0;
	int32_t monthly_change = 0;
	bool enabled = false;
};

bool IsLandValueEnabled();
std::optional<TileIndex> ResolveLandValueTileIndex(uint64_t raw_tile);
std::optional<TileIndex> ResolveLandValueTileCoordinates(uint64_t raw_x, uint64_t raw_y);
LandValueQueryResult GetLandValueQueryResult(TileIndex tile);
LandValueScore GetLandValueScore(TileIndex tile);
LandValueModifier GetLandValueModifier(TileIndex tile);
LandValueScore GetFinalLandValueScore(TileIndex tile);

/** Reason why a land-purchase surcharge is not applied. */
enum class LandPurchaseCostStatus : uint8_t {
	Chargeable,
	InvalidTile,
	Disabled,
	ZeroPercent,
	NoCompany,
	Editor,
	WorldGeneration,
	TownOperation,
	Bankruptcy,
	NoClearRequired,
};

/** Read-only decomposition of an existing land operation and its land-value surcharge. */
struct LandPurchaseCostBreakdown {
	Money original_cost = 0;
	Money base_land_unit = 0;
	Money land_value_surcharge = 0;
	Money total_cost = 0;
	LandValueScore final_score = LAND_VALUE_BASE;
	uint16_t purchase_percent = 0;
	LandPurchaseCostStatus status = LandPurchaseCostStatus::InvalidTile;
	bool enabled = false;
	bool clear_required = false;

	bool IsChargeable() const { return this->status == LandPurchaseCostStatus::Chargeable; }
};

Money CalculateLandPurchaseSurcharge(Money base_land_unit, LandValueScore final_score, uint16_t purchase_percent, bool enabled);
Money GetLandPurchaseBaseUnit();
bool IsLandPurchaseClearRequired(TileIndex tile);
LandPurchaseCostBreakdown GetLandPurchaseCostBreakdown(TileIndex tile, Money original_cost, DoCommandFlags flags = {}, bool force_purchase = false);
LandPurchaseCostBreakdown GetLandPurchaseCostBreakdownFromTotal(TileIndex tile, Money total_cost, DoCommandFlags flags = {}, bool force_purchase = false);
const char *GetLandPurchaseCostStatusName(LandPurchaseCostStatus status);

void InitializeTownLandValue(Town *town);
void InitializeLoadedTownLandValues(bool has_saved_land_value);
void RebuildLandValueCache(Town *town);
void RebuildAllLandValueCaches();
void LandValueMonthlyLoop();

#endif /* LAND_VALUE_H */
