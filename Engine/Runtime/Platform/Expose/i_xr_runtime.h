// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace UVE::Platform {

enum class XrEyeUVE : std::uint8_t { Left = 0, Right = 1 };
enum class XrHandUVE : std::uint8_t { Left = 0, Right = 1 };

struct XrPoseUVE final {
    std::array<float, 3> position{0.0F, 1.6F, 0.0F};
    std::array<float, 4> orientation{0.0F, 0.0F, 0.0F, 1.0F}; // xyzw
    bool tracked = false;
};

struct XrEyeViewUVE final {
    XrPoseUVE pose{};
    float fovLeftRad = -0.785398F;
    float fovRightRad = 0.785398F;
    float fovUpRad = 0.785398F;
    float fovDownRad = -0.785398F;
};

struct XrPlayAreaUVE final {
    float widthMetres = 2.5F;
    float depthMetres = 2.5F;
    bool roomScale = true;
};

struct XrControllerStateUVE final {
    XrPoseUVE pose{};
    float trigger = 0.0F;
    float grip = 0.0F;
    std::array<float, 2> thumbstick{0.0F, 0.0F};
    bool primaryButton = false;
};

/// Stereo VR/AR runtime abstraction (OpenXR-compatible shape) with HMD stereo views, tracked
/// controllers, and room-scale/seated play area reporting.
class IXrRuntimeUVE {
public:
    virtual ~IXrRuntimeUVE() = default;
    [[nodiscard]] virtual std::string_view RuntimeNameUVE() const noexcept = 0;
    [[nodiscard]] virtual bool IsSessionActiveUVE() const noexcept = 0;
    virtual bool BeginFrameUVE() = 0;
    virtual void EndFrameUVE() = 0;

    [[nodiscard]] virtual XrPoseUVE GetHeadPoseUVE() const noexcept = 0;
    [[nodiscard]] virtual XrEyeViewUVE GetEyeViewUVE(XrEyeUVE eye) const noexcept = 0;
    [[nodiscard]] virtual XrControllerStateUVE GetControllerStateUVE(XrHandUVE hand) const noexcept = 0;
    [[nodiscard]] virtual XrPlayAreaUVE GetPlayAreaUVE() const noexcept = 0;
};

/// Deterministic software XR/HMD simulator for stereo camera & tracked controller verification.
class MockXrRuntimeUVE final : public IXrRuntimeUVE {
public:
    explicit MockXrRuntimeUVE(float ipdMetres = 0.064F) noexcept;

    [[nodiscard]] std::string_view RuntimeNameUVE() const noexcept override { return "MockXR"; }
    [[nodiscard]] bool IsSessionActiveUVE() const noexcept override { return m_active; }
    bool BeginFrameUVE() override;
    void EndFrameUVE() override;

    void SetHeadPoseUVE(const XrPoseUVE& pose) noexcept { m_headPose = pose; }
    void SetControllerStateUVE(XrHandUVE hand, const XrControllerStateUVE& state) noexcept;
    void SetPlayAreaUVE(const XrPlayAreaUVE& area) noexcept { m_playArea = area; }

    [[nodiscard]] XrPoseUVE GetHeadPoseUVE() const noexcept override { return m_headPose; }
    [[nodiscard]] XrEyeViewUVE GetEyeViewUVE(XrEyeUVE eye) const noexcept override;
    [[nodiscard]] XrControllerStateUVE GetControllerStateUVE(XrHandUVE hand) const noexcept override;
    [[nodiscard]] XrPlayAreaUVE GetPlayAreaUVE() const noexcept override { return m_playArea; }

private:
    float m_ipdMetres = 0.064F;
    bool m_active = true;
    XrPoseUVE m_headPose{};
    std::array<XrControllerStateUVE, 2> m_controllers{};
    XrPlayAreaUVE m_playArea{};
};

} // namespace UVE::Platform
