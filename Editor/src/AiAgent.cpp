// AI Agent window: a chat with a model (Anthropic, OpenAI, Gemini, or a local OpenAI-compatible server) that
// reads and edits the open scene, the project's files and scripts through tools.
// The conversation, the providers and the tools run in C# (ScriptCore/Source/EditorAgent.cs); this file draws
// the window, and AgentQueries.cpp answers the "agent..." Native.Query requests the tools make on the scene.
#include "EditorApp.h"
#include "AgentQueries.h"
#include "Core/Application.h"
#include "Scripting/ScriptEngine.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <set>
#include <unordered_map>

namespace Stardust {
using json = nlohmann::json;

namespace {
EditorApp* s_Agent = nullptr;
bool s_AgentEdited = false;          // the scene changed: record an undo step
}

std::string EditorApp::AgentQuery(const std::string& request) {
    AgentContext ctx;
    ctx.scene = m_ActiveScene.get();
    ctx.selection = &m_EntitySelection;
    ctx.selected = &m_Selected;
    ctx.scenePath = m_ScenePath;
    ctx.selectedAsset = m_SelectedAsset;
    ctx.playing = m_State != PlayState::Edit;
    ctx.save = [this] { SaveScene(); };
    std::string reply = AgentSceneQuery(ctx, request);
    if (ctx.edited) s_AgentEdited = true;
    return reply;
}

namespace {
json Agent(const json& request) {
    auto reply = json::parse(ScriptEngine::DrawEditorUI(request.dump()), nullptr, false);
    return reply.is_object() ? reply : json::object();
}

const char* kProviders[] = { "anthropic", "openai", "gemini", "ollama", "lmstudio", "custom" };
const char* kProviderNames[] = { "Anthropic (Claude)", "OpenAI", "Google Gemini", "Ollama (local)", "LM Studio (local)", "Custom OpenAI-compatible" };
const char* kProviderShort[] = { "Claude", "OpenAI", "Gemini", "Ollama", "LM Studio", "Custom" };
const char* kEfforts[] = { "low", "medium", "high", "xhigh", "max" };

// Colours of the chat
const ImU32 kBubble = IM_COL32(52, 56, 64, 255), kCard = IM_COL32(37, 40, 46, 255), kCardHover = IM_COL32(44, 48, 55, 255);
const ImU32 kCode = IM_COL32(24, 26, 30, 255), kPanel = IM_COL32(40, 43, 49, 255), kBorder = IM_COL32(62, 66, 74, 255);
const ImU32 kDim = IM_COL32(150, 156, 166, 255), kGreen = IM_COL32(95, 200, 120, 255), kRed = IM_COL32(235, 105, 95, 255);
const ImU32 kAccent = IM_COL32(47, 128, 237, 255), kText = IM_COL32(222, 226, 232, 255);

struct AgentView {
    json state = json::object();
    long long version = -1;
    std::string input, apiKey, baseUrl, model, chat;
    bool busy = false, synced = false, scrollDown = false, allowForChat = false;
    int elapsed = 0;
    std::set<int> expanded;
};
AgentView s_View;

// ---- Text the user can select and copy ----------------------------------------------------------------------
// ImGui's read-only multiline input selects and copies, but does not wrap: the text is wrapped here first
// (cached per id and width) and the box is sized to its lines, without a frame or scrollbars.
struct Wrapped { std::string source, text; float width = 0; int lines = 1; ImFont* font = nullptr; };
std::unordered_map<ImGuiID, Wrapped> s_Wrapped;
const Wrapped& Wrap(ImGuiID id, const std::string& text, float width) {
    auto& w = s_Wrapped[id];
    ImFont* font = ImGui::GetFont();
    if (w.source == text && std::abs(w.width - width) < 0.5f && w.font == font) return w;
    w.source = text; w.width = width; w.font = font; w.text.clear(); w.lines = 1;
    const float scale = ImGui::GetFontSize() / font->FontSize;
    const char* s = text.c_str(); const char* end = s + text.size();
    while (s < end) {
        const char* lineEnd = (const char*)memchr(s, '\n', end - s);
        if (!lineEnd) lineEnd = end;
        const char* p = s;
        if (p == lineEnd) { /* empty line */ }
        while (p < lineEnd) {
            const char* wrap = font->CalcWordWrapPositionA(scale, p, lineEnd, width);
            if (wrap == p) wrap = p + 1;
            w.text.append(p, wrap);
            p = wrap;
            while (p < lineEnd && *p == ' ') ++p;
            if (p < lineEnd) { w.text += '\n'; ++w.lines; }
        }
        s = lineEnd;
        if (s < end) { w.text += '\n'; ++w.lines; ++s; }
    }
    return w;
}
void SelectableText(const char* id, const std::string& text, float width, ImU32 color = kText, const std::string* copy = nullptr) {
    ImGui::PushID(id);
    const ImGuiID key = ImGui::GetID("##text");
    auto& w = const_cast<Wrapped&>(Wrap(key, text, std::max(20.f, width)));
    const float height = w.lines * ImGui::GetTextLineHeight() + 2;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0, 0 });
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0);
    std::string buffer = w.text;
    ImGui::InputTextMultiline("##text", &buffer, { width + 4, height }, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (ImGui::BeginPopupContextItem("##copy")) {
        if (ImGui::MenuItem("Copy")) ImGui::SetClipboardText((copy ? *copy : text).c_str());
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// ---- Small drawn icons -------------------------------------------------------------------------------------
enum class Icon { Check, Cross, Expand, Collapse, Plus, Gear, Send, Stop, Copy, Chats, Chevron };
void DrawIcon(ImDrawList* d, ImVec2 c, float r, Icon icon, ImU32 color) {
    auto line = [&](float x0, float y0, float x1, float y1, float t = 1.6f) { d->AddLine({ c.x + x0 * r, c.y + y0 * r }, { c.x + x1 * r, c.y + y1 * r }, color, t); };
    switch (icon) {
    case Icon::Check: line(-.8f, 0, -.25f, .6f, 2); line(-.25f, .6f, .85f, -.6f, 2); break;
    case Icon::Cross: line(-.6f, -.6f, .6f, .6f, 2); line(.6f, -.6f, -.6f, .6f, 2); break;
    case Icon::Expand: line(.2f, -.8f, .8f, -.8f); line(.8f, -.8f, .8f, -.2f); line(.8f, -.8f, .15f, -.15f); line(-.2f, .8f, -.8f, .8f); line(-.8f, .8f, -.8f, .2f); line(-.8f, .8f, -.15f, .15f); break;
    case Icon::Collapse: line(.15f, -.15f, .75f, -.15f); line(.15f, -.15f, .15f, -.75f); line(.15f, -.15f, .8f, -.8f); line(-.15f, .15f, -.75f, .15f); line(-.15f, .15f, -.15f, .75f); line(-.15f, .15f, -.8f, .8f); break;
    case Icon::Plus: line(-.75f, 0, .75f, 0, 1.8f); line(0, -.75f, 0, .75f, 1.8f); break;
    case Icon::Gear:
        d->AddCircle(c, r * .42f, color, 16, 1.6f);
        for (int i = 0; i < 8; ++i) { float a = i * 0.785398f; line(std::cos(a) * .55f, std::sin(a) * .55f, std::cos(a) * .9f, std::sin(a) * .9f, 2.2f); }
        break;
    case Icon::Send: line(0, .8f, 0, -.75f, 2); line(-.6f, -.15f, 0, -.8f, 2); line(.6f, -.15f, 0, -.8f, 2); break;
    case Icon::Stop: d->AddRectFilled({ c.x - r * .55f, c.y - r * .55f }, { c.x + r * .55f, c.y + r * .55f }, color, 2); break;
    case Icon::Copy: d->AddRect({ c.x - r * .75f, c.y - r * .4f }, { c.x + r * .35f, c.y + r * .85f }, color, 2, 0, 1.4f); d->AddRect({ c.x - r * .35f, c.y - r * .85f }, { c.x + r * .75f, c.y + r * .4f }, color, 2, 0, 1.4f); break;
    case Icon::Chats: line(-.8f, -.5f, .8f, -.5f); line(-.8f, 0, .8f, 0); line(-.8f, .5f, .3f, .5f); break;
    case Icon::Chevron: line(-.5f, -.25f, 0, .3f, 1.8f); line(0, .3f, .5f, -.25f, 1.8f); break;
    }
}
void Spinner(ImDrawList* d, ImVec2 c, float r, ImU32 color) {
    const float t = (float)ImGui::GetTime() * 6.f;
    d->PathArcTo(c, r, t, t + 4.4f, 18);
    d->PathStroke(color, 0, 1.8f);
}
bool IconButton(const char* id, Icon icon, const char* tooltip, ImU32 fill = 0, float size = 0) {
    const float h = size > 0 ? size : ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, { h, h });
    const bool hovered = ImGui::IsItemHovered();
    auto* d = ImGui::GetWindowDrawList();
    if (fill) d->AddRectFilled(p, { p.x + h, p.y + h }, hovered ? (fill | IM_COL32(30, 30, 30, 0)) : fill, h * 0.3f);
    else if (hovered) d->AddRectFilled(p, { p.x + h, p.y + h }, IM_COL32(255, 255, 255, 22), 4);
    DrawIcon(d, { p.x + h * .5f, p.y + h * .5f }, h * .28f, icon, fill ? IM_COL32(255, 255, 255, 255) : hovered ? kText : kDim);
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}
bool TextButton(const char* label, bool primary = false) {
    ImGui::PushStyleColor(ImGuiCol_Button, primary ? kAccent : IM_COL32(58, 62, 70, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, primary ? IM_COL32(70, 145, 245, 255) : IM_COL32(70, 75, 85, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5);
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return clicked;
}

// ---- Messages ----------------------------------------------------------------------------------------------
// Model answers are Markdown: code blocks get a box, headings use the bold font, ** and ` marks are dropped
void Markdown(int index, const std::string& text, float width) {
    struct Block { int kind; std::string body; };   // 0 text, 1 heading, 2 code
    std::vector<Block> blocks;
    size_t at = 0;
    std::string paragraph;
    auto flush = [&] { while (!paragraph.empty() && paragraph.back() == '\n') paragraph.pop_back(); if (!paragraph.empty()) blocks.push_back({ 0, paragraph }); paragraph.clear(); };
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (line.rfind("```", 0) == 0) {
            flush();
            std::string code;
            at = end + 1;
            while (at < text.size()) {
                size_t e = text.find('\n', at);
                if (e == std::string::npos) e = text.size();
                std::string l = text.substr(at, e - at);
                at = e + 1;
                if (l.rfind("```", 0) == 0) break;
                code += l + "\n";
            }
            if (!code.empty()) code.pop_back();
            blocks.push_back({ 2, code });
            continue;
        }
        size_t hashes = 0;
        while (hashes < line.size() && line[hashes] == '#') ++hashes;
        if (hashes > 0 && hashes < line.size() && line[hashes] == ' ') { flush(); blocks.push_back({ 1, line.substr(hashes + 1) }); }
        else {
            for (std::string mark : { "**", "__" }) for (size_t p; (p = line.find(mark)) != std::string::npos;) line.erase(p, 2);
            std::erase(line, '`');
            if (line.rfind("* ", 0) == 0) line[0] = '-';
            paragraph += line + "\n";
        }
        at = end + 1;
    }
    flush();
    int n = 0;
    for (auto& b : blocks) {
        const std::string id = std::format("b{}_{}", index, n++);
        if (b.kind == 2) {
            ImFont* code = Application::CodeFont();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float pad = 8;
            ImGui::GetWindowDrawList()->ChannelsSplit(2);
            ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
            ImGui::SetCursorScreenPos({ p.x + pad, p.y + pad });
            if (code) ImGui::PushFont(code);
            SelectableText(id.c_str(), b.body, width - pad * 2 - 4, IM_COL32(205, 215, 225, 255));
            if (code) ImGui::PopFont();
            const float bottom = ImGui::GetItemRectMax().y + pad;
            ImGui::GetWindowDrawList()->ChannelsSetCurrent(0);
            ImGui::GetWindowDrawList()->AddRectFilled(p, { p.x + width, bottom }, kCode, 6);
            ImGui::GetWindowDrawList()->ChannelsMerge();
            ImGui::SetCursorScreenPos({ p.x, bottom + 4 });
            ImGui::Dummy({ 0, 0 });
        } else if (b.kind == 1) {
            if (ImFont* bold = Application::BoldFont()) ImGui::PushFont(bold);
            SelectableText(id.c_str(), b.body, width);
            if (Application::BoldFont()) ImGui::PopFont();
        } else SelectableText(id.c_str(), b.body, width);
    }
}
}

void EditorApp::DrawAiAgent() {
    s_Agent = this;
    ScriptEngine::SetQueryHandler([](const std::string& request) { return s_Agent ? s_Agent->AgentQuery(request) : std::string("{}"); });
    auto& v = s_View;
    if (!ScriptEngine::IsReady()) {
        if (m_ShowAiAgent) { if (ImGui::Begin("AI Agent", &m_ShowAiAgent)) ImGui::TextDisabled(".NET is not available: the agent runs in C#."); ImGui::End(); }
        return;
    }
    // The agent keeps working with the window closed: its tools run on these polls
    if (!m_ShowAiAgent && !v.busy) return;
    json poll = Agent({ { "kind", "agent" }, { "action", "poll" }, { "version", v.version } });
    if (poll.contains("messages")) {
        if (poll.value("chat", "") != v.chat) { v.chat = poll.value("chat", ""); v.expanded.clear(); }
        v.state = poll; v.version = poll.value("version", 0LL); v.scrollDown = true;
    }
    v.busy = poll.value("busy", false);
    v.elapsed = poll.value("elapsed", 0);
    if (s_AgentEdited) { s_AgentEdited = false; m_UndoPending = true; m_UndoTimer = 1.0f; }
    if (!m_ShowAiAgent) return;

    const float ui = Application::Get().UiScale();
    ImGui::SetNextWindowSize({ 520 * ui, 680 * ui }, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    const bool open = ImGui::Begin("AI Agent", &m_ShowAiAgent, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }
    json& st = v.state;
    const json settings = st.value("settings", json::object());
    auto send = [&](json request) { request["kind"] = "agent"; json reply = Agent(request); v.version = -1; return reply; };
    if (!v.synced && settings.is_object() && !settings.empty()) { v.model = settings.value("model", ""); v.baseUrl = settings.value("baseUrl", ""); v.synced = true; }
    const std::string provider = settings.value("provider", "anthropic");
    int current = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kProviders); ++i) if (provider == kProviders[i]) current = i;
    auto* draw = ImGui::GetWindowDrawList();
    const float pad = 10 * ui;

    // ---- Header: New, the chat's title, Chats -----------------------------------------------------------
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x, height = ImGui::GetFrameHeight() + pad;
        draw->AddRectFilled(p, { p.x + width, p.y + height }, IM_COL32(30, 32, 37, 255));
        draw->AddLine({ p.x, p.y + height }, { p.x + width, p.y + height }, kBorder);
        ImGui::SetCursorScreenPos({ p.x + pad * .6f, p.y + pad * .5f });
        if (TextButton("New")) send({ { "action", "clear" } });
        std::string title = st.value("title", std::string());
        if (title.empty()) title = "New chat";
        if (ImFont* bold = Application::BoldFont()) ImGui::PushFont(bold);
        const float chatsWidth = 70 * ui;
        const float room = width - 2 * (chatsWidth + pad);
        while (title.size() > 4 && ImGui::CalcTextSize(title.c_str()).x > room) title = title.substr(0, title.size() - 4) + "...";
        draw->AddText({ p.x + (width - ImGui::CalcTextSize(title.c_str()).x) * .5f, p.y + (height - ImGui::GetTextLineHeight()) * .5f }, kText, title.c_str());
        if (Application::BoldFont()) ImGui::PopFont();
        ImGui::SetCursorScreenPos({ p.x + width - chatsWidth - pad * .6f, p.y + pad * .5f });
        if (TextButton("Chats")) ImGui::OpenPopup("AgentChats");
        if (ImGui::BeginPopup("AgentChats")) {
            const json chats = st.value("chats", json::array());
            if (chats.empty()) ImGui::TextDisabled("No saved chats yet");
            for (auto& c : chats) {
                ImGui::PushID(c.value("id", "").c_str());
                const bool selected = c.value("id", "") == v.chat;
                std::string name = c.value("title", std::string("Chat"));
                if (name.size() > 60) name = name.substr(0, 57) + "...";
                if (ImGui::Selectable(name.c_str(), selected, 0, { 320 * ui, 0 })) send({ { "action", "openChat" }, { "id", c["id"] } });
                ImGui::SameLine(330 * ui);
                ImGui::TextDisabled("%s", c.value("when", "").c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) send({ { "action", "deleteChat" }, { "id", c["id"] } });
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this chat");
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos({ p.x, p.y + height + 1 });
    }

    // ---- Bottom panels: approval and composer ---------------------------------------------------------
    const bool approval = st.contains("approval") && st["approval"].is_object();
    const float composerHeight = 104 * ui, approvalHeight = approval ? 92 * ui : 0;

    // ---- Transcript ---------------------------------------------------------------------------------
    ImGui::BeginChild("##transcript", { 0, -(composerHeight + approvalHeight + pad) }, 0);
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float column = std::min(avail - 2 * pad, 760 * ui);
        const float left = (avail - column) * .5f;
        auto* d = ImGui::GetWindowDrawList();
        ImGui::Dummy({ 0, pad });
        const json messages = st.value("messages", json::array());
        if (messages.empty()) {
            ImGui::SetCursorPosX(left);
            ImGui::BeginGroup();
            if (ImFont* bold = Application::BoldFont()) ImGui::PushFont(bold);
            ImGui::TextUnformatted("What should we build?");
            if (Application::BoldFont()) ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + column);
            ImGui::TextWrapped("The agent changes the open scene, writes and fixes scripts and reads the console. Every scene change can be undone with Ctrl+Z.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::Spacing();
            for (const char* example : { "Add five crates in a row in front of the camera", "Write a script that makes Player jump with Space and attach it", "Why do my scripts not compile?" }) {
                ImGui::PushStyleColor(ImGuiCol_Button, kCard);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCardHover);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0, .5f });
                if (ImGui::Button(example, { column, ImGui::GetFrameHeight() * 1.4f })) v.input = example;
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);
            }
            ImGui::EndGroup();
        }
        int index = 0;
        for (auto& m : messages) {
            ImGui::PushID(index);
            const std::string role = m.value("role", ""), text = m.value("text", "");
            if (role == "user") {
                // Right-aligned bubble
                const float inner = 12 * ui;
                const float textWidth = std::min(ImGui::CalcTextSize(text.c_str()).x + 6, column * .8f - inner * 2);
                const float x = left + column - textWidth - inner * 2;
                ImGui::Dummy({ 0, 4 * ui });
                const ImVec2 p = { ImGui::GetCursorScreenPos().x + x, ImGui::GetCursorScreenPos().y };
                d->ChannelsSplit(2);
                d->ChannelsSetCurrent(1);
                ImGui::SetCursorScreenPos({ p.x + inner, p.y + inner * .7f });
                SelectableText("u", text, textWidth);
                const float bottom = ImGui::GetItemRectMax().y + inner * .7f;
                d->ChannelsSetCurrent(0);
                d->AddRectFilled(p, { p.x + textWidth + inner * 2, bottom }, kBubble, 8 * ui);
                d->ChannelsMerge();
                ImGui::SetCursorScreenPos({ ImGui::GetCursorScreenPos().x, bottom + 8 * ui });
                ImGui::Dummy({ 0, 0 });
            } else if (role == "assistant") {
                ImGui::SetCursorPosX(left);
                ImGui::BeginGroup();
                Markdown(index, text, column);
                // Copy the whole answer (the text boxes copy any part of it)
                if (IconButton("##copy", Icon::Copy, "Copy the answer", 0, 20 * ui)) ImGui::SetClipboardText(text.c_str());
                ImGui::EndGroup();
                ImGui::Dummy({ 0, 4 * ui });
            } else if (role == "tool") {
                const bool error = m.value("error", false), pending = !m.contains("result");
                const bool expanded = v.expanded.count(index) > 0;
                ImGui::SetCursorPosX(left);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetFrameHeight() * 1.55f;
                const bool clicked = ImGui::InvisibleButton("##card", { column, h });
                const bool hovered = ImGui::IsItemHovered();
                d->AddRectFilled(p, { p.x + column, p.y + h }, hovered ? kCardHover : kCard, 7 * ui);
                const ImVec2 iconAt = { p.x + h * .5f, p.y + h * .5f };
                if (pending) Spinner(d, iconAt, h * .2f, kDim);
                else DrawIcon(d, iconAt, h * .2f, error ? Icon::Cross : Icon::Check, error ? kRed : kGreen);
                const std::string title = m.value("title", m.value("name", ""));
                std::string summary = m.value("summary", "");
                const float textY = p.y + (h - ImGui::GetTextLineHeight()) * .5f;
                d->AddText({ p.x + h, textY }, kText, title.c_str());
                const float after = p.x + h + ImGui::CalcTextSize(title.c_str()).x + 8;
                const float room = p.x + column - h - after;
                if (!summary.empty() && room > 40) {
                    summary = "(" + summary + ")";
                    while (summary.size() > 5 && ImGui::CalcTextSize(summary.c_str()).x > room) summary = summary.substr(0, summary.size() - 5) + "...)";
                    d->AddText({ after, textY }, kDim, summary.c_str());
                }
                DrawIcon(d, { p.x + column - h * .5f, p.y + h * .5f }, h * .18f, expanded ? Icon::Collapse : Icon::Expand, hovered ? kText : kDim);
                if (clicked) { if (expanded) v.expanded.erase(index); else v.expanded.insert(index); }
                if (expanded) {
                    ImGui::SetCursorPosX(left + 10 * ui);
                    ImGui::BeginGroup();
                    ImFont* code = Application::CodeFont();
                    auto section = [&](const char* label, const char* id, const std::string& body) {
                        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
                        ImGui::TextUnformatted(label);
                        ImGui::PopStyleColor();
                        const ImVec2 q = ImGui::GetCursorScreenPos();
                        const float boxWidth = column - 10 * ui, inner = 8 * ui;
                        d->ChannelsSplit(2);
                        d->ChannelsSetCurrent(1);
                        ImGui::SetCursorScreenPos({ q.x + inner, q.y + inner });
                        if (code) ImGui::PushFont(code);
                        SelectableText(id, body, boxWidth - inner * 2 - 4, IM_COL32(200, 208, 218, 255));
                        if (code) ImGui::PopFont();
                        const float bottom = ImGui::GetItemRectMax().y + inner;
                        d->ChannelsSetCurrent(0);
                        d->AddRectFilled(q, { q.x + boxWidth, bottom }, kCode, 6 * ui);
                        d->ChannelsMerge();
                        ImGui::SetCursorScreenPos({ q.x, bottom + 6 * ui });
                        ImGui::Dummy({ 0, 0 });
                    };
                    section("Input", "args", m.value("args", ""));
                    if (!pending) section(error ? "Error" : "Result", "result", m.value("result", ""));
                    ImGui::EndGroup();
                }
                ImGui::Dummy({ 0, 2 * ui });
            } else if (role == "error") {
                ImGui::SetCursorPosX(left);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float inner = 10 * ui;
                d->ChannelsSplit(2);
                d->ChannelsSetCurrent(1);
                ImGui::SetCursorScreenPos({ p.x + inner, p.y + inner });
                SelectableText("e", text, column - inner * 2 - 4, IM_COL32(255, 170, 160, 255));
                const float bottom = ImGui::GetItemRectMax().y + inner;
                d->ChannelsSetCurrent(0);
                d->AddRectFilled(p, { p.x + column, bottom }, IM_COL32(80, 36, 34, 200), 7 * ui);
                d->AddRect(p, { p.x + column, bottom }, IM_COL32(150, 70, 62, 255), 7 * ui);
                d->ChannelsMerge();
                ImGui::SetCursorScreenPos({ p.x, bottom + 6 * ui });
                ImGui::Dummy({ 0, 0 });
            } else {
                ImGui::SetCursorPosX(left);
                SelectableText("n", text, column, kDim);
            }
            ImGui::PopID();
            ++index;
        }
        if (v.busy) {
            ImGui::SetCursorPosX(left);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            Spinner(d, { p.x + h * .5f, p.y + h * .5f }, h * .35f, kDim);
            ImGui::SetCursorScreenPos({ p.x + h * 1.4f, p.y });
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kDim), "%s  (%ds)", st.value("status", std::string("Working...")).c_str(), v.elapsed);
        }
        ImGui::Dummy({ 0, pad });
        if (v.scrollDown && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 80 * ui) ImGui::SetScrollHereY(1.f);
        v.scrollDown = false;
    }
    ImGui::EndChild();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float column = std::min(avail - 2 * pad, 780 * ui);
    const float left = (avail - column) * .5f;

    // ---- Approval: the agent waits for Allow / Deny ---------------------------------------------------
    if (approval) {
        auto& a = st["approval"];
        ImGui::SetCursorPosX(left);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = approvalHeight - 6 * ui;
        draw->AddRectFilled(p, { p.x + column, p.y + h }, kPanel, 8 * ui);
        draw->AddRect(p, { p.x + column, p.y + h }, IM_COL32(200, 150, 60, 255), 8 * ui);
        const float inner = 12 * ui;
        ImGui::SetCursorScreenPos({ p.x + inner, p.y + inner * .8f });
        ImGui::TextUnformatted("Agent wants to");
        ImGui::SameLine(0, 4 * ui);
        std::string action = a.value("action", "");
        if (!action.empty()) action[0] = (char)std::tolower((unsigned char)action[0]);
        if (ImFont* bold = Application::BoldFont()) ImGui::PushFont(bold);
        ImGui::TextUnformatted(action.c_str());
        if (Application::BoldFont()) ImGui::PopFont();
        ImGui::SetCursorScreenPos({ p.x + inner, ImGui::GetCursorScreenPos().y });
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::PushTextWrapPos(p.x - ImGui::GetWindowPos().x + column - inner);
        ImGui::TextWrapped("%s", a.value("detail", "").c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        const float rowY = p.y + h - ImGui::GetFrameHeight() - inner * .7f;
        ImGui::SetCursorScreenPos({ p.x + inner, rowY });
        ImGui::SetNextItemWidth(130 * ui);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5);
        if (ImGui::BeginCombo("##scope", v.allowForChat ? "For this chat" : "Just once")) {
            if (ImGui::Selectable("Just once", !v.allowForChat)) v.allowForChat = false;
            if (ImGui::Selectable("For this chat", v.allowForChat)) v.allowForChat = true;
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("For this chat: no more questions until New");
        const float allowWidth = ImGui::CalcTextSize("Allow").x + ImGui::GetStyle().FramePadding.x * 2, denyWidth = ImGui::CalcTextSize("Deny").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorScreenPos({ p.x + column - inner - allowWidth - denyWidth - 8 * ui, rowY });
        if (TextButton("Deny")) send({ { "action", "approve" }, { "allow", "no" } });
        ImGui::SameLine(0, 8 * ui);
        if (TextButton("Allow", true)) { send({ { "action", "approve" }, { "allow", v.allowForChat ? "all" : "once" } }); v.allowForChat = false; }
        ImGui::SetCursorScreenPos({ p.x, p.y + approvalHeight });
    }

    // ---- Composer: input, new chat, settings, model, send / stop ---------------------------------------
    {
        ImGui::SetCursorPosX(left);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = composerHeight - pad;
        draw->AddRectFilled(p, { p.x + column, p.y + h }, kPanel, 10 * ui);
        draw->AddRect(p, { p.x + column, p.y + h }, kBorder, 10 * ui);
        const float inner = 10 * ui, button = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos({ p.x + inner, p.y + inner * .6f });
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 2, 4 });
        const float inputHeight = h - button - inner * 1.6f;
        ImGui::InputTextMultiline("##input", &v.input, { column - inner * 2, inputHeight });
        const bool focused = ImGui::IsItemFocused() || ImGui::IsItemActive();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (v.input.empty() && !ImGui::IsItemActive()) draw->AddText({ p.x + inner + 4, p.y + inner * .6f + 4 }, kDim, v.busy ? "The agent is working..." : "Ask the agent to build, change or explain something");
        const bool submit = focused && ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::GetIO().KeyCtrl;

        const float rowY = p.y + h - button - inner * .6f;
        ImGui::SetCursorScreenPos({ p.x + inner * .6f, rowY });
        if (IconButton("##new", Icon::Plus, "New chat")) send({ { "action", "clear" } });
        ImGui::SameLine(0, 2 * ui);
        if (IconButton("##settings", Icon::Gear, "API key, server address, effort")) ImGui::OpenPopup("AgentSettings");
        if (ImGui::BeginPopup("AgentSettings")) {
            const bool local = provider == "ollama" || provider == "lmstudio" || provider == "custom";
            if (ImFont* bold = Application::BoldFont()) ImGui::PushFont(bold);
            ImGui::TextUnformatted(kProviderNames[current]);
            if (Application::BoldFont()) ImGui::PopFont();
            ImGui::TextDisabled("%s", settings.value("keyStatus", std::string()).c_str());
            ImGui::SetNextItemWidth(320 * ui);
            ImGui::InputTextWithHint("##key", local ? "API key (usually not needed)" : "API key", &v.apiKey, ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            if (ImGui::Button("Save##key")) { send({ { "action", "settings" }, { "apiKey", v.apiKey } }); v.apiKey.clear(); }
            if (provider != "anthropic" && provider != "gemini") {
                ImGui::SetNextItemWidth(320 * ui);
                ImGui::InputTextWithHint("##base", "server address", &v.baseUrl);
                ImGui::SameLine();
                if (ImGui::Button("Save##base")) send({ { "action", "settings" }, { "baseUrl", v.baseUrl } });
            }
            if (provider == "anthropic") {
                const std::string effort = settings.value("effort", "high");
                ImGui::SetNextItemWidth(120 * ui);
                if (ImGui::BeginCombo("Effort", effort.c_str())) {
                    for (auto* e : kEfforts) if (ImGui::Selectable(e, effort == e)) send({ { "action", "settings" }, { "effort", e } });
                    ImGui::EndCombo();
                }
            }
            ImGui::TextDisabled("Keys are kept in %s, never in the project.", settings.value("file", std::string()).c_str());
            ImGui::EndPopup();
        }

        // Provider and model, on the right next to Send
        std::string label = std::string(kProviderShort[current]) + "  " + settings.value("model", std::string());
        if (label.size() > 42) label = label.substr(0, 39) + "...";
        const float labelWidth = ImGui::CalcTextSize(label.c_str()).x + button;
        ImGui::SetCursorScreenPos({ p.x + column - inner * .6f - button - 6 * ui - labelWidth, rowY });
        {
            const ImVec2 q = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##modelpick", { labelWidth, button })) { send({ { "action", "models" } }); ImGui::OpenPopup("AgentModel"); }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) draw->AddRectFilled(q, { q.x + labelWidth, q.y + button }, IM_COL32(255, 255, 255, 22), 5);
            draw->AddText({ q.x + 6, q.y + (button - ImGui::GetTextLineHeight()) * .5f }, hovered ? kText : kDim, label.c_str());
            DrawIcon(draw, { q.x + labelWidth - button * .45f, q.y + button * .5f }, button * .22f, Icon::Chevron, kDim);
            if (hovered) ImGui::SetTooltip("Provider and model");
        }
        ImGui::SetNextWindowSizeConstraints({ 300 * ui, 0 }, { 520 * ui, 460 * ui });
        if (ImGui::BeginPopup("AgentModel")) {
            ImGui::TextDisabled("Provider");
            for (int i = 0; i < IM_ARRAYSIZE(kProviders); ++i)
                if (ImGui::Selectable(kProviderNames[i], i == current) && i != current) { send({ { "action", "settings" }, { "provider", kProviders[i] } }); send({ { "action", "models" } }); v.synced = false; v.apiKey.clear(); }
            ImGui::Separator();
            ImGui::TextDisabled("Model");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##model", "model name", &v.model, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
                send({ { "action", "settings" }, { "model", v.model } });
            const json models = st.value("models", json::array());
            if (st.value("modelsLoading", false)) ImGui::TextDisabled("Loading the models...");
            else if (models.empty()) ImGui::TextDisabled("%s", st.value("modelsError", std::string("No models listed")).c_str());
            ImGui::BeginChild("##models", { 0, std::min(260 * ui, (float)models.size() * ImGui::GetTextLineHeightWithSpacing() + 4) });
            for (auto& m : models) if (m.is_string() && ImGui::Selectable(m.get<std::string>().c_str(), m == v.model)) { v.model = m; send({ { "action", "settings" }, { "model", v.model } }); ImGui::CloseCurrentPopup(); }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos({ p.x + column - inner * .6f - button, rowY });
        if (v.busy) { if (IconButton("##stop", Icon::Stop, "Stop", IM_COL32(90, 95, 105, 255))) send({ { "action", "stop" } }); }
        else {
            const bool ready = !v.input.empty();
            if ((IconButton("##send", Icon::Send, "Send (Ctrl+Enter)", ready ? kAccent : IM_COL32(70, 74, 82, 255)) || submit) && ready) { send({ { "action", "send" }, { "text", v.input } }); v.input.clear(); }
        }
    }
    ImGui::End();
}

} // namespace Stardust
