#pragma once
// Shader Graph node library and code generation. A .shadergraph is JSON:
//   settings   { type: Lit|Unlit, surface: Opaque|Transparent, blend: Alpha|Additive|Premultiplied, twoSided, alphaClip }
//   properties [{ name, label, type: float|int|bool|color|vector4|texture, default, min, max }]
//   nodes      [{ id, type, pos: [x, y], inputs: { pin: [x, y, z, w] }, property, value: [x, y, z, w] }]
//   links      [{ from: [node, "Pin"], to: [node, "Pin"] }]
// It becomes a .shader text (ShaderAssets::GraphToShader), compiled like any .shader.
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

namespace Stardust::ShaderGraph {

struct PinDef {
    std::string name;
    int dims = 1;                // 1..4, 0 = dynamic (the widest connected dynamic input)
    float value[4] = { 0, 0, 0, 0 };
    std::string defaultExpr;     // when unconnected and not editable, e.g. IN.uv
    bool color = false;          // edited with a colour picker
};

struct NodeDef {
    std::string type, title, category;
    std::vector<PinDef> inputs, outputs;
    int valueDims = 0;           // constant nodes: an editable value in the node body
    bool colorValue = false;
    int propertyKind = 0;        // 0 none, 1 any non-texture property, 2 texture property
    // inputs (converted to each pin's size) -> output expressions
    std::function<std::vector<std::string>(const std::vector<std::string>& in, const nlohmann::json& node, int dynamicDims)> emit;
};

const std::vector<NodeDef>& Library();
const NodeDef* Find(const std::string& type);
int OutputDims(const NodeDef& def, size_t pin, int dynamicDims, const nlohmann::json& graph, const nlohmann::json& node);
bool IsLit(const nlohmann::json& graph);
// Pins of the Output node shown for the graph's settings
bool OutputPinVisible(const nlohmann::json& graph, const std::string& pin);

} // namespace Stardust::ShaderGraph
