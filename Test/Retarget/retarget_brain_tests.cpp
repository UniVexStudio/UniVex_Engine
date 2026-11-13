// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "quaternion.h"
#include "retarget_brain.h"
#include "retarget_humanoid.h"
#include "retarget_match.h"
#include "retarget_test_rig.h"

namespace UVE::Retarget {
namespace {

/// A brain small enough to train in a test, the same way the engine's is trained.
[[nodiscard]] RigBrainConfigUVE SmallConfigUVE() {
    RigBrainConfigUVE config;
    config.rigs = 60U;
    config.epochs = 5U;
    config.hidden = 48U;
    return config;
}

/// How many bones of held-out rigs it names right, over the bones that are the humanoid's core.
[[nodiscard]] float AccuracyUVE(const RigBrainUVE& brain, const std::vector<RigBrainSampleUVE>& rigs, const HumanoidReferenceUVE& reference) {
    std::size_t right = 0U;
    std::size_t total = 0U;
    for (const RigBrainSampleUVE& sample : rigs) {
        const std::vector<BonePredictionUVE> predictions = brain.PredictUVE(sample.rig, reference);
        for (std::size_t bone = 0U; bone < predictions.size(); ++bone) {
            const std::int32_t truth = sample.referenceOfBone[bone];
            if (truth < 0 || reference.info[static_cast<std::size_t>(truth)].optional) {
                continue;
            }
            ++total;
            right += predictions[bone].reference == truth ? 1U : 0U;
        }
    }
    return total == 0U ? 0.0F : static_cast<float>(right) / static_cast<float>(total);
}

TEST(RetargetBrainUVETest, SyntheticRigsAreValidAndLabelled) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const std::vector<RigBrainSampleUVE> rigs = MakeSyntheticRigsUVE(reference, 20U, 7U);
    ASSERT_EQ(rigs.size(), 20U);
    for (const RigBrainSampleUVE& sample : rigs) {
        EXPECT_TRUE(IsRetargetSkeletonValidUVE(sample.rig));
        EXPECT_EQ(sample.referenceOfBone.size(), sample.rig.bones.size());
        EXPECT_GE(sample.rig.bones.size(), 20U) << "at least the body";
    }
    EXPECT_EQ(MakeSyntheticRigsUVE(reference, 3U, 7U)[2].rig, MakeSyntheticRigsUVE(reference, 3U, 7U)[2].rig) << "deterministic";
}

TEST(RetargetBrainUVETest, TrainingIsDeterministicAndRoundTrips) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    RigBrainConfigUVE config = SmallConfigUVE();
    config.rigs = 12U;
    config.epochs = 2U;
    RigBrainUVE a;
    RigBrainUVE b;
    a.TrainUVE(reference, {}, config);
    b.TrainUVE(reference, {}, config);
    ASSERT_TRUE(a.IsTrainedUVE());
    EXPECT_EQ(a.WriteUVE(), b.WriteUVE());
    RigBrainUVE read;
    ASSERT_TRUE(read.ReadUVE(a.WriteUVE(), reference));
    EXPECT_EQ(read.WriteUVE(), a.WriteUVE());
    EXPECT_FALSE(read.ReadUVE("{\"format\":\"something else\"}", reference));
}

TEST(RetargetBrainUVETest, NamesHeldOutRigsCoreBonesFromNamesAndShape) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    RigBrainUVE brain;
    brain.TrainUVE(reference, {}, SmallConfigUVE());
    const std::vector<RigBrainSampleUVE> heldOut = MakeSyntheticRigsUVE(reference, 30U, 0xBADC0DEU);
    const float accuracy = AccuracyUVE(brain, heldOut, reference);
    std::printf("[ RigBrain ] held-out core accuracy %.1f%%\n", static_cast<double>(accuracy) * 100.0);
    EXPECT_GT(accuracy, 0.85F);
}

TEST(RetargetBrainUVETest, FindsTheBodyOfARigWhoseNamesSayNothing) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    RigBrainUVE brain;
    brain.TrainUVE(reference, {}, SmallConfigUVE());
    // The test rig with every name replaced by a number.
    TestRigUVE::TPoseRigUVE rig;
    RetargetSkeletonUVE anonymous = rig.skeleton;
    const HumanoidMatchUVE byName = MatchHumanoidUVE(rig.skeleton, reference);
    for (std::size_t bone = 0U; bone < anonymous.bones.size(); ++bone) {
        char text[16];
        std::snprintf(text, sizeof(text), "Bone%03zu", bone);
        anonymous.bones[bone].name = text;
    }
    const HumanoidMatchUVE names = MatchHumanoidUVE(anonymous, reference);
    EXPECT_EQ(names.CountUVE(JointStatusUVE::Good) + names.CountUVE(JointStatusUVE::Warning), 0U) << "names alone find nothing";
    const HumanoidMatchUVE match = MatchHumanoidWithBrainUVE(anonymous, reference, brain, 0.3F);
    std::size_t right = 0U;
    std::size_t total = 0U;
    for (std::size_t bone = 0U; bone < anonymous.bones.size(); ++bone) {
        if (byName.referenceOfBone[bone] < 0) {
            continue;
        }
        ++total;
        right += match.referenceOfBone[bone] == byName.referenceOfBone[bone] ? 1U : 0U;
    }
    std::printf("[ RigBrain ] anonymous rig: %zu of %zu bones found\n", right, total);
    EXPECT_GE(right * 10U, total * 7U) << "at least 70% of an unnamed body found from its shape";
}

/// The test rig with names that say nothing, stood the way `turn` stands it.
[[nodiscard]] RetargetSkeletonUVE AnonymousTurnedUVE(const RetargetSkeletonUVE& rig, const Math::QuaternionUVE& turn) {
    RetargetSkeletonUVE out = rig;
    for (std::size_t bone = 0U; bone < out.bones.size(); ++bone) {
        char text[16];
        std::snprintf(text, sizeof(text), "joint%zu", bone);
        out.bones[bone].name = text;
    }
    if (!out.bones.empty()) {
        // The root carries the turn; everything below follows it.
        out.bones.front().position = Math::RotateVectorUVE(turn, out.bones.front().position);
        out.bones.front().rotation = Math::MultiplyUVE(turn, out.bones.front().rotation);
    }
    return out;
}

TEST(RetargetBrainUVETest, FindsAnUnnamedBodyHoweverItsToolStoodIt) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const RigBrainUVE& brain = GetDefaultRigBrainUVE();
    TestRigUVE::TPoseRigUVE rig;
    const HumanoidMatchUVE byName = MatchHumanoidUVE(rig.skeleton, reference);
    Math::QuaternionUVE zUp{};
    Math::QuaternionUVE away{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, -1.5707963F, zUp));
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 3.1415927F, away));
    for (const Math::QuaternionUVE& turn : {Math::QuaternionUVE{}, zUp, away}) {
        const RetargetSkeletonUVE anonymous = AnonymousTurnedUVE(rig.skeleton, turn);
        const HumanoidMatchUVE match = MatchHumanoidWithBrainUVE(anonymous, reference, brain, 0.3F);
        std::size_t right = 0U;
        std::size_t total = 0U;
        for (std::size_t bone = 0U; bone < anonymous.bones.size(); ++bone) {
            if (byName.referenceOfBone[bone] >= 0) {
                ++total;
                right += match.referenceOfBone[bone] == byName.referenceOfBone[bone] ? 1U : 0U;
            }
        }
        std::printf("[ RigBrain ] unnamed rig turned (%.2f %.2f %.2f %.2f): %zu of %zu\n", static_cast<double>(turn.x),
                    static_cast<double>(turn.y), static_cast<double>(turn.z), static_cast<double>(turn.w), right, total);
        EXPECT_GE(right * 10U, total * 7U) << "at least 70% of an unnamed body, however it stands";
    }
}

TEST(RetargetBrainUVETest, NamesHeldOutRigsInEveryNamingStyle) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const RigBrainUVE& brain = GetDefaultRigBrainUVE();
    // Rigs it never saw (another seed): every naming style, stood every way, a little off the ideal.
    std::vector<RigBrainSampleUVE> named;
    std::vector<RigBrainSampleUVE> unnamed;
    for (RigBrainSampleUVE& sample : MakeSyntheticRigsUVE(reference, 120U, 0x51DE5U)) {
        const std::string& first = sample.rig.bones.front().name;
        (first.starts_with("Bone") || first.starts_with("joint") ? unnamed : named).push_back(std::move(sample));
    }
    ASSERT_FALSE(named.empty());
    ASSERT_FALSE(unnamed.empty());
    const float byName = AccuracyUVE(brain, named, reference);
    const float byShape = AccuracyUVE(brain, unnamed, reference);
    std::printf("[ RigBrain ] held-out: named rigs %.1f%%, rigs whose names say nothing %.1f%%\n",
                static_cast<double>(byName) * 100.0, static_cast<double>(byShape) * 100.0);
    EXPECT_GT(byName, 0.97F);
    // About 0.70 from the network alone; Debug and Release round differently, so a margin.
    EXPECT_GT(byShape, 0.65F);
}

TEST(RetargetBrainUVETest, LearnsFromTheProjectsOwnRigs) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    // A studio's own naming the generated rigs never use.
    TestRigUVE::TPoseRigUVE rig;
    const HumanoidMatchUVE byName = MatchHumanoidUVE(rig.skeleton, reference);
    RigBrainSampleUVE own;
    own.rig = rig.skeleton;
    for (std::size_t bone = 0U; bone < own.rig.bones.size(); ++bone) {
        own.rig.bones[bone].name = "zq" + std::to_string(bone * 7U % 31U) + "_kx";
    }
    own.referenceOfBone = byName.referenceOfBone;
    const auto score = [&](const RigBrainUVE& brain) {
        const std::vector<BonePredictionUVE> predictions = brain.PredictUVE(own.rig, reference);
        std::size_t right = 0U;
        for (std::size_t bone = 0U; bone < predictions.size(); ++bone) {
            right += own.referenceOfBone[bone] >= 0 && predictions[bone].reference == own.referenceOfBone[bone] ? 1U : 0U;
        }
        return right;
    };
    RigBrainUVE before;
    before.TrainUVE(reference, {}, SmallConfigUVE());
    RigBrainUVE after;
    after.TrainUVE(reference, {own}, SmallConfigUVE());
    EXPECT_GT(score(after), score(before));
}


TEST(RetargetBrainUVETest, TheEnginesOwnBrainIsTrainedOnFirstUse) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const RigBrainUVE& brain = GetDefaultRigBrainUVE();
    ASSERT_TRUE(brain.IsTrainedUVE());
    EXPECT_EQ(brain.ClassCountUVE(), reference.skeleton.bones.size() + 1U);
    const float accuracy = AccuracyUVE(brain, MakeSyntheticRigsUVE(reference, 30U, 0xFEEDU), reference);
    std::printf("[ RigBrain ] engine brain held-out core accuracy %.1f%%\n", static_cast<double>(accuracy) * 100.0);
    EXPECT_GT(accuracy, 0.92F);
}


TEST(RetargetBrainUVETest, AProjectsSamplesRoundTrip) {
    const std::vector<RigBrainSampleUVE> samples = MakeSyntheticRigsUVE(GetHumanoidReferenceUVE(), 2U, 3U);
    const std::vector<RigBrainSampleUVE> read = ReadRigBrainSamplesUVE(WriteRigBrainSamplesUVE(samples));
    ASSERT_EQ(read.size(), samples.size());
    for (std::size_t i = 0U; i < samples.size(); ++i) {
        EXPECT_EQ(read[i].referenceOfBone, samples[i].referenceOfBone);
        ASSERT_EQ(read[i].rig.bones.size(), samples[i].rig.bones.size());
        EXPECT_EQ(read[i].rig.bones.back().name, samples[i].rig.bones.back().name);
    }
    EXPECT_TRUE(ReadRigBrainSamplesUVE("not json").empty());
}

} // namespace
} // namespace UVE::Retarget
