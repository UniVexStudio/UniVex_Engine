// Generated from methods.uvs by uvsc. Do not edit: regenerate it.
// Program fingerprint 0x1555e2625572daaaULL.

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
    ValueUVE{std::int64_t{30}},
    ValueUVE{std::string{"take_damage"}},
    ValueUVE{std::string{"hide"}},
    ValueUVE{},
    ValueUVE{std::string{"gone"}},
    ValueUVE{std::string{"here "}},
    ValueUVE{std::string{"nobody"}},
    ValueUVE{std::string{"node"}},
    ValueUVE{std::string{"hide"}},
    ValueUVE{std::string{"friend"}},
    ValueUVE{std::string{"node"}},
    ValueUVE{std::string{"hi"}},
    ValueUVE{std::string{"greet"}},
    ValueUVE{},
    ValueUVE{std::string{"fire and forget"}},
};

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk1(ContextUVE& c, FrameUVE& f);
void T1(ContextUVE& c);

// on body_entered
StatusUVE Chunk0(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(4U); s.push_back(l[0]); }
    { c.StepUVE(4U); s.push_back(kConstants[0]); }
    { c.StepUVE(4U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); c.host.CallMethodUVE(AsObjectRefUVE(PopUVE(s)), "take_damage", args); s.push_back(ValueUVE{}); }
    { c.StepUVE(4U); s.pop_back(); }
    { c.StepUVE(5U); s.push_back(l[0]); }
    { c.StepUVE(5U); const std::vector<ValueUVE> args(s.end() - 0, s.end()); s.resize(s.size() - 0U); c.host.CallMethodUVE(AsObjectRefUVE(PopUVE(s)), "hide", args); s.push_back(ValueUVE{}); }
    { c.StepUVE(5U); s.pop_back(); }
    { c.StepUVE(6U); s.push_back(l[0]); }
    { c.StepUVE(6U); s.push_back(kConstants[3]); }
    { c.StepUVE(6U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(EqualUVE(x, y) == true); }
    { c.StepUVE(6U); if (!AsBoolUVE(PopUVE(s))) goto I15; }
    { c.StepUVE(7U); s.push_back(kConstants[4]); }
    { c.StepUVE(7U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 0, args); s.push_back(std::move(r)); }
    { c.StepUVE(7U); s.pop_back(); }
    { c.StepUVE(6U); goto I20; }
I15:
    { c.StepUVE(9U); s.push_back(kConstants[5]); }
    { c.StepUVE(1U); s.push_back(l[0]); }
    { c.StepUVE(9U); ValueUVE t = ConcatUVE(s, 2U); s.push_back(std::move(t)); }
    { c.StepUVE(9U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 0, args); s.push_back(std::move(r)); }
    { c.StepUVE(9U); s.pop_back(); }
I20:
    { c.StepUVE(10U); s.push_back(kConstants[6]); }
    { c.StepUVE(10U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = c.host.CallFunctionUVE("node", args); s.push_back(std::move(r)); }
    { c.StepUVE(10U); const std::vector<ValueUVE> args(s.end() - 0, s.end()); s.resize(s.size() - 0U); c.host.CallMethodUVE(AsObjectRefUVE(PopUVE(s)), "hide", args); s.push_back(ValueUVE{}); }
    { c.StepUVE(10U); s.pop_back(); }
    { c.StepUVE(11U); s.push_back(kConstants[9]); }
    { c.StepUVE(11U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = c.host.CallFunctionUVE("node", args); s.push_back(std::move(r)); }
    { c.StepUVE(11U); s.push_back(kConstants[11]); }
    { c.StepUVE(11U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); c.host.CallMethodUVE(AsObjectRefUVE(PopUVE(s)), "greet", args); s.push_back(ValueUVE{}); }
    { c.StepUVE(11U); s.push_back(kConstants[13]); }
    { c.StepUVE(11U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(EqualUVE(x, y) == true); }
    { c.StepUVE(11U); if (!AsBoolUVE(PopUVE(s))) goto I35; }
    { c.StepUVE(12U); s.push_back(kConstants[14]); }
    { c.StepUVE(12U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 0, args); s.push_back(std::move(r)); }
    { c.StepUVE(12U); s.pop_back(); }
    { c.StepUVE(11U); goto I35; }
I35:
    { c.StepUVE(3U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// field initializers (typed)
void T1(ContextUVE& c) {
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

[[maybe_unused]] const RegistrationUVE kRegistration{ProgramTableUVE{0x1555e2625572daaaULL, kChunks, 1U}};

} // namespace
