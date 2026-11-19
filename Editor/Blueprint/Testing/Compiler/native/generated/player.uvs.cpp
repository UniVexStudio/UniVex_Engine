// Generated from player.uvs by uvsc. Do not edit: regenerate it.
// Program fingerprint 0xf3f876930ad02b60ULL.

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
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4018000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL})},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::string{" jumps, speed "}},
    ValueUVE{std::string{"velocity"}},
    ValueUVE{std::string{"left"}},
    ValueUVE{std::string{"right"}},
    ValueUVE{std::string{"input.axis"}},
    ValueUVE{std::string{"grounded"}},
    ValueUVE{std::string{"velocity"}},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL})},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x402399999999999aULL})},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::string{"velocity"}},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x402399999999999aULL})},
};

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f);
void T0(ContextUVE& c);
StatusUVE Chunk1(ContextUVE& c, FrameUVE& f);
void T1(ContextUVE& c, double a0);
StatusUVE Chunk2(ContextUVE& c, FrameUVE& f);
void T2(ContextUVE& c);

// on ready (typed)
void T0(ContextUVE& c) {
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] std::string s0s{};
    [[maybe_unused]] std::string s1s{};
    [[maybe_unused]] double s2d{};
    { c.StepUVE(1U); s0i = std::get<std::int64_t>(c.fields[2]); }
    { c.StepUVE(7U); s1s = std::string{" jumps, speed "}; }
    { c.StepUVE(1U); s2d = std::get<double>(c.fields[0]); }
    { c.StepUVE(7U); { std::string t = FormatValueUVE(ValueUVE{s0i}) + FormatValueUVE(ValueUVE{s1s}) + FormatValueUVE(ValueUVE{s2d}); s0s = std::move(t); } }
    { c.StepUVE(7U); c.host.PrintUVE(FormatValueUVE(ValueUVE{s0s})); }
    { c.StepUVE(7U);  }
    { c.StepUVE(6U); return; }
    return;
}

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f) {
    T0(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// on tick (typed)
void T1(ContextUVE& c, double a0) {
    [[maybe_unused]] double L0 = std::move(a0);
    [[maybe_unused]] bool s0b{};
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] Vec3ValueUVE s0v{};
    [[maybe_unused]] std::int64_t s1i{};
    [[maybe_unused]] double s1d{};
    [[maybe_unused]] std::string s1s{};
    [[maybe_unused]] Vec3ValueUVE s1v{};
    [[maybe_unused]] double s2d{};
    [[maybe_unused]] std::string s2s{};
    [[maybe_unused]] double s3d{};
    { c.StepUVE(10U); s0v = std::get<Vec3ValueUVE>(c.host.GetPropertyUVE("velocity")); }
    { c.StepUVE(10U); s1s = std::string{"left"}; }
    { c.StepUVE(10U); s2s = std::string{"right"}; }
    { c.StepUVE(10U); s1d = std::get<double>(c.host.CallFunctionUVE("input.axis", std::vector<ValueUVE>{ValueUVE{s1s}, ValueUVE{s2s}})); }
    { c.StepUVE(10U); s2d = std::get<double>(c.fields[0]); }
    { c.StepUVE(10U); s1d = s1d * s2d; }
    { c.StepUVE(10U); s0v.x = s1d; }
    { c.StepUVE(10U); c.host.SetPropertyUVE("velocity", ValueUVE{s0v}); }
    { c.StepUVE(11U); s0b = std::get<bool>(c.host.GetPropertyUVE("grounded")); }
    { c.StepUVE(11U); if (!s0b) goto I24; }
    { c.StepUVE(12U); s0v = std::get<Vec3ValueUVE>(c.host.GetPropertyUVE("velocity")); }
    { c.StepUVE(12U); s1d = std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL}); }
    { c.StepUVE(12U); s2d = std::bit_cast<double>(std::uint64_t{0x402399999999999aULL}); }
    { c.StepUVE(12U); s1d = s1d * s2d; }
    { c.StepUVE(12U); s2d = std::get<double>(c.fields[1]); }
    { c.StepUVE(12U); s1d = s1d * s2d; }
    { c.StepUVE(12U); { const double x = s1d; if (x < 0.0) throw ErrorUVE{"sqrt of a negative number"}; s1d = std::sqrt(x); } }
    { c.StepUVE(12U); s0v.y = s1d; }
    { c.StepUVE(12U); c.host.SetPropertyUVE("velocity", ValueUVE{s0v}); }
    { c.StepUVE(13U); s0i = std::get<std::int64_t>(c.fields[2]); }
    { c.StepUVE(13U); s1i = std::int64_t{1}; }
    { c.StepUVE(13U); s0i = s0i + s1i; }
    { c.StepUVE(13U); c.fields[2] = ValueUVE{s0i}; }
    { c.StepUVE(11U); goto I33; }
I24:
    { c.StepUVE(15U); s0v = std::get<Vec3ValueUVE>(c.host.GetPropertyUVE("velocity")); }
    { c.StepUVE(15U); s1v = std::get<Vec3ValueUVE>(c.host.GetPropertyUVE("velocity")); }
    { c.StepUVE(15U); s1d = s1v.y; }
    { c.StepUVE(15U); s2d = std::bit_cast<double>(std::uint64_t{0x402399999999999aULL}); }
    { c.StepUVE(15U); s3d = L0; }
    { c.StepUVE(15U); s2d = s2d * s3d; }
    { c.StepUVE(15U); s1d = s1d - s2d; }
    { c.StepUVE(15U); s0v.y = s1d; }
    { c.StepUVE(15U); c.host.SetPropertyUVE("velocity", ValueUVE{s0v}); }
I33:
    { c.StepUVE(9U); return; }
    return;
}

StatusUVE Chunk1(ContextUVE& c, FrameUVE& f) {
    T1(c, std::get<double>(f.locals[0]));
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// field initializers (typed)
void T2(ContextUVE& c) {
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] double s0d{};
    { c.StepUVE(2U); s0d = std::bit_cast<double>(std::uint64_t{0x4018000000000000ULL}); }
    { c.StepUVE(2U); c.fields[0] = ValueUVE{s0d}; }
    { c.StepUVE(3U); s0d = std::bit_cast<double>(std::uint64_t{0x4000000000000000ULL}); }
    { c.StepUVE(3U); c.fields[1] = ValueUVE{s0d}; }
    { c.StepUVE(4U); s0i = std::int64_t{0}; }
    { c.StepUVE(4U); c.fields[2] = ValueUVE{s0i}; }
    { c.StepUVE(0U); return; }
    return;
}

StatusUVE Chunk2(ContextUVE& c, FrameUVE& f) {
    T2(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

const ChunkFunctionUVE kChunks[] = {Chunk0, Chunk1, Chunk2};

[[maybe_unused]] const RegistrationUVE kRegistration{ProgramTableUVE{0xf3f876930ad02b60ULL, kChunks, 2U}};

} // namespace
