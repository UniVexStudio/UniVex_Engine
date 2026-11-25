// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_ambisonics.h"

#include <array>
#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

constexpr float kTolerance = 1.0e-5F;

[[nodiscard]] float Determinant(const std::array<float, 9U>& m) noexcept {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
}

/// Row `row` dotted with row `column` of the matrix - the (row, column) entry of M * M^T.
[[nodiscard]] float RowDot(const std::array<float, 9U>& m, const std::size_t row,
                              const std::size_t column) noexcept {
    return m[row * 3U] * m[column * 3U] + m[row * 3U + 1U] * m[column * 3U + 1U] +
           m[row * 3U + 2U] * m[column * 3U + 2U];
}

/// Every basis a head can plausibly be in: identity, the four cardinal yaws, and two compound tilts.
struct Basis final {
    Math::Vector3 forward;
    Math::Vector3 up;
};

constexpr std::array<Basis, 7U> kBases{{{Math::Vector3{0.0F, 0.0F, -1.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}},
                                             {Math::Vector3{-1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}},
                                             {Math::Vector3{0.0F, 0.0F, 1.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}},
                                             {Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}},
                                             {Math::Vector3{0.0F, 0.0F, -1.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}},
                                             {Math::Vector3{0.6F, 0.48F, -0.64F}, Math::Vector3{-0.8F, 0.6F, 0.0F}},
                                             {Math::Vector3{-0.36F, 0.48F, 0.8F}, Math::Vector3{0.8F, 0.6F, 0.0F}}}};

/// The three directional coefficients of a frame, as the world vector they describe. Used to assert
/// that a rotation is a rotation rather than a scale or a shear.
[[nodiscard]] Math::Vector3 DirectionOfFrame(const float* frame) noexcept {
    return Math::Vector3{frame[kAmbisonicXChannelIndex], frame[kAmbisonicYChannelIndex],
                            frame[kAmbisonicZChannelIndex]};
}

TEST(AmbisonicGeometryTest, TheEngineBasisMapsOntoTheAmbiXAxes) {
    // AmbiX: +X front, +Y left, +Z up. Engine: +X right, +Y up, -Z forward.
    const Math::Vector3 front = ToAmbisonicDirection(Math::Vector3{0.0F, 0.0F, -1.0F});
    EXPECT_FLOAT_EQ(front.x, 1.0F);
    EXPECT_FLOAT_EQ(front.y, 0.0F);
    EXPECT_FLOAT_EQ(front.z, 0.0F);

    const Math::Vector3 right = ToAmbisonicDirection(Math::Vector3{1.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(right.x, 0.0F);
    EXPECT_FLOAT_EQ(right.y, -1.0F); // AmbiX +Y is LEFT, so the engine's right is -Y.
    EXPECT_FLOAT_EQ(right.z, 0.0F);

    const Math::Vector3 up = ToAmbisonicDirection(Math::Vector3{0.0F, 1.0F, 0.0F});
    EXPECT_FLOAT_EQ(up.x, 0.0F);
    EXPECT_FLOAT_EQ(up.y, 0.0F);
    EXPECT_FLOAT_EQ(up.z, 1.0F);

    // Lengths are preserved...
    const Math::Vector3 arbitrary{0.3F, -1.7F, 2.4F};
    EXPECT_NEAR(Math::LengthSquared(ToAmbisonicDirection(arbitrary)), Math::LengthSquared(arbitrary), kTolerance);

    // ...and so are cross products, which is true only of a proper rotation (determinant +1). A
    // mirrored mapping would flip handedness and silently invert every azimuth.
    const Math::Vector3 a{0.3F, -1.7F, 2.4F};
    const Math::Vector3 b{-2.0F, 0.5F, 0.25F};
    const Math::Vector3 mappedCross = ToAmbisonicDirection(Math::Cross(a, b));
    const Math::Vector3 crossOfMapped = Math::Cross(ToAmbisonicDirection(a), ToAmbisonicDirection(b));
    EXPECT_NEAR(mappedCross.x, crossOfMapped.x, kTolerance);
    EXPECT_NEAR(mappedCross.y, crossOfMapped.y, kTolerance);
    EXPECT_NEAR(mappedCross.z, crossOfMapped.z, kTolerance);
}

TEST(AmbisonicRotationTest, TheDefaultListenerBasisIsTheIdentity) {
    const AmbisonicRotation rotation =
        ComputeAmbisonicRotation(Math::Vector3{0.0F, 0.0F, -1.0F}, Math::Vector3{0.0F, 1.0F, 0.0F});
    EXPECT_EQ(rotation, AmbisonicRotation{});

    const float worldFrame[4U] = {0.5F, -0.25F, 0.125F, 0.75F};
    float listenerFrame[4U] = {};
    RotateFirstOrderAmbisonicFrame(rotation, worldFrame, listenerFrame);
    EXPECT_FLOAT_EQ(listenerFrame[0], worldFrame[0]);
    EXPECT_FLOAT_EQ(listenerFrame[1], worldFrame[1]);
    EXPECT_FLOAT_EQ(listenerFrame[2], worldFrame[2]);
    EXPECT_FLOAT_EQ(listenerFrame[3], worldFrame[3]);
}

TEST(AmbisonicRotationTest, FacingMinusXPlacesAWorldFrontalWaveOnTheRight) {
    // The listener faces -X; +Y is still up, so their right hand points along -Z - straight at a
    // wave arriving from the world's front. Yaw is therefore a sign flip on the Y coefficient.
    const AmbisonicRotation rotation =
        ComputeAmbisonicRotation(Math::Vector3{-1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F});
    EXPECT_NE(rotation, AmbisonicRotation{});

    // A unit plane wave from the world's front in AmbiX is (W, Y, Z, X) = (1, 0, 0, 1).
    const float worldFrame[4U] = {1.0F, 0.0F, 0.0F, 1.0F};
    float listenerFrame[4U] = {};
    RotateFirstOrderAmbisonicFrame(rotation, worldFrame, listenerFrame);
    EXPECT_FLOAT_EQ(listenerFrame[kAmbisonicWChannelIndex], 1.0F);
    EXPECT_NEAR(listenerFrame[kAmbisonicYChannelIndex], -1.0F, kTolerance);
    EXPECT_NEAR(listenerFrame[kAmbisonicZChannelIndex], 0.0F, kTolerance);
    EXPECT_NEAR(listenerFrame[kAmbisonicXChannelIndex], 0.0F, kTolerance);

    // ...which the stereo decode hears as hard right.
    const AudioPanGains stereo = DecodeFirstOrderAmbisonicFrameToStereo(listenerFrame);
    EXPECT_NEAR(stereo.left, 0.0F, kTolerance);
    EXPECT_NEAR(stereo.right, 2.0F, kTolerance);
}

TEST(AmbisonicRotationTest, EveryValidBasisProducesAnOrthonormalProperRotation) {
    for (const Basis& basis : kBases) {
        ASSERT_TRUE(IsAmbisonicListenerBasisValid(basis.forward, basis.up));
        const AmbisonicRotation rotation = ComputeAmbisonicRotation(basis.forward, basis.up);
        const std::array<float, 9U>& matrix = rotation.matrix;

        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                const float expected = row == column ? 1.0F : 0.0F;
                EXPECT_NEAR(RowDot(matrix, row, column), expected, kTolerance)
                    << "row " << row << ", column " << column;
            }
        }
        EXPECT_NEAR(Determinant(matrix), 1.0F, kTolerance);
    }
}

TEST(AmbisonicRotationTest, RotationPreservesWAndTheEnergyOfTheDirectionalChannels) {
    const std::array<std::array<float, 4U>, 4U> frames{{
        {1.0F, 0.0F, 0.0F, 1.0F},
        {0.5F, -1.0F, 0.25F, 0.75F},
        {0.125F, 0.0F, 1.0F, 0.0F},
        {-0.75F, 0.3F, -0.4F, -0.2F},
    }};
    for (const Basis& basis : kBases) {
        const AmbisonicRotation rotation = ComputeAmbisonicRotation(basis.forward, basis.up);
        for (const std::array<float, 4U>& frame : frames) {
            float rotated[4U] = {};
            RotateFirstOrderAmbisonicFrame(rotation, frame.data(), rotated);

            // The omnidirectional channel is a scalar field: turning cannot change it at all.
            EXPECT_FLOAT_EQ(rotated[kAmbisonicWChannelIndex], frame[kAmbisonicWChannelIndex]);
            // The directional part is a vector: turning may move it, never grow or shrink it.
            EXPECT_NEAR(Math::LengthSquared(DirectionOfFrame(rotated)),
                        Math::LengthSquared(DirectionOfFrame(frame.data())), kTolerance);
        }
    }
}

TEST(AmbisonicRotationTest, RollingTheHeadTakesOverheadContentToTheLeftEar) {
    // Head rolled 90 degrees to the right: the top of the head points along +X, so the left ear
    // points at the world's up axis.
    const AmbisonicRotation rotation =
        ComputeAmbisonicRotation(Math::Vector3{0.0F, 0.0F, -1.0F}, Math::Vector3{1.0F, 0.0F, 0.0F});
    const float overheadWave[4U] = {1.0F, 0.0F, 1.0F, 0.0F};
    float rotated[4U] = {};
    RotateFirstOrderAmbisonicFrame(rotation, overheadWave, rotated);
    EXPECT_NEAR(rotated[kAmbisonicYChannelIndex], 1.0F, kTolerance);

    const AudioPanGains stereo = DecodeFirstOrderAmbisonicFrameToStereo(rotated);
    EXPECT_NEAR(stereo.left, 2.0F, kTolerance);
    EXPECT_NEAR(stereo.right, 0.0F, kTolerance);
}

TEST(AmbisonicRotationTest, DegenerateBasesAreRefusedAndFallBackToNoRotation) {
    const Math::Vector3 forward{0.0F, 0.0F, -1.0F};
    const Math::Vector3 up{0.0F, 1.0F, 0.0F};
    EXPECT_TRUE(IsAmbisonicListenerBasisValid(forward, up));

    const std::array<Basis, 3U> degenerate{{
        {Math::Vector3{0.0F, 0.0F, 0.0F}, up},                                        // no forward
        {forward, Math::Vector3{0.0F, 0.0F, 2.0F}},                                   // up parallel to forward
        {Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, -1.0F}, up},    // not a number
    }};
    for (const Basis& basis : degenerate) {
        EXPECT_FALSE(IsAmbisonicListenerBasisValid(basis.forward, basis.up));
        EXPECT_EQ(ComputeAmbisonicRotation(basis.forward, basis.up), AmbisonicRotation{});
    }
}

TEST(AmbisonicRotationTest, InPlaceRotationIsSafe) {
    const AmbisonicRotation rotation =
        ComputeAmbisonicRotation(Math::Vector3{0.6F, 0.48F, -0.64F}, Math::Vector3{-0.8F, 0.6F, 0.0F});
    const float frame[4U] = {0.25F, -0.5F, 0.75F, 0.125F};

    float outOfPlace[4U] = {};
    RotateFirstOrderAmbisonicFrame(rotation, frame, outOfPlace);

    float inPlace[4U] = {frame[0], frame[1], frame[2], frame[3]};
    RotateFirstOrderAmbisonicFrame(rotation, inPlace, inPlace);

    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        EXPECT_FLOAT_EQ(inPlace[channel], outOfPlace[channel]) << "channel " << channel;
    }
}

TEST(AmbisonicStereoDecodeTest, OmnidirectionalContentIsExactlyUnityOnBothChannels) {
    const float frame[4U] = {1.0F, 0.0F, 0.0F, 0.0F};
    const AudioPanGains stereo = DecodeFirstOrderAmbisonicFrameToStereo(frame);
    EXPECT_FLOAT_EQ(stereo.left, 1.0F);
    EXPECT_FLOAT_EQ(stereo.right, 1.0F);

    // Quiet and loud omnidirectional fields scale linearly, with no cross-channel bleed.
    const float louder[4U] = {0.25F, 0.0F, 0.0F, 0.0F};
    const AudioPanGains quiet = DecodeFirstOrderAmbisonicFrameToStereo(louder);
    EXPECT_FLOAT_EQ(quiet.left, 0.25F);
    EXPECT_FLOAT_EQ(quiet.right, 0.25F);
}

TEST(AmbisonicStereoDecodeTest, YSteersTheImageAndIsTheOnlyAzimuthTerm) {
    const float hardLeft[4U] = {1.0F, 1.0F, 0.0F, 0.0F};
    const AudioPanGains left = DecodeFirstOrderAmbisonicFrameToStereo(hardLeft);
    EXPECT_FLOAT_EQ(left.left, 2.0F);
    EXPECT_FLOAT_EQ(left.right, 0.0F);

    const float hardRight[4U] = {1.0F, -1.0F, 0.0F, 0.0F};
    const AudioPanGains right = DecodeFirstOrderAmbisonicFrameToStereo(hardRight);
    EXPECT_FLOAT_EQ(right.left, 0.0F);
    EXPECT_FLOAT_EQ(right.right, 2.0F);

    // Halfway between: a +30 degree wave sits at 1.5 / 0.5, the cardioid pair's linear in-between.
    const float halfLeft[4U] = {1.0F, 0.5F, 0.0F, 0.0F};
    const AudioPanGains between = DecodeFirstOrderAmbisonicFrameToStereo(halfLeft);
    EXPECT_FLOAT_EQ(between.left, 1.5F);
    EXPECT_FLOAT_EQ(between.right, 0.5F);
}

TEST(AmbisonicStereoDecodeTest, FrontAndBackAreIdenticalByConstruction) {
    // Documented, not accidental: two microphones in a line cannot resolve the front/back cone of
    // confusion. If this ever changes, a decoder with more outputs has replaced this one.
    const float fromFront[4U] = {1.0F, 0.0F, 0.0F, 1.0F};
    const float fromBack[4U] = {1.0F, 0.0F, 0.0F, -1.0F};
    const AudioPanGains front = DecodeFirstOrderAmbisonicFrameToStereo(fromFront);
    const AudioPanGains back = DecodeFirstOrderAmbisonicFrameToStereo(fromBack);
    EXPECT_FLOAT_EQ(front.left, back.left);
    EXPECT_FLOAT_EQ(front.right, back.right);
    EXPECT_FLOAT_EQ(front.left, 1.0F);
}

TEST(AmbisonicStereoDecodeTest, GainIsFoldedInAndNonFiniteInputBecomesSilence) {
    const float frame[4U] = {1.0F, 0.0F, 0.0F, 0.0F};
    const AudioPanGains quarter = DecodeFirstOrderAmbisonicFrameToStereo(frame, 0.25F);
    EXPECT_FLOAT_EQ(quarter.left, 0.25F);
    EXPECT_FLOAT_EQ(quarter.right, 0.25F);

    const float badW[4U] = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F};
    const AudioPanGains silent = DecodeFirstOrderAmbisonicFrameToStereo(badW);
    EXPECT_FLOAT_EQ(silent.left, 0.0F);
    EXPECT_FLOAT_EQ(silent.right, 0.0F);

    EXPECT_FLOAT_EQ(DecodeFirstOrderAmbisonicFrameToStereo(frame, -1.0F).left, 0.0F);
    EXPECT_FLOAT_EQ(DecodeFirstOrderAmbisonicFrameToStereo(frame, std::numeric_limits<float>::infinity()).right,
                    0.0F);
    EXPECT_FLOAT_EQ(DecodeFirstOrderAmbisonicFrameToStereo(nullptr).left, 0.0F);

    // A null frame or a bad gain must leave the frame untouched, not partially written.
    float untouched[4U] = {1.0F, 0.0F, 0.0F, 0.0F};
    const AudioPanGains invalid = DecodeFirstOrderAmbisonicFrameToStereo(untouched, std::nanf(""));
    EXPECT_FLOAT_EQ(invalid.left, 0.0F);
    EXPECT_FLOAT_EQ(invalid.right, 0.0F);
}

} // namespace
} // namespace UVE::Audio::Tests
