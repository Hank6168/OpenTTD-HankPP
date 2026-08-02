/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file land_value.cpp Test land-value core calculations. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../land_value.h"
#include "../town.h"
#include "../sl/extended_ver_sl.h"

#include "../safeguards.h"

TEST_CASE("Land value scales remain distinct")
{
	CHECK(LAND_VALUE_BASE.base() == 100);
	CHECK(LAND_VALUE_MAX.base() == 10000);
	CHECK(LAND_VALUE_MODIFIER_BASE.base() == 10000);
	CHECK(LAND_VALUE_MODIFIER_MAX.base() == 40000);
}

TEST_CASE("Land value inputs are clamped")
{
	CHECK(ClampLandValueScore(0) == LAND_VALUE_MIN);
	CHECK(ClampLandValueScore(100) == LAND_VALUE_BASE);
	CHECK(ClampLandValueScore(10000) == LAND_VALUE_MAX);
	CHECK(ClampLandValueScore(10001) == LAND_VALUE_MAX);
	CHECK(ClampLandValueScore(std::numeric_limits<uint64_t>::max()) == LAND_VALUE_MAX);

	CHECK(ClampLandValueModifier(0) == LAND_VALUE_MODIFIER_MIN);
	CHECK(ClampLandValueModifier(10000) == LAND_VALUE_MODIFIER_BASE);
	CHECK(ClampLandValueModifier(40000) == LAND_VALUE_MODIFIER_MAX);
	CHECK(ClampLandValueModifier(40001) == LAND_VALUE_MODIFIER_MAX);
	CHECK(ClampLandValueModifier(std::numeric_limits<uint64_t>::max()) == LAND_VALUE_MODIFIER_MAX);
}

TEST_CASE("Land value scaling uses integer fixed point")
{
	CHECK(ApplyLandValueScore(12345, LAND_VALUE_MIN) == 0);
	CHECK(ApplyLandValueScore(12345, LAND_VALUE_BASE) == 12345);
	CHECK(ApplyLandValueScore(12345, LandValueScore{250}) == 30862);
	CHECK(ApplyLandValueScore(12345, LAND_VALUE_MAX) == 1234500);

	CHECK(ApplyLandValueModifier(12345, LAND_VALUE_MODIFIER_MIN) == 0);
	CHECK(ApplyLandValueModifier(12345, LAND_VALUE_MODIFIER_BASE) == 12345);
	CHECK(ApplyLandValueModifier(12345, LandValueModifier{16000}) == 19752);
}

TEST_CASE("Land value scaling saturates without intermediate overflow")
{
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();

	CHECK(ApplyLandValueScore(maximum, LAND_VALUE_BASE) == maximum);
	CHECK(ApplyLandValueScore(maximum, LAND_VALUE_MAX) == maximum);
	CHECK(ApplyLandValueModifier(maximum, LAND_VALUE_MODIFIER_BASE) == maximum);
	CHECK(ApplyLandValueModifier(maximum, LAND_VALUE_MODIFIER_MAX) == maximum);
	CHECK(ApplyLandValueScore(1000, LAND_VALUE_MAX, 50000) == 50000);
	CHECK(ApplyLandValueModifier(1000, LAND_VALUE_MODIFIER_MAX, 2500) == 2500);
}

TEST_CASE("Land value score and modifier are combined safely")
{
	CHECK(CombineLandValueScoreAndModifier(LAND_VALUE_BASE, LAND_VALUE_MODIFIER_BASE) == LAND_VALUE_BASE);
	CHECK(CombineLandValueScoreAndModifier(LandValueScore{5000}, LandValueModifier{16000}) == LandValueScore{8000});
	CHECK(CombineLandValueScoreAndModifier(LAND_VALUE_MAX, LAND_VALUE_MODIFIER_MAX) == LAND_VALUE_MAX);
	CHECK(CombineLandValueScoreAndModifier(LAND_VALUE_MAX, LAND_VALUE_MODIFIER_MIN) == LAND_VALUE_MIN);
}

TEST_CASE("Land value distances map to stable cache bands")
{
	CHECK(GetLandValueDistanceBand(0, 0) == 0);
	CHECK(GetLandValueDistanceBand(100, 0) == 0);
	CHECK(GetLandValueDistanceBand(0, 100) == 0);
	CHECK(GetLandValueDistanceBand(50, 100) == 31);
	CHECK(GetLandValueDistanceBand(99, 100) == 62);
	CHECK(GetLandValueDistanceBand(100, 100) == 63);
	CHECK(GetLandValueDistanceBand(101, 100) == 63);
	CHECK(GetLandValueDistanceBand(std::numeric_limits<uint32_t>::max() - 1, std::numeric_limits<uint32_t>::max()) == 62);
}

TEST_CASE("Land value tile queries are neutral before cache integration")
{
	const TileIndex tile{0};

	CHECK(GetLandValueScore(tile) == LAND_VALUE_BASE);
	CHECK(GetLandValueModifier(tile) == LAND_VALUE_MODIFIER_BASE);
	CHECK(GetFinalLandValueScore(tile) == LAND_VALUE_BASE);
}

TEST_CASE("Land value extended save feature is registered as version one")
{
	const auto previous_versions = _sl_xv_feature_static_versions;
	SlXvSetStaticCurrentVersions();

	CHECK(_sl_xv_feature_static_versions[XSLFI_LAND_VALUE] == 1);
	CHECK(std::string_view{SlXvGetFeatureName(XSLFI_LAND_VALUE)} == "land_value");

	_sl_xv_feature_static_versions = previous_versions;
}

TEST_CASE("Town land value state initializes and preserves valid saved values")
{
	_town_pool.CleanPool();
	REQUIRE(Town::CanAllocateItem());
	Town *town = Town::Create(TileIndex{0});

	CHECK(sizeof(town->land_value_score) == sizeof(uint32_t));
	CHECK(town->land_value_score == LAND_VALUE_BASE.base());

	town->land_value_score = 777;
	InitializeLoadedTownLandValues(true);
	CHECK(town->land_value_score == 777);

	town->land_value_score = LAND_VALUE_MAX.base() + 1;
	InitializeLoadedTownLandValues(true);
	CHECK(town->land_value_score == LAND_VALUE_MAX.base());

	town->land_value_score = 777;
	InitializeLoadedTownLandValues(false);
	CHECK(town->land_value_score == LAND_VALUE_BASE.base());

	_town_pool.CleanPool();
}
