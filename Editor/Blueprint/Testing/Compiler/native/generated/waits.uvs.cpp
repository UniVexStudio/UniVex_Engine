// Generated from waits.uvs by uvsc. Do not edit: regenerate it.
// Program fingerprint 0x2b23c074297fd74aULL.

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
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::string{"a"}},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x3fe0000000000000ULL})},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::string{"b "}},
    ValueUVE{std::bit_cast<double>(std::uint64_t{0x3fb999999999999aULL})},
    ValueUVE{std::string{"c"}},
};

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk1(ContextUVE& c, FrameUVE& f);
void T1(ContextUVE& c);

// on ready
StatusUVE Chunk0(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        case 5U: goto I5;
        case 16U: goto I16;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(4U); s.push_back(kConstants[1]); }
    { c.StepUVE(4U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 0, args); s.push_back(std::move(r)); }
    { c.StepUVE(4U); s.pop_back(); }
    { c.StepUVE(5U); s.push_back(kConstants[2]); }
    { c.StepUVE(5U); f.waitSeconds = std::max(0.0, AsDoubleUVE(PopUVE(s))); f.resume = 5U; return StatusUVE::Waiting; }
I5:
    { c.StepUVE(6U); s.push_back(c.fields[0]); }
    { c.StepUVE(6U); s.push_back(kConstants[3]); }
    { c.StepUVE(6U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(ArithmeticUVE(ArithmeticOpUVE::Add, x, y)); }
    { c.StepUVE(6U); c.fields[0] = PopUVE(s); }
    { c.StepUVE(7U); s.push_back(kConstants[4]); }
    { c.StepUVE(1U); s.push_back(c.fields[0]); }
    { c.StepUVE(7U); ValueUVE t = ConcatUVE(s, 2U); s.push_back(std::move(t)); }
    { c.StepUVE(7U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 0, args); s.push_back(std::move(r)); }
    { c.StepUVE(7U); s.pop_back(); }
    { c.StepUVE(8U); s.push_back(kConstants[5]); }
    { c.StepUVE(8U); f.waitSeconds = std::max(0.0, AsDoubleUVE(PopUVE(s))); f.resume = 16U; return StatusUVE::Waiting; }
I16:
    { c.StepUVE(9U); s.push_back(kConstants[6]); }
    { c.StepUVE(9U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 0, args); s.push_back(std::move(r)); }
    { c.StepUVE(9U); s.pop_back(); }
    { c.StepUVE(3U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// field initializers (typed)
void T1(ContextUVE& c) {
    [[maybe_unused]] std::int64_t s0i{};
    { c.StepUVE(1U); s0i = std::int64_t{0}; }
    { c.StepUVE(1U); c.fields[0] = ValueUVE{s0i}; }
    { c.StepUVE(0U); return; }
    return;
}

StatusUVE Chunk1(ContextUVE& c, FrameUVE& f) {
    T1(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

const ChunkFunctionUVE kChunks[] = {Chunk0, Chunk1};

[[maybe_unused]] const RegistrationUVE kRegistration{ProgramTableUVE{0x2b23c074297fd74aULL, kChunks, 1U}};

} // namespace
