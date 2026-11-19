// Generated from collections.uvs by uvsc. Do not edit: regenerate it.
// Program fingerprint 0x5e3e658cbc115bdeULL.

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
    ValueUVE{std::string{"rope"}},
    ValueUVE{std::string{"torch"}},
    ValueUVE{std::string{"coins"}},
    ValueUVE{std::int64_t{3}},
    ValueUVE{std::string{"map"}},
    ValueUVE{std::string{" things: "}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::string{", "}},
    ValueUVE{std::int64_t{2}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{2}},
    ValueUVE{std::int64_t{3}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::string{"one"}},
    ValueUVE{std::string{"="}},
    ValueUVE{std::string{"bedroll"}},
    ValueUVE{std::int64_t{0}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::int64_t{2}},
    ValueUVE{std::int64_t{3}},
    ValueUVE{std::string{"torch"}},
    ValueUVE{std::string{"sword"}},
    ValueUVE{std::string{"coins"}},
    ValueUVE{std::string{"coins"}},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::string{"coins"}},
};

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk1(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk2(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk3(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk4(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk5(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk6(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk7(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk8(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk9(ContextUVE& c, FrameUVE& f);
StatusUVE Chunk10(ContextUVE& c, FrameUVE& f);
void T10(ContextUVE& c);
StatusUVE Chunk11(ContextUVE& c, FrameUVE& f);

// describe
StatusUVE Chunk0(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(7U); s.push_back(c.fields[0]); }
    { c.StepUVE(7U); s.push_back(kConstants[4]); }
    { c.StepUVE(7U); const std::vector<ValueUVE> args(s.end() - 2, s.end()); s.resize(s.size() - 2U); ValueUVE r = CallBuiltinUVE(c.host, 16, args); s.push_back(std::move(r)); }
    { c.StepUVE(7U); l[0] = PopUVE(s); }
    { c.StepUVE(1U); s.push_back(l[0]); }
    { c.StepUVE(1U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 14, args); s.push_back(std::move(r)); }
    { c.StepUVE(8U); s.push_back(kConstants[5]); }
    { c.StepUVE(1U); s.push_back(l[0]); }
    { c.StepUVE(1U); s.push_back(kConstants[6]); }
    { c.StepUVE(1U); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k)); }
    { c.StepUVE(8U); s.push_back(kConstants[7]); }
    { c.StepUVE(1U); s.push_back(l[0]); }
    { c.StepUVE(1U); s.push_back(kConstants[8]); }
    { c.StepUVE(1U); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k)); }
    { c.StepUVE(8U); ValueUVE t = ConcatUVE(s, 5U); s.push_back(std::move(t)); }
    { c.StepUVE(8U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(6U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// total
StatusUVE Chunk1(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(11U); s.push_back(kConstants[9]); }
    { c.StepUVE(11U); l[0] = PopUVE(s); }
    { c.StepUVE(12U); s.push_back(kConstants[10]); }
    { c.StepUVE(12U); s.push_back(kConstants[11]); }
    { c.StepUVE(12U); s.push_back(kConstants[12]); }
    { c.StepUVE(12U); ValueUVE v = BuildListUVE(s, 3U); s.push_back(std::move(v)); }
    { c.StepUVE(12U); l[1] = PopUVE(s); }
    { c.StepUVE(12U); s.push_back(l[1]); }
    { c.StepUVE(12U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 14, args); s.push_back(std::move(r)); }
    { c.StepUVE(12U); l[2] = PopUVE(s); }
    { c.StepUVE(12U); s.push_back(kConstants[13]); }
    { c.StepUVE(12U); l[3] = PopUVE(s); }
I12:
    { c.StepUVE(12U); s.push_back(l[3]); }
    { c.StepUVE(12U); s.push_back(l[2]); }
    { c.StepUVE(12U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(CompareUVE(CompareOpUVE::Lt, x, y)); }
    { c.StepUVE(12U); if (!AsBoolUVE(PopUVE(s))) goto I29; }
    { c.StepUVE(12U); s.push_back(l[1]); }
    { c.StepUVE(12U); s.push_back(l[3]); }
    { c.StepUVE(12U); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k)); }
    { c.StepUVE(12U); l[4] = PopUVE(s); }
    { c.StepUVE(13U); s.push_back(l[0]); }
    { c.StepUVE(13U); s.push_back(l[4]); }
    { c.StepUVE(13U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(ArithmeticUVE(ArithmeticOpUVE::Add, x, y)); }
    { c.StepUVE(13U); l[0] = PopUVE(s); }
    { c.StepUVE(12U); s.push_back(l[3]); }
    { c.StepUVE(12U); s.push_back(kConstants[14]); }
    { c.StepUVE(12U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(ArithmeticUVE(ArithmeticOpUVE::Add, x, y)); }
    { c.StepUVE(12U); l[3] = PopUVE(s); }
    { c.StepUVE(12U); goto I12; }
I29:
    { c.StepUVE(14U); s.push_back(l[0]); }
    { c.StepUVE(14U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(10U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// tally
StatusUVE Chunk2(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(17U); ValueUVE v = BuildMapUVE(s, 0U); s.push_back(std::move(v)); }
    { c.StepUVE(17U); l[1] = PopUVE(s); }
    { c.StepUVE(18U); s.push_back(l[0]); }
    { c.StepUVE(18U); l[2] = PopUVE(s); }
    { c.StepUVE(18U); s.push_back(l[2]); }
    { c.StepUVE(18U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 14, args); s.push_back(std::move(r)); }
    { c.StepUVE(18U); l[3] = PopUVE(s); }
    { c.StepUVE(18U); s.push_back(kConstants[15]); }
    { c.StepUVE(18U); l[4] = PopUVE(s); }
I9:
    { c.StepUVE(18U); s.push_back(l[4]); }
    { c.StepUVE(18U); s.push_back(l[3]); }
    { c.StepUVE(18U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(CompareUVE(CompareOpUVE::Lt, x, y)); }
    { c.StepUVE(18U); if (!AsBoolUVE(PopUVE(s))) goto I41; }
    { c.StepUVE(18U); s.push_back(l[2]); }
    { c.StepUVE(18U); s.push_back(l[4]); }
    { c.StepUVE(18U); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k)); }
    { c.StepUVE(18U); l[5] = PopUVE(s); }
    { c.StepUVE(19U); s.push_back(l[1]); }
    { c.StepUVE(19U); s.push_back(l[5]); }
    { c.StepUVE(19U); const std::vector<ValueUVE> args(s.end() - 2, s.end()); s.resize(s.size() - 2U); ValueUVE r = CallBuiltinUVE(c.host, 18, args); s.push_back(std::move(r)); }
    { c.StepUVE(19U); if (!AsBoolUVE(PopUVE(s))) goto I31; }
    { c.StepUVE(20U); s.push_back(l[1]); }
    { c.StepUVE(20U); s.push_back(l[5]); }
    { c.StepUVE(20U); s.push_back(l[1]); }
    { c.StepUVE(20U); s.push_back(l[5]); }
    { c.StepUVE(20U); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k)); }
    { c.StepUVE(20U); s.push_back(kConstants[16]); }
    { c.StepUVE(20U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(ArithmeticUVE(ArithmeticOpUVE::Add, x, y)); }
    { c.StepUVE(20U); const ValueUVE v = PopUVE(s); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(SetIndexUVE(box, k, v)); }
    { c.StepUVE(20U); l[1] = PopUVE(s); }
    { c.StepUVE(19U); goto I36; }
I31:
    { c.StepUVE(22U); s.push_back(l[1]); }
    { c.StepUVE(22U); s.push_back(l[5]); }
    { c.StepUVE(22U); s.push_back(kConstants[17]); }
    { c.StepUVE(22U); const ValueUVE v = PopUVE(s); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(SetIndexUVE(box, k, v)); }
    { c.StepUVE(22U); l[1] = PopUVE(s); }
I36:
    { c.StepUVE(18U); s.push_back(l[4]); }
    { c.StepUVE(18U); s.push_back(kConstants[18]); }
    { c.StepUVE(18U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(ArithmeticUVE(ArithmeticOpUVE::Add, x, y)); }
    { c.StepUVE(18U); l[4] = PopUVE(s); }
    { c.StepUVE(18U); goto I9; }
I41:
    { c.StepUVE(23U); s.push_back(l[1]); }
    { c.StepUVE(23U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(16U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// first_pair
StatusUVE Chunk3(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(26U); s.push_back(kConstants[19]); }
    { c.StepUVE(26U); s.push_back(kConstants[20]); }
    { c.StepUVE(26U); ValueUVE v = BuildTupleUVE(s, 2U); s.push_back(std::move(v)); }
    { c.StepUVE(26U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(25U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// unpacked
StatusUVE Chunk4(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(29U); FrameUVE g; g.locals.assign(s.end() - 0, s.end()); s.resize(s.size() - 0U); g.locals.resize(0U); CallChunkUVE(c, Chunk3, g); s.push_back(std::move(g.result)); }
    { c.StepUVE(29U); UnpackUVE(s, 2U); }
    { c.StepUVE(29U); l[1] = PopUVE(s); }
    { c.StepUVE(29U); l[0] = PopUVE(s); }
    { c.StepUVE(1U); s.push_back(l[1]); }
    { c.StepUVE(30U); s.push_back(kConstants[21]); }
    { c.StepUVE(1U); s.push_back(l[0]); }
    { c.StepUVE(30U); ValueUVE t = ConcatUVE(s, 3U); s.push_back(std::move(t)); }
    { c.StepUVE(30U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(28U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// key_list
StatusUVE Chunk5(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(33U); s.push_back(c.fields[1]); }
    { c.StepUVE(33U); const std::vector<ValueUVE> args(s.end() - 1, s.end()); s.resize(s.size() - 1U); ValueUVE r = CallBuiltinUVE(c.host, 17, args); s.push_back(std::move(r)); }
    { c.StepUVE(33U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(32U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// dropped
StatusUVE Chunk6(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(36U); s.push_back(c.fields[0]); }
    { c.StepUVE(36U); l[0] = PopUVE(s); }
    { c.StepUVE(37U); s.push_back(l[0]); }
    { c.StepUVE(37U); s.push_back(kConstants[22]); }
    { c.StepUVE(37U); const std::vector<ValueUVE> args(s.end() - 2, s.end()); s.resize(s.size() - 2U); ValueUVE r = CallBuiltinUVE(c.host, 16, args); s.push_back(std::move(r)); }
    { c.StepUVE(37U); l[0] = PopUVE(s); }
    { c.StepUVE(38U); s.push_back(l[0]); }
    { c.StepUVE(38U); s.push_back(kConstants[23]); }
    { c.StepUVE(38U); const std::vector<ValueUVE> args(s.end() - 2, s.end()); s.resize(s.size() - 2U); ValueUVE r = CallBuiltinUVE(c.host, 19, args); s.push_back(std::move(r)); }
    { c.StepUVE(38U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(35U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// joined
StatusUVE Chunk7(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(41U); s.push_back(kConstants[24]); }
    { c.StepUVE(41U); s.push_back(kConstants[25]); }
    { c.StepUVE(41U); ValueUVE v = BuildListUVE(s, 2U); s.push_back(std::move(v)); }
    { c.StepUVE(41U); s.push_back(kConstants[26]); }
    { c.StepUVE(41U); ValueUVE v = BuildListUVE(s, 1U); s.push_back(std::move(v)); }
    { c.StepUVE(41U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(ArithmeticUVE(ArithmeticOpUVE::Add, x, y)); }
    { c.StepUVE(41U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(40U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// has_torch
StatusUVE Chunk8(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(44U); s.push_back(c.fields[0]); }
    { c.StepUVE(44U); s.push_back(kConstants[27]); }
    { c.StepUVE(44U); const std::vector<ValueUVE> args(s.end() - 2, s.end()); s.resize(s.size() - 2U); ValueUVE r = CallBuiltinUVE(c.host, 18, args); s.push_back(std::move(r)); }
    { c.StepUVE(44U); if (!AsBoolUVE(s.back())) goto I8; s.pop_back(); }
    { c.StepUVE(44U); s.push_back(c.fields[0]); }
    { c.StepUVE(44U); s.push_back(kConstants[28]); }
    { c.StepUVE(44U); const std::vector<ValueUVE> args(s.end() - 2, s.end()); s.resize(s.size() - 2U); ValueUVE r = CallBuiltinUVE(c.host, 18, args); s.push_back(std::move(r)); }
    { c.StepUVE(44U); const bool v = !AsBoolUVE(PopUVE(s)); s.push_back(v); }
I8:
    { c.StepUVE(44U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(43U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// count_words
StatusUVE Chunk9(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(47U); s.push_back(c.fields[1]); }
    { c.StepUVE(47U); s.push_back(kConstants[29]); }
    { c.StepUVE(47U); s.push_back(c.fields[1]); }
    { c.StepUVE(47U); s.push_back(kConstants[30]); }
    { c.StepUVE(47U); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k)); }
    { c.StepUVE(47U); s.push_back(kConstants[31]); }
    { c.StepUVE(47U); const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(ArithmeticUVE(ArithmeticOpUVE::Add, x, y)); }
    { c.StepUVE(47U); const ValueUVE v = PopUVE(s); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(SetIndexUVE(box, k, v)); }
    { c.StepUVE(47U); c.fields[1] = PopUVE(s); }
    { c.StepUVE(48U); s.push_back(c.fields[1]); }
    { c.StepUVE(48U); s.push_back(kConstants[32]); }
    { c.StepUVE(48U); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k)); }
    { c.StepUVE(48U); f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished; }
    { c.StepUVE(46U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

// on ready (typed)
void T10(ContextUVE& c) {
    [[maybe_unused]] std::string s0s{};
    { c.StepUVE(51U); FrameUVE g; g.locals.resize(1U); CallChunkUVE(c, Chunk0, g); s0s = std::get<std::string>(g.result); }
    { c.StepUVE(51U); c.host.PrintUVE(FormatValueUVE(ValueUVE{s0s})); }
    { c.StepUVE(51U);  }
    { c.StepUVE(50U); return; }
    return;
}

StatusUVE Chunk10(ContextUVE& c, FrameUVE& f) {
    T10(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// field initializers
StatusUVE Chunk11(ContextUVE& c, FrameUVE& f) {
    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;
    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;
    switch (f.resume) {
        case 0U: break;
        default: throw ErrorUVE{"a paused handler cannot continue"};
    }
    { c.StepUVE(3U); s.push_back(kConstants[0]); }
    { c.StepUVE(3U); s.push_back(kConstants[1]); }
    { c.StepUVE(3U); ValueUVE v = BuildListUVE(s, 2U); s.push_back(std::move(v)); }
    { c.StepUVE(3U); c.fields[0] = PopUVE(s); }
    { c.StepUVE(4U); s.push_back(kConstants[2]); }
    { c.StepUVE(4U); s.push_back(kConstants[3]); }
    { c.StepUVE(4U); ValueUVE v = BuildMapUVE(s, 1U); s.push_back(std::move(v)); }
    { c.StepUVE(4U); c.fields[1] = PopUVE(s); }
    { c.StepUVE(0U); f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished; }
    f.result = ValueUVE{};
    return StatusUVE::Finished;
}

const ChunkFunctionUVE kChunks[] = {Chunk0, Chunk1, Chunk2, Chunk3, Chunk4, Chunk5, Chunk6, Chunk7, Chunk8, Chunk9, Chunk10, Chunk11};

[[maybe_unused]] const RegistrationUVE kRegistration{ProgramTableUVE{0x5e3e658cbc115bdeULL, kChunks, 11U}};

} // namespace
