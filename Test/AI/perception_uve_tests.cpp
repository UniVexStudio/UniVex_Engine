// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ai/perception_uve.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

namespace {

EntityUVE MakeEntityUVE(std::uint32_t id) {
    EntityUVE entity;
    entity.index = id;
    entity.generation = 1U;
    return entity;
}

PerceptionComponentUVE MakeSensorUVE() {
    PerceptionComponentUVE sensor;
    sensor.watchedTag = "enemy";
    sensor.sightRangeMetres = 20.0F;
    sensor.sightFieldOfViewDegrees = 90.0F;
    sensor.hearingRadiusMetres = 10.0F;
    return sensor;
}

} // namespace

TEST(PerceptionUVETest, Sight_SeesNearestUnoccludedAndAsksNearestFirst) {
    const PerceptionComponentUVE sensor = MakeSensorUVE();
    const std::vector<SightTargetUVE> candidates{
        SightTargetUVE{MakeEntityUVE(1U), Math::Vector3UVE{0.0F, 0.0F, -4.0F}},
        SightTargetUVE{MakeEntityUVE(2U), Math::Vector3UVE{3.0F, 0.0F, -4.0F}},
        SightTargetUVE{MakeEntityUVE(3U), Math::Vector3UVE{0.0F, 0.0F, -50.0F}},
    };
    std::vector<EntityUVE> asked;
    const SightOcclusionQueryUVE occluded =
        [&asked](const Math::Vector3UVE&, const Math::Vector3UVE&, const EntityUVE target) {
            asked.push_back(target);
            return target == MakeEntityUVE(1U);
        };
    const SightResultUVE result =
        SenseSightUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}, sensor, candidates, occluded);
    EXPECT_TRUE(result.visible);
    EXPECT_EQ(result.entity, MakeEntityUVE(2U));
    EXPECT_FLOAT_EQ(result.distance01, 0.25F);
    EXPECT_EQ(result.position, Math::Vector3UVE(3.0F, 0.0F, -4.0F));
    EXPECT_EQ(asked, (std::vector<EntityUVE>{MakeEntityUVE(1U), MakeEntityUVE(2U)}));
}

TEST(PerceptionUVETest, Sight_RespectsConeOmnidirectionalAndContact) {
    const PerceptionComponentUVE sensor = MakeSensorUVE();
    const std::vector<SightTargetUVE> behind{
        SightTargetUVE{MakeEntityUVE(1U), Math::Vector3UVE{0.0F, 0.0F, 5.0F}},
    };
    const SightOcclusionQueryUVE clear =
        [](const Math::Vector3UVE&, const Math::Vector3UVE&, const EntityUVE) { return false; };
    EXPECT_FALSE(SenseSightUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}, sensor, behind, clear)
                     .visible);

    PerceptionComponentUVE eyesBehind = sensor;
    eyesBehind.sightFieldOfViewDegrees = 360.0F;
    const SightResultUVE seen =
        SenseSightUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}, eyesBehind, behind, clear);
    EXPECT_TRUE(seen.visible);
    EXPECT_FLOAT_EQ(seen.distance01, 0.25F);

    const std::vector<SightTargetUVE> contact{
        SightTargetUVE{MakeEntityUVE(1U), Math::Vector3UVE{}},
    };
    const SightResultUVE felt =
        SenseSightUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}, sensor, contact, clear);
    EXPECT_TRUE(felt.visible);
    EXPECT_FLOAT_EQ(felt.distance01, 0.0F);
}

TEST(PerceptionUVETest, Sight_SkipsOcclusionWhenDisabled) {
    PerceptionComponentUVE sensor = MakeSensorUVE();
    sensor.requiresLineOfSight = false;
    const std::vector<SightTargetUVE> candidates{
        SightTargetUVE{MakeEntityUVE(1U), Math::Vector3UVE{0.0F, 0.0F, -9.0F}},
        SightTargetUVE{MakeEntityUVE(2U), Math::Vector3UVE{0.0F, 0.0F, -4.0F}},
    };
    bool asked = false;
    const SightOcclusionQueryUVE never =
        [&asked](const Math::Vector3UVE&, const Math::Vector3UVE&, const EntityUVE) {
            asked = true;
            return true;
        };
    const SightResultUVE result =
        SenseSightUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}, sensor, candidates, never);
    EXPECT_TRUE(result.visible);
    EXPECT_EQ(result.entity, MakeEntityUVE(2U));
    EXPECT_FALSE(asked);
}

TEST(PerceptionUVETest, Hearing_HearsLoudestWithFalloff) {
    const std::vector<HeardNoiseUVE> noises{
        HeardNoiseUVE{Math::Vector3UVE{5.0F, 0.0F, 0.0F}, 1.0F},
        HeardNoiseUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0.5F},
        HeardNoiseUVE{Math::Vector3UVE{9.0F, 0.0F, 0.0F}, 0.5F},
        HeardNoiseUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0.0F},
    };
    const HearingResultUVE result = SenseHearingUVE(Math::Vector3UVE{}, 10.0F, noises);
    EXPECT_FLOAT_EQ(result.loudness, 0.5F);
    EXPECT_EQ(result.position, Math::Vector3UVE(5.0F, 0.0F, 0.0F));

    EXPECT_FLOAT_EQ(SenseHearingUVE(Math::Vector3UVE{}, 0.0F, noises).loudness, 0.0F);
    EXPECT_FLOAT_EQ(SenseHearingUVE(Math::Vector3UVE{}, 10.0F, {}).loudness, 0.0F);
}

TEST(PerceptionUVETest, Write_KeepsLastPositionWhenUnseen) {
    BlackboardComponentUVE board;
    SightResultUVE sight;
    sight.visible = true;
    sight.distance01 = 0.3F;
    sight.position = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    sight.entity = MakeEntityUVE(7U);
    HearingResultUVE hearing;
    hearing.loudness = 0.5F;
    hearing.position = Math::Vector3UVE{4.0F, 5.0F, 6.0F};
    WritePerceptionResultsUVE(board, "enemy", sight, hearing);
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "sight.enemy.visible"), 1.0F);
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "sight.enemy.distance01"), 0.3F);
    EXPECT_EQ(GetBlackboardVectorUVE(board, "sight.enemy.position"),
              Math::Vector3UVE(1.0F, 2.0F, 3.0F));
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "hearing.loudest"), 0.5F);

    WritePerceptionResultsUVE(board, "enemy", SightResultUVE{}, HearingResultUVE{});
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "sight.enemy.visible"), 0.0F);
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "sight.enemy.distance01"), 0.3F);
    EXPECT_EQ(GetBlackboardVectorUVE(board, "sight.enemy.position"),
              Math::Vector3UVE(1.0F, 2.0F, 3.0F));
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "hearing.loudest"), 0.0F);
    EXPECT_EQ(GetBlackboardVectorUVE(board, "hearing.position"),
              Math::Vector3UVE(4.0F, 5.0F, 6.0F));
}

TEST(PerceptionUVETest, Decay_ClampsAtZeroAndHonoursStillness) {
    NoiseEmitterComponentUVE bang;
    bang.loudness = 1.0F;
    bang.decayPerSecond = 0.5F;
    DecayNoiseEmitterUVE(bang, 1.0F);
    EXPECT_FLOAT_EQ(bang.loudness, 0.5F);
    DecayNoiseEmitterUVE(bang, 5.0F);
    EXPECT_FLOAT_EQ(bang.loudness, 0.0F);

    NoiseEmitterComponentUVE drone;
    drone.loudness = 0.7F;
    drone.decayPerSecond = 0.0F;
    DecayNoiseEmitterUVE(drone, 10.0F);
    EXPECT_FLOAT_EQ(drone.loudness, 0.7F);

    NoiseEmitterComponentUVE frozen = bang;
    frozen.loudness = 0.4F;
    DecayNoiseEmitterUVE(frozen, 0.0F);
    DecayNoiseEmitterUVE(frozen, -1.0F);
    DecayNoiseEmitterUVE(frozen, std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(frozen.loudness, 0.4F);
}

TEST(PerceptionUVETest, BlackboardVectors_SetGetRemove) {
    BlackboardComponentUVE board;
    EXPECT_EQ(GetBlackboardVectorUVE(board, "missing"), Math::Vector3UVE{});
    ASSERT_TRUE(SetBlackboardVectorUVE(board, "home", Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(GetBlackboardVectorUVE(board, "home"), Math::Vector3UVE(1.0F, 2.0F, 3.0F));
    ASSERT_TRUE(SetBlackboardVectorUVE(board, "home", Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(GetBlackboardVectorUVE(board, "home"), Math::Vector3UVE(4.0F, 5.0F, 6.0F));
    EXPECT_FALSE(SetBlackboardVectorUVE(board, "", Math::Vector3UVE{}));
    EXPECT_FALSE(SetBlackboardVectorUVE(
        board, "nan", Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}));
    EXPECT_FALSE(RemoveBlackboardVectorUVE(board, "missing"));
    EXPECT_TRUE(RemoveBlackboardVectorUVE(board, "home"));
    EXPECT_TRUE(board.vectors.empty());
}

TEST(PerceptionUVETest, Validity_RejectsBrokenSensorsEmittersAndVectors) {
    const PerceptionComponentUVE good = MakeSensorUVE();
    EXPECT_TRUE(IsPerceptionComponentValidUVE(good));

    PerceptionComponentUVE noTag = good;
    noTag.watchedTag.clear();
    EXPECT_FALSE(IsPerceptionComponentValidUVE(noTag));

    PerceptionComponentUVE longTag = good;
    longTag.watchedTag = std::string(33U, 'x');
    EXPECT_FALSE(IsPerceptionComponentValidUVE(longTag));

    PerceptionComponentUVE badRange = good;
    badRange.sightRangeMetres = 0.0F;
    EXPECT_FALSE(IsPerceptionComponentValidUVE(badRange));

    PerceptionComponentUVE badFov = good;
    badFov.sightFieldOfViewDegrees = 361.0F;
    EXPECT_FALSE(IsPerceptionComponentValidUVE(badFov));

    PerceptionComponentUVE badHearing = good;
    badHearing.hearingRadiusMetres = -1.0F;
    EXPECT_FALSE(IsPerceptionComponentValidUVE(badHearing));

    NoiseEmitterComponentUVE badLoudness;
    badLoudness.loudness = 1.5F;
    EXPECT_FALSE(IsNoiseEmitterComponentValidUVE(badLoudness));

    BlackboardComponentUVE dupeVectors;
    dupeVectors.vectors.push_back(AiBlackboardVectorEntryUVE{"p", Math::Vector3UVE{}});
    dupeVectors.vectors.push_back(AiBlackboardVectorEntryUVE{"p", Math::Vector3UVE{}});
    EXPECT_FALSE(IsBlackboardComponentValidUVE(dupeVectors));
}

} // namespace UVE::Scene::Tests
