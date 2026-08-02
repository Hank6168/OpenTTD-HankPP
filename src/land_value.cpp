/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file land_value.cpp Implementation of land-value calculations. */

#include "stdafx.h"

#include "land_value.h"
#include "town.h"

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
 * Map a squared distance to one of the cached distance bands.
 * Distances at or beyond the maximum are assigned to the final band.
 */
uint8_t GetLandValueDistanceBand(uint32_t distance_squared, uint32_t max_distance_squared)
{
	if (max_distance_squared == 0 || distance_squared == 0) return 0;
	if (distance_squared >= max_distance_squared) return LAND_VALUE_DISTANCE_BAND_COUNT - 1;

	return static_cast<uint8_t>(static_cast<uint64_t>(distance_squared) * (LAND_VALUE_DISTANCE_BAND_COUNT - 1) / max_distance_squared);
}

/** Return the distance-adjusted score for a tile. */
LandValueScore GetLandValueScore(TileIndex)
{
	return LAND_VALUE_BASE;
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
}
