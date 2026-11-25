// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace UVE::Platform {

enum class XrEye : std::uint8_t { Left = 0, Right = 1 };
enum class XrHand : std::uint8_t { Left = 0, Right = 1 };

struct XrPose final {
    std::array<float, 3> position{0.0F, 1.6F, 0.0F};
    std::array<float, 4> orientation{0.0F, 0.0F, 0.0F, 1.0F}; // xyzw
    bool tracked = false;
};

struct XrEyeView final {
    XrPose pose{};
    float fovLeftRad = -0.785398F;
    float fovRightRad = 0.785398F;
    float fovUpRad = 0.785398F;
    float fovDownRad = -0.785398F;
};

struct XrPlayArea final {
    float widthMetres = 2.5F;
    float depthMetres = 2.5F;
    bool roomScale = true;
};

struct XrControllerState final {
    XrPose pose{};
    float trigger = 0.0F;
    float grip = 0.0F;
    std::array<float, 2> thumbstick{0.0F, 0.0F};
    bool primaryButton = false;
};

/// Stereo VR/AR runtime abstraction (OpenXR-compatible shape) with HMD stereo views, tracked
/// controllers, and room-scale/seated play area reporting.
class IXrRuntime {
public:
    virtual ~IXrRuntime() = default;
    [[nodiscard]] virtual std::string_view RuntimeName() const noexcept = 0;
    [[nodiscard]] virtual bool IsSessionActive() const noexcept = 0;
    virtual bool BeginFrame() = 0;
    virtual void EndFrame() = 0;

    [[nodiscard]] virtual XrPose GetHeadPose() const noexcept = 0;
    [[nodiscard]] virtual XrEyeView GetEyeView(XrEye eye) const noexcept = 0;
    [[nodiscard]] virtual XrControllerState GetControllerState(XrHand hand) const noexcept = 0;
    [[nodiscard]] virtual XrPlayArea GetPlayArea() const noexcept = 0;
};

/// Deterministic software XR/HMD simulator for stereo camera & tracked controller verification.
class MockXrRuntime final : public IXrRuntime {
public:
    explicit MockXrRuntime(float ipdMetres = 0.064F) noexcept;

    [[nodiscard]] std::string_view RuntimeName() const noexcept override { return "MockXR"; }
    [[nodiscard]] bool IsSessionActive() const noexcept override { return m_active; }
    bool BeginFrame() override;
    void EndFrame() override;

    void SetHeadPose(const XrPose& pose) noexcept { m_headPose = pose; }
    void SetControllerState(XrHand hand, const XrControllerState& state) noexcept;
    void SetPlayArea(const XrPlayArea& area) noexcept { m_playArea = area; }

    [[nodiscard]] XrPose GetHeadPose() const noexcept override { return m_headPose; }
    [[nodiscard]] XrEyeView GetEyeView(XrEye eye) const noexcept override;
    [[nodiscard]] XrControllerState GetControllerState(XrHand hand) const noexcept override;
    [[nodiscard]] XrPlayArea GetPlayArea() const noexcept override { return m_playArea; }

private:
    float m_ipdMetres = 0.064F;
    bool m_active = true;
    XrPose m_headPose{};
    std::array<XrControllerState, 2> m_controllers{};
    XrPlayArea m_playArea{};
};

} // namespace UVE::Platform
