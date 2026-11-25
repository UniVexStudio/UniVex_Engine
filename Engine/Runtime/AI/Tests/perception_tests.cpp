// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "perception.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

namespace {

Entity MakeEntity(std::uint32_t id) {
    Entity entity;
    entity.index = id;
    entity.generation = 1U;
    return entity;
}

PerceptionComponent MakeSensor() {
    PerceptionComponent sensor;
    sensor.watchedTag = "enemy";
    sensor.sightRangeMetres = 20.0F;
    sensor.sightFieldOfViewDegrees = 90.0F;
    sensor.hearingRadiusMetres = 10.0F;
    return sensor;
}

} // namespace

TEST(PerceptionTest, Sight_SeesNearestUnoccludedAndAsksNearestFirst) {
    const PerceptionComponent sensor = MakeSensor();
    const std::vector<SightTarget> candidates{
        SightTarget{MakeEntity(1U), Math::Vector3{0.0F, 0.0F, -4.0F}},
        SightTarget{MakeEntity(2U), Math::Vector3{3.0F, 0.0F, -4.0F}},
        SightTarget{MakeEntity(3U), Math::Vector3{0.0F, 0.0F, -50.0F}},
    };
    std::vector<Entity> asked;
    const SightOcclusionQuery occluded =
        [&asked](const Math::Vector3&, const Math::Vector3&, const Entity target) {
            asked.push_back(target);
            return target == MakeEntity(1U);
        };
    const SightResult result =
        SenseSight(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -1.0F}, sensor, candidates, occluded);
    EXPECT_TRUE(result.visible);
    EXPECT_EQ(result.entity, MakeEntity(2U));
    EXPECT_FLOAT_EQ(result.distance01, 0.25F);
    EXPECT_EQ(result.position, Math::Vector3(3.0F, 0.0F, -4.0F));
    EXPECT_EQ(asked, (std::vector<Entity>{MakeEntity(1U), MakeEntity(2U)}));
}

TEST(PerceptionTest, Sight_RespectsConeOmnidirectionalAndContact) {
    const PerceptionComponent sensor = MakeSensor();
    const std::vector<SightTarget> behind{
        SightTarget{MakeEntity(1U), Math::Vector3{0.0F, 0.0F, 5.0F}},
    };
    const SightOcclusionQuery clear =
        [](const Math::Vector3&, const Math::Vector3&, const Entity) { return false; };
    EXPECT_FALSE(SenseSight(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -1.0F}, sensor, behind, clear)
                     .visible);

    PerceptionComponent eyesBehind = sensor;
    eyesBehind.sightFieldOfViewDegrees = 360.0F;
    const SightResult seen =
        SenseSight(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -1.0F}, eyesBehind, behind, clear);
    EXPECT_TRUE(seen.visible);
    EXPECT_FLOAT_EQ(seen.distance01, 0.25F);

    const std::vector<SightTarget> contact{
        SightTarget{MakeEntity(1U), Math::Vector3{}},
    };
    const SightResult felt =
        SenseSight(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -1.0F}, sensor, contact, clear);
    EXPECT_TRUE(felt.visible);
    EXPECT_FLOAT_EQ(felt.distance01, 0.0F);
}

TEST(PerceptionTest, Sight_SkipsOcclusionWhenDisabled) {
    PerceptionComponent sensor = MakeSensor();
    sensor.requiresLineOfSight = false;
    const std::vector<SightTarget> candidates{
        SightTarget{MakeEntity(1U), Math::Vector3{0.0F, 0.0F, -9.0F}},
        SightTarget{MakeEntity(2U), Math::Vector3{0.0F, 0.0F, -4.0F}},
    };
    bool asked = false;
    const SightOcclusionQuery never =
        [&asked](const Math::Vector3&, const Math::Vector3&, const Entity) {
            asked = true;
            return true;
        };
    const SightResult result =
        SenseSight(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -1.0F}, sensor, candidates, never);
    EXPECT_TRUE(result.visible);
    EXPECT_EQ(result.entity, MakeEntity(2U));
    EXPECT_FALSE(asked);
}

TEST(PerceptionTest, Hearing_HearsLoudestWithFalloff) {
    const std::vector<HeardNoise> noises{
        HeardNoise{Math::Vector3{5.0F, 0.0F, 0.0F}, 1.0F},
        HeardNoise{Math::Vector3{1.0F, 0.0F, 0.0F}, 0.5F},
        HeardNoise{Math::Vector3{9.0F, 0.0F, 0.0F}, 0.5F},
        HeardNoise{Math::Vector3{1.0F, 0.0F, 0.0F}, 0.0F},
    };
    const HearingResult result = SenseHearing(Math::Vector3{}, 10.0F, noises);
    EXPECT_FLOAT_EQ(result.loudness, 0.5F);
    EXPECT_EQ(result.position, Math::Vector3(5.0F, 0.0F, 0.0F));

    EXPECT_FLOAT_EQ(SenseHearing(Math::Vector3{}, 0.0F, noises).loudness, 0.0F);
    EXPECT_FLOAT_EQ(SenseHearing(Math::Vector3{}, 10.0F, {}).loudness, 0.0F);
}

TEST(PerceptionTest, Write_KeepsLastPositionWhenUnseen) {
    BlackboardComponent board;
    SightResult sight;
    sight.visible = true;
    sight.distance01 = 0.3F;
    sight.position = Math::Vector3{1.0F, 2.0F, 3.0F};
    sight.entity = MakeEntity(7U);
    HearingResult hearing;
    hearing.loudness = 0.5F;
    hearing.position = Math::Vector3{4.0F, 5.0F, 6.0F};
    WritePerceptionResults(board, "enemy", sight, hearing);
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "sight.enemy.visible"), 1.0F);
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "sight.enemy.distance01"), 0.3F);
    EXPECT_EQ(GetBlackboardVector(board, "sight.enemy.position"),
              Math::Vector3(1.0F, 2.0F, 3.0F));
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "hearing.loudest"), 0.5F);

    WritePerceptionResults(board, "enemy", SightResult{}, HearingResult{});
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "sight.enemy.visible"), 0.0F);
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "sight.enemy.distance01"), 0.3F);
    EXPECT_EQ(GetBlackboardVector(board, "sight.enemy.position"),
              Math::Vector3(1.0F, 2.0F, 3.0F));
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "hearing.loudest"), 0.0F);
    EXPECT_EQ(GetBlackboardVector(board, "hearing.position"),
              Math::Vector3(4.0F, 5.0F, 6.0F));
}

TEST(PerceptionTest, Decay_ClampsAtZeroAndHonoursStillness) {
    NoiseEmitterComponent bang;
    bang.loudness = 1.0F;
    bang.decayPerSecond = 0.5F;
    DecayNoiseEmitter(bang, 1.0F);
    EXPECT_FLOAT_EQ(bang.loudness, 0.5F);
    DecayNoiseEmitter(bang, 5.0F);
    EXPECT_FLOAT_EQ(bang.loudness, 0.0F);

    NoiseEmitterComponent drone;
    drone.loudness = 0.7F;
    drone.decayPerSecond = 0.0F;
    DecayNoiseEmitter(drone, 10.0F);
    EXPECT_FLOAT_EQ(drone.loudness, 0.7F);

    NoiseEmitterComponent frozen = bang;
    frozen.loudness = 0.4F;
    DecayNoiseEmitter(frozen, 0.0F);
    DecayNoiseEmitter(frozen, -1.0F);
    DecayNoiseEmitter(frozen, std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(frozen.loudness, 0.4F);
}

TEST(PerceptionTest, BlackboardVectors_SetGetRemove) {
    BlackboardComponent board;
    EXPECT_EQ(GetBlackboardVector(board, "missing"), Math::Vector3{});
    ASSERT_TRUE(SetBlackboardVector(board, "home", Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(GetBlackboardVector(board, "home"), Math::Vector3(1.0F, 2.0F, 3.0F));
    ASSERT_TRUE(SetBlackboardVector(board, "home", Math::Vector3{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(GetBlackboardVector(board, "home"), Math::Vector3(4.0F, 5.0F, 6.0F));
    EXPECT_FALSE(SetBlackboardVector(board, "", Math::Vector3{}));
    EXPECT_FALSE(SetBlackboardVector(
        board, "nan", Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}));
    EXPECT_FALSE(RemoveBlackboardVector(board, "missing"));
    EXPECT_TRUE(RemoveBlackboardVector(board, "home"));
    EXPECT_TRUE(board.vectors.empty());
}

TEST(PerceptionTest, Validity_RejectsBrokenSensorsEmittersAndVectors) {
    const PerceptionComponent good = MakeSensor();
    EXPECT_TRUE(IsPerceptionComponentValid(good));

    PerceptionComponent noTag = good;
    noTag.watchedTag.clear();
    EXPECT_FALSE(IsPerceptionComponentValid(noTag));

    PerceptionComponent longTag = good;
    longTag.watchedTag = std::string(33U, 'x');
    EXPECT_FALSE(IsPerceptionComponentValid(longTag));

    PerceptionComponent badRange = good;
    badRange.sightRangeMetres = 0.0F;
    EXPECT_FALSE(IsPerceptionComponentValid(badRange));

    PerceptionComponent badFov = good;
    badFov.sightFieldOfViewDegrees = 361.0F;
    EXPECT_FALSE(IsPerceptionComponentValid(badFov));

    PerceptionComponent badHearing = good;
    badHearing.hearingRadiusMetres = -1.0F;
    EXPECT_FALSE(IsPerceptionComponentValid(badHearing));

    NoiseEmitterComponent badLoudness;
    badLoudness.loudness = 1.5F;
    EXPECT_FALSE(IsNoiseEmitterComponentValid(badLoudness));

    BlackboardComponent dupeVectors;
    dupeVectors.vectors.push_back(AiBlackboardVectorEntry{"p", Math::Vector3{}});
    dupeVectors.vectors.push_back(AiBlackboardVectorEntry{"p", Math::Vector3{}});
    EXPECT_FALSE(IsBlackboardComponentValid(dupeVectors));
}

} // namespace UVE::Scene::Tests
