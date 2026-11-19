// Generated from math.uvs by uvsc. Do not edit: regenerate it.
// Program fingerprint 0xebabbc91fba6a2adULL.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "blueprint_native.h"

namespace {

using namespace UVE::Blueprint;
using namespace UVE::Blueprint::Native;

const ValueUVE kConstants[] = {
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x3ff921fb54442d18ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x3fd0000000000000ULL})},
    ValueUVE{std::string{"tab\011here \"quoted\" \077\077 done"}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{2}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{2}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{2}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{true},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{7}},
    ValueUVE{std::int64_t{2}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4004000000000000ULL})},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x400c000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x3ff8000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4008000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4010000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4024000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4014000000000000ULL})},
};

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f);
std::int64_t T0(ContextUVE& c, std::int64_t a0);
StatusUVE Chunk1(ContextUVE& c, FrameUVE& f);
std::int64_t T1(ContextUVE& c, std::int64_t a0);
StatusUVE Chunk2(ContextUVE& c, FrameUVE& f);
std::int64_t T2(ContextUVE& c, std::int64_t a0);
StatusUVE Chunk3(ContextUVE& c, FrameUVE& f);
double T3(ContextUVE& c);
StatusUVE Chunk4(ContextUVE& c, FrameUVE& f);
Vec3ValueUVE T4(ContextUVE& c, double a0);
StatusUVE Chunk5(ContextUVE& c, FrameUVE& f);
void T5(ContextUVE& c);

// sum_to (typed)
std::int64_t T0(ContextUVE& c, std::int64_t a0) {
    [[maybe_unused]] std::int64_t L0 = std::move(a0);
    [[maybe_unused]] std::int64_t L1{};
    [[maybe_unused]] std::int64_t L2{};
    [[maybe_unused]] std::int64_t L3{};
    [[maybe_unused]] bool s0b{};
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] std::int64_t s1i{};
    { c.StepUVE(6U); s0i = std::int64_t{0}; }
    { c.StepUVE(6U); L1 = std::move(s0i); }
    { c.StepUVE(7U); s0i = std::int64_t{0}; }
    { c.StepUVE(7U); L2 = std::move(s0i); }
    { c.StepUVE(7U); s0i = L0; }
    { c.StepUVE(7U); s1i = std::int64_t{1}; }
    { c.StepUVE(7U); s0i = s0i + s1i; }
    { c.StepUVE(7U); L3 = std::move(s0i); }
I8:
    { c.StepUVE(7U); s0i = L2; }
    { c.StepUVE(7U); s1i = L3; }
    { c.StepUVE(7U); s0b = static_cast<double>(s0i) < static_cast<double>(s1i); }
    { c.StepUVE(7U); if (!s0b) goto I29; }
    { c.StepUVE(8U); s0i = L2; }
    { c.StepUVE(8U); s1i = std::int64_t{2}; }
    { c.StepUVE(8U); if (s1i == 0) throw ErrorUVE{"'%' by zero"}; s0i = s0i % s1i; }
    { c.StepUVE(8U); s1i = std::int64_t{0}; }
    { c.StepUVE(8U); s0b = static_cast<double>(s0i) == static_cast<double>(s1i); }
    { c.StepUVE(8U); if (!s0b) goto I20; }
    { c.StepUVE(9U); goto I24; }
I20:
    { c.StepUVE(10U); s0i = L1; }
    { c.StepUVE(10U); s1i = L2; }
    { c.StepUVE(10U); s0i = s0i + s1i; }
    { c.StepUVE(10U); L1 = std::move(s0i); }
I24:
    { c.StepUVE(7U); s0i = L2; }
    { c.StepUVE(7U); s1i = std::int64_t{1}; }
    { c.StepUVE(7U); s0i = s0i + s1i; }
    { c.StepUVE(7U); L2 = std::move(s0i); }
    { c.StepUVE(7U); goto I8; }
I29:
    { c.StepUVE(11U); s0i = L1; }
    { c.StepUVE(11U); return s0i; }
    return {};
}

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f) {
    f.result = ValueUVE{T0(c, std::get<std::int64_t>(f.locals[0]))};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// fib (typed)
std::int64_t T1(ContextUVE& c, std::int64_t a0) {
    [[maybe_unused]] std::int64_t L0 = std::move(a0);
    [[maybe_unused]] bool s0b{};
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] std::int64_t s1i{};
    [[maybe_unused]] std::int64_t s2i{};
    { c.StepUVE(14U); s0i = L0; }
    { c.StepUVE(14U); s1i = std::int64_t{2}; }
    { c.StepUVE(14U); s0b = static_cast<double>(s0i) < static_cast<double>(s1i); }
    { c.StepUVE(14U); if (!s0b) goto I7; }
    { c.StepUVE(15U); s0i = L0; }
    { c.StepUVE(15U); return s0i; }
I7:
    { c.StepUVE(16U); s0i = L0; }
    { c.StepUVE(16U); s1i = std::int64_t{1}; }
    { c.StepUVE(16U); s0i = s0i - s1i; }
    { c.StepUVE(16U); if (c.depth + 1U >= 256U) throw ErrorUVE{"functions call each other too deeply"}; ++c.depth; s0i = T1(c, s0i); --c.depth; }
    { c.StepUVE(16U); s1i = L0; }
    { c.StepUVE(16U); s2i = std::int64_t{2}; }
    { c.StepUVE(16U); s1i = s1i - s2i; }
    { c.StepUVE(16U); if (c.depth + 1U >= 256U) throw ErrorUVE{"functions call each other too deeply"}; ++c.depth; s1i = T1(c, s1i); --c.depth; }
    { c.StepUVE(16U); s0i = s0i + s1i; }
    { c.StepUVE(16U); return s0i; }
    return {};
}

StatusUVE Chunk1(ContextUVE& c, FrameUVE& f) {
    f.result = ValueUVE{T1(c, std::get<std::int64_t>(f.locals[0]))};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// first_over (typed)
std::int64_t T2(ContextUVE& c, std::int64_t a0) {
    [[maybe_unused]] std::int64_t L0 = std::move(a0);
    [[maybe_unused]] std::int64_t L1{};
    [[maybe_unused]] bool s0b{};
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] std::int64_t s1i{};
    { c.StepUVE(19U); s0i = std::int64_t{0}; }
    { c.StepUVE(19U); L1 = std::move(s0i); }
I2:
    { c.StepUVE(20U); s0b = true; }
    { c.StepUVE(20U); if (!s0b) goto I17; }
    { c.StepUVE(21U); s0i = L1; }
    { c.StepUVE(21U); s1i = std::int64_t{1}; }
    { c.StepUVE(21U); s0i = s0i + s1i; }
    { c.StepUVE(21U); L1 = std::move(s0i); }
    { c.StepUVE(22U); s0i = L1; }
    { c.StepUVE(22U); s1i = L1; }
    { c.StepUVE(22U); s0i = s0i * s1i; }
    { c.StepUVE(22U); s1i = L0; }
    { c.StepUVE(22U); s0b = static_cast<double>(s0i) > static_cast<double>(s1i); }
    { c.StepUVE(22U); if (!s0b) goto I16; }
    { c.StepUVE(23U); goto I17; }
I16:
    { c.StepUVE(20U); goto I2; }
I17:
    { c.StepUVE(24U); s0i = L1; }
    { c.StepUVE(24U); return s0i; }
    return {};
}

StatusUVE Chunk2(ContextUVE& c, FrameUVE& f) {
    f.result = ValueUVE{T2(c, std::get<std::int64_t>(f.locals[0]))};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// mix (typed)
double T3(ContextUVE& c) {
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] double s0d{};
    [[maybe_unused]] std::int64_t s1i{};
    [[maybe_unused]] double s1d{};
    [[maybe_unused]] double s2d{};
    { c.StepUVE(27U); s0i = std::int64_t{7}; }
    { c.StepUVE(27U); s0d = static_cast<double>(s0i); }
    { c.StepUVE(27U); s1i = std::int64_t{2}; }
    { c.StepUVE(27U); s1d = static_cast<double>(s1i); }
    { c.StepUVE(27U); if (s1d == 0.0) throw ErrorUVE{"division by zero"}; s0d = s0d / s1d; }
    { c.StepUVE(27U); s1i = std::int64_t{1}; }
    { c.StepUVE(27U); s1d = static_cast<double>(s1i); }
    { c.StepUVE(27U); s2d = std::bit_cast<double>(std::uint64_t{0x4004000000000000ULL}); }
    { c.StepUVE(27U); s1d = s1d * s2d; }
    { c.StepUVE(27U); s0d = s0d + s1d; }
    { c.StepUVE(27U); s1i = std::int64_t{1}; }
    { c.StepUVE(27U); s1i = -s1i; }
    { c.StepUVE(27U); s1d = static_cast<double>(s1i); }
    { c.StepUVE(27U); s0d = s0d - s1d; }
    { c.StepUVE(27U); return s0d; }
    return {};
}

StatusUVE Chunk3(ContextUVE& c, FrameUVE& f) {
    f.result = ValueUVE{T3(c)};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// shapes (typed)
Vec3ValueUVE T4(ContextUVE& c, double a0) {
    [[maybe_unused]] double L0 = std::move(a0);
    [[maybe_unused]] Vec3ValueUVE L1{};
    [[maybe_unused]] bool s0b{};
    [[maybe_unused]] double s0d{};
    [[maybe_unused]] Vec3ValueUVE s0v{};
    [[maybe_unused]] double s1d{};
    [[maybe_unused]] Vec3ValueUVE s1v{};
    [[maybe_unused]] double s2d{};
    [[maybe_unused]] Vec3ValueUVE s2v{};
    [[maybe_unused]] double s3d{};
    [[maybe_unused]] double s4d{};
    { c.StepUVE(30U); s0d = L0; }
    { c.StepUVE(30U); s1d = std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL}); }
    { c.StepUVE(30U); s2d = std::bit_cast<double>(std::uint64_t{0x400c000000000000ULL}); }
    { c.StepUVE(30U); s2d = -s2d; }
    { c.StepUVE(30U); s0v = Vec3ValueUVE{s0d, s1d, s2d}; }
    { c.StepUVE(30U); s1d = std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL}); }
    { c.StepUVE(30U); s0v = Vec3ValueUVE{s0v.x * s1d, s0v.y * s1d, s0v.z * s1d}; }
    { c.StepUVE(30U); L1 = std::move(s0v); }
    { c.StepUVE(31U); s0v = L1; }
    { c.StepUVE(31U); s1v = L1; }
    { c.StepUVE(31U); s1d = s1v.y; }
    { c.StepUVE(31U); s2d = std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}); }
    { c.StepUVE(31U); s3d = std::bit_cast<double>(std::uint64_t{0x3ff8000000000000ULL}); }
    { c.StepUVE(31U); s1d = std::clamp(s1d, s2d, std::max(s2d, s3d)); }
    { c.StepUVE(31U); s2d = std::bit_cast<double>(std::uint64_t{0x4008000000000000ULL}); }
    { c.StepUVE(31U); s3d = std::bit_cast<double>(std::uint64_t{0x4010000000000000ULL}); }
    { c.StepUVE(31U); s4d = std::bit_cast<double>(std::uint64_t{0x0000000000000000ULL}); }
    { c.StepUVE(31U); s2v = Vec3ValueUVE{s2d, s3d, s4d}; }
    { c.StepUVE(31U); s2d = std::sqrt(s2v.x * s2v.x + s2v.y * s2v.y + s2v.z * s2v.z); }
    { c.StepUVE(31U); s1d = s1d + s2d; }
    { c.StepUVE(31U); s0v.y = s1d; }
    { c.StepUVE(31U); L1 = std::move(s0v); }
    { c.StepUVE(32U); s0d = L0; }
    { c.StepUVE(32U); s1d = std::bit_cast<double>(std::uint64_t{0x3ff0000000000000ULL}); }
    { c.StepUVE(32U); s0b = s0d > s1d; }
    { c.StepUVE(32U); if (!s0b) goto I29; }
    { c.StepUVE(32U); s0d = L0; }
    { c.StepUVE(32U); s1d = std::bit_cast<double>(std::uint64_t{0x4024000000000000ULL}); }
    { c.StepUVE(32U); s0b = s0d < s1d; }
I29:
    { c.StepUVE(32U); s0b = !s0b; }
    { c.StepUVE(32U); if (s0b) goto I34; }
    { c.StepUVE(32U); s0d = L0; }
    { c.StepUVE(32U); s1d = std::bit_cast<double>(std::uint64_t{0x4014000000000000ULL}); }
    { c.StepUVE(32U); s0b = s0d == s1d; }
I34:
    { c.StepUVE(32U); if (!s0b) goto I39; }
    { c.StepUVE(33U); s0v = L1; }
    { c.StepUVE(33U); { const Vec3ValueUVE v = s0v; const double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); s0v = n == 0.0 ? v : Vec3ValueUVE{v.x / n, v.y / n, v.z / n}; } }
    { c.StepUVE(33U); return s0v; }
I39:
    { c.StepUVE(34U); s0v = L1; }
    { c.StepUVE(34U); return s0v; }
    return {};
}

StatusUVE Chunk4(ContextUVE& c, FrameUVE& f) {
    f.result = ValueUVE{T4(c, std::get<double>(f.locals[0]))};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// field initializers (typed)
void T5(ContextUVE& c) {
    [[maybe_unused]] double s0d{};
    [[maybe_unused]] std::string s0s{};
    { c.StepUVE(1U); s0d = std::bit_cast<double>(std::uint64_t{0x3ff921fb54442d18ULL}); }
    { c.StepUVE(1U); c.fields[0] = ValueUVE{s0d}; }
    { c.StepUVE(2U); s0d = std::bit_cast<double>(std::uint64_t{0x3fd0000000000000ULL}); }
    { c.StepUVE(2U); c.fields[1] = ValueUVE{s0d}; }
    { c.StepUVE(3U); s0s = std::string{"tab\011here \"quoted\" \077\077 done"}; }
    { c.StepUVE(3U); c.fields[2] = ValueUVE{s0s}; }
    { c.StepUVE(0U); return; }
    return;
}

StatusUVE Chunk5(ContextUVE& c, FrameUVE& f) {
    T5(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

const ChunkFunctionUVE kChunks[] = {Chunk0, Chunk1, Chunk2, Chunk3, Chunk4, Chunk5};

[[maybe_unused]] const RegistrationUVE kRegistration{ProgramTableUVE{0xebabbc91fba6a2adULL, kChunks, 5U}};

} // namespace
