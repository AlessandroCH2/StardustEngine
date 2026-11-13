#pragma once
// .shader files (properties, render state and HLSL in one text file) and .shadergraph node graphs: both compile, in the editor, into
// <asset>.spv plus <asset>.spv.properties.json (material properties and render state), which materials use at run time.
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace Stardust::ShaderAssets {

struct Property {
    std::string name, label, type;   // type: float, int, bool, color, vector4, texture
    nlohmann::json value;            // default
    bool hasRange = false; float min = 0, max = 1;
    std::string fallback = "white";  // textures: white, black, gray, normal
    bool srgb = true;
};

struct ShaderSource {
    std::string name;                 // Shader "Custom/Name"
    std::vector<Property> properties;
    std::string queue = "geometry";   // geometry, transparent
    std::string blend = "opaque";     // opaque, alpha, additive, additiveAlpha, premultiplied
    int zwrite = -1;                  // -1 default (off when transparent)
    std::string cull = "back";        // back, front, off
    bool cullSet = false;
    std::string code;                 // HLSL between HLSLPROGRAM / ENDHLSL (CGPROGRAM / ENDCG)
    int codeLine = 1;                 // line of the code's first line in the .shader file
    std::string entry = "surf";
    std::string model = "Standard";   // Standard, Unlit (surface functions), Fragment (float4 frag(SurfaceInput IN))
    std::string alphaClip;            // property name compared with Alpha (alphatest:_Cutoff)
};

bool Parse(const std::string& text, ShaderSource& out, std::string& error);
std::string GenerateHlsl(const ShaderSource& source, const std::string& sourcePath);
nlohmann::json PropertySchema(const ShaderSource& source);

// Shader Graph -> .shader text (ShaderGraph.cpp)
std::string GraphToShader(const nlohmann::json& graph, const std::string& name, std::string& error);

// .shader / .shadergraph -> .spv + schema. Blocking (runs dxc); errors go to the log and to LastError().
bool Compile(const std::filesystem::path& asset);
void CompileAsync(const std::filesystem::path& asset);
std::string LastError(const std::filesystem::path& asset);
bool IsCompiling();
// Recompiles shader sources changed on disk (watched folder; cheap when nothing changed). Call once per frame.
void Update();

bool IsShaderAsset(const std::filesystem::path& path);

// Starting points for Create > Shader
std::string SurfaceTemplate(const std::string& name);
std::string UnlitTemplate(const std::string& name);
nlohmann::json GraphTemplate(bool lit);

} // namespace Stardust::ShaderAssets
