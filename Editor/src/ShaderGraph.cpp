#include "ShaderGraph.h"
#include "ShaderAssets.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace Stardust::ShaderGraph {

using json = nlohmann::json;

namespace {

PinDef Pin(const std::string& name, int dims, std::initializer_list<float> value = {}, const std::string& expr = {}, bool color = false) {
    PinDef p; p.name = name; p.dims = dims; p.defaultExpr = expr; p.color = color;
    int i = 0; for (float v : value) if (i < 4) p.value[i++] = v;
    return p;
}
using In = std::vector<std::string>;
using Out = std::vector<std::string>;

NodeDef Def(const std::string& type, const std::string& category, std::vector<PinDef> inputs, std::vector<PinDef> outputs,
            std::function<Out(const In&, const json&, int)> emit, const std::string& title = {}) {
    NodeDef d; d.type = type; d.title = title.empty() ? type : title; d.category = category;
    d.inputs = std::move(inputs); d.outputs = std::move(outputs); d.emit = std::move(emit);
    return d;
}
// A math node: dynamic inputs, one dynamic output
NodeDef Math(const std::string& type, std::vector<std::string> pins, std::function<std::string(const In&)> expr, std::vector<float> defaults = {}) {
    std::vector<PinDef> inputs;
    for (size_t i = 0; i < pins.size(); ++i) inputs.push_back(Pin(pins[i], 0, { i < defaults.size() ? defaults[i] : 0.0f }));
    return Def(type, "Math", inputs, { Pin("Out", 0) }, [expr](const In& in, const json&, int) { return Out{ expr(in) }; });
}

std::vector<NodeDef> Build() {
    std::vector<NodeDef> l;
    // ---- Output
    l.push_back(Def("Output", "Output", {
        Pin("Base Color", 3, { 0.5f, 0.5f, 0.5f }, {}, true), Pin("Normal", 3, { 0, 0, 1 }, "float3(0, 0, 1)"), Pin("Metallic", 1, { 0 }),
        Pin("Smoothness", 1, { 0.5f }), Pin("Emission", 3, { 0, 0, 0 }, {}, true), Pin("Ambient Occlusion", 1, { 1 }),
        Pin("Alpha", 1, { 1 }), Pin("Alpha Clip Threshold", 1, { 0.5f }) }, {}, nullptr, "Fragment Output"));

    // ---- Input
    auto constant = [&](const std::string& type, int dims, bool color) {
        NodeDef d = Def(type, "Input", {}, { Pin("Out", dims) }, [dims](const In&, const json& node, int) {
            json v = node.value("value", json::array({ 0, 0, 0, 0 }));
            auto at = [&](int i) { return v.is_array() && (size_t)i < v.size() && v[i].is_number() ? v[i].get<float>() : 0.0f; };
            std::ostringstream s; s.precision(7);
            if (dims == 1) s << "float(" << at(0) << ")";
            else { s << "float" << dims << "("; for (int i = 0; i < dims; ++i) s << (i ? ", " : "") << at(i); s << ")"; }
            return Out{ s.str() };
        });
        d.valueDims = dims; d.colorValue = color;
        return d;
    };
    l.push_back(constant("Float", 1, false));
    l.push_back(constant("Vector 2", 2, false));
    l.push_back(constant("Vector 3", 3, false));
    l.push_back(constant("Vector 4", 4, false));
    l.push_back(constant("Color", 4, true));
    {
        NodeDef d = Def("Property", "Input", {}, { Pin("Out", 4) }, [](const In&, const json& node, int) { return Out{ node.value("property", std::string("0")) }; });
        d.propertyKind = 1; l.push_back(d);
    }
    l.push_back(Def("UV", "Input", {}, { Pin("Out", 2) }, [](const In&, const json&, int) { return Out{ "IN.uv" }; }));
    l.push_back(Def("Time", "Input", {}, { Pin("Time", 1), Pin("Sine Time", 1), Pin("Cosine Time", 1) },
        [](const In&, const json&, int) { return Out{ "IN.time", "sin(IN.time)", "cos(IN.time)" }; }));
    l.push_back(Def("Position", "Input", {}, { Pin("Out", 3) }, [](const In&, const json&, int) { return Out{ "IN.worldPos" }; }, "Position (World)"));
    l.push_back(Def("Normal Vector", "Input", {}, { Pin("Out", 3) }, [](const In&, const json&, int) { return Out{ "IN.worldNormal" }; }, "Normal Vector (World)"));
    l.push_back(Def("View Direction", "Input", {}, { Pin("Out", 3) }, [](const In&, const json&, int) { return Out{ "IN.viewDir" }; }, "View Direction (World)"));
    l.push_back(Def("Screen Position", "Input", {}, { Pin("Out", 4) }, [](const In&, const json&, int) { return Out{ "IN.screenPos" }; }));
    l.push_back(Def("Camera Position", "Input", {}, { Pin("Out", 3) }, [](const In&, const json&, int) { return Out{ "_WorldSpaceCameraPos" }; }));

    // ---- Texture
    {
        NodeDef d = Def("Sample Texture 2D", "Texture", { Pin("UV", 2, {}, "IN.uv") }, { Pin("RGBA", 4), Pin("R", 1), Pin("G", 1), Pin("B", 1), Pin("A", 1) },
            [](const In& in, const json& node, int) {
                const std::string t = "tex2D(" + node.value("property", std::string("0")) + ", " + in[0] + ")";
                return Out{ t, t + ".r", t + ".g", t + ".b", t + ".a" };
            });
        d.propertyKind = 2; l.push_back(d);
    }
    {
        NodeDef d = Def("Sample Normal Map", "Texture", { Pin("UV", 2, {}, "IN.uv"), Pin("Strength", 1, { 1 }) }, { Pin("Normal", 3) },
            [](const In& in, const json& node, int) { return Out{ "UnpackScaleNormal(tex2D(" + node.value("property", std::string("0")) + ", " + in[0] + "), " + in[1] + ")" }; });
        d.propertyKind = 2; l.push_back(d);
    }

    // ---- Math (dynamic size)
    l.push_back(Math("Add", { "A", "B" }, [](const In& i) { return "(" + i[0] + " + " + i[1] + ")"; }));
    l.push_back(Math("Subtract", { "A", "B" }, [](const In& i) { return "(" + i[0] + " - " + i[1] + ")"; }));
    l.push_back(Math("Multiply", { "A", "B" }, [](const In& i) { return "(" + i[0] + " * " + i[1] + ")"; }, { 1, 1 }));
    l.push_back(Math("Divide", { "A", "B" }, [](const In& i) { return "(" + i[0] + " / " + i[1] + ")"; }, { 1, 1 }));
    l.push_back(Math("Power", { "A", "B" }, [](const In& i) { return "pow(abs(" + i[0] + "), " + i[1] + ")"; }, { 1, 2 }));
    l.push_back(Math("Minimum", { "A", "B" }, [](const In& i) { return "min(" + i[0] + ", " + i[1] + ")"; }));
    l.push_back(Math("Maximum", { "A", "B" }, [](const In& i) { return "max(" + i[0] + ", " + i[1] + ")"; }));
    l.push_back(Math("Lerp", { "A", "B", "T" }, [](const In& i) { return "lerp(" + i[0] + ", " + i[1] + ", " + i[2] + ")"; }, { 0, 1, 0.5f }));
    l.push_back(Math("Clamp", { "In", "Min", "Max" }, [](const In& i) { return "clamp(" + i[0] + ", " + i[1] + ", " + i[2] + ")"; }, { 0, 0, 1 }));
    l.push_back(Math("Step", { "Edge", "In" }, [](const In& i) { return "step(" + i[0] + ", " + i[1] + ")"; }, { 0.5f, 0 }));
    l.push_back(Math("Smoothstep", { "Edge1", "Edge2", "In" }, [](const In& i) { return "smoothstep(" + i[0] + ", " + i[1] + ", " + i[2] + ")"; }, { 0, 1, 0 }));
    l.push_back(Math("Modulo", { "A", "B" }, [](const In& i) { return "fmod(" + i[0] + ", " + i[1] + ")"; }, { 0, 1 }));
    l.push_back(Math("Remap", { "In", "In Min", "In Max", "Out Min", "Out Max" }, [](const In& i) {
        return "(" + i[3] + " + (" + i[0] + " - " + i[1] + ") * (" + i[4] + " - " + i[3] + ") / (" + i[2] + " - " + i[1] + "))"; }, { 0, -1, 1, 0, 1 }));
    auto unary = [&](const std::string& type, const std::string& fn) { l.push_back(Math(type, { "In" }, [fn](const In& i) { return fn + "(" + i[0] + ")"; })); };
    unary("Saturate", "saturate"); unary("Absolute", "abs"); unary("Sine", "sin"); unary("Cosine", "cos"); unary("Fraction", "frac");
    unary("Floor", "floor"); unary("Ceiling", "ceil"); unary("Round", "round"); unary("Square Root", "sqrt"); unary("Normalize", "normalize");
    l.push_back(Math("One Minus", { "In" }, [](const In& i) { return "(1.0 - " + i[0] + ")"; }));
    l.push_back(Math("Negate", { "In" }, [](const In& i) { return "(-" + i[0] + ")"; }));

    // ---- Vector
    l.push_back(Def("Dot Product", "Vector", { Pin("A", 3), Pin("B", 3) }, { Pin("Out", 1) }, [](const In& i, const json&, int) { return Out{ "dot(" + i[0] + ", " + i[1] + ")" }; }));
    l.push_back(Def("Cross Product", "Vector", { Pin("A", 3), Pin("B", 3) }, { Pin("Out", 3) }, [](const In& i, const json&, int) { return Out{ "cross(" + i[0] + ", " + i[1] + ")" }; }));
    l.push_back(Def("Length", "Vector", { Pin("In", 0) }, { Pin("Out", 1) }, [](const In& i, const json&, int) { return Out{ "length(" + i[0] + ")" }; }));
    l.push_back(Def("Distance", "Vector", { Pin("A", 0), Pin("B", 0) }, { Pin("Out", 1) }, [](const In& i, const json&, int) { return Out{ "distance(" + i[0] + ", " + i[1] + ")" }; }));
    l.push_back(Def("Reflect", "Vector", { Pin("In", 3), Pin("Normal", 3, {}, "IN.worldNormal") }, { Pin("Out", 3) }, [](const In& i, const json&, int) { return Out{ "reflect(" + i[0] + ", " + i[1] + ")" }; }));

    // ---- Channel
    l.push_back(Def("Split", "Channel", { Pin("In", 4) }, { Pin("R", 1), Pin("G", 1), Pin("B", 1), Pin("A", 1) },
        [](const In& i, const json&, int) { return Out{ i[0] + ".x", i[0] + ".y", i[0] + ".z", i[0] + ".w" }; }));
    l.push_back(Def("Combine", "Channel", { Pin("R", 1), Pin("G", 1), Pin("B", 1), Pin("A", 1, { 1 }) }, { Pin("RGBA", 4), Pin("RGB", 3), Pin("RG", 2) },
        [](const In& i, const json&, int) { return Out{ "float4(" + i[0] + ", " + i[1] + ", " + i[2] + ", " + i[3] + ")", "float3(" + i[0] + ", " + i[1] + ", " + i[2] + ")", "float2(" + i[0] + ", " + i[1] + ")" }; }));

    // ---- UV
    l.push_back(Def("Tiling And Offset", "UV", { Pin("UV", 2, {}, "IN.uv"), Pin("Tiling", 2, { 1, 1 }), Pin("Offset", 2, { 0, 0 }) }, { Pin("Out", 2) },
        [](const In& i, const json&, int) { return Out{ "(" + i[0] + " * " + i[1] + " + " + i[2] + ")" }; }));
    l.push_back(Def("Panner", "UV", { Pin("UV", 2, {}, "IN.uv"), Pin("Speed", 2, { 0.1f, 0 }) }, { Pin("Out", 2) },
        [](const In& i, const json&, int) { return Out{ "(" + i[0] + " + " + i[1] + " * IN.time)" }; }));
    l.push_back(Def("Rotate", "UV", { Pin("UV", 2, {}, "IN.uv"), Pin("Center", 2, { 0.5f, 0.5f }), Pin("Rotation", 1, { 0 }) }, { Pin("Out", 2) },
        [](const In& i, const json&, int) {
            return Out{ "(mul(float2x2(cos(" + i[2] + "), -sin(" + i[2] + "), sin(" + i[2] + "), cos(" + i[2] + ")), " + i[0] + " - " + i[1] + ") + " + i[1] + ")" }; }));

    // ---- Procedural
    l.push_back(Def("Simple Noise", "Procedural", { Pin("UV", 2, {}, "IN.uv"), Pin("Scale", 1, { 10 }) }, { Pin("Out", 1) },
        [](const In& i, const json&, int) { return Out{ "SG_SimpleNoise(" + i[0] + ", " + i[1] + ")" }; }));
    l.push_back(Def("Gradient Noise", "Procedural", { Pin("UV", 2, {}, "IN.uv"), Pin("Scale", 1, { 10 }) }, { Pin("Out", 1) },
        [](const In& i, const json&, int) { return Out{ "SG_GradientNoise(" + i[0] + ", " + i[1] + ")" }; }));
    l.push_back(Def("Voronoi", "Procedural", { Pin("UV", 2, {}, "IN.uv"), Pin("Angle Offset", 1, { 2 }), Pin("Cell Density", 1, { 5 }) }, { Pin("Out", 1) },
        [](const In& i, const json&, int) { return Out{ "SG_Voronoi(" + i[0] + ", " + i[2] + ", " + i[1] + ")" }; }));
    l.push_back(Def("Checkerboard", "Procedural", { Pin("UV", 2, {}, "IN.uv"), Pin("Color A", 3, { 0.2f, 0.2f, 0.2f }, {}, true), Pin("Color B", 3, { 0.7f, 0.7f, 0.7f }, {}, true), Pin("Frequency", 2, { 4, 4 }) }, { Pin("Out", 3) },
        [](const In& i, const json&, int) { return Out{ "lerp(" + i[1] + ", " + i[2] + ", SG_Checkerboard(" + i[0] + ", " + i[3] + "))" }; }));

    // ---- Effects
    l.push_back(Def("Fresnel Effect", "Effects", { Pin("Normal", 3, {}, "IN.worldNormal"), Pin("View Dir", 3, {}, "IN.viewDir"), Pin("Power", 1, { 5 }) }, { Pin("Out", 1) },
        [](const In& i, const json&, int) { return Out{ "SG_Fresnel(" + i[0] + ", " + i[1] + ", " + i[2] + ")" }; }));
    l.push_back(Def("Normal Strength", "Effects", { Pin("In", 3, { 0, 0, 1 }), Pin("Strength", 1, { 1 }) }, { Pin("Out", 3) },
        [](const In& i, const json&, int) { return Out{ "normalize(float3(" + i[0] + ".xy * " + i[1] + ", " + i[0] + ".z))" }; }));
    return l;
}

std::string Literal(const float* v, int dims) {
    std::ostringstream s; s.precision(7);
    if (dims == 1) { s << "float(" << v[0] << ")"; return s.str(); }
    s << "float" << dims << "(";
    for (int i = 0; i < dims; ++i) s << (i ? ", " : "") << v[i];
    s << ")";
    return s.str();
}

std::string Convert(const std::string& expr, int from, int to) {
    if (from == to || to <= 0) return expr;
    static const char* swizzle[] = { "", "x", "xy", "xyz", "xyzw" };
    if (from == 1) return "((float" + std::to_string(to) + ")" + expr + ")";
    if (from > to) return "(" + expr + ")." + swizzle[to];
    std::string s = "float" + std::to_string(to) + "(" + expr;
    for (int i = from; i < to; ++i) s += i == 3 ? ", 1" : ", 0";
    return s + ")";
}

int PropertyDims(const json& graph, const std::string& name) {
    for (const auto& p : graph.value("properties", json::array()))
        if (p.value("name", std::string()) == name) {
            const std::string type = p.value("type", std::string("float"));
            return type == "color" || type == "vector4" ? 4 : 1;
        }
    return 1;
}

} // namespace

const std::vector<NodeDef>& Library() { static const std::vector<NodeDef> library = Build(); return library; }
const NodeDef* Find(const std::string& type) { for (const auto& d : Library()) if (d.type == type) return &d; return nullptr; }
bool IsLit(const json& graph) { return graph.value("settings", json::object()).value("type", std::string("Lit")) != "Unlit"; }

bool OutputPinVisible(const json& graph, const std::string& pin) {
    const json settings = graph.value("settings", json::object());
    if (pin == "Alpha") return settings.value("surface", std::string("Opaque")) == "Transparent" || settings.value("alphaClip", false);
    if (pin == "Alpha Clip Threshold") return settings.value("alphaClip", false);
    if (!IsLit(graph)) return pin == "Base Color" || pin == "Emission";
    return true;
}

int OutputDims(const NodeDef& def, size_t pin, int dynamicDims, const json& graph, const json& node) {
    if (def.propertyKind == 1) return PropertyDims(graph, node.value("property", std::string()));
    const int d = def.outputs[pin].dims;
    return d == 0 ? std::max(1, dynamicDims) : d;
}

} // namespace Stardust::ShaderGraph

namespace Stardust::ShaderAssets {

using json = nlohmann::json;
using namespace Stardust::ShaderGraph;

std::string GraphToShader(const json& graph, const std::string& shaderName, std::string& error) {
    error.clear();
    std::map<int, json> nodes;
    for (const auto& n : graph.value("nodes", json::array())) if (n.is_object()) nodes[n.value("id", 0)] = n;
    // input pin -> (source node, source pin)
    std::map<std::pair<int, std::string>, std::pair<int, std::string>> links;
    for (const auto& l : graph.value("links", json::array())) {
        if (!l.contains("from") || !l.contains("to")) continue;
        links[{ l["to"][0].get<int>(), l["to"][1].get<std::string>() }] = { l["from"][0].get<int>(), l["from"][1].get<std::string>() };
    }
    int outputId = 0;
    for (auto& [id, n] : nodes) if (n.value("type", std::string()) == "Output") outputId = id;
    if (!outputId) { error = "The graph has no Fragment Output node"; return {}; }

    std::ostringstream body;
    std::map<int, std::vector<std::pair<std::string, int>>> emitted; // node -> output (variable, dims)
    std::set<int> visiting;
    std::function<bool(int)> emit = [&](int id) -> bool {
        if (emitted.count(id)) return true;
        if (visiting.count(id)) { error = "The graph has a loop"; return false; }
        auto found = nodes.find(id);
        if (found == nodes.end()) { error = "A link points to a missing node"; return false; }
        const json& node = found->second;
        const NodeDef* def = Find(node.value("type", std::string()));
        if (!def) { error = "Unknown node type " + node.value("type", std::string()); return false; }
        visiting.insert(id);
        // Sources first, and the dynamic size (widest connected dynamic input)
        std::vector<std::pair<std::string, int>> sources(def->inputs.size(), { "", 0 });
        int dynamicDims = 1;
        for (size_t i = 0; i < def->inputs.size(); ++i) {
            auto link = links.find({ id, def->inputs[i].name });
            if (link == links.end()) continue;
            if (!emit(link->second.first)) return false;
            const NodeDef* src = Find(nodes[link->second.first].value("type", std::string()));
            const auto& outs = emitted[link->second.first];
            size_t pin = 0;
            for (size_t k = 0; k < src->outputs.size(); ++k) if (src->outputs[k].name == link->second.second) pin = k;
            if (pin >= outs.size()) continue;
            sources[i] = outs[pin];
            if (def->inputs[i].dims == 0) dynamicDims = std::max(dynamicDims, outs[pin].second);
        }
        std::vector<std::string> in;
        for (size_t i = 0; i < def->inputs.size(); ++i) {
            const auto& pin = def->inputs[i];
            const int dims = pin.dims == 0 ? dynamicDims : pin.dims;
            if (!sources[i].first.empty()) { in.push_back(Convert(sources[i].first, sources[i].second, dims)); continue; }
            float v[4] = { pin.value[0], pin.value[1], pin.value[2], pin.value[3] };
            const json stored = node.value("inputs", json::object()).value(pin.name, json());
            bool hasStored = stored.is_array();
            if (hasStored) for (int k = 0; k < 4 && (size_t)k < stored.size(); ++k) if (stored[k].is_number()) v[k] = stored[k].get<float>();
            if (!pin.defaultExpr.empty() && !hasStored) in.push_back(Convert(pin.defaultExpr, pin.dims == 0 ? 1 : pin.dims, dims));
            else if (pin.dims == 0) in.push_back(Convert(Literal(v, 1), 1, dims));
            else in.push_back(Literal(v, dims));
        }
        visiting.erase(id);
        if (def->type == "Output") { emitted[id] = {}; return true; }
        if (def->propertyKind && node.value("property", std::string()).empty()) { error = def->title + ": choose a property"; return false; }
        const auto exprs = def->emit(in, node, dynamicDims);
        std::vector<std::pair<std::string, int>> outs;
        for (size_t k = 0; k < def->outputs.size() && k < exprs.size(); ++k) {
            const int dims = OutputDims(*def, k, dynamicDims, graph, node);
            const std::string var = "n" + std::to_string(id) + "_" + std::to_string(k);
            body << "            float" << (dims == 1 ? "" : std::to_string(dims)) << " " << var << " = " << exprs[k] << ";\n";
            outs.push_back({ var, dims });
        }
        emitted[id] = outs;
        return true;
    };
    if (!emit(outputId)) return {};

    // Output pins
    const json& output = nodes[outputId];
    auto value = [&](const std::string& pinName) -> std::string {
        const NodeDef* def = Find("Output");
        for (const auto& pin : def->inputs) {
            if (pin.name != pinName) continue;
            auto link = links.find({ outputId, pin.name });
            if (link != links.end()) {
                const NodeDef* src = Find(nodes[link->second.first].value("type", std::string()));
                const auto& outs = emitted[link->second.first];
                for (size_t k = 0; k < src->outputs.size() && k < outs.size(); ++k)
                    if (src->outputs[k].name == link->second.second) return Convert(outs[k].first, outs[k].second, pin.dims);
            }
            float v[4] = { pin.value[0], pin.value[1], pin.value[2], pin.value[3] };
            const json stored = output.value("inputs", json::object()).value(pin.name, json());
            if (stored.is_array()) for (int k = 0; k < 4 && (size_t)k < stored.size(); ++k) if (stored[k].is_number()) v[k] = stored[k].get<float>();
            if (!pin.defaultExpr.empty() && !stored.is_array()) return pin.defaultExpr;
            return Literal(v, pin.dims);
        }
        return "0";
    };

    const json settings = graph.value("settings", json::object());
    const bool lit = IsLit(graph), transparent = settings.value("surface", std::string("Opaque")) == "Transparent", clip = settings.value("alphaClip", false);
    const std::string blend = settings.value("blend", std::string("Alpha"));
    std::ostringstream s;
    s << "Shader \"" << shaderName << "\"\n{\n    Properties\n    {\n";
    for (const auto& p : graph.value("properties", json::array())) {
        const std::string name = p.value("name", std::string()), label = p.value("label", name), type = p.value("type", std::string("float"));
        if (name.empty()) continue;
        const json def = p.value("default", json());
        auto number = [&](const json& v) { std::ostringstream o; o << (v.is_number() ? v.get<float>() : v.is_boolean() ? (v.get<bool>() ? 1.0f : 0.0f) : 0.0f); return o.str(); };
        auto list = [&](const json& v) { std::ostringstream o; o << "("; for (int i = 0; i < 4; ++i) o << (i ? ", " : "") << (v.is_array() && (size_t)i < v.size() && v[i].is_number() ? v[i].get<float>() : (i == 3 ? 1.0f : 0.0f)); o << ")"; return o.str(); };
        if (type == "texture") {
            const std::string path = def.is_string() ? def.get<std::string>() : std::string();
            const std::string fallback = p.value("fallback", std::string("white"));
            s << "        " << (fallback == "normal" ? "[Normal] " : "") << name << " (\"" << label << "\", 2D) = \"" << (path.empty() ? (fallback == "normal" ? "bump" : fallback) : path) << "\" {}\n";
        } else if (type == "color") s << "        " << name << " (\"" << label << "\", Color) = " << list(def) << "\n";
        else if (type == "vector4") s << "        " << name << " (\"" << label << "\", Vector) = " << list(def) << "\n";
        else if (type == "int") s << "        " << name << " (\"" << label << "\", Int) = " << number(def) << "\n";
        else if (type == "bool") s << "        [Toggle] " << name << " (\"" << label << "\", Float) = " << number(def) << "\n";
        else if (p.contains("min") && p.contains("max")) s << "        " << name << " (\"" << label << "\", Range(" << p.value("min", 0.0f) << ", " << p.value("max", 1.0f) << ")) = " << number(def) << "\n";
        else s << "        " << name << " (\"" << label << "\", Float) = " << number(def) << "\n";
    }
    s << "    }\n    SubShader\n    {\n";
    s << "        Tags { \"Queue\" = \"" << (transparent ? "Transparent" : "Geometry") << "\" }\n";
    if (transparent) s << "        Blend " << (blend == "Additive" ? "SrcAlpha One" : blend == "Premultiplied" ? "One OneMinusSrcAlpha" : "SrcAlpha OneMinusSrcAlpha") << "\n        ZWrite Off\n";
    s << "        Cull " << (settings.value("twoSided", false) ? "Off" : "Back") << "\n";
    s << "        HLSLPROGRAM\n        #pragma surface surf " << (lit ? "Standard" : "Unlit") << "\n";
    s << "        void surf(SurfaceInput IN, inout SurfaceOutputStandard o)\n        {\n";
    s << body.str();
    s << "            o.Albedo = " << value("Base Color") << ";\n";
    s << "            o.Emission = " << value("Emission") << ";\n";
    if (lit) {
        s << "            o.Normal = " << value("Normal") << ";\n";
        s << "            o.Metallic = " << value("Metallic") << ";\n";
        s << "            o.Smoothness = " << value("Smoothness") << ";\n";
        s << "            o.Occlusion = " << value("Ambient Occlusion") << ";\n";
    }
    if (transparent || clip) s << "            o.Alpha = " << value("Alpha") << ";\n";
    if (clip) s << "            if (o.Alpha < " << value("Alpha Clip Threshold") << ") discard;\n";
    s << "        }\n        ENDHLSL\n    }\n}\n";
    return s.str();
}

json GraphTemplate(bool lit) {
    json graph = {
        { "version", 1 },
        { "settings", { { "type", lit ? "Lit" : "Unlit" }, { "surface", "Opaque" }, { "blend", "Alpha" }, { "twoSided", false }, { "alphaClip", false } } },
        { "properties", json::array({ { { "name", "_BaseColor" }, { "label", "Base Color" }, { "type", "color" }, { "default", { 1, 1, 1, 1 } } },
                                      { { "name", "_BaseMap" }, { "label", "Base Map" }, { "type", "texture" }, { "default", "" } } }) },
        { "nodes", json::array({
            { { "id", 1 }, { "type", "Output" }, { "pos", { 520, 80 } } },
            { { "id", 2 }, { "type", "Sample Texture 2D" }, { "pos", { 60, 60 } }, { "property", "_BaseMap" } },
            { { "id", 3 }, { "type", "Property" }, { "pos", { 60, 260 } }, { "property", "_BaseColor" } },
            { { "id", 4 }, { "type", "Multiply" }, { "pos", { 300, 120 } } } }) },
        { "links", json::array({ { { "from", { 2, "RGBA" } }, { "to", { 4, "A" } } }, { { "from", { 3, "Out" } }, { "to", { 4, "B" } } },
                                 { { "from", { 4, "Out" } }, { "to", { 1, "Base Color" } } } }) },
    };
    return graph;
}

} // namespace Stardust::ShaderAssets
