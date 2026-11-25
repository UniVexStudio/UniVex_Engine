// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_math_nodes.h"

#include <string>

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterMathBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    const auto binary = [&b](const std::string& key, const std::string& title, const std::string& category, T a, T result, const std::string& op,
                             const std::string& keywords) {
        const std::string def = a == T::Int ? "0" : a == T::Vector ? "0,0,0" : a == T::Bool ? "false" : "0.0";
        b.Pure(key, title, category, {Pin{"A", a, def}, Pin{"B", a, def}}, {Pin{"Result", result, ""}}, {"({0} " + op + " {1})"}, keywords);
    };

    // Rounding, grid snap, wrap, sign, axis rotation, spatial random
    b.Pure("float_round", "Round", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Int, ""}}, {"math.round({0})"}, "nearest integer");
    b.Pure("float_ceil", "Ceil", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Int, ""}}, {"math.ceil({0})"}, "round up");
    b.Pure("float_truncate", "Truncate", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Int, ""}}, {"int({0})"}, "integer whole");
    b.Pure("snap_to_grid", "Snap To Grid", "Math|Float", {Pin{"Value", T::Float, "0.0"}, Pin{"Grid", T::Float, "1.0"}}, {Pin{"Result", T::Float, ""}},
           {"math.snap({0}, {1})"}, "round step");
    b.Pure("float_wrap", "Wrap", "Math|Float", {Pin{"Value", T::Float, "0.0"}, Pin{"Min", T::Float, "0.0"}, Pin{"Max", T::Float, "360.0"}},
           {Pin{"Result", T::Float, ""}}, {"math.wrap({0}, {1}, {2})"}, "loop modulo angle");
    b.Pure("float_sign", "Sign", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"math.sign({0})"}, "positive negative");
    b.Pure("vector_rotate_axis", "Rotate Vector Around Axis", "Math|Vector",
           {Pin{"Vector", T::Vector, "1,0,0"}, Pin{"Axis", T::Vector, "0,1,0"}, Pin{"Degrees", T::Float, "90.0"}}, {Pin{"Result", T::Vector, ""}},
           {"math.rotate_about({0}, {1}, {2})"}, "turn spin angle");
    b.Pure("random_unit_vector", "Random Unit Vector", "Math|Random", {}, {Pin{"Vector", T::Vector, ""}}, {"math.random_unit_vector()"},
           "random direction");
    b.Pure("random_point_in_box", "Random Point In Box", "Math|Random", {Pin{"Center", T::Vector, "0,0,0"}, Pin{"Half Size", T::Vector, "5,0,5"}},
           {Pin{"Point", T::Vector, ""}}, {"math.random_in_box({0}, {1})"}, "random location spawn area");

    // Arithmetic, comparison, boolean logic
    binary("float_add", "Add (Float)", "Math|Float", T::Float, T::Float, "+", "plus sum");
    binary("float_sub", "Subtract (Float)", "Math|Float", T::Float, T::Float, "-", "minus");
    binary("float_mul", "Multiply (Float)", "Math|Float", T::Float, T::Float, "*", "times");
    binary("float_div", "Divide (Float)", "Math|Float", T::Float, T::Float, "/", "");
    binary("float_greater", "Greater (Float)", "Math|Compare", T::Float, T::Bool, ">", "more than");
    binary("float_less", "Less (Float)", "Math|Compare", T::Float, T::Bool, "<", "smaller than");
    binary("float_greater_equal", "Greater Or Equal (Float)", "Math|Compare", T::Float, T::Bool, ">=", "");
    binary("float_less_equal", "Less Or Equal (Float)", "Math|Compare", T::Float, T::Bool, "<=", "");
    binary("float_equal", "Equal (Float)", "Math|Compare", T::Float, T::Bool, "==", "same");
    binary("int_add", "Add (Int)", "Math|Int", T::Int, T::Int, "+", "plus sum");
    binary("int_sub", "Subtract (Int)", "Math|Int", T::Int, T::Int, "-", "minus");
    binary("int_mul", "Multiply (Int)", "Math|Int", T::Int, T::Int, "*", "times");
    binary("int_equal", "Equal (Int)", "Math|Compare", T::Int, T::Bool, "==", "same");
    binary("int_greater", "Greater (Int)", "Math|Compare", T::Int, T::Bool, ">", "");
    binary("int_less", "Less (Int)", "Math|Compare", T::Int, T::Bool, "<", "");
    binary("bool_and", "AND", "Math|Boolean", T::Bool, T::Bool, "and", "both");
    binary("bool_or", "OR", "Math|Boolean", T::Bool, T::Bool, "or", "either");
    b.Pure("bool_not", "NOT", "Math|Boolean", {Pin{"A", T::Bool, "false"}}, {Pin{"Result", T::Bool, ""}}, {"(not {0})"}, "invert");
    binary("string_equal", "Equal (String)", "Math|Compare", T::String, T::Bool, "==", "same name");

    // Scalar, trig, vector, interpolation, random
    b.Pure("float_min", "Min", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"min({0}, {1})"});
    b.Pure("float_max", "Max", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"max({0}, {1})"});
    b.Pure("float_clamp", "Clamp", "Math|Float", {Pin{"Value", T::Float, "0.0"}, Pin{"Min", T::Float, "0.0"}, Pin{"Max", T::Float, "1.0"}},
           {Pin{"Result", T::Float, ""}}, {"clamp({0}, {1}, {2})"}, "limit");
    b.Pure("float_lerp", "Lerp", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "1.0"}, Pin{"Alpha", T::Float, "0.5"}},
           {Pin{"Result", T::Float, ""}}, {"lerp({0}, {1}, {2})"}, "mix interpolate blend");
    b.Pure("float_abs", "Absolute", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"abs({0})"}, "abs");
    b.Pure("float_sqrt", "Square Root", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"sqrt({0})"}, "sqrt");
    b.Pure("float_sin", "Sin", "Math|Trig", {Pin{"Radians", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"sin({0})"}, "sine wave");
    b.Pure("float_cos", "Cos", "Math|Trig", {Pin{"Radians", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"cos({0})"}, "cosine");
    b.Pure("float_floor", "Floor", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"floor({0})"}, "round down");
    b.Pure("make_vector", "Make Vector", "Math|Vector", {Pin{"X", T::Float, "0.0"}, Pin{"Y", T::Float, "0.0"}, Pin{"Z", T::Float, "0.0"}},
           {Pin{"Vector", T::Vector, ""}}, {"vec3({0}, {1}, {2})"}, "vec3 make");
    b.Pure("break_vector", "Break Vector", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}},
           {Pin{"X", T::Float, ""}, Pin{"Y", T::Float, ""}, Pin{"Z", T::Float, ""}}, {"{0}.x", "{0}.y", "{0}.z"}, "split components");
    binary("vector_add", "Add (Vector)", "Math|Vector", T::Vector, T::Vector, "+", "plus");
    binary("vector_sub", "Subtract (Vector)", "Math|Vector", T::Vector, T::Vector, "-", "minus");
    b.Pure("vector_scale", "Scale Vector", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}, Pin{"Scale", T::Float, "1.0"}}, {Pin{"Result", T::Vector, ""}},
           {"({0} * {1})"}, "multiply times");
    b.Pure("vector_length", "Vector Length", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}}, {Pin{"Length", T::Float, ""}}, {"length({0})"}, "size magnitude");
    b.Pure("vector_normalize", "Normalize", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}}, {Pin{"Result", T::Vector, ""}}, {"normalize({0})"}, "direction unit");
    b.Pure("random_float_range", "Random Float In Range", "Math|Random", {Pin{"Min", T::Float, "0.0"}, Pin{"Max", T::Float, "1.0"}},
           {Pin{"Value", T::Float, ""}}, {"math.random_range({0}, {1})"}, "rand");
    b.Pure("random_int_range", "Random Integer In Range", "Math|Random", {Pin{"Min", T::Int, "0"}, Pin{"Max", T::Int, "10"}},
           {Pin{"Value", T::Int, ""}}, {"math.random_int({0}, {1})"}, "rand");
    b.Pure("random_bool", "Random Bool", "Math|Random", {}, {Pin{"Value", T::Bool, ""}}, {"math.random_bool()"}, "rand coin");
    b.Pure("float_pow", "Power", "Math|Float", {Pin{"Base", T::Float, "2.0"}, Pin{"Exponent", T::Float, "2.0"}}, {Pin{"Result", T::Float, ""}},
           {"math.pow({0}, {1})"}, "pow exponent square");
    b.Pure("float_mod", "Modulo (Float)", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "1.0"}}, {Pin{"Result", T::Float, ""}},
           {"math.fmod({0}, {1})"}, "remainder fmod");
    b.Pure("float_atan2", "Atan2 (Degrees)", "Math|Trig", {Pin{"Y", T::Float, "0.0"}, Pin{"X", T::Float, "1.0"}}, {Pin{"Degrees", T::Float, ""}},
           {"math.atan2({0}, {1})"}, "angle direction");
    b.Pure("degrees_to_radians", "Degrees To Radians", "Math|Trig", {Pin{"Degrees", T::Float, "0.0"}}, {Pin{"Radians", T::Float, ""}},
           {"({0} * 0.017453292)"}, "convert");
    b.Pure("radians_to_degrees", "Radians To Degrees", "Math|Trig", {Pin{"Radians", T::Float, "0.0"}}, {Pin{"Degrees", T::Float, ""}},
           {"({0} * 57.29578)"}, "convert");
    b.Pure("map_range_clamped", "Map Range Clamped", "Math|Float",
           {Pin{"Value", T::Float, "0.0"}, Pin{"In Min", T::Float, "0.0"}, Pin{"In Max", T::Float, "1.0"}, Pin{"Out Min", T::Float, "0.0"},
            Pin{"Out Max", T::Float, "1.0"}},
           {Pin{"Result", T::Float, ""}}, {"lerp({3}, {4}, clamp(({0} - {1}) / ({2} - {1}), 0.0, 1.0))"}, "remap scale normalize");
    b.Pure("finterp_to", "Interp To (Float)", "Math|Float",
           {Pin{"Current", T::Float, "0.0"}, Pin{"Target", T::Float, "1.0"}, Pin{"Delta Seconds", T::Float, "0.016"}, Pin{"Speed", T::Float, "5.0"}},
           {Pin{"Result", T::Float, ""}}, {"lerp({0}, {1}, clamp({2} * {3}, 0.0, 1.0))"}, "smooth ease approach finterp");
    b.Pure("vinterp_to", "Interp To (Vector)", "Math|Vector",
           {Pin{"Current", T::Vector, "0,0,0"}, Pin{"Target", T::Vector, "0,0,0"}, Pin{"Delta Seconds", T::Float, "0.016"}, Pin{"Speed", T::Float, "5.0"}},
           {Pin{"Result", T::Vector, ""}}, {"({0} + ({1} - {0}) * clamp({2} * {3}, 0.0, 1.0))"}, "smooth ease approach vinterp");
    b.Pure("vector_lerp", "Lerp (Vector)", "Math|Vector", {Pin{"A", T::Vector, "0,0,0"}, Pin{"B", T::Vector, "0,0,0"}, Pin{"Alpha", T::Float, "0.5"}},
           {Pin{"Result", T::Vector, ""}}, {"({0} + ({1} - {0}) * {2})"}, "mix blend");
    b.Pure("vector_distance", "Distance (Vector)", "Math|Vector", {Pin{"A", T::Vector, "0,0,0"}, Pin{"B", T::Vector, "0,0,0"}},
           {Pin{"Distance", T::Float, ""}}, {"length({0} - {1})"}, "between far");
    b.Pure("vector_cross", "Cross Product", "Math|Vector", {Pin{"A", T::Vector, "1,0,0"}, Pin{"B", T::Vector, "0,1,0"}}, {Pin{"Result", T::Vector, ""}},
           {"vec3({0}.y * {1}.z - {0}.z * {1}.y, {0}.z * {1}.x - {0}.x * {1}.z, {0}.x * {1}.y - {0}.y * {1}.x)"}, "cross normal perpendicular");
    b.Pure("vector_dot", "Dot Product", "Math|Vector", {Pin{"A", T::Vector, "0,0,0"}, Pin{"B", T::Vector, "0,0,0"}}, {Pin{"Result", T::Float, ""}},
           {"({0}.x * {1}.x + {0}.y * {1}.y + {0}.z * {1}.z)"}, "dot");
}

} // namespace UVE::Blueprint::Nodes
