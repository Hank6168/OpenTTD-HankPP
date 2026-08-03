/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file land_value_type.h Types related to land value. */

#ifndef LAND_VALUE_TYPE_H
#define LAND_VALUE_TYPE_H

#include "core/strong_typedef_type.hpp"

#include <array>

/** A land-value score, where 100 represents the base land value. */
struct LandValueScoreTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare> {};
using LandValueScore = StrongType::Typedef<LandValueScoreTag>;

/** A land-value modifier expressed in basis points, where 10000 represents 100%. */
struct LandValueModifierTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare> {};
using LandValueModifier = StrongType::Typedef<LandValueModifierTag>;

static constexpr LandValueScore LAND_VALUE_MIN{0};       ///< Minimum valid land-value score.
static constexpr LandValueScore LAND_VALUE_BASE{100};    ///< Base land-value score (1x base price).
static constexpr LandValueScore LAND_VALUE_MAX{10000};   ///< Maximum valid land-value score (100x base price).

static constexpr LandValueModifier LAND_VALUE_MODIFIER_MIN{0};      ///< Minimum valid modifier (0%).
static constexpr LandValueModifier LAND_VALUE_MODIFIER_BASE{10000}; ///< Neutral modifier (100%).
static constexpr LandValueModifier LAND_VALUE_MODIFIER_MAX{40000};  ///< Maximum valid modifier (400%).

static constexpr uint8_t LAND_VALUE_DISTANCE_BAND_COUNT = 64; ///< Number of cached distance bands.

using LandValueDistanceScores = std::array<LandValueScore, LAND_VALUE_DISTANCE_BAND_COUNT>;

/** A bounded, sub-linear measure of a town's existing development scale. */
struct TownDevelopmentMassTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare> {};
using TownDevelopmentMass = StrongType::Typedef<TownDevelopmentMassTag>;

static constexpr uint16_t TOWN_DEVELOPMENT_DEMAND_NEUTRAL = 10000; ///< Neutral demand (100%).
static constexpr uint16_t TOWN_DEVELOPMENT_DEMAND_MAX = 20000;     ///< Maximum demand (200%).
static constexpr TownDevelopmentMass TOWN_DEVELOPMENT_MASS_MAX{20000};

/** In-memory derived development demand for a town. This structure is never saved. */
struct TownDevelopmentDemandCache {
	TownDevelopmentMass mass{0};
	uint16_t overall_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	uint16_t residential_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	uint16_t commercial_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	uint16_t industrial_demand = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	uint16_t affordability = TOWN_DEVELOPMENT_DEMAND_NEUTRAL;
	bool active = false;
};

/** In-memory derived land-value data for a town. */
struct LandValueCache {
	LandValueDistanceScores distance_score = [] {
		LandValueDistanceScores scores{};
		scores.fill(LAND_VALUE_BASE);
		return scores;
	}();
	LandValueScore center_score = LAND_VALUE_BASE;
	uint32_t rank = 0;
	int32_t monthly_change = 0;
	uint32_t max_distance_squared = 0;
};

#endif /* LAND_VALUE_TYPE_H */
