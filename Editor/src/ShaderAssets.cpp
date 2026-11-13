#include "ShaderAssets.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Core/Process.h"
#include "Project/Project.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Stardust::ShaderAssets {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return s; }
int LineOf(const std::string& text, size_t offset) { return 1 + (int)std::count(text.begin(), text.begin() + std::min(offset, text.size()), '\n'); }

// Text with // and /* */ comments blanked out (same length, so offsets and lines still match)
std::string WithoutComments(const std::string& text) {
    std::string out = text;
    bool inString = false;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '"' && (i == 0 || out[i - 1] != '\\')) inString = !inString;
        if (inString) continue;
        if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') { while (i < out.size() && out[i] != '\n') out[i++] = ' '; }
        else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*') {
            while (i < out.size() && !(out[i] == '*' && i + 1 < out.size() && out[i + 1] == '/')) { if (out[i] != '\n') out[i] = ' '; ++i; }
            if (i + 1 < out.size()) { out[i] = out[i + 1] = ' '; ++i; }
        }
    }
    return out;
}

// The text inside the braces that follow `keyword` (balanced), or npos
size_t BlockAfter(const std::string& text, const std::string& keyword, size_t from, std::string& block, size_t& blockStart) {
    std::regex word("\\b" + keyword + "\\b");
    std::smatch m;
    auto begin = text.cbegin() + std::min(from, text.size());
    if (!std::regex_search(begin, text.cend(), m, word)) return std::string::npos;
    size_t at = (size_t)(m.position(0) + (begin - text.cbegin()));
    size_t open = text.find('{', at);
    if (open == std::string::npos) return std::string::npos;
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}' && --depth == 0) { block = text.substr(open + 1, i - open - 1); blockStart = open + 1; return at; }
    }
    return std::string::npos;
}

json NumberList(const std::string& inside) {
    json list = json::array();
    std::stringstream ss(inside);
    std::string part;
    while (std::getline(ss, part, ',')) { try { list.push_back(std::stof(Trim(part))); } catch (...) { list.push_back(0.0f); } }
    return list;
}

bool ParseProperties(const std::string& block, std::vector<Property>& out, std::string& error) {
    // [Attributes] _Name ("Label", Type[(args)]) = default
    static const std::regex property(R"re(((?:\[[^\]]*\]\s*)*)(\w+)\s*\(\s*"([^"]*)"\s*,\s*(\w+)\s*(?:\(([^)]*)\))?\s*\)\s*=\s*)re");
    auto begin = block.cbegin();
    std::smatch m;
    std::set<std::string> names;
    while (std::regex_search(begin, block.cend(), m, property)) {
        Property p;
        const std::string attributes = Lower(m[1].str());
        p.name = m[2].str(); p.label = m[3].str();
        const std::string type = Lower(m[4].str()), args = m[5].str();
        auto rest = m.suffix().first;
        // The default runs to the end of the line
        auto lineEnd = std::find(rest, block.cend(), '\n');
        std::string value = Trim(std::string(rest, lineEnd));
        begin = lineEnd;
        if (!names.insert(p.name).second) continue;
        if (type == "float" || type == "range") {
            p.type = attributes.find("toggle") != std::string::npos ? "bool" : "float";
            if (type == "range") { auto r = NumberList(args); if (r.size() == 2) { p.hasRange = true; p.min = r[0]; p.max = r[1]; } }
            try { p.value = std::stof(value); } catch (...) { p.value = 0.0f; }
            if (p.type == "bool") p.value = p.value.get<float>() != 0;
        } else if (type == "int" || type == "integer") {
            p.type = "int";
            try { p.value = std::stoi(value); } catch (...) { p.value = 0; }
        } else if (type == "color" || type == "vector") {
            p.type = type == "color" ? "color" : "vector4";
            auto open = value.find('('), close = value.rfind(')');
            json list = open != std::string::npos && close != std::string::npos ? NumberList(value.substr(open + 1, close - open - 1)) : json::array();
            while (list.size() < 4) list.push_back(type == "color" ? 1.0f : 0.0f);
            p.value = list;
        } else if (type == "2d") {
            p.type = "texture";
            auto q1 = value.find('"'), q2 = value.find('"', q1 + 1);
            std::string text = q1 != std::string::npos && q2 != std::string::npos ? value.substr(q1 + 1, q2 - q1 - 1) : std::string();
            const std::string lower = Lower(text);
            if (lower == "white" || lower == "black" || lower == "gray" || lower == "grey" || lower == "bump" || lower == "normal" || lower.empty()) {
                p.fallback = lower == "bump" || lower == "normal" ? "normal" : lower == "grey" ? "gray" : lower.empty() ? "white" : lower;
                p.value = "";
            } else p.value = text;   // an asset path
            p.srgb = !(attributes.find("normal") != std::string::npos || p.fallback == "normal" || attributes.find("linear") != std::string::npos);
        } else {
            error = "Unknown property type '" + m[4].str() + "' for " + p.name;
            return false;
        }
        out.push_back(p);
    }
    return true;
}

std::string HlslNumber(float v) { std::ostringstream ss; ss.precision(9); ss << v; std::string s = ss.str(); if (s.find_first_of(".eE") == std::string::npos) s += ".0"; return s; }
std::string HlslVector(const json& list) {
    std::string s = "float4(";
    for (int i = 0; i < 4; ++i) { s += HlslNumber(list.is_array() && (size_t)i < list.size() && list[i].is_number() ? list[i].get<float>() : 0.0f); if (i < 3) s += ", "; }
    return s + ")";
}

} // namespace

// ------------------------------------------------------------------------------------------------ parse

bool Parse(const std::string& original, ShaderSource& out, std::string& error) {
    out = ShaderSource();
    const std::string text = WithoutComments(original);
    // Program block first: everything else is searched outside it
    static const std::regex programBegin(R"(\b(HLSLPROGRAM|CGPROGRAM)\b)"), programEnd(R"(\b(ENDHLSL|ENDCG)\b)");
    std::smatch b, e;
    if (!std::regex_search(text, b, programBegin)) { error = "No HLSLPROGRAM ... ENDHLSL block"; return false; }
    const size_t codeStart = (size_t)b.position(0) + b.length(0);
    auto afterBegin = text.cbegin() + codeStart;
    if (!std::regex_search(afterBegin, text.cend(), e, programEnd)) { error = "HLSLPROGRAM without ENDHLSL"; return false; }
    const size_t codeEnd = codeStart + (size_t)e.position(0);
    out.code = original.substr(codeStart, codeEnd - codeStart);
    out.codeLine = LineOf(original, codeStart);
    const std::string outside = text.substr(0, codeStart) + std::string(codeEnd - codeStart, ' ') + text.substr(codeEnd);

    std::smatch m;
    static const std::regex shaderName(R"re(\bShader\s*"([^"]*)")re");
    if (std::regex_search(outside, m, shaderName)) out.name = m[1].str();

    std::string block; size_t blockStart = 0;
    if (BlockAfter(outside, "Properties", 0, block, blockStart) != std::string::npos && !ParseProperties(block, out.properties, error)) return false;
    if (out.properties.size() > 64) { error = "At most 64 properties"; return false; }

    // Render state (SubShader or Pass level; the last one written wins)
    static const std::regex queue(R"re("Queue"\s*=\s*"(\w+)[^"]*")re"), blend(R"(\bBlend\s+(\w+)(?:\s+(\w+))?)"), zwrite(R"(\bZWrite\s+(\w+))"), cull(R"(\bCull\s+(\w+))");
    if (std::regex_search(outside, m, queue)) out.queue = Lower(m[1].str()) == "transparent" ? "transparent" : "geometry";
    for (auto it = std::sregex_iterator(outside.begin(), outside.end(), blend); it != std::sregex_iterator(); ++it) {
        const std::string src = Lower((*it)[1].str()), dst = Lower((*it)[2].str());
        if (src == "off") out.blend = "opaque";
        else if (src == "srcalpha" && dst == "oneminussrcalpha") out.blend = "alpha";
        else if (src == "one" && dst == "one") out.blend = "additive";
        else if (src == "srcalpha" && dst == "one") out.blend = "additiveAlpha";
        else if (src == "one" && dst == "oneminussrcalpha") out.blend = "premultiplied";
        else { error = "Unsupported Blend " + (*it)[1].str() + " " + (*it)[2].str() + " (use SrcAlpha OneMinusSrcAlpha, One One, SrcAlpha One, One OneMinusSrcAlpha or Off)"; return false; }
    }
    for (auto it = std::sregex_iterator(outside.begin(), outside.end(), zwrite); it != std::sregex_iterator(); ++it) out.zwrite = Lower((*it)[1].str()) == "on" ? 1 : 0;
    for (auto it = std::sregex_iterator(outside.begin(), outside.end(), cull); it != std::sregex_iterator(); ++it) {
        const std::string mode = Lower((*it)[1].str());
        out.cull = mode == "front" ? "front" : mode == "off" ? "off" : "back"; out.cullSet = true;
    }

    // Pragmas inside the program
    static const std::regex surfacePragma(R"(#pragma\s+surface\s+(\w+)\s+(\w+)([^\n]*))"), fragmentPragma(R"(#pragma\s+fragment\s+(\w+))");
    if (std::regex_search(out.code, m, surfacePragma)) {
        out.entry = m[1].str();
        const std::string model = Lower(m[2].str());
        out.model = model == "unlit" ? "Unlit" : "Standard";
        const std::string options = m[3].str();
        std::smatch o;
        if (std::regex_search(options, o, std::regex(R"(alphatest:(\w+))"))) out.alphaClip = o[1].str();
        if (std::regex_search(options, o, std::regex(R"(\balpha(:fade|:blend)?\b)"))) { out.queue = "transparent"; if (out.blend == "opaque") out.blend = "alpha"; }
        if (std::regex_search(options, o, std::regex(R"(\balpha:premul\b)"))) { out.queue = "transparent"; out.blend = "premultiplied"; }
    } else if (std::regex_search(out.code, m, fragmentPragma)) { out.entry = m[1].str(); out.model = "Fragment"; }
    else { error = "Add #pragma surface surf Standard (lit), #pragma surface surf Unlit, or #pragma fragment frag"; return false; }
    if (out.blend != "opaque") out.queue = "transparent";
    if (out.queue == "transparent" && out.blend == "opaque") out.blend = "alpha";
    return true;
}

nlohmann::json PropertySchema(const ShaderSource& s) {
    json properties = json::array();
    for (const auto& p : s.properties) {
        json field = { { "name", p.name }, { "label", p.label.empty() ? p.name : p.label }, { "type", p.type }, { "default", p.value } };
        if (p.hasRange) { field["min"] = p.min; field["max"] = p.max; }
        if (p.type == "texture") { field["srgb"] = p.srgb; field["fallback"] = p.fallback; }
        properties.push_back(field);
    }
    json state = { { "queue", s.queue }, { "blend", s.blend }, { "cull", s.cull } };
    if (s.zwrite >= 0) state["zwrite"] = s.zwrite == 1;
    if (!s.cullSet) state.erase("cull");
    return { { "name", s.name }, { "properties", properties }, { "renderState", state } };
}

// ------------------------------------------------------------------------------------------------ HLSL

std::string GenerateHlsl(const ShaderSource& s, const std::string& sourcePath) {
    std::string code = s.code;
    std::set<std::string> propertyNames;
    for (const auto& p : s.properties) propertyNames.insert(p.name);
    // Properties redeclared as uniforms in the program, and .cginc includes, are dropped
    {
        std::string cleaned;
        std::istringstream lines(code);
        std::string line;
        static const std::regex declaration(R"(^\s*(?:uniform\s+)?(?:float|half|fixed|int|uint|bool|sampler2D|Texture2D|SamplerState|float[234]|half[234]|fixed[234])\s+(\w+)\s*;\s*$)");
        static const std::regex legacyInclude(R"(^\s*#include\s+"[^"]*\.cginc")"), pragma(R"(^\s*#pragma\b)");
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::smatch m;
            bool drop = std::regex_search(line, pragma) || std::regex_search(line, legacyInclude);
            if (std::regex_match(line, m, declaration)) {
                std::string name = m[1].str();
                if (propertyNames.count(name) || (name.size() > 3 && name.ends_with("_ST") && propertyNames.count(name.substr(0, name.size() - 3)))) drop = true;
            }
            cleaned += drop ? "\n" : line + "\n"; // keep the line count for error positions
        }
        code = cleaned;
    }
    std::ostringstream h;
    h << "// Generated by the Stardust editor from " << sourcePath << ". Edit the source, not this file.\n";
    h << "#include \"Surface.hlsli\"\n";
    for (size_t i = 0; i < s.properties.size(); ++i) {
        const auto& p = s.properties[i];
        if (p.type == "texture") h << "static const uint " << p.name << " = " << i << ";\n#define " << p.name << "_ST float4(1, 1, 0, 0)\n";
        else if (p.type == "color" || p.type == "vector4") h << "#define " << p.name << " MaterialProperty(" << i << ", " << HlslVector(p.value) << ")\n";
        else if (p.type == "int") h << "#define " << p.name << " ((int)MaterialFloat(" << i << ", " << HlslNumber(p.value.is_number() ? p.value.get<float>() : 0) << "))\n";
        else h << "#define " << p.name << " MaterialFloat(" << i << ", " << HlslNumber(p.value.is_boolean() ? (p.value.get<bool>() ? 1.f : 0.f) : p.value.is_number() ? p.value.get<float>() : 0.f) << ")\n";
    }
    std::string escaped = sourcePath; std::replace(escaped.begin(), escaped.end(), '\\', '/');
    h << "#line " << s.codeLine << " \"" << escaped << "\"\n" << code << "\n";

    // A `struct Input` is filled from the surface data by member name
    std::string inputFill;
    std::smatch m;
    static const std::regex inputStruct(R"(struct\s+Input\s*\{([^}]*)\})");
    const bool customInput = s.model != "Fragment" && std::regex_search(code, m, inputStruct);
    if (customInput) {
        static const std::regex member(R"((float[234]?|half[234]?|fixed[234]?)\s+(\w+)\s*;)");
        const std::string body = m[1].str();
        for (auto it = std::sregex_iterator(body.begin(), body.end(), member); it != std::sregex_iterator(); ++it) {
            const std::string name = (*it)[2].str();
            if (name.starts_with("uv")) inputFill += "    IN." + name + " = IN_.uv;\n";
            else if (name == "worldPos") inputFill += "    IN.worldPos = IN_.worldPos;\n";
            else if (name == "worldNormal") inputFill += "    IN.worldNormal = IN_.worldNormal;\n";
            else if (name == "viewDir") inputFill += "    IN.viewDir = IN_.viewDir;\n";
            else if (name == "screenPos") inputFill += "    IN.screenPos = IN_.screenPos;\n";
            else if (name == "worldRefl") inputFill += "    IN.worldRefl = reflect(-IN_.viewDir, IN_.worldNormal);\n";
            else if (name == "color") inputFill += "    IN.color = 1;\n";
        }
    }
    const bool opaque = s.queue != "transparent";
    h << "\n#line 100000 \"generated main\"\n";
    h << "MeshPSOut main(MeshVSOut i, bool frontFace : SV_IsFrontFace) {\n";
    h << "    SurfaceInput IN_ = MakeSurfaceInput(i, frontFace);\n";
    if (s.model == "Fragment") {
        h << "    float4 c = " << s.entry << "(IN_);\n";
    } else {
        if (customInput) h << "    Input IN = (Input)0;\n" << inputFill;
        else h << "    SurfaceInput IN = IN_;\n";
        h << "    SurfaceOutputStandard o = DefaultSurfaceOutput();\n";
        h << "    " << s.entry << "(IN, o);\n";
        if (!s.alphaClip.empty()) h << "    if (o.Alpha < " << s.alphaClip << ") discard;\n";
        if (s.model == "Unlit") h << "    float4 c = ShadeUnlit(IN_, float4(o.Albedo + o.Emission, o.Alpha));\n";
        else h << "    float4 c = ShadeStandard(IN_, o);\n";
    }
    if (opaque) h << "    c.a = 1;\n";
    if (s.blend == "additiveAlpha") h << "    c.rgb *= c.a;\n";
    h << "    MeshPSOut r;\n    r.color = c;\n    r.entityId = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex).ids.z;\n    return r;\n}\n";
    return h.str();
}

// ------------------------------------------------------------------------------------------------ compile

namespace {
std::mutex s_Mutex;
std::map<std::string, std::string> s_Errors;
std::deque<fs::path> s_Queue;
std::set<std::string> s_Queued;
std::atomic<int> s_Active{ 0 };
bool s_WorkerRunning = false;

std::string Key(const fs::path& p) { return FileSystem::Utf8(fs::absolute(p).lexically_normal()); }
void SetError(const fs::path& asset, const std::string& error) { std::lock_guard lock(s_Mutex); if (error.empty()) s_Errors.erase(Key(asset)); else s_Errors[Key(asset)] = error; }

fs::path DxcPath() {
    const fs::path bundled = FileSystem::ExecutableDir() / "tools/dxc/bin/dxc";
    return fs::exists(FileSystem::ExecutableDir() / "tools/dxc/bin") ? bundled : fs::path("dxc");
}
} // namespace

bool IsShaderAsset(const fs::path& path) { const auto ext = path.extension().string(); return ext == ".shader" || ext == ".shadergraph"; }

bool Compile(const fs::path& asset) {
    std::string error;
    const std::string text = FileSystem::ReadText(asset);
    ShaderSource source;
    const bool graph = asset.extension() == ".shadergraph";
    std::string shaderText = text;
    if (graph) {
        auto j = json::parse(text, nullptr, false);
        if (!j.is_object()) { SetError(asset, "Not a valid shader graph"); SD_ERROR("Shader Graph {}: not valid JSON", FileSystem::Utf8(asset)); return false; }
        shaderText = GraphToShader(j, "Shader Graphs/" + asset.stem().string(), error);
        if (!error.empty()) { SetError(asset, error); SD_ERROR("Shader Graph {}: {}", FileSystem::Utf8(asset), error); return false; }
    }
    if (!Parse(shaderText, source, error)) { SetError(asset, error); SD_ERROR("Shader {}: {}", FileSystem::Utf8(asset), error); return false; }
    if (source.name.empty()) source.name = asset.stem().string();

    // Generated HLSL goes to a temporary folder; dxc reports errors against the .shader lines (#line)
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec) / "StardustShaders";
    fs::create_directories(temp, ec);
    const fs::path generated = temp / (std::to_string(std::hash<std::string>{}(Key(asset))) + ".ps.hlsl");
    const std::string displayPath = graph ? FileSystem::Utf8(asset) + " (generated)" : FileSystem::Utf8(asset);
    if (!FileSystem::WriteText(generated, GenerateHlsl(source, displayPath))) { SetError(asset, "Cannot write the generated shader"); return false; }
    const fs::path output = fs::path(FileSystem::Utf8(asset) + ".spv"), building = fs::path(FileSystem::Utf8(asset) + ".spv.tmp");
    std::wstring command = L"\"" + DxcPath().wstring() + L"\" -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -T ps_6_6 -E main -HV 2021 -O3 -Zpc -D STARDUST_VULKAN=1 -I \"" +
                           (FileSystem::EngineRoot() / "Assets/Shaders").wstring() + L"\" -Fo \"" + building.wstring() + L"\" \"" + generated.wstring() + L"\"";
    std::string log;
    const int result = RunProcess(command, asset.parent_path(), [&](const std::string& line) { if (!line.empty()) log += line + "\n"; });
    if (result != 0) {
        fs::remove(building, ec);
        // The first error line is enough for the inspector; the whole output goes to the Console
        std::string first;
        std::istringstream lines(log); std::string line;
        while (std::getline(lines, line)) if (line.find("error") != std::string::npos) { first = line; break; }
        SetError(asset, first.empty() ? (result < 0 ? "dxc not found (tools/dxc next to the editor)" : log) : first);
        SD_ERROR("Shader compilation failed: {}\n{}", FileSystem::Utf8(asset), log);
        return false;
    }
    // Schema first, then the shader: the renderer reloads when the .spv changes
    FileSystem::WriteText(fs::path(FileSystem::Utf8(asset) + ".spv.properties.json"), PropertySchema(source).dump(2));
    fs::rename(building, output, ec);
    if (ec) { fs::copy_file(building, output, fs::copy_options::overwrite_existing, ec); fs::remove(building, ec); }
    SetError(asset, "");
    SD_INFO("Shader compiled: {}", AssetManager::ToRelative(asset));
    return true;
}

void CompileAsync(const fs::path& asset) {
    std::lock_guard lock(s_Mutex);
    if (!s_Queued.insert(Key(asset)).second) return;
    s_Queue.push_back(asset);
    ++s_Active;
    if (!s_WorkerRunning) {
        s_WorkerRunning = true;
        std::thread([] {
            for (;;) {
                fs::path next;
                {
                    std::lock_guard lock(s_Mutex);
                    if (s_Queue.empty()) { s_WorkerRunning = false; return; }
                    next = s_Queue.front(); s_Queue.pop_front();
                }
                Compile(next);
                { std::lock_guard lock(s_Mutex); s_Queued.erase(Key(next)); } // a scan during the compile must not queue it again
                --s_Active;
            }
        }).detach();
    }
}

std::string LastError(const fs::path& asset) { std::lock_guard lock(s_Mutex); auto it = s_Errors.find(Key(asset)); return it == s_Errors.end() ? std::string() : it->second; }
bool IsCompiling() { return s_Active > 0; }

void Update() {
    if (!Project::IsLoaded()) return;
    static std::string watchedRoot;
    static auto lastScan = std::chrono::steady_clock::time_point();
    bool scan = false;
    const std::string root = FileSystem::Utf8(Project::AssetsDir());
#ifdef _WIN32
    static HANDLE handle = INVALID_HANDLE_VALUE;
    if (watchedRoot != root) {
        if (handle != INVALID_HANDLE_VALUE) FindCloseChangeNotification(handle);
        handle = FindFirstChangeNotificationW(Project::AssetsDir().wstring().c_str(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
        watchedRoot = root; scan = true;
    } else if (handle == INVALID_HANDLE_VALUE) scan = std::chrono::steady_clock::now() - lastScan > std::chrono::seconds(2);
    else if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) { scan = true; FindNextChangeNotification(handle); }
#else
    if (watchedRoot != root) { watchedRoot = root; scan = true; }
    else scan = std::chrono::steady_clock::now() - lastScan > std::chrono::seconds(2);
#endif
    if (!scan) return;
    lastScan = std::chrono::steady_clock::now();
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(Project::AssetsDir(), fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) || !IsShaderAsset(it->path())) continue;
        const fs::path spv = fs::path(FileSystem::Utf8(it->path()) + ".spv");
        std::error_code e1, e2;
        const auto source = fs::last_write_time(it->path(), e1), compiled = fs::last_write_time(spv, e2);
        if (e1) continue;
        if (e2 || compiled < source) {
            // A failed source is retried only when it changes again
            static std::map<std::string, fs::file_time_type> failed;
            auto f = failed.find(Key(it->path()));
            if (f != failed.end() && f->second == source && !LastError(it->path()).empty()) continue;
            failed[Key(it->path())] = source;
            CompileAsync(it->path());
        }
    }
}

// ------------------------------------------------------------------------------------------------ templates

std::string SurfaceTemplate(const std::string& name) {
    return R"(Shader "Custom/)" + name + R"("
{
    Properties
    {
        _Color ("Color", Color) = (1, 1, 1, 1)
        _MainTex ("Albedo", 2D) = "white" {}
        [Normal] _BumpMap ("Normal Map", 2D) = "bump" {}
        _Glossiness ("Smoothness", Range(0, 1)) = 0.5
        _Metallic ("Metallic", Range(0, 1)) = 0
        [HDR] _EmissionColor ("Emission", Color) = (0, 0, 0, 1)
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" }
        Cull Back

        HLSLPROGRAM
        #pragma surface surf Standard

        // SurfaceInput: worldPos, worldNormal, viewDir, uv, screenPos, time, frontFace
        void surf(SurfaceInput IN, inout SurfaceOutputStandard o)
        {
            float4 c = tex2D(_MainTex, IN.uv) * _Color;
            o.Albedo = c.rgb;
            o.Normal = UnpackNormal(tex2D(_BumpMap, IN.uv));
            o.Metallic = _Metallic;
            o.Smoothness = _Glossiness;
            o.Emission = _EmissionColor.rgb;
            o.Alpha = c.a;
        }
        ENDHLSL
    }
}
)";
}

std::string UnlitTemplate(const std::string& name) {
    return R"(Shader "Custom/)" + name + R"("
{
    Properties
    {
        _Color ("Color", Color) = (1, 1, 1, 1)
        _MainTex ("Texture", 2D) = "white" {}
        _Speed ("Scroll Speed", Range(0, 5)) = 0.5
    }
    SubShader
    {
        Tags { "Queue" = "Transparent" }
        Blend SrcAlpha OneMinusSrcAlpha
        ZWrite Off
        Cull Off

        HLSLPROGRAM
        #pragma fragment frag

        // Returns the final colour (alpha used by the blending)
        float4 frag(SurfaceInput IN)
        {
            float2 uv = IN.uv + float2(_Time.y * _Speed, 0);
            return tex2D(_MainTex, uv) * _Color;
        }
        ENDHLSL
    }
}
)";
}

} // namespace Stardust::ShaderAssets
