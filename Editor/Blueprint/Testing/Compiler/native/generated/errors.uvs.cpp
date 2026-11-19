// Generated from errors.uvs by uvsc. Do not edit: regenerate it.
// Program fingerprint 0xa1fa2a1c26cfb224ULL.

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
    ValueUVE{true},
    ValueUVE{std::int64_t{1}},
    ValueUVE{std::string{"health"}},
    ValueUVE{std::int64_t{3}},
};

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f);
void T0(ContextUVE& c);
StatusUVE Chunk1(ContextUVE& c, FrameUVE& f);
std::int64_t T1(ContextUVE& c, std::int64_t a0, std::int64_t a1);
StatusUVE Chunk2(ContextUVE& c, FrameUVE& f);
std::int64_t T2(ContextUVE& c, std::int64_t a0);
StatusUVE Chunk3(ContextUVE& c, FrameUVE& f);
void T3(ContextUVE& c);
StatusUVE Chunk4(ContextUVE& c, FrameUVE& f);
void T4(ContextUVE& c);

// spin (typed)
void T0(ContextUVE& c) {
    [[maybe_unused]] bool s0b{};
I0:
    { c.StepUVE(2U); s0b = true; }
    { c.StepUVE(2U); if (!s0b) goto I3; }
    { c.StepUVE(2U); goto I0; }
I3:
    { c.StepUVE(1U); return; }
    return;
}

StatusUVE Chunk0(ContextUVE& c, FrameUVE& f) {
    T0(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// divide (typed)
std::int64_t T1(ContextUVE& c, std::int64_t a0, std::int64_t a1) {
    [[maybe_unused]] std::int64_t L0 = std::move(a0);
    [[maybe_unused]] std::int64_t L1 = std::move(a1);
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] std::int64_t s1i{};
    { c.StepUVE(6U); s0i = L0; }
    { c.StepUVE(6U); s1i = L1; }
    { c.StepUVE(6U); if (s1i == 0) throw ErrorUVE{"'%' by zero"}; s0i = s0i % s1i; }
    { c.StepUVE(6U); return s0i; }
    return {};
}

StatusUVE Chunk1(ContextUVE& c, FrameUVE& f) {
    f.result = ValueUVE{T1(c, std::get<std::int64_t>(f.locals[0]), std::get<std::int64_t>(f.locals[1]))};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// deep (typed)
std::int64_t T2(ContextUVE& c, std::int64_t a0) {
    [[maybe_unused]] std::int64_t L0 = std::move(a0);
    [[maybe_unused]] std::int64_t s0i{};
    [[maybe_unused]] std::int64_t s1i{};
    { c.StepUVE(9U); s0i = L0; }
    { c.StepUVE(9U); s1i = std::int64_t{1}; }
    { c.StepUVE(9U); s0i = s0i + s1i; }
    { c.StepUVE(9U); if (c.depth + 1U >= 256U) throw ErrorUVE{"functions call each other too deeply"}; ++c.depth; s0i = T2(c, s0i); --c.depth; }
    { c.StepUVE(9U); return s0i; }
    return {};
}

StatusUVE Chunk2(ContextUVE& c, FrameUVE& f) {
    f.result = ValueUVE{T2(c, std::get<std::int64_t>(f.locals[0]))};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// on ready (typed)
void T3(ContextUVE& c) {
    [[maybe_unused]] std::int64_t s0i{};
    { c.StepUVE(12U); s0i = std::int64_t{3}; }
    { c.StepUVE(12U); c.host.SetPropertyUVE("health", ValueUVE{s0i}); }
    { c.StepUVE(11U); return; }
    return;
}

StatusUVE Chunk3(ContextUVE& c, FrameUVE& f) {
    T3(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

// field initializers (typed)
void T4(ContextUVE& c) {
    { c.StepUVE(0U); return; }
    return;
}

StatusUVE Chunk4(ContextUVE& c, FrameUVE& f) {
    T4(c);
    f.result = ValueUVE{};
    f.resume = 0U;
    return StatusUVE::Finished;
}

const ChunkFunctionUVE kChunks[] = {Chunk0, Chunk1, Chunk2, Chunk3, Chunk4};

[[maybe_unused]] const RegistrationUVE kRegistration{ProgramTableUVE{0xa1fa2a1c26cfb224ULL, kChunks, 4U}};

} // namespace
