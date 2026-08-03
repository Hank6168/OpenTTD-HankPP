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
#include "tile_type.h"

#include <limits>

struct Town;

LandValueScore ClampLandValueScore(uint64_t score);
LandValueModifier ClampLandValueModifier(uint64_t modifier);

uint64_t ApplyLandValueScore(uint64_t value, LandValueScore score, uint64_t limit = std::numeric_limits<uint64_t>::max());
uint64_t ApplyLandValueModifier(uint64_t value, LandValueModifier modifier, uint64_t limit = std::numeric_limits<uint64_t>::max());
LandValueScore CombineLandValueScoreAndModifier(LandValueScore score, LandValueModifier modifier);

LandValueScore CalculateLandValueTargetScore(uint32_t population, uint32_t num_houses, bool is_city);
LandValueScore SmoothLandValueScore(LandValueScore old_score, LandValueScore target_score, uint8_t smoothing_percent);
LandValueDistanceScores BuildLandValueDistanceScores(LandValueScore center_score);
uint32_t CalculateLandValueMaxDistanceSquared(uint32_t town_radius_squared);

uint8_t GetLandValueDistanceBand(uint32_t distance_squared, uint32_t max_distance_squared);

LandValueScore GetLandValueScore(TileIndex tile);
LandValueModifier GetLandValueModifier(TileIndex tile);
LandValueScore GetFinalLandValueScore(TileIndex tile);

void InitializeTownLandValue(Town *town);
void InitializeLoadedTownLandValues(bool has_saved_land_value);
void RebuildLandValueCache(Town *town);
void RebuildAllLandValueCaches();
void LandValueMonthlyLoop();

#endif /* LAND_VALUE_H */
