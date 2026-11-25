// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_utilities_nodes.h"

#include <string>
#include <utility>
#include <vector>

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterUtilitiesBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // Logging & conversions
    b.Action("print", "Print String", "Utilities", {Pin{"Text", T::String, "Hello"}}, "print({1})", "log debug message", "Writes to the Output Log");
    b.Pure("to_string_float", "Float To String", "Utilities|Conversion", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    b.Pure("to_string_int", "Int To String", "Utilities|Conversion", {Pin{"Value", T::Int, "0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    b.Pure("to_string_bool", "Bool To String", "Utilities|Conversion", {Pin{"Value", T::Bool, "false"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    b.Pure("to_string_vector", "Vector To String", "Utilities|Conversion", {Pin{"Value", T::Vector, "0,0,0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    b.Pure("int_to_float", "Int To Float", "Utilities|Conversion", {Pin{"Value", T::Int, "0"}}, {Pin{"Value", T::Float, ""}}, {"float({0})"});
    b.Pure("float_to_int", "Float To Int", "Utilities|Conversion", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Value", T::Int, ""}}, {"int({0})"});
    b.Pure("append_string", "Append", "Utilities|String", {Pin{"A", T::String, ""}, Pin{"B", T::String, ""}}, {Pin{"Text", T::String, ""}}, {"{0} + {1}"}, "concat join");

    // Arrays: item operations per typed array
    const std::vector<std::pair<T, std::string>> arrays = {
        {T::ObjectArray, "Object"}, {T::IntArray, "Int"}, {T::FloatArray, "Float"}, {T::StringArray, "String"}};
    for (const auto& [arrayType, word] : arrays) {
        const T item = Asset::BlueprintArrayItemType(arrayType);
        const std::string suffix = " (" + word + " Array)";
        const std::string key = "_" + word;
        const std::string itemDefault = item == T::Int ? "0" : item == T::Float ? "0.0" : item == T::String ? "" : "";
        b.Pure("array_get" + key, "Get" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Index", T::Int, "0"}},
               {Pin{"Item", item, ""}}, {"{0}[{1}]"}, "array item index element", "The item at that index (the first is 0)");
        b.Action("array_add" + key, "Add" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Item", item, itemDefault}},
                 "{1}.push({2})", "array append push insert", "Puts the item at the end (wire the array from its variable)");
        b.Pure("array_contains" + key, "Contains Item" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Item", item, itemDefault}},
               {Pin{"Contains", T::Bool, ""}}, {"{0}.contains({1})"}, "array has find in");
        b.Pure("array_random" + key, "Random Item" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}}, {Pin{"Item", item, ""}},
               {"{0}[math.random_int(0, {0}.length() - 1)]"}, "array pick random any", "One of the items, at random (the array must not be empty)");
        b.Pure("array_length" + key, "Length" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}}, {Pin{"Length", T::Int, ""}},
               {"{0}.length()"}, "array count size num");
        b.Action("array_remove_index" + key, "Remove Index" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Index", T::Int, "0"}},
                 "{1}.remove({2})", "array delete erase", "Takes the item at that index out (wire the array from its variable)");
        b.Action("array_clear" + key, "Clear" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}},
                 "{1} = array.empty_" + std::string(word == "Object" ? "object" : word == "Int" ? "int" : word == "Float" ? "float" : "string") + "()",
                 "array empty reset", "Empties the array (wire it from its variable)");
        b.Pure("array_is_empty" + key, "Is Empty" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}}, {Pin{"Empty", T::Bool, ""}},
               {"({0}.length() == 0)"}, "array none");
    }
    b.Pure("make_array_object", "Make Array (Object)", "Utilities|Array", {Pin{"0", T::Object, ""}, Pin{"1", T::Object, ""}},
           {Pin{"Array", T::ObjectArray, ""}}, {"[{0}, {1}]"}, "array list new");
    b.Pure("make_array_string", "Make Array (String)", "Utilities|Array", {Pin{"0", T::String, "A"}, Pin{"1", T::String, "B"}, Pin{"2", T::String, "C"}},
           {Pin{"Array", T::StringArray, ""}}, {"[{0}, {1}, {2}]"}, "array list new");
    b.Pure("make_array_float", "Make Array (Float)", "Utilities|Array", {Pin{"0", T::Float, "0.0"}, Pin{"1", T::Float, "1.0"}, Pin{"2", T::Float, "2.0"}},
           {Pin{"Array", T::FloatArray, ""}}, {"[{0}, {1}, {2}]"}, "array list new");

    // String & text manipulation
    b.Pure("int_to_string", "To String (Int)", "Utilities|String", {Pin{"Value", T::Int, "0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"},
           "convert text number");
    b.Pure("float_to_string", "To String (Float)", "Utilities|String", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"},
           "convert text number");
    b.Pure("bool_to_string", "To String (Bool)", "Utilities|String", {Pin{"Value", T::Bool, "false"}}, {Pin{"Text", T::String, ""}}, {"str({0})"},
           "convert text");
    b.Pure("vector_to_string", "To String (Vector)", "Utilities|String", {Pin{"Value", T::Vector, "0,0,0"}}, {Pin{"Text", T::String, ""}},
           {"str({0})"}, "convert text");
    b.Pure("string_to_int", "To Int (String)", "Utilities|String", {Pin{"Text", T::String, "0"}}, {Pin{"Value", T::Int, ""}}, {"text.to_int({0})"},
           "parse convert number");
    b.Pure("string_to_float", "To Float (String)", "Utilities|String", {Pin{"Text", T::String, "0.0"}}, {Pin{"Value", T::Float, ""}},
           {"text.to_float({0})"}, "parse convert number");
    b.Pure("string_contains", "Contains (String)", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"Part", T::String, ""}},
           {Pin{"Contains", T::Bool, ""}}, {"{0}.contains({1})"}, "find substring search");
    b.Pure("string_length", "Length (String)", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Length", T::Int, ""}}, {"{0}.length()"},
           "count characters len");
    b.Pure("string_upper", "To Upper", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Result", T::String, ""}}, {"text.upper({0})"},
           "uppercase capital");
    b.Pure("string_lower", "To Lower", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Result", T::String, ""}}, {"text.lower({0})"},
           "lowercase small");
    b.Pure("string_trim", "Trim", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Result", T::String, ""}}, {"text.trim({0})"},
           "spaces whitespace strip");
    b.Pure("string_replace", "Replace", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"From", T::String, ""}, Pin{"To", T::String, ""}},
           {Pin{"Result", T::String, ""}}, {"text.replace({0}, {1}, {2})"}, "substitute swap");
    b.Pure("string_split", "Split", "Utilities|String", {Pin{"Text", T::String, "a,b,c"}, Pin{"Separator", T::String, ","}},
           {Pin{"Parts", T::StringArray, ""}}, {"text.split({0}, {1})"}, "parse explode tokens");
    b.Pure("string_substring", "Substring", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"Start", T::Int, "0"}, Pin{"Length", T::Int, "1"}},
           {Pin{"Result", T::String, ""}}, {"text.substring({0}, {1}, {2})"}, "mid slice part");
    b.Pure("string_starts_with", "Starts With", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"Start", T::String, ""}},
           {Pin{"Result", T::Bool, ""}}, {"text.starts_with({0}, {1})"}, "prefix begins");
    b.Pure("format_text", "Format Text", "Utilities|String", {Pin{"Format", T::String, "Score: {0}"}, Pin{"0", T::String, ""}, Pin{"1", T::String, ""}},
           {Pin{"Result", T::String, ""}}, {"text.format({0}, {1}, {2})"}, "format template placeholder",
           "Puts each value where the format says {0} and {1}");

    // Select helpers
    const auto select = [&b](const std::string& key, const std::string& title, T type, const std::string& a, const std::string& bVal,
                             const std::string& fn) {
        b.Pure(key, title, "Utilities|Select", {Pin{"Pick A", T::Bool, "true"}, Pin{"A", type, a}, Pin{"B", type, bVal}}, {Pin{"Result", type, ""}},
               {"math." + fn + "({0}, {1}, {2})"}, "select choose ternary if", "A when Pick A is true, else B");
    };
    select("select_float", "Select Float", T::Float, "0.0", "1.0", "select_float");
    select("select_int", "Select Int", T::Int, "0", "1", "select_int");
    select("select_vector", "Select Vector", T::Vector, "0,0,0", "1,1,1", "select_vector");
    select("select_string", "Select String", T::String, "A", "B", "select_string");
}

} // namespace UVE::Blueprint::Nodes
