/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file land_value.cpp Test land-value core calculations. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../industry.h"
#include "../clear_map.h"
#include "../command_func.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../economy_func.h"
#include "../genworld.h"
#include "../land_value.h"
#include "../landscape_cmd.h"
#include "../map_func.h"
#include "../newgrf_object.h"
#include "../object_cmd.h"
#include "../object_type.h"
#include "../openttd.h"
#include "../rail_cmd.h"
#include "../rail.h"
#include "../road_cmd.h"
#include "../road.h"
#include "../settings_internal.h"
#include "../settings_type.h"
#include "../town.h"
#include "../sl/extended_ver_sl.h"

#include "../safeguards.h"

static void ResetLandValueTestWorld()
{
	_settings_game.economy.land_value_enabled = true;
	_settings_game.economy.land_value_smoothing_percent = 25;
	_settings_game.economy.land_value_distance_scale = 100;
	_settings_game.economy.land_value_purchase_percent = 100;
	_settings_game.economy.land_value_infrastructure_percent = 25;
	_settings_game.economy.land_value_growth_percent = 20;
	_settings_game.economy.land_value_density_percent = 50;
	_town_pool.CleanPool();
	RebuildTownKdtree();
	AllocateMap(64, 64);
}

static Town *CreateLandValueTestTown(uint x, uint y)
{
	REQUIRE(Town::CanAllocateItem());
	Town *town = Town::Create(TileXY(x, y));
	RebuildTownKdtree();
	return town;
}

TEST_CASE("Land value scales remain distinct")
{
	CHECK(LAND_VALUE_BASE.base() == 100);
	CHECK(LAND_VALUE_MAX.base() == 10000);
	CHECK(LAND_VALUE_MODIFIER_BASE.base() == 10000);
	CHECK(LAND_VALUE_MODIFIER_MAX.base() == 40000);
}

TEST_CASE("Land value game settings use Patch PATX persistence and network synchronization")
{
	struct ExpectedSetting {
		const char *name;
		int32_t def;
		int32_t min;
		uint32_t max;
		bool has_post_callback;
	};

	static constexpr ExpectedSetting expected[] = {
		{ "economy.land_value_enabled", 1, 0, 1, true },
		{ "economy.land_value_smoothing_percent", 25, 0, 100, true },
		{ "economy.land_value_distance_scale", 100, 25, 400, true },
		{ "economy.land_value_purchase_percent", 100, 0, 400, false },
		{ "economy.land_value_infrastructure_percent", 25, 0, 400, false },
		{ "economy.land_value_growth_percent", 20, 0, 100, true },
		{ "economy.land_value_density_percent", 50, 0, 200, false },
	};

	for (const ExpectedSetting &item : expected) {
		const SettingDesc *setting = GetSettingFromName(item.name);
		REQUIRE(setting != nullptr);
		REQUIRE(setting->IsIntSetting());
		const IntSettingDesc *integer = setting->AsIntSetting();
		CHECK(integer->def == item.def);
		CHECK(integer->min == item.min);
		CHECK(integer->max == item.max);
		CHECK(setting->flags.Test(SettingFlag::Patch));
		CHECK_FALSE(setting->flags.Test(SettingFlag::NoNetworkSync));
		CHECK_FALSE(setting->flags.Test(SettingFlag::NotInSave));
		CHECK(setting->GetType() == ST_GAME);
		REQUIRE(setting->patx_name != nullptr);
		CHECK(std::string_view{setting->patx_name}.starts_with("land_value."));
		CHECK((integer->post_callback != nullptr) == item.has_post_callback);
	}
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

TEST_CASE("Land purchase surcharge uses bounded integer fixed point")
{
	CHECK(CalculateLandPurchaseSurcharge(100, LAND_VALUE_BASE, 100, true) == 100);
	CHECK(CalculateLandPurchaseSurcharge(100, LandValueScore{500}, 100, true) == 500);
	CHECK(CalculateLandPurchaseSurcharge(100, LandValueScore{2500}, 100, true) == 2500);
	CHECK(CalculateLandPurchaseSurcharge(100, LandValueScore{2500}, 50, true) == 1250);
	CHECK(CalculateLandPurchaseSurcharge(100, LandValueScore{2500}, 0, true) == 0);
	CHECK(CalculateLandPurchaseSurcharge(100, LandValueScore{2500}, 100, false) == 0);
	CHECK(CalculateLandPurchaseSurcharge(-100, LandValueScore{2500}, 100, true) == 0);
	CHECK(CalculateLandPurchaseSurcharge(Money::max(), LAND_VALUE_MAX, 400, true) == Money::max());

	Money previous_score_cost = 0;
	for (uint32_t score = 0; score <= LAND_VALUE_MAX.base(); ++score) {
		const Money score_cost = CalculateLandPurchaseSurcharge(100, LandValueScore{score}, 100, true);
		CHECK(score_cost >= previous_score_cost);
		previous_score_cost = score_cost;
	}

	Money previous_percent_cost = 0;
	for (uint16_t percent = 0; percent <= 400; ++percent) {
		const Money percent_cost = CalculateLandPurchaseSurcharge(100, LAND_VALUE_MAX, percent, true);
		CHECK(percent_cost >= previous_percent_cost);
		previous_percent_cost = percent_cost;
	}
}

TEST_CASE("Land infrastructure surcharge uses bounded integer fixed point")
{
	CHECK(CalculateLandInfrastructureSurcharge(100, LAND_VALUE_BASE, 100, 1, true) == 100);
	CHECK(CalculateLandInfrastructureSurcharge(100, LandValueScore{500}, 25, 1, true) == 125);
	CHECK(CalculateLandInfrastructureSurcharge(100, LandValueScore{2500}, 25, 3, true) == 1875);
	CHECK(CalculateLandInfrastructureSurcharge(100, LAND_VALUE_MAX, 400, UINT32_MAX, true) == Money{171798691800000});
	CHECK(CalculateLandInfrastructureSurcharge(Money::max(), LAND_VALUE_MAX, 400, UINT32_MAX, true) == Money::max());
	CHECK(CalculateLandInfrastructureSurcharge(100, LAND_VALUE_BASE, 0, 1, true) == 0);
	CHECK(CalculateLandInfrastructureSurcharge(100, LAND_VALUE_BASE, 100, 0, true) == 0);
	CHECK(CalculateLandInfrastructureSurcharge(100, LAND_VALUE_BASE, 100, 1, false) == 0);
	/* Frozen order: floor(base * score / 100), then percent / 100, then units. */
	CHECK(CalculateLandInfrastructureSurcharge(101, LandValueScore{333}, 25, 3, true) == 252);

	Money previous = 0;
	for (uint32_t score = 0; score <= LAND_VALUE_MAX.base(); ++score) {
		const Money current = CalculateLandInfrastructureSurcharge(100, LandValueScore{score}, 25, 1, true);
		CHECK(current >= previous);
		previous = current;
	}
}

TEST_CASE("Land infrastructure breakdown applies shared context and conservative base units")
{
	ResetLandValueTestWorld();
	_price[Price::ClearGrass] = 100;
	const TileIndex tile = TileXY(10, 10);
	MakeClear(tile, ClearGround::Grass, 0);
	CHECK(GetLandInfrastructureCostBreakdown(INVALID_TILE, LandInfrastructureType::Rail).status == LandInfrastructureCostStatus::InvalidTile);
	CHECK(GetLandInfrastructureCostBreakdown(tile, LandInfrastructureType::Rail).status == LandInfrastructureCostStatus::NoCompany);

	_company_pool.CleanPool();
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::Create();
	company->money = Money::max();
	_current_company = company->index;
	_game_mode = GameMode::Normal;
	_generating_world = false;

	CHECK(GetLandInfrastructureBaseUnit(LandInfrastructureType::Road) == 100);
	CHECK(GetLandInfrastructureBaseUnit(LandInfrastructureType::Rail) == 200);
	CHECK(GetLandInfrastructureBaseUnit(LandInfrastructureType::RoadStop) == 200);
	CHECK(GetLandInfrastructureBaseUnit(LandInfrastructureType::RailStation) == 300);
	CHECK(GetLandInfrastructureCostBreakdown(tile, LandInfrastructureType::Rail).land_value_surcharge == 50);
	_settings_game.economy.land_value_infrastructure_percent = 0;
	CHECK(GetLandInfrastructureCostBreakdown(tile, LandInfrastructureType::Rail).status == LandInfrastructureCostStatus::ZeroPercent);
	_settings_game.economy.land_value_infrastructure_percent = 25;
	CHECK(GetLandInfrastructureCostBreakdown(tile, LandInfrastructureType::Rail, 0).status == LandInfrastructureCostStatus::ZeroUnits);
	CHECK(GetLandInfrastructureCostBreakdown(tile, LandInfrastructureType::Rail, 1, DoCommandFlag::Town).status == LandInfrastructureCostStatus::TownOperation);

	_current_company = COMPANY_SPECTATOR;
	_company_pool.CleanPool();
}

TEST_CASE("Single-tile rail construction charges infrastructure land once per newly occupied tile")
{
	ResetLandValueTestWorld();
	ResetRailTypes();
	_price[Price::ClearGrass] = 100;
	const TileIndex tile = TileXY(10, 10);
	MakeClear(tile, ClearGround::Grass, 0);
	Town *town = CreateLandValueTestTown(10, 10);
	town->land_value_score = 500;
	RebuildLandValueCache(town);

	_company_pool.CleanPool();
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::Create();
	company->money = Money::max();
	company->clear_limit = UINT32_MAX;
	company->avail_railtypes.Set(RAILTYPE_RAIL);
	_current_company = company->index;
	_game_mode = GameMode::Normal;
	_generating_world = false;

	_settings_game.economy.land_value_infrastructure_percent = 0;
	const CommandCost original = Command<Commands::BuildRail>::Do(DoCommandFlag::QueryCost, tile, RAILTYPE_RAIL, TRACK_X, BuildRailTrackFlags::None);
	INFO(original.SummaryMessage(0));
	INFO(original.GetErrorMessage());
	REQUIRE(original.Succeeded());
	_settings_game.economy.land_value_infrastructure_percent = 25;
	const CommandCost query = Command<Commands::BuildRail>::Do(DoCommandFlag::QueryCost, tile, RAILTYPE_RAIL, TRACK_X, BuildRailTrackFlags::None);
	REQUIRE(query.Succeeded());
	CHECK(query.GetCost() - original.GetCost() == 250);
	const CommandCost execute = Command<Commands::BuildRail>::Do(DoCommandFlag::Execute, tile, RAILTYPE_RAIL, TRACK_X, BuildRailTrackFlags::None);
	REQUIRE(execute.Succeeded());
	CHECK(execute.GetCost() == query.GetCost());

	_settings_game.economy.land_value_infrastructure_percent = 0;
	const CommandCost second_original = Command<Commands::BuildRail>::Do(DoCommandFlag::QueryCost, tile, RAILTYPE_RAIL, TRACK_Y, BuildRailTrackFlags::None);
	_settings_game.economy.land_value_infrastructure_percent = 25;
	const CommandCost second = Command<Commands::BuildRail>::Do(DoCommandFlag::QueryCost, tile, RAILTYPE_RAIL, TRACK_Y, BuildRailTrackFlags::None);
	REQUIRE(second_original.Succeeded());
	REQUIRE(second.Succeeded());
	CHECK(second.GetCost() == second_original.GetCost());

	_current_company = COMPANY_SPECTATOR;
	_company_pool.CleanPool();
	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Land purchase breakdown handles invalid tiles and exemption contexts")
{
	ResetLandValueTestWorld();
	_price[Price::ClearGrass] = 100;
	const TileIndex tile = TileXY(10, 10);
	MakeClear(tile, ClearGround::Rough, 3);

	CHECK(GetLandPurchaseCostBreakdown(INVALID_TILE, 25).status == LandPurchaseCostStatus::InvalidTile);
	CHECK(GetLandPurchaseCostBreakdown(tile, 25).status == LandPurchaseCostStatus::NoCompany);

	_company_pool.CleanPool();
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::Create();
	company->money = Money::max();
	company->clear_limit = UINT32_MAX;
	_current_company = company->index;
	_game_mode = GameMode::Normal;
	_generating_world = false;

	LandPurchaseCostBreakdown chargeable = GetLandPurchaseCostBreakdown(tile, 25);
	CHECK(chargeable.IsChargeable());
	CHECK(chargeable.base_land_unit == 100);
	CHECK(chargeable.land_value_surcharge == 100);
	CHECK(chargeable.total_cost == 125);

	_settings_game.economy.land_value_enabled = false;
	CHECK(GetLandPurchaseCostBreakdown(tile, 25).status == LandPurchaseCostStatus::Disabled);
	_settings_game.economy.land_value_enabled = true;
	_settings_game.economy.land_value_purchase_percent = 0;
	CHECK(GetLandPurchaseCostBreakdown(tile, 25).status == LandPurchaseCostStatus::ZeroPercent);
	_settings_game.economy.land_value_purchase_percent = 100;
	_game_mode = GameMode::Editor;
	CHECK(GetLandPurchaseCostBreakdown(tile, 25).status == LandPurchaseCostStatus::Editor);
	_game_mode = GameMode::Normal;
	_generating_world = true;
	CHECK(GetLandPurchaseCostBreakdown(tile, 25).status == LandPurchaseCostStatus::WorldGeneration);
	_generating_world = false;
	CHECK(GetLandPurchaseCostBreakdown(tile, 25, DoCommandFlag::Town).status == LandPurchaseCostStatus::TownOperation);
	CHECK(GetLandPurchaseCostBreakdown(tile, 25, DoCommandFlag::Bankrupt).status == LandPurchaseCostStatus::Bankruptcy);

	MakeClear(tile, ClearGround::Grass, 0);
	CHECK(GetLandPurchaseCostBreakdown(tile, 0).status == LandPurchaseCostStatus::NoClearRequired);
	CHECK(GetLandPurchaseCostBreakdown(tile, 0, {}, true).IsChargeable());

	_current_company = COMPANY_SPECTATOR;
	_company_pool.CleanPool();
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

TEST_CASE("Land value display levels use stable non-authoritative thresholds")
{
	CHECK(GetLandValueLevel(LandValueScore{0}) == LandValueLevel::VeryLow);
	CHECK(GetLandValueLevel(LandValueScore{149}) == LandValueLevel::VeryLow);
	CHECK(GetLandValueLevel(LandValueScore{150}) == LandValueLevel::Low);
	CHECK(GetLandValueLevel(LandValueScore{299}) == LandValueLevel::Low);
	CHECK(GetLandValueLevel(LandValueScore{300}) == LandValueLevel::Average);
	CHECK(GetLandValueLevel(LandValueScore{599}) == LandValueLevel::Average);
	CHECK(GetLandValueLevel(LandValueScore{600}) == LandValueLevel::AboveAverage);
	CHECK(GetLandValueLevel(LandValueScore{1199}) == LandValueLevel::AboveAverage);
	CHECK(GetLandValueLevel(LandValueScore{1200}) == LandValueLevel::High);
	CHECK(GetLandValueLevel(LandValueScore{2499}) == LandValueLevel::High);
	CHECK(GetLandValueLevel(LandValueScore{2500}) == LandValueLevel::VeryHigh);
	CHECK(GetLandValueLevel(LandValueScore{4999}) == LandValueLevel::VeryHigh);
	CHECK(GetLandValueLevel(LandValueScore{5000}) == LandValueLevel::Core);
	CHECK(GetLandValueLevel(LAND_VALUE_MAX) == LandValueLevel::Core);
}

TEST_CASE("Land value display radius reuses the rounded integer square root")
{
	CHECK(CalculateLandValueInfluenceRadius(0) == 0);
	CHECK(CalculateLandValueInfluenceRadius(1) == 1);
	CHECK(CalculateLandValueInfluenceRadius(63) == 8);
	CHECK(CalculateLandValueInfluenceRadius(64) == 8);
	CHECK(CalculateLandValueInfluenceRadius(7055) == 84);
	CHECK(CalculateLandValueInfluenceRadius(7056) == 84);
	CHECK(CalculateLandValueInfluenceRadius(std::numeric_limits<uint32_t>::max()) == 0x10000);
}

TEST_CASE("Land value cache defaults are safe")
{
	const LandValueCache cache{};

	CHECK(cache.center_score == LAND_VALUE_BASE);
	CHECK(cache.rank == 0);
	CHECK(cache.monthly_change == 0);
	CHECK(cache.max_distance_squared == 0);
	CHECK(std::ranges::all_of(cache.distance_score, [](LandValueScore score) { return score == LAND_VALUE_BASE; }));
}

TEST_CASE("Land value target score is bounded and monotonic")
{
	CHECK(CalculateLandValueTargetScore(0, 0, false) == LAND_VALUE_BASE);
	CHECK(CalculateLandValueTargetScore(1000, 100, false) == LandValueScore{190});
	CHECK(CalculateLandValueTargetScore(1000, 100, true) == LandValueScore{440});
	CHECK(CalculateLandValueTargetScore(2000, 100, false) > CalculateLandValueTargetScore(1000, 100, false));
	CHECK(CalculateLandValueTargetScore(1000, 200, false) > CalculateLandValueTargetScore(1000, 100, false));
	CHECK(CalculateLandValueTargetScore(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), true) == LandValueScore{9350});
	CHECK(CalculateLandValueTargetScore(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), true) < LAND_VALUE_MAX);
}

TEST_CASE("Land value monthly smoothing uses bounded integer arithmetic")
{
	CHECK(SmoothLandValueScore(LandValueScore{100}, LandValueScore{500}, 25) == LandValueScore{200});
	CHECK(SmoothLandValueScore(LandValueScore{500}, LandValueScore{100}, 25) == LandValueScore{400});
	CHECK(SmoothLandValueScore(LAND_VALUE_MAX, LAND_VALUE_MAX, 25) == LAND_VALUE_MAX);
	CHECK(SmoothLandValueScore(LAND_VALUE_MIN, LAND_VALUE_MAX, 0) == LAND_VALUE_MIN);
	CHECK(SmoothLandValueScore(LAND_VALUE_MIN, LAND_VALUE_MAX, 100) == LAND_VALUE_MAX);
	CHECK(SmoothLandValueScore(LAND_VALUE_MIN, LAND_VALUE_MAX, 255) == LAND_VALUE_MAX);
}

TEST_CASE("Land value distance cache is monotonic and bounded")
{
	const LandValueDistanceScores scores = BuildLandValueDistanceScores(LandValueScore{500});
	CHECK(scores.front() == LandValueScore{500});
	CHECK(scores.back() == LAND_VALUE_BASE);
	for (size_t i = 1; i < scores.size(); ++i) CHECK(scores[i] <= scores[i - 1]);

	const LandValueDistanceScores low_scores = BuildLandValueDistanceScores(LandValueScore{50});
	CHECK(low_scores.front() == LandValueScore{50});
	CHECK(low_scores.back() == LandValueScore{50});
	for (size_t i = 1; i < low_scores.size(); ++i) CHECK(low_scores[i] <= low_scores[i - 1]);

	CHECK(CalculateLandValueMaxDistanceSquared(0) == 64);
	CHECK(CalculateLandValueMaxDistanceSquared(4) == 64);
	CHECK(CalculateLandValueMaxDistanceSquared(100) == 1600);
	CHECK(CalculateLandValueMaxDistanceSquared(std::numeric_limits<uint32_t>::max()) == std::numeric_limits<uint32_t>::max());
	CHECK(CalculateLandValueMaxDistanceSquared(100, 25) == 100);
	CHECK(CalculateLandValueMaxDistanceSquared(100, 100) == 1600);
	CHECK(CalculateLandValueMaxDistanceSquared(100, 400) == 25600);
	CHECK(CalculateLandValueMaxDistanceSquared(100, 0) == 1);
	CHECK(CalculateLandValueMaxDistanceSquared(std::numeric_limits<uint32_t>::max(), 400) == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("Land value tile queries use the nearest town cache without rebuilding statistics")
{
	ResetLandValueTestWorld();
	const TileIndex centre = TileXY(10, 10);
	const TileIndex far_tile = TileXY(63, 63);

	CHECK(GetLandValueScore(centre) == LAND_VALUE_BASE);
	CHECK(GetFinalLandValueScore(centre) == LAND_VALUE_BASE);

	Town *town = CreateLandValueTestTown(10, 10);
	town->land_value_score = 500;
	town->cache.population = 1234;
	town->cache.num_houses = 123;
	RebuildLandValueCache(town);

	const LandValueQueryResult centre_result = GetLandValueQueryResult(centre);
	CHECK(centre_result.town_id == town->index);
	CHECK(centre_result.town_score == LandValueScore{500});
	CHECK(centre_result.center_score == town->cache.land_value.center_score);
	CHECK(centre_result.distance_band == 0);
	CHECK(centre_result.distance_score == town->cache.land_value.distance_score.front());
	CHECK(centre_result.modifier == GetLandValueModifier(centre));
	CHECK(centre_result.final_score == GetFinalLandValueScore(centre));
	CHECK(GetLandValueScore(centre) == town->cache.land_value.distance_score.front());
	CHECK(GetLandValueScore(centre) == LandValueScore{500});
	CHECK(GetLandValueScore(far_tile) == town->cache.land_value.distance_score.back());
	CHECK(GetLandValueScore(far_tile) == LAND_VALUE_BASE);
	CHECK(GetLandValueModifier(centre) == LAND_VALUE_MODIFIER_BASE);
	CHECK(GetFinalLandValueScore(centre) == LandValueScore{500});

	const uint32_t population = town->cache.population;
	const uint32_t num_houses = town->cache.num_houses;
	const size_t station_count = town->stations_near.size();
	const size_t industry_count = town->industry_cache.size();
	const LandValueCache cache = town->cache.land_value;
	for (uint i = 0; i < 10000; ++i) {
		const LandValueQueryResult result = GetLandValueQueryResult(far_tile);
		CHECK(result.distance_score == LAND_VALUE_BASE);
		CHECK(result.distance_band == LAND_VALUE_DISTANCE_BAND_COUNT - 1);
		CHECK(result.final_score == GetFinalLandValueScore(far_tile));
	}

	CHECK(town->cache.population == population);
	CHECK(town->cache.num_houses == num_houses);
	CHECK(town->stations_near.size() == station_count);
	CHECK(town->industry_cache.size() == industry_count);
	CHECK(town->cache.land_value.distance_score == cache.distance_score);
	CHECK(town->cache.land_value.max_distance_squared == cache.max_distance_squared);

	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Land value query is safe without a town and rejects invalid console tile indices")
{
	ResetLandValueTestWorld();
	const LandValueQueryResult result = GetLandValueQueryResult(TileXY(5, 5));
	CHECK(result.town_id == TownID::Invalid());
	CHECK(result.town_score == LAND_VALUE_BASE);
	CHECK(result.distance_score == LAND_VALUE_BASE);
	CHECK(result.modifier == LAND_VALUE_MODIFIER_BASE);
	CHECK(result.final_score == LAND_VALUE_BASE);
	CHECK(result.rank == 0);
	CHECK(result.monthly_change == 0);

	CHECK(ResolveLandValueTileIndex(0).has_value());
	CHECK(ResolveLandValueTileIndex(Map::Size() - 1).has_value());
	CHECK_FALSE(ResolveLandValueTileIndex(Map::Size()).has_value());
	CHECK_FALSE(ResolveLandValueTileIndex(std::numeric_limits<uint64_t>::max()).has_value());
	CHECK(ResolveLandValueTileCoordinates(5, 5) == TileXY(5, 5));
	CHECK(ResolveLandValueTileCoordinates(Map::SizeX() - 1, Map::SizeY() - 1) == TileXY(Map::SizeX() - 1, Map::SizeY() - 1));
	CHECK_FALSE(ResolveLandValueTileCoordinates(Map::SizeX(), 0).has_value());
	CHECK_FALSE(ResolveLandValueTileCoordinates(0, Map::SizeY()).has_value());
	CHECK_FALSE(ResolveLandValueTileCoordinates(std::numeric_limits<uint64_t>::max(), 0).has_value());
}

TEST_CASE("LandscapeClear injects one deterministic land surcharge into real CommandCost")
{
	ResetLandValueTestWorld();
	_price[Price::ClearGrass] = 100;
	_price[Price::ClearRough] = 120;
	const TileIndex tile = TileXY(10, 10);
	MakeClear(tile, ClearGround::Rough, 3);
	Town *town = CreateLandValueTestTown(10, 10);
	town->land_value_score = 500;
	RebuildLandValueCache(town);

	_company_pool.CleanPool();
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::Create();
	company->money = Money::max();
	company->clear_limit = UINT32_MAX;
	_current_company = company->index;
	_game_mode = GameMode::Normal;
	_generating_world = false;

	const CommandCost query = Command<Commands::LandscapeClear>::Do(DoCommandFlag::QueryCost, tile);
	REQUIRE(query.Succeeded());
	CHECK(query.GetCost() == 620);
	CHECK(IsLandPurchaseClearRequired(tile));

	const LandPurchaseCostBreakdown split = GetLandPurchaseCostBreakdownFromTotal(tile, query.GetCost(), DoCommandFlag::QueryCost);
	CHECK(split.original_cost == 120);
	CHECK(split.land_value_surcharge == 500);
	CHECK(split.total_cost == query.GetCost());

	const CommandCost execute = Command<Commands::LandscapeClear>::Do(DoCommandFlag::Execute, tile);
	REQUIRE(execute.Succeeded());
	CHECK(execute.GetCost() == query.GetCost());
	CHECK_FALSE(IsLandPurchaseClearRequired(tile));

	const CommandCost empty_query = Command<Commands::LandscapeClear>::Do(DoCommandFlag::QueryCost, tile);
	REQUIRE(empty_query.Succeeded());
	CHECK(empty_query.GetCost() == 0);

	_current_company = COMPANY_SPECTATOR;
	_company_pool.CleanPool();
	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Purchase land charges bare and occupied tiles once through the shared surcharge")
{
	ResetLandValueTestWorld();
	ResetObjects();
	_settings_game.construction.purchase_land_permitted = 1;
	_price[Price::ClearGrass] = 100;
	_price[Price::ClearRough] = 120;
	const TileIndex bare_tile = TileXY(10, 10);
	const TileIndex rough_tile = TileXY(11, 10);
	MakeClear(bare_tile, ClearGround::Grass, 0);
	MakeClear(rough_tile, ClearGround::Rough, 3);
	Town *town = CreateLandValueTestTown(10, 10);
	town->land_value_score = 500;
	RebuildLandValueCache(town);

	_company_pool.CleanPool();
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::Create();
	company->money = Money::max();
	company->clear_limit = UINT32_MAX;
	company->purchase_land_limit = UINT32_MAX;
	_current_company = company->index;
	_game_mode = GameMode::Normal;
	_generating_world = false;

	auto query_purchase = [](TileIndex tile) {
		return Command<Commands::BuildObject>::Do(DoCommandFlag::QueryCost, tile, OBJECT_OWNED_LAND, 0);
	};

	_settings_game.economy.land_value_enabled = false;
	const CommandCost bare_original = query_purchase(bare_tile);
	const CommandCost rough_original = query_purchase(rough_tile);
	INFO(bare_original.SummaryMessage(0));
	REQUIRE(bare_original.Succeeded());
	INFO(rough_original.SummaryMessage(0));
	REQUIRE(rough_original.Succeeded());

	_settings_game.economy.land_value_enabled = true;
	const CommandCost bare_land_value = query_purchase(bare_tile);
	const CommandCost rough_land_value = query_purchase(rough_tile);
	REQUIRE(bare_land_value.Succeeded());
	REQUIRE(rough_land_value.Succeeded());
	CHECK(bare_land_value.GetCost() - bare_original.GetCost() == 500);
	CHECK(rough_land_value.GetCost() - rough_original.GetCost() == 500);

	_current_company = COMPANY_SPECTATOR;
	_company_pool.CleanPool();
	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("ClearArea accumulates the shared surcharge once per changed tile")
{
	ResetLandValueTestWorld();
	_price[Price::ClearGrass] = 100;
	_price[Price::ClearRough] = 120;
	const TileIndex first = TileXY(10, 10);
	const TileIndex second = TileXY(11, 10);
	MakeClear(first, ClearGround::Rough, 3);
	MakeClear(second, ClearGround::Rough, 3);
	Town *town = CreateLandValueTestTown(10, 10);
	town->land_value_score = 500;
	RebuildLandValueCache(town);

	_company_pool.CleanPool();
	REQUIRE(Company::CanAllocateItem());
	Company *company = Company::Create();
	company->money = Money::max();
	company->clear_limit = UINT32_MAX;
	_current_company = company->index;
	_game_mode = GameMode::Normal;
	_generating_world = false;

	_settings_game.economy.land_value_enabled = false;
	const CommandCost original = Command<Commands::ClearArea>::Do(DoCommandFlag::QueryCost, second, first, false);
	REQUIRE(original.Succeeded());
	_settings_game.economy.land_value_enabled = true;
	const CommandCost with_land_value = Command<Commands::ClearArea>::Do(DoCommandFlag::QueryCost, second, first, false);
	REQUIRE(with_land_value.Succeeded());
	CHECK(with_land_value.GetCost() - original.GetCost() == 1000);

	_current_company = COMPANY_SPECTATOR;
	_company_pool.CleanPool();
	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Disabling land value makes queries neutral and preserves persistent state")
{
	ResetLandValueTestWorld();
	const TileIndex centre = TileXY(10, 10);
	Town *town = CreateLandValueTestTown(10, 10);
	town->land_value_score = 500;
	town->cache.population = 1000;
	town->cache.num_houses = 100;
	town->cache.squared_town_zone_radius = {100, 81, 49, 25, 16};
	RebuildAllLandValueCaches();

	const uint32_t persistent_score = town->land_value_score;
	const LandValueCache enabled_cache = town->cache.land_value;
	CHECK(GetLandValueScore(centre) == LandValueScore{500});

	_settings_game.economy.land_value_enabled = false;
	RebuildAllLandValueCaches();
	const LandValueQueryResult disabled = GetLandValueQueryResult(centre);
	CHECK_FALSE(disabled.enabled);
	CHECK(disabled.town_id == town->index);
	CHECK(disabled.town_score == LandValueScore{persistent_score});
	CHECK(disabled.distance_score == LAND_VALUE_BASE);
	CHECK(disabled.final_score == LAND_VALUE_BASE);
	CHECK(town->land_value_score == persistent_score);
	CHECK(town->cache.land_value.center_score == enabled_cache.center_score);
	CHECK(town->cache.land_value.distance_score == enabled_cache.distance_score);

	const LandValueCache before_month = town->cache.land_value;
	LandValueMonthlyLoop();
	CHECK(town->land_value_score == persistent_score);
	CHECK(town->cache.land_value.center_score == before_month.center_score);
	CHECK(town->cache.land_value.monthly_change == before_month.monthly_change);

	_settings_game.economy.land_value_enabled = true;
	CHECK(GetLandValueScore(centre) == LandValueScore{500});
	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Land value settings control smoothing and cache distance without touching reserved gameplay")
{
	ResetLandValueTestWorld();
	Town *town = CreateLandValueTestTown(10, 10);
	town->cache.population = 10000;
	town->cache.num_houses = 0;
	town->cache.squared_town_zone_radius = {100, 81, 49, 25, 16};

	_settings_game.economy.land_value_smoothing_percent = 0;
	LandValueMonthlyLoop();
	CHECK(town->land_value_score == 100);
	CHECK(town->cache.land_value.monthly_change == 0);

	_settings_game.economy.land_value_smoothing_percent = 25;
	LandValueMonthlyLoop();
	CHECK(town->land_value_score == 200);
	CHECK(town->cache.land_value.monthly_change == 100);

	town->land_value_score = 100;
	_settings_game.economy.land_value_smoothing_percent = 100;
	LandValueMonthlyLoop();
	CHECK(town->land_value_score == 500);
	CHECK(town->cache.land_value.monthly_change == 400);

	const uint32_t persistent_score = town->land_value_score;
	_settings_game.economy.land_value_distance_scale = 100;
	RebuildLandValueCache(town);
	CHECK(town->cache.land_value.max_distance_squared == 1600);
	const IntSettingDesc *distance_setting = GetSettingFromName("economy.land_value_distance_scale")->AsIntSetting();
	REQUIRE(distance_setting->post_callback != nullptr);
	_settings_game.economy.land_value_distance_scale = 25;
	distance_setting->post_callback(25);
	CHECK(town->cache.land_value.max_distance_squared == 100);
	CHECK(town->land_value_score == persistent_score);
	_settings_game.economy.land_value_distance_scale = 400;
	distance_setting->post_callback(400);
	CHECK(town->cache.land_value.max_distance_squared == 25600);
	CHECK(town->land_value_score == persistent_score);

	_settings_game.economy.land_value_purchase_percent = 0;
	_settings_game.economy.land_value_infrastructure_percent = 400;
	_settings_game.economy.land_value_growth_percent = 100;
	_settings_game.economy.land_value_density_percent = 200;
	CHECK(CalculateLandValueTargetScore(10000, 0, false) == LandValueScore{500});
	CHECK(GetLandValueModifier(town->xy) == LAND_VALUE_MODIFIER_BASE);

	_town_pool.CleanPool();
	RebuildTownKdtree();
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
	RebuildTownKdtree();
}

TEST_CASE("Land value rebuild is deterministic and preserves the persistent score")
{
	ResetLandValueTestWorld();
	Town *town = CreateLandValueTestTown(20, 20);
	town->land_value_score = 777;
	town->cache.squared_town_zone_radius = {100, 81, 49, 25, 16};

	RebuildLandValueCache(town);
	const LandValueDistanceScores scores = town->cache.land_value.distance_score;
	const uint32_t max_distance_squared = town->cache.land_value.max_distance_squared;
	CHECK(town->land_value_score == 777);
	CHECK(town->cache.land_value.center_score == LandValueScore{777});

	RebuildLandValueCache(town);
	CHECK(town->land_value_score == 777);
	CHECK(town->cache.land_value.center_score == LandValueScore{777});
	CHECK(town->cache.land_value.distance_score == scores);
	CHECK(town->cache.land_value.max_distance_squared == max_distance_squared);

	InitializeLoadedTownLandValues(true);
	CHECK(town->land_value_score == 777);
	CHECK(town->cache.land_value.center_score == LandValueScore{777});
	CHECK(town->cache.land_value.distance_score == scores);

	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Land value monthly loop records negative change and stable TownID tie ranks")
{
	ResetLandValueTestWorld();
	Town *first = CreateLandValueTestTown(10, 10);
	Town *second = CreateLandValueTestTown(20, 20);
	Town *third = CreateLandValueTestTown(30, 30);

	first->land_value_score = 500;
	second->land_value_score = 500;
	third->land_value_score = LAND_VALUE_BASE.base();
	first->cache.population = second->cache.population = third->cache.population = 0;
	first->cache.num_houses = second->cache.num_houses = third->cache.num_houses = 0;

	LandValueMonthlyLoop();

	CHECK(first->land_value_score == 400);
	CHECK(second->land_value_score == 400);
	CHECK(third->land_value_score == LAND_VALUE_BASE.base());
	CHECK(first->cache.land_value.monthly_change == -100);
	CHECK(second->cache.land_value.monthly_change == -100);
	CHECK(third->cache.land_value.monthly_change == 0);
	CHECK(first->index < second->index);
	CHECK(first->cache.land_value.rank == 1);
	CHECK(second->cache.land_value.rank == 2);
	CHECK(third->cache.land_value.rank == 3);

	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Land value scores survive cache reconstruction after two economy months")
{
	ResetLandValueTestWorld();
	Town *town = CreateLandValueTestTown(10, 10);
	Town *city = CreateLandValueTestTown(30, 30);

	town->cache.population = 1000;
	town->cache.num_houses = 100;
	city->cache.population = 5000;
	city->cache.num_houses = 500;
	city->larger_town = true;

	LandValueMonthlyLoop();
	LandValueMonthlyLoop();
	CHECK(town->land_value_score == 139);
	CHECK(city->land_value_score == 406);

	const uint32_t saved_town_score = town->land_value_score;
	const uint32_t saved_city_score = city->land_value_score;
	town->cache.land_value = LandValueCache{};
	city->cache.land_value = LandValueCache{};

	InitializeLoadedTownLandValues(true);
	CHECK(town->land_value_score == saved_town_score);
	CHECK(city->land_value_score == saved_city_score);
	CHECK(town->cache.land_value.center_score == LandValueScore{saved_town_score});
	CHECK(city->cache.land_value.center_score == LandValueScore{saved_city_score});
	CHECK(city->cache.land_value.rank == 1);
	CHECK(town->cache.land_value.rank == 2);

	_town_pool.CleanPool();
	RebuildTownKdtree();
}

TEST_CASE("Town development demand curves use frozen boundaries and influence scaling")
{
	static constexpr std::array<uint32_t, 15> SCORES = {
		0, 149, 150, 299, 300, 599, 600, 1199, 1200, 2499, 2500, 4999, 5000, 10000, 10001,
	};
	static constexpr std::array<uint16_t, 15> AFFORDABILITY = {
		10500, 10500, 12500, 12500, 14000, 14000, 12000, 12000, 9000, 9000, 6500, 6500, 4000, 4000, 4000,
	};
	static constexpr std::array<uint16_t, 15> RESIDENTIAL = {
		8500, 8500, 11500, 11500, 13000, 13000, 12000, 12000, 10000, 10000, 8000, 8000, 6500, 6500, 6500,
	};
	static constexpr std::array<uint16_t, 15> COMMERCIAL = {
		6000, 6000, 7500, 7500, 9500, 9500, 11500, 11500, 13500, 13500, 15000, 15000, 15000, 15000, 15000,
	};
	static constexpr std::array<uint16_t, 15> INDUSTRIAL = {
		8500, 8500, 12500, 12500, 13500, 13500, 11000, 11000, 7500, 7500, 4000, 4000, 2000, 2000, 2000,
	};
	static constexpr TownDevelopmentMass NEUTRAL_MASS{10000};

	for (size_t i = 0; i < SCORES.size(); ++i) {
		const LandValueScore score{SCORES[i]};
		CHECK(CalculateLandAffordability(score, 100) == AFFORDABILITY[i]);
		CHECK(CalculateResidentialDevelopmentDemand(NEUTRAL_MASS, score, 100) == RESIDENTIAL[i]);
		CHECK(CalculateCommercialDevelopmentDemand(NEUTRAL_MASS, score, 100) == COMMERCIAL[i]);
		CHECK(CalculateIndustrialDevelopmentDemand(NEUTRAL_MASS, score, 100) == INDUSTRIAL[i]);
	}

	CHECK(CalculateLandAffordability(LandValueScore{500}, 0) == 10000);
	CHECK(CalculateLandAffordability(LandValueScore{500}, 20) == 10800);
	CHECK(CalculateLandAffordability(LandValueScore{500}, 50) == 12000);
	CHECK(CalculateResidentialDevelopmentDemand(NEUTRAL_MASS, LandValueScore{500}, 20) == 10600);
	CHECK(CalculateCommercialDevelopmentDemand(NEUTRAL_MASS, LandValueScore{500}, 20) == 9900);
	CHECK(CalculateIndustrialDevelopmentDemand(NEUTRAL_MASS, LandValueScore{500}, 20) == 10700);
	CHECK(CalculateLandAffordability(LandValueScore{500}, 100, false) == 10000);
	CHECK(CalculateResidentialDevelopmentDemand(NEUTRAL_MASS, LandValueScore{500}, 100, false) == 10000);
	CHECK(CalculateCommercialDevelopmentDemand(NEUTRAL_MASS, LandValueScore{500}, 100, false) == 10000);
	CHECK(CalculateIndustrialDevelopmentDemand(NEUTRAL_MASS, LandValueScore{500}, 100, false) == 10000);
}

TEST_CASE("Town development demand is bounded deterministic and scales deviations monotonically")
{
	const TownDevelopmentMass mass{17321};
	for (uint32_t score = 0; score <= LAND_VALUE_MAX.base(); ++score) {
		const auto first = CalculateTownDevelopmentDemand(50000, 6000, true, LandValueScore{score}, 20, true);
		const auto second = CalculateTownDevelopmentDemand(50000, 6000, true, LandValueScore{score}, 20, true);
		CHECK(first.overall_demand == second.overall_demand);
		CHECK(first.affordability <= TOWN_DEVELOPMENT_DEMAND_MAX);
		CHECK(first.residential_demand <= TOWN_DEVELOPMENT_DEMAND_MAX);
		CHECK(first.commercial_demand <= TOWN_DEVELOPMENT_DEMAND_MAX);
		CHECK(first.industrial_demand <= TOWN_DEVELOPMENT_DEMAND_MAX);
		CHECK(first.overall_demand <= TOWN_DEVELOPMENT_DEMAND_MAX);
	}

	for (LandValueScore score : {LandValueScore{0}, LandValueScore{100}, LandValueScore{500}, LandValueScore{2000}, LandValueScore{5000}, LAND_VALUE_MAX}) {
		int32_t previous_residential_deviation = 0;
		int32_t previous_commercial_deviation = 0;
		int32_t previous_industrial_deviation = 0;
		for (uint8_t influence = 0; influence <= 100; influence += 5) {
			const uint16_t residential = CalculateResidentialDevelopmentDemand(mass, score, influence);
			const uint16_t commercial = CalculateCommercialDevelopmentDemand(mass, score, influence);
			const uint16_t industrial = CalculateIndustrialDevelopmentDemand(mass, score, influence);
			const int32_t residential_deviation = std::abs(static_cast<int32_t>(residential) - 10000);
			const int32_t commercial_deviation = std::abs(static_cast<int32_t>(commercial) - 10000);
			const int32_t industrial_deviation = std::abs(static_cast<int32_t>(industrial) - 10000);
			CHECK(residential_deviation >= previous_residential_deviation);
			CHECK(commercial_deviation >= previous_commercial_deviation);
			CHECK(industrial_deviation >= previous_industrial_deviation);
			previous_residential_deviation = residential_deviation;
			previous_commercial_deviation = commercial_deviation;
			previous_industrial_deviation = industrial_deviation;
		}
	}
}

TEST_CASE("Town development mass is monotonic sub-linear bounded and city-aware")
{
	TownDevelopmentMass previous{0};
	for (uint32_t population : {0U, 1U, 100U, 500U, 5000U, 50000U, 200000U, 1000000U, UINT32_MAX}) {
		const TownDevelopmentMass current = CalculateTownDevelopmentMass(population, 0, false);
		CHECK(current >= previous);
		CHECK(current <= TOWN_DEVELOPMENT_MASS_MAX);
		previous = current;
	}

	previous = TownDevelopmentMass{0};
	for (uint32_t houses : {0U, 1U, 80U, 700U, 6000U, 20000U, 1000000U, UINT32_MAX}) {
		const TownDevelopmentMass current = CalculateTownDevelopmentMass(0, houses, false);
		CHECK(current >= previous);
		CHECK(current <= TOWN_DEVELOPMENT_MASS_MAX);
		previous = current;
	}

	const TownDevelopmentMass small = CalculateTownDevelopmentMass(500, 80, false);
	const TownDevelopmentMass medium = CalculateTownDevelopmentMass(5000, 700, false);
	const TownDevelopmentMass large = CalculateTownDevelopmentMass(50000, 6000, true);
	const TownDevelopmentMass very_large = CalculateTownDevelopmentMass(200000, 20000, true);
	CHECK(small < medium);
	CHECK(medium < large);
	CHECK(large < very_large);
	CHECK(very_large == TOWN_DEVELOPMENT_MASS_MAX);
	CHECK(CalculateTownDevelopmentMass(5000, 700, true).base() == medium.base() + 1000);
	CHECK(CalculateTownDevelopmentMass(UINT32_MAX, UINT32_MAX, true) == TOWN_DEVELOPMENT_MASS_MAX);

	const uint32_t first_population_increment = CalculateTownDevelopmentMass(250000, 0, false).base();
	const uint32_t second_population_increment = CalculateTownDevelopmentMass(1000000, 0, false).base() - first_population_increment;
	CHECK(second_population_increment <= first_population_increment);
}

TEST_CASE("Town scale and land value produce distinct residential commercial and industrial demand")
{
	const TownDevelopmentMass small = CalculateTownDevelopmentMass(500, 80, false);
	const TownDevelopmentMass large = CalculateTownDevelopmentMass(50000, 6000, true);

	CHECK(CalculateResidentialDevelopmentDemand(small, LandValueScore{100}, 100) <
			CalculateResidentialDevelopmentDemand(small, LandValueScore{250}, 100));
	CHECK(CalculateResidentialDevelopmentDemand(large, LandValueScore{250}, 100) >
			CalculateResidentialDevelopmentDemand(small, LandValueScore{250}, 100));
	CHECK(CalculateResidentialDevelopmentDemand(large, LandValueScore{500}, 100) >
			CalculateResidentialDevelopmentDemand(large, LandValueScore{5000}, 100));

	CHECK(CalculateCommercialDevelopmentDemand(large, LandValueScore{2500}, 100) >
			CalculateCommercialDevelopmentDemand(small, LandValueScore{2500}, 100));
	CHECK(CalculateCommercialDevelopmentDemand(large, LandValueScore{2500}, 100) ==
			CalculateCommercialDevelopmentDemand(large, LandValueScore{5000}, 100));
	CHECK(CalculateCommercialDevelopmentDemand(small, LandValueScore{5000}, 100) < TOWN_DEVELOPMENT_DEMAND_MAX);

	CHECK(CalculateIndustrialDevelopmentDemand(small, LandValueScore{100}, 100) <
			CalculateIndustrialDevelopmentDemand(small, LandValueScore{500}, 100));
	CHECK(CalculateIndustrialDevelopmentDemand(large, LandValueScore{250}, 100) >
			CalculateIndustrialDevelopmentDemand(small, LandValueScore{250}, 100));
	CHECK(CalculateIndustrialDevelopmentDemand(large, LandValueScore{500}, 100) >
			CalculateIndustrialDevelopmentDemand(large, LandValueScore{5000}, 100));
	CHECK(CalculateIndustrialDevelopmentDemand(large, LandValueScore{5000}, 100) < 8000);
}

TEST_CASE("Overall town development demand uses bounded five-three-two weights")
{
	CHECK(CalculateOverallDevelopmentDemand(10000, 10000, 10000) == 10000);
	CHECK(CalculateOverallDevelopmentDemand(12000, 15000, 4000) == 11300);
	CHECK(CalculateOverallDevelopmentDemand(20000, 20000, 20000) == 20000);
	CHECK(CalculateOverallDevelopmentDemand(UINT16_MAX, UINT16_MAX, UINT16_MAX) == 20000);
	CHECK(CalculateOverallDevelopmentDemand(12000, 15000, 4000, false) == 10000);

	const TownDevelopmentDemandCache disabled = CalculateTownDevelopmentDemand(50000, 6000, true, LandValueScore{2500}, 100, false);
	CHECK_FALSE(disabled.active);
	CHECK(disabled.affordability == 10000);
	CHECK(disabled.residential_demand == 10000);
	CHECK(disabled.commercial_demand == 10000);
	CHECK(disabled.industrial_demand == 10000);
	CHECK(disabled.overall_demand == 10000);

	const TownDevelopmentDemandCache zero = CalculateTownDevelopmentDemand(50000, 6000, true, LandValueScore{2500}, 0, true);
	CHECK_FALSE(zero.active);
	CHECK(zero.affordability == 10000);
	CHECK(zero.residential_demand == 10000);
	CHECK(zero.commercial_demand == 10000);
	CHECK(zero.industrial_demand == 10000);
	CHECK(zero.overall_demand == 10000);
}

TEST_CASE("Town development demand cache rebuilds without changing growth houses or industries")
{
	ResetLandValueTestWorld();
	Town *town = CreateLandValueTestTown(10, 10);
	town->cache.population = 50000;
	town->cache.num_houses = 6000;
	town->larger_town = true;
	town->land_value_score = 500;
	town->growth_rate = 123;
	town->grow_counter = 45;
	const uint32_t houses_before = town->cache.num_houses;
	const size_t industries_before = Industry::GetNumItems();

	RebuildLandValueCache(town);
	const TownDevelopmentDemandCache expected = CalculateTownDevelopmentDemand(50000, 6000, true, LandValueScore{500}, 20, true);
	CHECK(town->cache.development_demand.mass == expected.mass);
	CHECK(town->cache.development_demand.overall_demand == expected.overall_demand);
	CHECK(GetTownDevelopmentDemand(town).residential_demand == expected.residential_demand);
	CHECK(town->growth_rate == 123);
	CHECK(town->grow_counter == 45);
	CHECK(town->cache.num_houses == houses_before);
	CHECK(Industry::GetNumItems() == industries_before);

	const IntSettingDesc *influence_setting = GetSettingFromName("economy.land_value_growth_percent")->AsIntSetting();
	REQUIRE(influence_setting->post_callback != nullptr);
	const uint32_t score_before_setting = town->land_value_score;
	_settings_game.economy.land_value_growth_percent = 0;
	influence_setting->post_callback(0);
	CHECK(GetTownDevelopmentDemand(town).overall_demand == 10000);
	CHECK(town->land_value_score == score_before_setting);
	_settings_game.economy.land_value_growth_percent = 100;
	influence_setting->post_callback(100);
	CHECK(GetTownDevelopmentDemand(town).active);
	CHECK(GetTownDevelopmentDemand(town).overall_demand != 10000);
	CHECK(town->land_value_score == score_before_setting);

	const uint16_t growth_rate_before_month = town->growth_rate;
	const uint16_t grow_counter_before_month = town->grow_counter;
	const uint16_t demand_before_month = GetTownDevelopmentDemand(town).overall_demand;
	_settings_game.economy.land_value_smoothing_percent = 100;
	LandValueMonthlyLoop();
	CHECK(GetTownDevelopmentDemand(town).overall_demand != demand_before_month);
	CHECK(town->growth_rate == growth_rate_before_month);
	CHECK(town->grow_counter == grow_counter_before_month);
	CHECK(town->cache.num_houses == houses_before);
	CHECK(Industry::GetNumItems() == industries_before);

	const TownDevelopmentDemandCache rebuilt = town->cache.development_demand;
	town->cache.development_demand = {};
	InitializeLoadedTownLandValues(true);
	CHECK(town->cache.development_demand.mass == rebuilt.mass);
	CHECK(town->cache.development_demand.overall_demand == rebuilt.overall_demand);

	const uint32_t score_before_disable = town->land_value_score;
	_settings_game.economy.land_value_enabled = false;
	LandValueMonthlyLoop();
	CHECK(GetTownDevelopmentDemand(town).overall_demand == 10000);
	CHECK(GetTownDevelopmentDemand(town).affordability == 10000);
	CHECK(town->land_value_score == score_before_disable);
	CHECK(GetTownDevelopmentDemand(nullptr).overall_demand == 10000);

	_town_pool.CleanPool();
	RebuildTownKdtree();
}
