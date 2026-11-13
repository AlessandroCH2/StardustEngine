// Script Editor: a small code editor inside the Stardust editor. Tabs for open files, C# / HLSL / .shader syntax
// colours, line numbers, undo, find, comment toggling, and autocompletion from the engine API (reflected from
// Stardust.ScriptCore and the game scripts), the language keywords and the identifiers of the file.
#include "ScriptEditor.h"
#include "ShaderAssets.h"
#include "Core/Application.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Scripting/ScriptEngine.h"
#include "Assets/AssetManager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace Stardust::ScriptEditor {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

enum class Lang { CSharp, Shader, Text };

struct Pos {
    int line = 0, col = 0; // col: byte offset in the line
    bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
    bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
};

struct Snapshot { std::vector<std::string> lines; Pos cursor; };

struct Doc {
    fs::path path;
    std::vector<std::string> lines{ "" };
    Pos cursor, anchor;
    bool dirty = false, crlf = false;
    Lang lang = Lang::Text;
    std::vector<Snapshot> undo, redo;
    int lastEditKind = 0;
    std::chrono::steady_clock::time_point lastEdit;
    fs::file_time_type stamp;
    bool scrollToCursor = true;
    int gotoLine = -1;
    float preferredX = -1;
};

struct Member { std::string name, kind, type, signature; };
struct TypeInfo { std::string name, ns, kind, base; std::vector<Member> statics, instance; };

struct Completion { std::string text, detail; char kind; std::string ns; }; // ns: namespace to import when a type is picked

// A compiler message for a line of a file (from the last script build)
struct Diagnostic { int line = 0, col = 0; bool error = true; std::string code, message, symbol; };
// A fix offered for a diagnostic: optionally replace `symbol` at the position, and add a using
struct Fix { std::string label, addUsing, replaceWith; int line = 0, col = 0; std::string symbol; };

std::vector<Doc> s_Docs;
int s_Active = -1, s_RequestTab = -1;
bool s_Open = false;
std::unordered_map<std::string, TypeInfo> s_Types;
std::unordered_map<std::string, std::vector<std::string>> s_TypeNamespaces; // type name -> namespaces that have it
std::set<std::string> s_Namespaces;
u64 s_DiagnosticsGeneration = ~0ull;
std::vector<std::pair<fs::path, Diagnostic>> s_Diagnostics;
bool s_ShowProblems = true;
bool s_FixMenu = false;
bool s_SymbolsLoaded = false;
std::chrono::steady_clock::time_point s_SymbolsTime;
// Completion popup
bool s_CompletionOpen = false;
std::vector<Completion> s_Completions;
int s_CompletionIndex = 0;
Pos s_CompletionStart;
// Find
bool s_FindOpen = false, s_FindFocus = false;
std::string s_Find;
int s_ConfirmClose = -1;
bool s_Refocus = false;
float s_Zoom = 1.0f; // text size of the code (Ctrl+wheel, Ctrl+=, Ctrl+-, Ctrl+0)
bool s_CompletionScroll = false; // keyboard moved the selection: scroll the list to it once

const std::unordered_set<std::string>& Keywords(Lang lang) {
    static const std::unordered_set<std::string> cs = {
        "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue", "decimal", "default", "delegate",
        "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in",
        "int", "interface", "internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override", "params", "private",
        "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string", "struct", "switch",
        "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "virtual", "void", "volatile", "while",
        "var", "async", "await", "get", "set", "value", "yield", "partial", "where", "nameof", "record", "init", "with", "when" };
    static const std::unordered_set<std::string> hlsl = {
        "Shader", "Properties", "SubShader", "Pass", "Tags", "Blend", "ZWrite", "Cull", "ZTest", "HLSLPROGRAM", "ENDHLSL", "CGPROGRAM", "ENDCG", "Off", "On", "Back", "Front",
        "Range", "Color", "Vector", "Float", "Int", "2D", "SrcAlpha", "OneMinusSrcAlpha", "One", "Zero",
        "float", "float2", "float3", "float4", "float2x2", "float3x3", "float4x4", "half", "half2", "half3", "half4", "fixed", "fixed2", "fixed3", "fixed4",
        "int", "int2", "int3", "int4", "uint", "uint2", "uint3", "uint4", "bool", "void", "struct", "return", "if", "else", "for", "while", "do", "break",
        "continue", "discard", "static", "const", "in", "out", "inout", "uniform", "true", "false", "typedef", "cbuffer", "Texture2D", "SamplerState", "sampler2D" };
    static const std::unordered_set<std::string> none;
    return lang == Lang::CSharp ? cs : lang == Lang::Shader ? hlsl : none;
}
const std::vector<std::string>& ShaderBuiltins() {
    static const std::vector<std::string> list = {
        "abs", "acos", "all", "any", "asin", "atan", "atan2", "ceil", "clamp", "clip", "cos", "cross", "ddx", "ddy", "degrees", "distance", "dot", "exp", "exp2",
        "floor", "fmod", "frac", "fwidth", "length", "lerp", "log", "log2", "max", "min", "mul", "normalize", "pow", "radians", "reflect", "refract", "round",
        "rsqrt", "saturate", "sign", "sin", "smoothstep", "sqrt", "step", "tan", "tex2D", "tex2Dlod", "UnpackNormal", "UnpackScaleNormal", "TRANSFORM_TEX",
        "Luminance", "MaterialProperty", "MaterialFloat", "MaterialTexture", "ShadeStandard", "ShadeUnlit", "TangentToWorld", "SG_SimpleNoise", "SG_GradientNoise",
        "SG_Voronoi", "SG_Checkerboard", "SG_Fresnel", "_Time", "_SinTime", "_CosTime", "_WorldSpaceCameraPos", "SurfaceInput", "SurfaceOutputStandard",
        "Albedo", "Normal", "Emission", "Metallic", "Smoothness", "Occlusion", "Alpha", "worldPos", "worldNormal", "worldTangent", "worldBitangent", "viewDir",
        "uv", "screenPos", "time", "frontFace", "surf", "frag", "#pragma surface surf Standard", "#pragma surface surf Unlit", "#pragma fragment frag", "alphatest:_Cutoff" };
    return list;
}

Lang LangOf(const fs::path& p) {
    const std::string ext = p.extension().string();
    if (ext == ".cs") return Lang::CSharp;
    if (ext == ".shader" || ext == ".hlsl" || ext == ".hlsli" || ext == ".compute") return Lang::Shader;
    return Lang::Text;
}

bool IsIdent(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }
bool IsContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }
int NextChar(const std::string& s, int i) { if (i >= (int)s.size()) return (int)s.size(); ++i; while (i < (int)s.size() && IsContinuation((unsigned char)s[i])) ++i; return i; }
int PrevChar(const std::string& s, int i) { if (i <= 0) return 0; --i; while (i > 0 && IsContinuation((unsigned char)s[i])) --i; return i; }
// Columns on screen (tabs every 4) for a byte offset, and back
int VisualCol(const std::string& s, int byte) {
    int v = 0;
    for (int i = 0; i < byte && i < (int)s.size(); i = NextChar(s, i)) v = s[i] == '\t' ? (v / 4 + 1) * 4 : v + 1;
    return v;
}
int ByteAtVisual(const std::string& s, float visual) {
    int v = 0, i = 0;
    while (i < (int)s.size()) {
        const int w = s[i] == '\t' ? (v / 4 + 1) * 4 - v : 1;
        if (visual < v + w * 0.5f) return i;
        v += w; i = NextChar(s, i);
    }
    return (int)s.size();
}

Doc* Active() { return s_Active >= 0 && s_Active < (int)s_Docs.size() ? &s_Docs[s_Active] : nullptr; }

std::string Joined(const Doc& d) {
    std::string out;
    for (size_t i = 0; i < d.lines.size(); ++i) { out += d.lines[i]; if (i + 1 < d.lines.size()) out += d.crlf ? "\r\n" : "\n"; }
    return out;
}
void Load(Doc& d) {
    std::string text = FileSystem::ReadText(d.path);
    d.crlf = text.find("\r\n") != std::string::npos;
    d.lines.clear();
    std::string line;
    for (char c : text) { if (c == '\n') { d.lines.push_back(line); line.clear(); } else if (c != '\r') line += c; }
    d.lines.push_back(line);
    std::error_code ec; d.stamp = fs::last_write_time(d.path, ec);
    d.dirty = false;
    d.cursor.line = std::min(d.cursor.line, (int)d.lines.size() - 1);
    d.cursor.col = std::min(d.cursor.col, (int)d.lines[d.cursor.line].size());
    d.anchor = d.cursor;
}
bool Save(Doc& d) {
    if (!FileSystem::WriteText(d.path, Joined(d))) { SD_ERROR("Cannot save {}", FileSystem::Utf8(d.path)); return false; }
    std::error_code ec; d.stamp = fs::last_write_time(d.path, ec);
    d.dirty = false;
    if (ShaderAssets::IsShaderAsset(d.path)) ShaderAssets::CompileAsync(d.path);
    return true;
}

// ---- editing
bool HasSelection(const Doc& d) { return !(d.cursor == d.anchor); }
void Ordered(const Doc& d, Pos& a, Pos& b) { a = d.anchor; b = d.cursor; if (b < a) std::swap(a, b); }
std::string SelectedText(const Doc& d) {
    Pos a, b; Ordered(d, a, b);
    if (a.line == b.line) return d.lines[a.line].substr(a.col, b.col - a.col);
    std::string out = d.lines[a.line].substr(a.col) + "\n";
    for (int l = a.line + 1; l < b.line; ++l) out += d.lines[l] + "\n";
    return out + d.lines[b.line].substr(0, b.col);
}
// Kinds group typing into one undo step: 1 typing, 2 deleting, 3 other
void PushUndo(Doc& d, int kind) {
    const auto now = std::chrono::steady_clock::now();
    if (kind != 3 && kind == d.lastEditKind && now - d.lastEdit < std::chrono::milliseconds(800)) { d.lastEdit = now; return; }
    d.undo.push_back({ d.lines, d.cursor });
    if (d.undo.size() > 300) d.undo.erase(d.undo.begin());
    d.redo.clear();
    d.lastEditKind = kind; d.lastEdit = now;
}
void DeleteSelection(Doc& d) {
    if (!HasSelection(d)) return;
    Pos a, b; Ordered(d, a, b);
    d.lines[a.line] = d.lines[a.line].substr(0, a.col) + d.lines[b.line].substr(b.col);
    d.lines.erase(d.lines.begin() + a.line + 1, d.lines.begin() + b.line + 1);
    d.cursor = d.anchor = a;
    d.dirty = true;
}
void Insert(Doc& d, const std::string& text) {
    DeleteSelection(d);
    std::string& line = d.lines[d.cursor.line];
    const std::string tail = line.substr(d.cursor.col);
    line.erase(d.cursor.col);
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        std::string part = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        part.erase(std::remove(part.begin(), part.end(), '\r'), part.end());
        d.lines[d.cursor.line] += part;
        if (nl == std::string::npos) break;
        d.lines.insert(d.lines.begin() + d.cursor.line + 1, std::string());
        ++d.cursor.line;
        start = nl + 1;
    }
    d.cursor.col = (int)d.lines[d.cursor.line].size();
    d.lines[d.cursor.line] += tail;
    d.anchor = d.cursor;
    d.dirty = true; d.scrollToCursor = true; d.preferredX = -1;
}
std::string Indentation(const std::string& line) { size_t i = 0; while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i; return line.substr(0, i); }

// ---- symbols
void LoadSymbols() {
    s_SymbolsTime = std::chrono::steady_clock::now();
    s_TypeNamespaces.clear(); s_Namespaces.clear();
    const std::string response = ScriptEngine::DrawEditorUI(R"({"kind":"symbols"})");
    auto j = json::parse(response, nullptr, false);
    if (!j.is_object() || !j.contains("types")) return;
    s_Types.clear();
    for (auto& t : j["types"]) {
        TypeInfo info;
        info.name = t.value("name", std::string()); info.ns = t.value("ns", std::string()); info.kind = t.value("kind", std::string()); info.base = t.value("base", std::string());
        auto members = [](const json& list, std::vector<Member>& out) { for (auto& m : list) out.push_back({ m.value("n", std::string()), m.value("k", std::string()), m.value("t", std::string()), m.value("s", std::string()) }); };
        members(t.value("static", json::array()), info.statics);
        members(t.value("instance", json::array()), info.instance);
        if (info.name.empty()) continue;
        if (!info.ns.empty()) {
            s_TypeNamespaces[info.name].push_back(info.ns);
            for (size_t dot = 0; dot != std::string::npos; dot = info.ns.find('.', dot + 1)) s_Namespaces.insert(info.ns.substr(0, info.ns.find('.', dot + 1)));
            s_Namespaces.insert(info.ns);
        }
        // Stardust types win over .NET ones with the same name
        if (!s_Types.count(info.name) || info.ns.rfind("Stardust", 0) == 0) s_Types[info.name] = std::move(info);
    }
    for (const char* ns : { "System", "System.Collections", "System.Collections.Generic", "System.Linq", "System.Text", "System.IO", "System.Threading.Tasks", "System.Text.Json", "System.Text.Json.Nodes" }) s_Namespaces.insert(ns);
    s_SymbolsLoaded = true;
}
std::string StripGeneric(std::string t) { auto lt = t.find('<'); if (lt != std::string::npos) t = t.substr(0, lt); if (t.ends_with("[]")) t.resize(t.size() - 2); return t; }
const TypeInfo* FindType(const std::string& name) { auto it = s_Types.find(StripGeneric(name)); return it == s_Types.end() ? nullptr : &it->second; }
const Member* FindMember(const std::string& type, const std::string& name, bool instance) {
    for (const TypeInfo* t = FindType(type); t; t = FindType(t->base)) {
        for (auto& m : instance ? t->instance : t->statics) if (m.name == name) return &m;
        if (t->base.empty() || t->base == t->name) break;
    }
    return nullptr;
}

// Type of a variable from its declaration in the file (`Rigidbody body`, `var x = new Foo(`), or of a member of the script
std::string TypeOfIdentifier(const Doc& d, const std::string& name, int beforeLine) {
    if (FindType(name)) return {};
    const std::regex declared("\\b([A-Z][\\w]*(?:<[^>]*>)?(?:\\[\\])?)\\s+" + name + "\\b\\s*[=;,)]");
    const std::regex inferred("\\bvar\\s+" + name + "\\s*=\\s*(?:new\\s+)?([A-Z]\\w*)");
    const std::regex component("\\b" + name + "\\s*=\\s*(?:\\w+\\.)?(?:GetComponent|AddComponent)<(\\w+)>");
    std::smatch m;
    for (int l = std::min(beforeLine, (int)d.lines.size() - 1); l >= 0; --l) {
        const std::string& line = d.lines[l];
        if (line.find(name) == std::string::npos) continue;
        if (std::regex_search(line, m, component)) return m[1].str();
        if (std::regex_search(line, m, declared) && m[1].str() != "return") return m[1].str();
        if (std::regex_search(line, m, inferred)) return m[1].str();
    }
    for (const char* base : { "ScriptBehaviour", "Component", "Entity" }) if (const Member* member = FindMember(base, name, true)) return member->type;
    return {};
}

std::set<std::string> ImportedNamespaces(const Doc& d) {
    std::set<std::string> out;
    static const std::regex usingLine(R"(^\s*using\s+([\w.]+)\s*;)");
    std::smatch m;
    for (const auto& line : d.lines) if (std::regex_search(line, m, usingLine)) out.insert(m[1].str());
    // The namespace of the file (and its parents) is visible too
    static const std::regex namespaceLine(R"(^\s*namespace\s+([\w.]+))");
    for (const auto& line : d.lines) if (std::regex_search(line, m, namespaceLine)) { std::string ns = m[1].str(); for (;;) { out.insert(ns); auto dot = ns.rfind('.'); if (dot == std::string::npos) break; ns.resize(dot); } }
    return out;
}
// Adds `using ns;` after the last using at the top of the file (keeps the cursor on its text)
void AddUsing(Doc& d, const std::string& ns) {
    if (ns.empty() || ImportedNamespaces(d).count(ns)) return;
    int insertAt = 0;
    static const std::regex usingLine(R"(^\s*using\s+[\w.]+\s*;)");
    for (int l = 0; l < (int)d.lines.size() && l < 200; ++l) {
        const std::string& t = d.lines[l];
        if (std::regex_search(t, usingLine)) insertAt = l + 1;
        else if (t.find_first_not_of(" \t") != std::string::npos && t.rfind("//", t.find_first_not_of(" \t")) != t.find_first_not_of(" \t") && insertAt > 0) break;
    }
    d.lines.insert(d.lines.begin() + insertAt, "using " + ns + ";");
    if (d.cursor.line >= insertAt) ++d.cursor.line;
    if (d.anchor.line >= insertAt) ++d.anchor.line;
    d.dirty = true;
}
// Fixes for "name/type not found": a type of that name in another namespace, or Namespace+Name written as one word (UIImage)
std::vector<Fix> FixesFor(const Doc& d, const Diagnostic& diag) {
    std::vector<Fix> fixes;
    if (diag.symbol.empty()) return fixes;
    const auto imported = ImportedNamespaces(d);
    auto offer = [&](const std::string& ns, const std::string& name) {
        Fix f; f.addUsing = imported.count(ns) ? std::string() : ns; f.line = diag.line; f.col = diag.col; f.symbol = diag.symbol;
        if (name != diag.symbol) f.replaceWith = name;
        if (f.addUsing.empty() && f.replaceWith.empty()) return;
        f.label = !f.replaceWith.empty() && !f.addUsing.empty() ? "Use " + ns + "." + name + " (add using " + ns + ")" : !f.replaceWith.empty() ? "Use " + name : "Add using " + ns + ";";
        for (auto& existing : fixes) if (existing.label == f.label) return;
        fixes.push_back(f);
    };
    if (auto it = s_TypeNamespaces.find(diag.symbol); it != s_TypeNamespaces.end()) for (auto& ns : it->second) offer(ns, diag.symbol);
    for (auto& [name, list] : s_TypeNamespaces) {
        for (auto& ns : list) {
            const std::string last = ns.substr(ns.rfind('.') == std::string::npos ? 0 : ns.rfind('.') + 1);
            if (diag.symbol == last + name) offer(ns, name);                       // UIImage -> UI.Image
            else if (name.size() == diag.symbol.size() && std::equal(name.begin(), name.end(), diag.symbol.begin(), [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }) && name != diag.symbol)
                offer(ns, name);                                                   // rigidbody -> Rigidbody
        }
    }
    return fixes;
}
void ApplyFix(Doc& d, const Fix& f) {
    PushUndo(d, 3);
    if (!f.replaceWith.empty() && f.line < (int)d.lines.size()) {
        std::string& t = d.lines[f.line];
        size_t at = t.find(f.symbol, std::max(0, f.col - 1));
        if (at == std::string::npos) at = t.find(f.symbol);
        if (at != std::string::npos) { t.replace(at, f.symbol.size(), f.replaceWith); d.dirty = true; }
    }
    AddUsing(d, f.addUsing);
}

void AddMembers(std::vector<Completion>& out, std::set<std::string>& seen, const std::string& type, bool instance, const std::string& prefix) {
    for (const TypeInfo* t = FindType(type); t; t = FindType(t->base)) {
        for (auto& m : instance ? t->instance : t->statics) {
            if (!seen.insert(m.name).second) continue;
            out.push_back({ m.name, m.signature, m.kind.empty() ? 'f' : m.kind[0] });
        }
        if (t->base.empty() || t->base == t->name || t->base == "Object" || t->base == "ValueType") break;
    }
    (void)prefix;
}

void UpdateCompletions(Doc& d, bool force) {
    const std::string& line = d.lines[d.cursor.line];
    int start = d.cursor.col;
    while (start > 0 && IsIdent((unsigned char)line[start - 1])) --start;
    const std::string prefix = line.substr(start, d.cursor.col - start);
    const bool member = start > 0 && line[start - 1] == '.';
    if (!force && prefix.empty() && !member) { s_CompletionOpen = false; return; }
    if (!force && !member && prefix.size() < 1) { s_CompletionOpen = false; return; }
    if (d.lang == Lang::CSharp && (!s_SymbolsLoaded || std::chrono::steady_clock::now() - s_SymbolsTime > std::chrono::seconds(30))) LoadSymbols();

    std::vector<Completion> all;
    std::set<std::string> seen;
    // using Stardust.  -> the namespaces under it
    static const std::regex usingStart(R"(^\s*using\s+([\w.]*)$)");
    std::smatch usingMatch;
    const std::string beforeCursor = line.substr(0, d.cursor.col);
    if (d.lang == Lang::CSharp && std::regex_match(beforeCursor, usingMatch, usingStart)) {
        std::string typed = usingMatch[1].str();
        const std::string parent = typed.rfind('.') == std::string::npos ? std::string() : typed.substr(0, typed.rfind('.') + 1);
        for (const auto& ns : s_Namespaces) {
            if (ns.size() <= parent.size() || ns.compare(0, parent.size(), parent) != 0) continue;
            std::string segment = ns.substr(parent.size());
            segment = segment.substr(0, segment.find('.'));
            if (seen.insert(segment).second) all.push_back({ segment, "namespace " + parent + segment, 'n' });
        }
    } else if (member && d.lang == Lang::CSharp) {
        int e = start - 1, b = e;
        while (b > 0 && IsIdent((unsigned char)line[b - 1])) --b;
        const std::string receiver = line.substr(b, e - b);
        if (const TypeInfo* t = FindType(receiver)) { AddMembers(all, seen, t->name, false, prefix); if (t->kind != "enum") {} }
        else {
            std::string type = receiver == "this" || receiver.empty() ? "ScriptBehaviour" : TypeOfIdentifier(d, receiver, d.cursor.line);
            if (!type.empty()) AddMembers(all, seen, type, true, prefix);
            else for (auto& [name, t] : s_Types) for (auto& m : t.instance) if (seen.insert(m.name).second) all.push_back({ m.name, m.signature, m.kind.empty() ? 'f' : m.kind[0] });
        }
    } else if (member) {
        for (const char* f : { "x", "y", "z", "w", "xy", "xyz", "rgb", "rgba", "r", "g", "b", "a", "Albedo", "Normal", "Emission", "Metallic", "Smoothness", "Occlusion", "Alpha", "worldPos", "worldNormal", "viewDir", "uv", "screenPos", "time" })
            if (seen.insert(f).second) all.push_back({ f, "", 'f' });
    } else {
        for (auto& k : Keywords(d.lang)) if (seen.insert(k).second) all.push_back({ k, "keyword", 'k' });
        if (d.lang == Lang::CSharp) {
            for (auto& [name, t] : s_Types) if (seen.insert(name).second) all.push_back({ name, t.kind + (t.ns.empty() ? "" : " in " + t.ns), 't', t.ns });
            AddMembers(all, seen, "ScriptBehaviour", true, prefix);
        }
        if (d.lang == Lang::Shader) for (auto& b : ShaderBuiltins()) if (seen.insert(b).second) all.push_back({ b, "", 'b' });
    }
    // Identifiers of the file
    if (!member || d.lang != Lang::CSharp) {
        static const std::regex word(R"(\b[A-Za-z_]\w{2,}\b)");
        for (int l = 0; l < (int)d.lines.size(); ++l) {
            if (std::abs(l - d.cursor.line) > 2000) continue;
            const std::string& text = d.lines[l];
            for (auto it = std::sregex_iterator(text.begin(), text.end(), word); it != std::sregex_iterator(); ++it) {
                const std::string w = it->str();
                if (l == d.cursor.line && it->position() <= d.cursor.col && it->position() + (long)w.size() >= d.cursor.col) continue; // the word being typed
                if (seen.insert(w).second) all.push_back({ w, "", 'w' });
            }
        }
    }
    auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    const std::string needle = lower(prefix);
    std::vector<std::pair<int, Completion>> ranked;
    for (auto& c : all) {
        const std::string l = lower(c.text);
        int score;
        if (needle.empty()) score = 2;
        else if (c.text.rfind(prefix, 0) == 0) score = 0;
        else if (l.rfind(needle, 0) == 0) score = 1;
        else if (l.find(needle) != std::string::npos) score = 3;
        else continue;
        if (c.text == prefix) continue;
        ranked.push_back({ score, c });
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first < b.first : a.second.text < b.second.text; });
    s_Completions.clear();
    for (size_t i = 0; i < ranked.size() && i < 60; ++i) s_Completions.push_back(ranked[i].second);
    s_CompletionOpen = !s_Completions.empty();
    s_CompletionIndex = 0;
    s_CompletionScroll = true;
    s_CompletionStart = { d.cursor.line, start };
}
void AcceptCompletion(Doc& d) {
    if (!s_CompletionOpen || s_Completions.empty()) return;
    PushUndo(d, 3);
    d.anchor = s_CompletionStart; d.anchor.line = d.cursor.line;
    const Completion picked = s_Completions[std::clamp(s_CompletionIndex, 0, (int)s_Completions.size() - 1)];
    Insert(d, picked.text);
    s_CompletionOpen = false;
    // A type from a namespace the file does not use yet brings its using along (unless an imported namespace has one too)
    if (picked.kind == 't' && !picked.ns.empty() && d.lang == Lang::CSharp) {
        const auto imported = ImportedNamespaces(d);
        bool visible = imported.count(picked.ns) > 0;
        if (auto it = s_TypeNamespaces.find(picked.text); it != s_TypeNamespaces.end()) for (auto& ns : it->second) if (imported.count(ns)) visible = true;
        if (!visible) AddUsing(d, picked.ns);
    }
}

// ---- syntax colours
struct Span { int begin, end; ImU32 color; };
const ImU32 kText = IM_COL32(212, 212, 212, 255), kKeyword = IM_COL32(86, 156, 214, 255), kControl = IM_COL32(197, 134, 192, 255), kType = IM_COL32(78, 201, 176, 255),
            kString = IM_COL32(206, 145, 120, 255), kNumber = IM_COL32(181, 206, 168, 255), kComment = IM_COL32(106, 153, 85, 255), kPre = IM_COL32(155, 155, 155, 255),
            kMethod = IM_COL32(220, 220, 170, 255), kProperty = IM_COL32(156, 220, 254, 255);
bool IsControl(const std::string& w) {
    static const std::unordered_set<std::string> c = { "if", "else", "for", "foreach", "while", "do", "switch", "case", "break", "continue", "return", "goto", "try", "catch", "finally", "throw", "yield", "await", "discard", "default" };
    return c.count(w) > 0;
}
// Colours one line; inComment carries a /* */ comment across lines
std::vector<Span> Highlight(const std::string& s, Lang lang, bool& inComment) {
    std::vector<Span> spans;
    const auto& keywords = Keywords(lang);
    int i = 0;
    const int n = (int)s.size();
    if (lang == Lang::Text) { spans.push_back({ 0, n, kText }); return spans; }
    while (i < n) {
        if (inComment) {
            size_t end = s.find("*/", i);
            int e = end == std::string::npos ? n : (int)end + 2;
            spans.push_back({ i, e, kComment });
            if (end != std::string::npos) inComment = false;
            i = e; continue;
        }
        const char c = s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { spans.push_back({ i, n, kComment }); break; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { inComment = true; continue; }
        if (c == '#') {
            int b = i; while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
            if (b == 0) { spans.push_back({ i, n, kPre }); break; }
        }
        // Interpolated strings: the parts inside { } are coloured as code ({{ and }} stay text)
        if ((c == '$' && i + 1 < n && (s[i + 1] == '"' || (s[i + 1] == '@' && i + 2 < n && s[i + 2] == '"'))) || (c == '@' && i + 2 < n && s[i + 1] == '$' && s[i + 2] == '"')) {
            int e = (int)s.find('"', i) + 1;
            spans.push_back({ i, e, kString });
            int textStart = e;
            while (e < n && s[e] != '"') {
                if (s[e] == '\\') { e += 2; continue; }
                if (s[e] == '{' && e + 1 < n && s[e + 1] == '{') { e += 2; continue; }
                if (s[e] == '{') {
                    if (e > textStart) spans.push_back({ textStart, e, kString });
                    spans.push_back({ e, e + 1, kControl });
                    int depth = 1, close = e + 1;
                    while (close < n && depth > 0) { if (s[close] == '{') ++depth; else if (s[close] == '}') --depth; if (depth > 0) ++close; }
                    bool innerComment = false;
                    const std::string inner = s.substr(e + 1, close - e - 1);
                    for (Span sp : Highlight(inner, lang, innerComment)) { sp.begin += e + 1; sp.end += e + 1; spans.push_back(sp); }
                    if (close < n) spans.push_back({ close, close + 1, kControl });
                    e = close + 1; textStart = e;
                    continue;
                }
                ++e;
            }
            e = std::min(n, e + 1);
            if (e > textStart) spans.push_back({ textStart, e, kString });
            i = e; continue;
        }
        if (c == '"' || c == '\'') {
            int e = i + 1;
            while (e < n && s[e] != c) { if (s[e] == '\\') ++e; ++e; }
            e = std::min(n, e + 1);
            spans.push_back({ i, e, kString }); i = e; continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            int e = i + 1;
            while (e < n && (std::isalnum((unsigned char)s[e]) || s[e] == '.')) ++e;
            spans.push_back({ i, e, kNumber }); i = e; continue;
        }
        if (IsIdent((unsigned char)c)) {
            int e = i;
            while (e < n && IsIdent((unsigned char)s[e])) ++e;
            const std::string w = s.substr(i, e - i);
            int after = e; while (after < n && s[after] == ' ') ++after;
            ImU32 color = kText;
            if (keywords.count(w)) color = IsControl(w) ? kControl : kKeyword;
            else if (after < n && s[after] == '(') color = kMethod;
            else if (FindType(w) || (lang == Lang::CSharp && std::isupper((unsigned char)w[0]) && i > 0 && s[i - 1] != '.')) color = kType;
            else if (i > 0 && s[i - 1] == '.') color = kProperty;
            spans.push_back({ i, e, color }); i = e; continue;
        }
        int e = i + 1;
        while (e < n && !IsIdent((unsigned char)s[e]) && s[e] != '"' && s[e] != '\'' && s[e] != '/' && s[e] != '#' && s[e] != '$' && s[e] != '@' && !std::isdigit((unsigned char)s[e])) ++e;
        spans.push_back({ i, e, kText }); i = e;
    }
    return spans;
}

void ToggleComment(Doc& d) {
    Pos a, b; Ordered(d, a, b);
    if (b.line > a.line && b.col == 0) --b.line;
    const std::string mark = d.lang == Lang::Text ? "#" : "//";
    bool allCommented = true;
    for (int l = a.line; l <= b.line; ++l) { const std::string& t = d.lines[l]; auto first = t.find_first_not_of(" \t"); if (first != std::string::npos && t.compare(first, mark.size(), mark) != 0) allCommented = false; }
    PushUndo(d, 3);
    for (int l = a.line; l <= b.line; ++l) {
        std::string& t = d.lines[l];
        auto first = t.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (allCommented) { t.erase(first, mark.size()); if (first < t.size() && t[first] == ' ') t.erase(first, 1); }
        else t.insert(first, mark + " ");
    }
    d.cursor.col = std::min(d.cursor.col, (int)d.lines[d.cursor.line].size());
    d.anchor.col = std::min(d.anchor.col, (int)d.lines[d.anchor.line].size());
    d.dirty = true;
}

void Indent(Doc& d, bool outdent) {
    Pos a, b; Ordered(d, a, b);
    PushUndo(d, 3);
    for (int l = a.line; l <= b.line; ++l) {
        std::string& t = d.lines[l];
        if (outdent) { int remove = 0; while (remove < 4 && remove < (int)t.size() && t[remove] == ' ') ++remove; if (remove == 0 && !t.empty() && t[0] == '\t') remove = 1; t.erase(0, remove); }
        else t.insert(0, "    ");
    }
    d.cursor.col = std::min(d.cursor.col + (outdent ? 0 : 4), (int)d.lines[d.cursor.line].size());
    d.anchor.col = std::min(d.anchor.col + (outdent ? 0 : 4), (int)d.lines[d.anchor.line].size());
    d.dirty = true;
}

int WordBoundary(const std::string& s, int i, int dir) {
    if (dir < 0) { i = PrevChar(s, i); while (i > 0 && s[i] == ' ') i = PrevChar(s, i); bool ident = IsIdent((unsigned char)s[i]); while (i > 0 && IsIdent((unsigned char)s[i - 1]) == ident && s[i - 1] != ' ') --i; return i; }
    const int n = (int)s.size(); if (i >= n) return n;
    bool ident = IsIdent((unsigned char)s[i]);
    while (i < n && IsIdent((unsigned char)s[i]) == ident && s[i] != ' ') ++i;
    while (i < n && s[i] == ' ') ++i;
    return i;
}

bool FindNext(Doc& d, const std::string& needle, bool backwards = false) {
    if (needle.empty()) return false;
    const int count = (int)d.lines.size();
    Pos from = backwards ? (d.anchor < d.cursor ? d.anchor : d.cursor) : (d.anchor < d.cursor ? d.cursor : d.anchor);
    for (int step = 0; step <= count; ++step) {
        const int l = ((backwards ? from.line - step : from.line + step) % count + count) % count;
        const std::string& t = d.lines[l];
        size_t at;
        if (!backwards) at = t.find(needle, step == 0 ? from.col : 0);
        else at = step == 0 ? (from.col > 0 ? t.rfind(needle, from.col - 1) : std::string::npos) : t.rfind(needle);
        if (at != std::string::npos) { d.anchor = { l, (int)at }; d.cursor = { l, (int)(at + needle.size()) }; d.scrollToCursor = true; return true; }
    }
    return false;
}

// ------------------------------------------------------------------------------------------------ diagnostics

std::string PathKey(const fs::path& p) { std::string k = FileSystem::Utf8(fs::absolute(p).lexically_normal()); std::transform(k.begin(), k.end(), k.begin(), ::tolower); std::replace(k.begin(), k.end(), '\\', '/'); return k; }

void RefreshDiagnostics() {
    const u64 generation = ScriptEngine::BuildDiagnosticsGeneration();
    if (generation == s_DiagnosticsGeneration) return;
    s_DiagnosticsGeneration = generation;
    s_Diagnostics.clear();
    // file(line,col): error CS0103: message [project]
    static const std::regex format(R"(^\s*(.*?)\((\d+),(\d+)\)\s*:\s*(error|warning)\s+(\w+)\s*:\s*(.*?)(\s*\[[^\]]*\])?\s*$)");
    std::smatch m;
    for (const auto& text : ScriptEngine::BuildDiagnostics()) {
        if (!std::regex_match(text, m, format)) continue;
        Diagnostic d;
        d.line = std::max(0, std::stoi(m[2].str()) - 1); d.col = std::max(0, std::stoi(m[3].str()) - 1);
        d.error = m[4].str() == "error"; d.code = m[5].str(); d.message = m[6].str();
        std::smatch q;
        if (std::regex_search(d.message, q, std::regex(R"('([A-Za-z_][\w.]*)')"))) d.symbol = q[1].str();
        s_Diagnostics.push_back({ fs::path(m[1].str()), d });
    }
}
std::vector<Diagnostic> DiagnosticsFor(const Doc& doc) {
    std::vector<Diagnostic> out;
    if (doc.lang == Lang::CSharp) {
        RefreshDiagnostics();
        if (!s_SymbolsLoaded && !s_Diagnostics.empty() && std::chrono::steady_clock::now() - s_SymbolsTime > std::chrono::seconds(5)) LoadSymbols(); // fixes need the type list
        const std::string key = PathKey(doc.path);
        for (auto& [path, d] : s_Diagnostics) if (PathKey(path) == key) out.push_back(d);
    } else if (ShaderAssets::IsShaderAsset(doc.path)) {
        // "file:12:5: error: message"
        const std::string error = ShaderAssets::LastError(doc.path);
        std::smatch m;
        if (std::regex_search(error, m, std::regex(R"(:(\d+):(\d+):\s*(error|warning):\s*(.*)$)"))) {
            Diagnostic d; d.line = std::max(0, std::stoi(m[1].str()) - 1); d.col = std::max(0, std::stoi(m[2].str()) - 1); d.error = m[3].str() == "error"; d.message = m[4].str();
            out.push_back(d);
        } else if (!error.empty()) { Diagnostic d; d.message = error; out.push_back(d); }
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.line != b.line ? a.line < b.line : a.error > b.error; });
    return out;
}

void DrawWave(ImDrawList* draw, float x0, float x1, float y, ImU32 color) {
    std::vector<ImVec2> points;
    for (float x = x0; x <= x1 + 0.01f; x += 2.0f) points.push_back({ x, y + ((int)((x - x0) / 2.0f) % 2 ? 1.5f : -1.0f) });
    if (points.size() >= 2) draw->AddPolyline(points.data(), (int)points.size(), color, 0, 1.0f);
}

void UndoStep(Doc& d, bool redo) {
    auto& from = redo ? d.redo : d.undo;
    auto& to = redo ? d.undo : d.redo;
    if (from.empty()) return;
    to.push_back({ d.lines, d.cursor });
    d.lines = from.back().lines; d.cursor = d.anchor = from.back().cursor; from.pop_back();
    d.cursor.line = std::min(d.cursor.line, (int)d.lines.size() - 1); d.cursor.col = std::min(d.cursor.col, (int)d.lines[d.cursor.line].size()); d.anchor = d.cursor;
    d.dirty = true; d.lastEditKind = 0; d.scrollToCursor = true; s_CompletionOpen = false;
}

// ------------------------------------------------------------------------------------------------ editor view

void HandleKeys(Doc& d, float lineHeight, float visibleLines) {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    auto move = [&](Pos p, bool keepPreferred = false) { d.cursor = p; if (!shift) d.anchor = p; d.scrollToCursor = true; if (!keepPreferred) d.preferredX = -1; };
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    std::string& line = d.lines[d.cursor.line];

    // Completion popup keys first
    if (s_CompletionOpen) {
        if (pressed(ImGuiKey_DownArrow)) { s_CompletionIndex = std::min(s_CompletionIndex + 1, (int)s_Completions.size() - 1); s_CompletionScroll = true; return; }
        if (pressed(ImGuiKey_UpArrow)) { s_CompletionIndex = std::max(s_CompletionIndex - 1, 0); s_CompletionScroll = true; return; }
        if (pressed(ImGuiKey_PageDown)) { s_CompletionIndex = std::min(s_CompletionIndex + 8, (int)s_Completions.size() - 1); s_CompletionScroll = true; return; }
        if (pressed(ImGuiKey_PageUp)) { s_CompletionIndex = std::max(s_CompletionIndex - 8, 0); s_CompletionScroll = true; return; }
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || pressed(ImGuiKey_Tab)) { AcceptCompletion(d); return; }
        if (pressed(ImGuiKey_Escape)) { s_CompletionOpen = false; return; }
    }

    if (ctrl && pressed(ImGuiKey_S)) { Save(d); return; }
    if (ctrl && pressed(ImGuiKey_F)) { s_FindOpen = true; s_FindFocus = true; if (HasSelection(d) && d.anchor.line == d.cursor.line) s_Find = SelectedText(d); return; }
    if (pressed(ImGuiKey_F3)) { FindNext(d, s_Find, shift); return; }
    if (ctrl && pressed(ImGuiKey_Z)) { UndoStep(d, shift); return; }
    if (ctrl && pressed(ImGuiKey_Y)) { UndoStep(d, true); return; }
    if (ctrl && (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd))) { s_Zoom = std::min(3.0f, s_Zoom + 0.1f); return; }
    if (ctrl && (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract))) { s_Zoom = std::max(0.5f, s_Zoom - 0.1f); return; }
    if (ctrl && (pressed(ImGuiKey_0) || pressed(ImGuiKey_Keypad0))) { s_Zoom = 1.0f; return; }
    if (ctrl && pressed(ImGuiKey_A)) { d.anchor = { 0, 0 }; d.cursor = { (int)d.lines.size() - 1, (int)d.lines.back().size() }; return; }
    if (ctrl && (pressed(ImGuiKey_C) || pressed(ImGuiKey_X))) {
        const bool cut = ImGui::IsKeyPressed(ImGuiKey_X);
        if (HasSelection(d)) { ImGui::SetClipboardText(SelectedText(d).c_str()); if (cut) { PushUndo(d, 3); DeleteSelection(d); } }
        else { ImGui::SetClipboardText((line + "\n").c_str()); if (cut) { PushUndo(d, 3); if (d.lines.size() > 1) d.lines.erase(d.lines.begin() + d.cursor.line); else d.lines[0].clear(); d.cursor.line = std::min(d.cursor.line, (int)d.lines.size() - 1); d.cursor.col = 0; d.anchor = d.cursor; d.dirty = true; } }
        return;
    }
    if (ctrl && pressed(ImGuiKey_V)) { if (const char* clip = ImGui::GetClipboardText()) { PushUndo(d, 3); Insert(d, clip); } return; }
    if (ctrl && pressed(ImGuiKey_D)) { PushUndo(d, 3); d.lines.insert(d.lines.begin() + d.cursor.line + 1, line); ++d.cursor.line; d.anchor = d.cursor; d.dirty = true; return; }
    if (ctrl && (pressed(ImGuiKey_Slash) || pressed(ImGuiKey_KeypadDivide))) { ToggleComment(d); return; }
    if (ctrl && pressed(ImGuiKey_Space)) { UpdateCompletions(d, true); return; }

    // Movement
    if (pressed(ImGuiKey_LeftArrow)) {
        if (!shift && HasSelection(d)) { Pos a, b; Ordered(d, a, b); move(a); }
        else if (d.cursor.col > 0) move({ d.cursor.line, ctrl ? WordBoundary(line, d.cursor.col, -1) : PrevChar(line, d.cursor.col) });
        else if (d.cursor.line > 0) move({ d.cursor.line - 1, (int)d.lines[d.cursor.line - 1].size() });
        s_CompletionOpen = false; return;
    }
    if (pressed(ImGuiKey_RightArrow)) {
        if (!shift && HasSelection(d)) { Pos a, b; Ordered(d, a, b); move(b); }
        else if (d.cursor.col < (int)line.size()) move({ d.cursor.line, ctrl ? WordBoundary(line, d.cursor.col, 1) : NextChar(line, d.cursor.col) });
        else if (d.cursor.line + 1 < (int)d.lines.size()) move({ d.cursor.line + 1, 0 });
        s_CompletionOpen = false; return;
    }
    auto vertical = [&](int delta) {
        if (d.preferredX < 0) d.preferredX = (float)VisualCol(line, d.cursor.col);
        const int target = std::clamp(d.cursor.line + delta, 0, (int)d.lines.size() - 1);
        move({ target, ByteAtVisual(d.lines[target], d.preferredX) }, true);
    };
    if (pressed(ImGuiKey_UpArrow)) { if (ctrl) ImGui::SetScrollY(ImGui::GetScrollY() - lineHeight); else vertical(-1); return; }
    if (pressed(ImGuiKey_DownArrow)) { if (ctrl) ImGui::SetScrollY(ImGui::GetScrollY() + lineHeight); else vertical(1); return; }
    if (pressed(ImGuiKey_PageUp)) { vertical(-(int)visibleLines); return; }
    if (pressed(ImGuiKey_PageDown)) { vertical((int)visibleLines); return; }
    if (pressed(ImGuiKey_Home)) {
        if (ctrl) move({ 0, 0 });
        else { const int first = (int)Indentation(line).size(); move({ d.cursor.line, d.cursor.col == first ? 0 : first }); }
        return;
    }
    if (pressed(ImGuiKey_End)) { if (ctrl) move({ (int)d.lines.size() - 1, (int)d.lines.back().size() }); else move({ d.cursor.line, (int)line.size() }); return; }

    // Editing
    if (pressed(ImGuiKey_Tab)) { if (HasSelection(d) && d.anchor.line != d.cursor.line) Indent(d, shift); else if (shift) Indent(d, true); else { PushUndo(d, 1); Insert(d, "    "); } return; }
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
        PushUndo(d, 3);
        std::string indent = Indentation(line);
        const bool open = d.cursor.col > 0 && line[d.cursor.col - 1] == '{';
        const bool closes = d.cursor.col < (int)line.size() && line[d.cursor.col] == '}';
        if (open) indent += "    ";
        Insert(d, "\n" + indent);
        if (open && closes) { const Pos keep = d.cursor; std::string back = indent.size() >= 4 ? indent.substr(4) : ""; Insert(d, "\n" + back); d.cursor = d.anchor = keep; }
        s_CompletionOpen = false;
        return;
    }
    if (pressed(ImGuiKey_Backspace)) {
        PushUndo(d, 2);
        if (HasSelection(d)) DeleteSelection(d);
        else if (d.cursor.col > 0) {
            // Whitespace up to the indentation goes four columns at a time
            int from = PrevChar(line, d.cursor.col);
            if (ctrl) from = WordBoundary(line, d.cursor.col, -1);
            else if (d.cursor.col == (int)Indentation(line).size() && d.cursor.col >= 4 && line.compare(d.cursor.col - 4, 4, "    ") == 0) from = d.cursor.col - 4;
            line.erase(from, d.cursor.col - from); d.cursor.col = from;
        } else if (d.cursor.line > 0) {
            const int col = (int)d.lines[d.cursor.line - 1].size();
            d.lines[d.cursor.line - 1] += line; d.lines.erase(d.lines.begin() + d.cursor.line);
            --d.cursor.line; d.cursor.col = col;
        }
        d.anchor = d.cursor; d.dirty = true; d.scrollToCursor = true;
        if (s_CompletionOpen) UpdateCompletions(d, false);
        return;
    }
    if (pressed(ImGuiKey_Delete)) {
        PushUndo(d, 2);
        if (HasSelection(d)) DeleteSelection(d);
        else if (d.cursor.col < (int)line.size()) { const int to = ctrl ? WordBoundary(line, d.cursor.col, 1) : NextChar(line, d.cursor.col); line.erase(d.cursor.col, to - d.cursor.col); }
        else if (d.cursor.line + 1 < (int)d.lines.size()) { line += d.lines[d.cursor.line + 1]; d.lines.erase(d.lines.begin() + d.cursor.line + 1); }
        d.anchor = d.cursor; d.dirty = true;
        return;
    }
    if (pressed(ImGuiKey_Escape)) { d.anchor = d.cursor; s_FindOpen = false; return; }

    // Typed characters
    if (!ctrl || io.KeyAlt) {
        std::string typed;
        for (ImWchar c : io.InputQueueCharacters) {
            if (c < 32 && c != '\t') continue;
            char buf[5] = {};
            ImTextCharToUtf8(buf, c);
            typed += buf;
        }
        if (!typed.empty()) {
            PushUndo(d, 1);
            // Closing brackets and quotes typed over the one already there
            if (typed.size() == 1 && !HasSelection(d) && d.cursor.col < (int)line.size() && line[d.cursor.col] == typed[0] && (typed[0] == ')' || typed[0] == ']' || typed[0] == '}' || typed[0] == '"')) {
                ++d.cursor.col; d.anchor = d.cursor;
            } else {
                Insert(d, typed);
                const char c = typed.back();
                std::string& now = d.lines[d.cursor.line];
                const bool atEnd = d.cursor.col >= (int)now.size() || now[d.cursor.col] == ' ' || now[d.cursor.col] == ')' || now[d.cursor.col] == ';';
                const char close = c == '(' ? ')' : c == '[' ? ']' : c == '"' && d.lang != Lang::Text ? '"' : 0;
                if (close && atEnd && typed.size() == 1) { now.insert(d.cursor.col, 1, close); }
                // An outdent for a closing brace typed on an indented empty line
                if (c == '}' && typed.size() == 1) {
                    const std::string before = now.substr(0, d.cursor.col - 1);
                    if (before.find_first_not_of(" \t") == std::string::npos && before.size() >= 4) { now.erase(0, 4); d.cursor.col -= 4; d.anchor = d.cursor; }
                }
            }
            io.InputQueueCharacters.resize(0);
            const char last = typed.back();
            if (IsIdent((unsigned char)last) || last == '.') UpdateCompletions(d, false);
            else s_CompletionOpen = false;
        }
    }
}

void DrawDocument(Doc& d, const std::vector<Diagnostic>& diags, float bottom) {
    ImFont* code = Application::CodeFont();
    const float baseScale = code ? code->Scale : 1.0f;
    if (code) { code->Scale = baseScale * s_Zoom; ImGui::PushFont(code); }
    const float lineH = std::round(ImGui::GetTextLineHeight() + 3), charW = std::max(1.0f, std::round(ImGui::CalcTextSize("M").x));
    const float gutter = charW * (std::max(3, (int)std::to_string(d.lines.size()).size()) + 2);
    int longest = 0;
    for (auto& l : d.lines) longest = std::max(longest, (int)l.size());

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 30, 255));
    ImGui::BeginChild("##text", { 0, -bottom }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoMove | (ImGui::GetIO().KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0));
    ImGui::PopStyleColor();
    if (s_Refocus) { ImGui::SetWindowFocus(); s_Refocus = false; }
    const bool focused = ImGui::IsWindowFocused();
    ImGuiIO& io = ImGui::GetIO();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin(std::floor(ImGui::GetCursorScreenPos().x), std::floor(ImGui::GetCursorScreenPos().y));
    const float scrollY = ImGui::GetScrollY(), viewH = ImGui::GetWindowHeight();
    ImGui::Dummy({ gutter + charW * (longest * 1.1f + 10), lineH * (d.lines.size() + 3) });
    if (focused) ImGui::GetCurrentContext()->WantTextInputNextFrame = 1; // editor shortcuts stay quiet while typing

    if (d.gotoLine >= 0) {
        d.cursor = d.anchor = { std::clamp(d.gotoLine, 0, (int)d.lines.size() - 1), 0 };
        ImGui::SetScrollY(std::max(0.0f, d.cursor.line * lineH - viewH * 0.4f));
        d.gotoLine = -1; d.scrollToCursor = false;
    }

    // Mouse
    auto posAt = [&](ImVec2 m) {
        const int l = std::clamp((int)((m.y - origin.y) / lineH), 0, (int)d.lines.size() - 1);
        return Pos{ l, ByteAtVisual(d.lines[l], (m.x - origin.x - gutter) / charW) };
    };
    const bool hovered = ImGui::IsWindowHovered();
    if (hovered && io.KeyCtrl && io.MouseWheel != 0) s_Zoom = std::clamp(s_Zoom + (io.MouseWheel > 0 ? 0.1f : -0.1f), 0.5f, 3.0f);
    // The text area without the scroll bars: presses on a scroll bar scroll, they never select
    const ImRect textArea = ImGui::GetCurrentWindow()->InnerRect;
    const bool overText = hovered && textArea.Contains(io.MousePos) && !ImGui::IsAnyItemActive();
    static bool s_TextDrag = false; // the left button went down on the text
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_TextDrag = false;
    if (overText) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (overText && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s_TextDrag = true;
        ImGui::SetWindowFocus();
        const Pos p = posAt(io.MousePos);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const std::string& t = d.lines[p.line];
            int a = p.col, b = p.col;
            while (a > 0 && IsIdent((unsigned char)t[a - 1])) --a;
            while (b < (int)t.size() && IsIdent((unsigned char)t[b])) ++b;
            d.anchor = { p.line, a }; d.cursor = { p.line, b };
        } else { d.cursor = p; if (!io.KeyShift) d.anchor = p; }
        d.preferredX = -1; s_CompletionOpen = false;
    } else if (s_TextDrag && focused && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (ImGui::IsMouseHoveringRect(ImGui::GetWindowPos(), { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + viewH }) || true) {
            d.cursor = posAt(io.MousePos);
            if (io.MousePos.y > ImGui::GetWindowPos().y + viewH - lineH) ImGui::SetScrollY(scrollY + lineH);
            if (io.MousePos.y < ImGui::GetWindowPos().y + lineH) ImGui::SetScrollY(scrollY - lineH);
        }
    }
    if (focused) HandleKeys(d, lineH, viewH / lineH);

    // Keep the cursor in view
    if (d.scrollToCursor) {
        const float cy = d.cursor.line * lineH, cx = gutter + VisualCol(d.lines[d.cursor.line], d.cursor.col) * charW;
        if (cy < scrollY) ImGui::SetScrollY(cy);
        else if (cy + lineH * 2 > scrollY + viewH) ImGui::SetScrollY(cy + lineH * 2 - viewH);
        const float scrollX = ImGui::GetScrollX(), viewW = ImGui::GetWindowWidth();
        if (cx < scrollX + gutter) ImGui::SetScrollX(std::max(0.0f, cx - gutter - charW * 4));
        else if (cx > scrollX + viewW - charW * 4) ImGui::SetScrollX(cx - viewW + charW * 8);
        d.scrollToCursor = false;
    }

    // Visible lines
    const int first = std::max(0, (int)(scrollY / lineH) - 1), last = std::min((int)d.lines.size() - 1, (int)((scrollY + viewH) / lineH) + 1);
    bool inComment = false;
    for (int l = 0; l < first && l < (int)d.lines.size(); ++l) Highlight(d.lines[l], d.lang, inComment); // carry /* */ state
    Pos selA, selB; Ordered(d, selA, selB);
    const float scrollX = ImGui::GetScrollX();
    const float gutterX = ImGui::GetWindowPos().x;
    for (int l = first; l <= last; ++l) {
        const float y = origin.y + l * lineH;
        const std::string& t = d.lines[l];
        if (l == d.cursor.line && !HasSelection(d)) draw->AddRectFilled({ gutterX, y }, { gutterX + ImGui::GetWindowWidth(), y + lineH }, IM_COL32(40, 40, 44, 255));
        // Selection
        if (HasSelection(d) && l >= selA.line && l <= selB.line) {
            const int a = l == selA.line ? VisualCol(t, selA.col) : 0, b = l == selB.line ? VisualCol(t, selB.col) : VisualCol(t, (int)t.size()) + 1;
            draw->AddRectFilled({ origin.x + gutter + a * charW, y }, { origin.x + gutter + b * charW, y + lineH }, IM_COL32(38, 79, 120, 255));
        }
        for (const Span& sp : Highlight(t, d.lang, inComment)) {
            if (sp.end <= sp.begin) continue;
            const float x = origin.x + gutter + VisualCol(t, sp.begin) * charW;
            // One glyph per column cell (tabs are empty cells), so nothing drifts from the cursor
            int column = VisualCol(t, sp.begin);
            for (int i = sp.begin; i < sp.end;) {
                const int next = NextChar(t, i);
                if (t[i] == '\t') column = (column / 4 + 1) * 4;
                else {
                    if (t[i] != ' ') draw->AddText({ origin.x + gutter + column * charW, y + 1 }, sp.color, t.c_str() + i, t.c_str() + next);
                    ++column;
                }
                i = next;
            }
            (void)x;
        }
        // Compiler messages: a wavy line under the word, the message on hover
        for (const auto& diag : diags) {
            if (diag.line != l) continue;
            int a = std::min(diag.col, (int)t.size()), b = a;
            while (b < (int)t.size() && IsIdent((unsigned char)t[b])) ++b;
            if (b == a) { b = std::min((int)t.size(), a + 1); if (a == b && a > 0) --a; }
            const float x0 = origin.x + gutter + VisualCol(t, a) * charW, x1 = std::max(x0 + charW, origin.x + gutter + VisualCol(t, b) * charW);
            const ImU32 color = diag.error ? IM_COL32(240, 80, 70, 255) : IM_COL32(230, 190, 70, 255);
            DrawWave(draw, x0, x1, y + lineH - 2, color);
            if (ImGui::IsMouseHoveringRect({ x0, y }, { x1, y + lineH }) && hovered) {
                ImGui::BeginTooltip();
                if (code) ImGui::PushFont(ImGui::GetIO().FontDefault);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s %s", diag.error ? "Error" : "Warning", diag.code.c_str());
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
                ImGui::TextUnformatted(diag.message.c_str());
                ImGui::PopTextWrapPos();
                if (!FixesFor(d, diag).empty()) ImGui::TextDisabled("Ctrl+. or the light bulb: quick fixes");
                if (code) ImGui::PopFont();
                ImGui::EndTooltip();
            }
        }
        // Cursor
        if (l == d.cursor.line && focused && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
            const float cx = origin.x + gutter + VisualCol(t, d.cursor.col) * charW;
            draw->AddLine({ cx, y + 1 }, { cx, y + lineH - 1 }, IM_COL32(230, 230, 230, 255), 1.5f);
        }
    }
    // Gutter (fixed while scrolling sideways)
    draw->AddRectFilled({ gutterX, ImGui::GetWindowPos().y }, { gutterX + gutter - charW * 0.5f, ImGui::GetWindowPos().y + viewH }, IM_COL32(30, 30, 30, 255));
    for (int l = first; l <= last; ++l) {
        const std::string number = std::to_string(l + 1);
        const float y = origin.y + l * lineH;
        draw->AddText({ gutterX + gutter - charW * (number.size() + 1.5f), y + 1 }, l == d.cursor.line ? IM_COL32(200, 200, 200, 255) : IM_COL32(110, 110, 115, 255), number.c_str());
        bool error = false, warning = false;
        for (const auto& diag : diags) if (diag.line == l) { if (diag.error) error = true; else warning = true; }
        if (error || warning) draw->AddCircleFilled({ gutterX + charW * 0.6f, y + lineH * 0.5f }, charW * 0.32f, error ? IM_COL32(240, 80, 70, 255) : IM_COL32(230, 190, 70, 255));
    }
    // Light bulb on the cursor line when a fix exists; Ctrl+. opens the same list
    {
        std::vector<Fix> fixes;
        for (const auto& diag : diags) if (diag.line == d.cursor.line) for (auto& f : FixesFor(d, diag)) fixes.push_back(f);
        if (!fixes.empty() && d.cursor.line >= first && d.cursor.line <= last) {
            const float y = origin.y + d.cursor.line * lineH;
            const ImVec2 c(origin.x + gutter + (VisualCol(d.lines[d.cursor.line], (int)d.lines[d.cursor.line].size()) + 2) * charW, y + lineH * 0.5f);
            draw->AddCircleFilled(c, lineH * 0.32f, IM_COL32(240, 200, 60, 255));
            draw->AddRectFilled({ c.x - lineH * 0.12f, c.y + lineH * 0.22f }, { c.x + lineH * 0.12f, c.y + lineH * 0.4f }, IM_COL32(200, 200, 200, 255));
            if (hovered && ImGui::IsMouseHoveringRect({ c.x - lineH * 0.4f, y }, { c.x + lineH * 0.4f, y + lineH })) {
                ImGui::SetTooltip("Quick fixes (Ctrl+.)");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s_FixMenu = true;
            }
            if (focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Period)) s_FixMenu = true;
            if (s_FixMenu) { ImGui::OpenPopup("##fixes"); s_FixMenu = false; ImGui::SetNextWindowPos({ c.x, c.y + lineH * 0.6f }); }
        }
        if (ImGui::BeginPopup("##fixes")) {
            if (code) ImGui::PushFont(ImGui::GetIO().FontDefault);
            for (size_t i = 0; i < fixes.size(); ++i) if (ImGui::MenuItem(fixes[i].label.c_str())) { ApplyFix(d, fixes[i]); s_Refocus = true; }
            if (fixes.empty()) ImGui::TextDisabled("No fixes here");
            if (code) ImGui::PopFont();
            ImGui::EndPopup();
        }
    }
    (void)scrollX;

    // Completion list under the cursor
    static bool completionHovered = false;
    if (s_CompletionOpen && (focused || completionHovered)) {
        const std::string& t = d.lines[d.cursor.line];
        const ImVec2 at(origin.x + gutter + VisualCol(t, s_CompletionStart.col) * charW, origin.y + (d.cursor.line + 1) * lineH);
        ImGui::SetNextWindowPos(at);
        ImGui::SetNextWindowSizeConstraints({ 260, 0 }, { 520, lineH * 12 + 40 });
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(37, 37, 38, 250));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 4, 4 });
        if (ImGui::Begin("##completion", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking)) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            completionHovered = ImGui::IsWindowHovered();
            for (int i = 0; i < (int)s_Completions.size(); ++i) {
                const auto& c = s_Completions[i];
                const ImU32 kindColor = c.kind == 'm' ? IM_COL32(180, 140, 230, 255) : c.kind == 'p' || c.kind == 'f' ? IM_COL32(120, 180, 240, 255) : c.kind == 't' ? kType : c.kind == 'k' ? kKeyword : c.kind == 'e' ? IM_COL32(230, 190, 90, 255) : IM_COL32(160, 160, 160, 255);
                ImGui::PushID(i);
                const bool selected = i == s_CompletionIndex;
                // Picked on press, before the click moves the focus away from the text
                if (ImGui::Selectable("##item", selected, ImGuiSelectableFlags_SelectOnClick, { 0, 0 })) { s_CompletionIndex = i; AcceptCompletion(d); s_Refocus = true; ImGui::PopID(); break; }
                if (selected && s_CompletionScroll) { ImGui::SetScrollHereY(); s_CompletionScroll = false; }
                ImGui::SameLine(0, 0);
                const char* icon = c.kind == 'n' ? "N" : c.kind == 'm' ? "M" : c.kind == 'p' ? "P" : c.kind == 'f' ? "F" : c.kind == 't' ? "T" : c.kind == 'k' ? "K" : c.kind == 'e' ? "E" : c.kind == 'b' ? "B" : "~";
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kindColor), "%s", icon);
                ImGui::SameLine();
                ImGui::TextUnformatted(c.text.c_str());
                ImGui::PopID();
            }
            if (s_CompletionIndex < (int)s_Completions.size() && !s_Completions[s_CompletionIndex].detail.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", s_Completions[s_CompletionIndex].detail.c_str());
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    if (code) { ImGui::PopFont(); code->Scale = baseScale; }
}

} // namespace

void Open(const fs::path& file, int line) {
    if (file.empty()) { s_Open = true; return; }
    const fs::path absolute = fs::absolute(file).lexically_normal();
    for (int i = 0; i < (int)s_Docs.size(); ++i)
        if (fs::equivalent(s_Docs[i].path, absolute)) { s_RequestTab = i; s_Open = true; if (line > 0) s_Docs[i].gotoLine = line - 1; return; }
    Doc d;
    d.path = absolute;
    d.lang = LangOf(absolute);
    Load(d);
    if (line > 0) d.gotoLine = line - 1;
    s_Docs.push_back(std::move(d));
    s_RequestTab = (int)s_Docs.size() - 1;
    s_Open = true;
}

bool IsOpen() { return s_Open; }

bool HasUnsavedChanges(const fs::path& file) {
    for (auto& d : s_Docs) { std::error_code ec; if (fs::equivalent(d.path, file, ec)) return d.dirty; }
    return false;
}

void Draw() {
    if (!s_Open) return;
    const float ui = ImGui::GetFontSize() / 14.0f;
    ImGui::SetNextWindowSize({ 900 * ui, 640 * ui }, ImGuiCond_FirstUseEver);
    bool open = true;
    if (s_RequestTab >= 0) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Script Editor", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); if (!open) s_Open = false; return; }
    if (!open) {
        bool anyDirty = false; for (auto& d : s_Docs) anyDirty |= d.dirty;
        if (!anyDirty) { s_Open = false; s_Docs.clear(); s_Active = -1; }
        else s_ConfirmClose = -2;
    }
    // Files changed on disk (another editor, a generator) reload when they have no unsaved edits here
    static auto lastCheck = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - lastCheck > std::chrono::milliseconds(700)) {
        lastCheck = std::chrono::steady_clock::now();
        for (auto& d : s_Docs) { std::error_code ec; auto t = fs::last_write_time(d.path, ec); if (!ec && t != d.stamp && !d.dirty) Load(d); }
    }

    int closeTab = -1;
    // Top bar: flat buttons on a band across the window, status and zoom on the right
    {
        Doc* d = Active();
        const ImVec2 bandMin = ImGui::GetCursorScreenPos();
        const float bandH = ImGui::GetFrameHeight() + 8 * ui;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({ ImGui::GetWindowPos().x, bandMin.y - 2 * ui }, { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), bandMin.y + bandH }, IM_COL32(36, 38, 43, 255));
        draw->AddLine({ ImGui::GetWindowPos().x, bandMin.y + bandH }, { ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), bandMin.y + bandH }, IM_COL32(18, 19, 22, 255));
        ImGui::SetCursorScreenPos({ bandMin.x, bandMin.y + 4 * ui });
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(58, 62, 70, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(44, 93, 160, 255));
        auto tool = [&](const char* label, const char* tip, bool enabled = true, bool active = false) {
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(52, 56, 64, 255));
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::Button(label);
            ImGui::EndDisabled();
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tip) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine(0, 2 * ui);
            return clicked;
        };
        auto separator = [&]() {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            draw->AddLine({ p.x + 4 * ui, p.y + 3 * ui }, { p.x + 4 * ui, p.y + ImGui::GetFrameHeight() - 3 * ui }, IM_COL32(70, 74, 82, 255));
            ImGui::Dummy({ 9 * ui, ImGui::GetFrameHeight() }); ImGui::SameLine(0, 2 * ui);
        };
        if (tool(d && d->dirty ? "Save *" : "Save", "Save (Ctrl+S)", d != nullptr) && d) Save(*d);
        if (tool("Save All", "Save every open file", std::any_of(s_Docs.begin(), s_Docs.end(), [](auto& x) { return x.dirty; }))) for (auto& x : s_Docs) if (x.dirty) Save(x);
        separator();
        if (tool("Undo", "Undo (Ctrl+Z)", d && !d->undo.empty()) && d) UndoStep(*d, false);
        if (tool("Redo", "Redo (Ctrl+Y)", d && !d->redo.empty()) && d) UndoStep(*d, true);
        separator();
        if (tool("Find", "Find (Ctrl+F, F3 next)", d != nullptr, s_FindOpen)) { s_FindOpen = !s_FindOpen; s_FindFocus = s_FindOpen; }
        if (tool("Comment", "Comment or uncomment the selected lines (Ctrl+/)", d != nullptr) && d) { ToggleComment(*d); s_Refocus = true; }
        if (tool("Suggest", "Show suggestions (Ctrl+Space)", d != nullptr && d->lang != Lang::Text) && d) { UpdateCompletions(*d, true); s_Refocus = true; }
        separator();
        if (tool("Open Externally", "Open the file in the system editor", d != nullptr) && d) {
            if (d->dirty) Save(*d);
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", d->path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        }
        // Right side: shader status, cursor position, zoom
        const std::string zoomText = std::format("{}%", (int)std::lround(s_Zoom * 100));
        const std::string position = d ? std::format("Ln {}, Col {}", d->cursor.line + 1, VisualCol(d->lines[d->cursor.line], d->cursor.col) + 1) : std::string();
        const float zoomW = ImGui::CalcTextSize("-").x + ImGui::CalcTextSize("+").x + ImGui::CalcTextSize("000%").x + ImGui::GetStyle().FramePadding.x * 6 + 8 * ui;
        const float rightW = zoomW + ImGui::CalcTextSize(position.c_str()).x + 24 * ui;
        if (d && ShaderAssets::IsShaderAsset(d->path)) {
            const std::string error = ShaderAssets::LastError(d->path);
            ImGui::AlignTextToFramePadding();
            if (ShaderAssets::IsCompiling()) ImGui::TextColored({ 1, 0.85f, 0.3f, 1 }, "Compiling...");
            else if (!error.empty()) ImGui::TextColored({ 1, 0.45f, 0.4f, 1 }, "Shader error (see Problems)");
            else ImGui::TextDisabled("Compiled");
            ImGui::SameLine();
        }
        ImGui::SetCursorScreenPos({ bandMin.x + ImGui::GetContentRegionAvail().x + (ImGui::GetCursorScreenPos().x - bandMin.x) - rightW, bandMin.y + 4 * ui });
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", position.c_str());
        ImGui::SameLine(0, 14 * ui);
        if (tool("-", "Smaller text (Ctrl+-, Ctrl+wheel)")) s_Zoom = std::max(0.5f, s_Zoom - 0.1f);
        if (tool(zoomText.c_str(), "Reset the zoom (Ctrl+0)")) s_Zoom = 1.0f;
        if (tool("+", "Larger text (Ctrl+=, Ctrl+wheel)")) s_Zoom = std::min(3.0f, s_Zoom + 0.1f);
        ImGui::PopStyleColor(3);
        ImGui::SetCursorScreenPos({ bandMin.x, bandMin.y + bandH + 4 * ui });
        ImGui::Dummy({ 0, 0 });
    }
    // File tabs in the segmented style of the other editor windows (Lighting, settings): the open file raised with a
    // blue underline, a dot for unsaved changes, x (or middle click) to close
    if (s_RequestTab >= 0 && s_RequestTab < (int)s_Docs.size()) { if (s_Active != s_RequestTab) s_CompletionOpen = false; s_Active = s_RequestTab; }
    if (!s_Docs.empty()) {
        const float gap = 2 * ui, height = ImGui::GetFrameHeight() + 4 * ui, full = ImGui::GetContentRegionAvail().x;
        const float closeW = 18 * ui;
        std::vector<float> widths;
        float total = 0;
        for (auto& d : s_Docs) { float w = std::clamp(ImGui::CalcTextSize(d.path.filename().string().c_str()).x + closeW + 34 * ui, 110 * ui, 260 * ui); widths.push_back(w); total += w + gap; }
        // Many files: the tabs share the row
        if (total > full) for (auto& w : widths) w = std::max(70 * ui, (full - gap * s_Docs.size()) / s_Docs.size());
        const ImVec2 start = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, { start.x + full, start.y + height }, IM_COL32(28, 30, 34, 255), 4 * ui);
        for (int i = 0; i < (int)s_Docs.size(); ++i) {
            auto& d = s_Docs[i];
            if (i) ImGui::SameLine(0, gap);
            ImGui::PushID(i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = widths[i];
            const bool clicked = ImGui::InvisibleButton("##tab", { w, height });
            const bool hovered = ImGui::IsItemHovered(), on = i == s_Active;
            const bool overClose = hovered && ImGui::GetIO().MousePos.x > p.x + w - closeW - 6 * ui;
            if (clicked && !overClose) { if (s_Active != i) s_CompletionOpen = false; s_Active = i; }
            if ((clicked && overClose) || (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Middle))) closeTab = i;
            if (hovered) ImGui::SetTooltip("%s", AssetManager::ToRelative(d.path).c_str());
            if (on || hovered) draw->AddRectFilled({ p.x + 1, p.y + 1 }, { p.x + w - 1, p.y + height - 1 }, on ? IM_COL32(58, 62, 70, 255) : IM_COL32(40, 43, 48, 255), 3 * ui);
            if (on) draw->AddRectFilled({ p.x + 6 * ui, p.y + height - 3 * ui }, { p.x + w - 6 * ui, p.y + height - 1 * ui }, IM_COL32(47, 128, 237, 255), 1);
            // Language mark, name, unsaved dot / close button
            const char* mark = d.lang == Lang::CSharp ? "C#" : d.lang == Lang::Shader ? "<>" : "Tx";
            const ImU32 markColor = d.lang == Lang::CSharp ? IM_COL32(111, 189, 123, 255) : d.lang == Lang::Shader ? IM_COL32(168, 129, 231, 255) : IM_COL32(160, 160, 160, 255);
            const float textY = p.y + (height - ImGui::GetFontSize()) * 0.5f;
            draw->AddText({ p.x + 8 * ui, textY }, markColor, mark);
            const float nameX = p.x + 8 * ui + ImGui::CalcTextSize("C#").x + 6 * ui;
            const ImVec4 clip(nameX, p.y, p.x + w - closeW - 6 * ui, p.y + height);
            draw->AddText(nullptr, 0, { nameX, textY }, on ? IM_COL32(240, 242, 246, 255) : IM_COL32(165, 170, 178, 255), d.path.filename().string().c_str(), nullptr, 0, &clip);
            const ImVec2 c(p.x + w - closeW * 0.5f - 6 * ui, p.y + height * 0.5f);
            if (hovered || (on && !d.dirty)) {
                if (overClose) draw->AddRectFilled({ c.x - 8 * ui, c.y - 8 * ui }, { c.x + 8 * ui, c.y + 8 * ui }, IM_COL32(75, 80, 90, 255), 3 * ui);
                const float r = 3.5f * ui;
                const ImU32 xColor = overClose ? IM_COL32(240, 242, 246, 255) : IM_COL32(150, 155, 165, 255);
                draw->AddLine({ c.x - r, c.y - r }, { c.x + r, c.y + r }, xColor, 1.5f * ui);
                draw->AddLine({ c.x - r, c.y + r }, { c.x + r, c.y - r }, xColor, 1.5f * ui);
            } else if (d.dirty) draw->AddCircleFilled(c, 3.5f * ui, IM_COL32(220, 222, 228, 255));
            ImGui::PopID();
        }
        ImGui::Dummy({ 0, 2 * ui });
    }
    s_RequestTab = -1;
    if (closeTab >= 0) { if (s_Docs[closeTab].dirty) s_ConfirmClose = closeTab; else { s_Docs.erase(s_Docs.begin() + closeTab); s_Active = std::min(s_Active, (int)s_Docs.size() - 1); } }

    if (Doc* d = Active()) {
        if (s_FindOpen) {
            ImGui::SetNextItemWidth(260 * ui);
            if (s_FindFocus) { ImGui::SetKeyboardFocusHere(); s_FindFocus = false; }
            if (ImGui::InputTextWithHint("##find", "Find (Enter: next, Shift+Enter: previous)", &s_Find, ImGuiInputTextFlags_EnterReturnsTrue)) { FindNext(*d, s_Find, ImGui::GetIO().KeyShift); ImGui::SetKeyboardFocusHere(-1); }
            ImGui::SameLine(); if (ImGui::SmallButton("Next")) FindNext(*d, s_Find);
            ImGui::SameLine(); if (ImGui::SmallButton("Close")) s_FindOpen = false;
        }
        const auto diags = DiagnosticsFor(*d);
        int errors = 0, warnings = 0;
        for (auto& diag : diags) (diag.error ? errors : warnings)++;
        const float rowH = ImGui::GetTextLineHeightWithSpacing();
        const float problemsH = ImGui::GetFrameHeightWithSpacing() + (s_ShowProblems && !diags.empty() ? rowH * std::min<size_t>(diags.size(), 6) + 6 : 0);
        DrawDocument(*d, diags, problemsH);
        // Problems: counts, and the list (click: go to the line; fixes inline)
        const std::string header = std::format("{} Problems   {} errors   {} warnings{}", s_ShowProblems ? "v" : ">", errors, warnings,
                                               d->dirty && d->lang == Lang::CSharp && !diags.empty() ? "   (from the last build: save to check again)" : "");
        ImGui::PushStyleColor(ImGuiCol_Text, errors ? ImVec4(1, 0.5f, 0.45f, 1) : warnings ? ImVec4(0.95f, 0.8f, 0.35f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(header.c_str(), false)) s_ShowProblems = !s_ShowProblems;
        ImGui::PopStyleColor();
        if (s_ShowProblems && !diags.empty()) {
            ImGui::BeginChild("##problems", { 0, rowH * std::min<size_t>(diags.size(), 6) + 4 });
            for (size_t i = 0; i < diags.size(); ++i) {
                const auto& diag = diags[i];
                ImGui::PushID((int)i);
                ImGui::TextColored(diag.error ? ImVec4(1, 0.4f, 0.35f, 1) : ImVec4(0.95f, 0.8f, 0.35f, 1), diag.error ? "x" : "!");
                ImGui::SameLine();
                if (ImGui::Selectable(std::format("Ln {}  {}  {}", diag.line + 1, diag.code, diag.message).c_str(), false, ImGuiSelectableFlags_AllowOverlap)) { d->gotoLine = diag.line; d->cursor.col = diag.col; s_Refocus = true; }
                for (auto& fix : FixesFor(*d, diag)) { ImGui::SameLine(); if (ImGui::SmallButton(fix.label.c_str())) ApplyFix(*d, fix); }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    } else ImGui::TextDisabled("Double-click a script or a shader in the Project panel to edit it here.");

    if (s_ConfirmClose != -1) ImGui::OpenPopup("Unsaved Script");
    if (ImGui::BeginPopupModal("Unsaved Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(s_ConfirmClose == -2 ? "Some files have unsaved changes. Save them?" : "Save the changes to %s?", s_ConfirmClose >= 0 && s_ConfirmClose < (int)s_Docs.size() ? s_Docs[s_ConfirmClose].path.filename().string().c_str() : "");
        auto finish = [&](bool save) {
            if (s_ConfirmClose == -2) { if (save) for (auto& d : s_Docs) if (d.dirty) Save(d); s_Docs.clear(); s_Active = -1; s_Open = false; }
            else if (s_ConfirmClose >= 0 && s_ConfirmClose < (int)s_Docs.size()) { if (save) Save(s_Docs[s_ConfirmClose]); s_Docs.erase(s_Docs.begin() + s_ConfirmClose); s_Active = std::min(s_Active, (int)s_Docs.size() - 1); }
            s_ConfirmClose = -1; ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button("Save", { 90 * ui, 0 })) finish(true);
        ImGui::SameLine(); if (ImGui::Button("Don't Save", { 90 * ui, 0 })) finish(false);
        ImGui::SameLine(); if (ImGui::Button("Cancel", { 90 * ui, 0 })) { s_ConfirmClose = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace Stardust::ScriptEditor
