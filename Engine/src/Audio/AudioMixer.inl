// Audio Mixer: groups that Audio Sources play into (Settings/AudioMixer.json). Each group has a
// volume and optional effects (low-pass, high-pass, echo, reverb) and feeds its parent group;
// Master feeds the output. Snapshots store a set of group values to blend to.
// Included by AudioEngine.cpp (it shares the miniaudio engine).

namespace Stardust {
namespace {
struct GroupNode {
    ma_sound_group group{};
    bool groupReady = false;
    Scope<ma_lpf_node> lpf; Scope<ma_hpf_node> hpf; Scope<ma_delay_node> delay; Scope<ma_reverb_node> reverb;
    AudioMixerGroup built; // settings the effect chain was built for
};
std::vector<AudioMixerGroup> s_Groups;
std::vector<AudioMixerSnapshot> s_Snapshots;
std::unordered_map<std::string, std::unique_ptr<GroupNode>> s_Nodes;
struct Transition { std::vector<AudioMixerGroup> from, to; float time = 0, duration = 0; bool active = false; } s_Transition;

std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
AudioMixerGroup* FindGroup(const std::string& name) {
    for (auto& g : s_Groups) if (Lower(g.name) == Lower(name)) return &g;
    return nullptr;
}
void TearDownChain(GroupNode& n) {
    if (n.groupReady) ma_node_detach_output_bus(&n.group, 0);
    if (n.lpf) { ma_lpf_node_uninit(n.lpf.get(), nullptr); n.lpf.reset(); }
    if (n.hpf) { ma_hpf_node_uninit(n.hpf.get(), nullptr); n.hpf.reset(); }
    if (n.delay) { ma_delay_node_uninit(n.delay.get(), nullptr); n.delay.reset(); }
    if (n.reverb) { ma_reverb_node_uninit(n.reverb.get(), nullptr); n.reverb.reset(); }
}
bool NeedsRebuild(const AudioMixerGroup& a, const AudioMixerGroup& b) {
    return (a.lowpass > 0) != (b.lowpass > 0) || (a.highpass > 0) != (b.highpass > 0) || (a.echo > 0) != (b.echo > 0) ||
           (a.reverb > 0) != (b.reverb > 0) || std::abs(a.echoDelay - b.echoDelay) > 1e-4f || a.parent != b.parent;
}
ma_node* InputOf(const std::string& name) {
    auto it = s_Nodes.find(Lower(name));
    if (it == s_Nodes.end() || !it->second->groupReady) return ma_engine_get_endpoint(&s_Engine);
    return (ma_node*)&it->second->group;
}
void BuildChain(const AudioMixerGroup& g, GroupNode& n) {
    TearDownChain(n);
    ma_node_graph* graph = ma_engine_get_node_graph(&s_Engine);
    const ma_uint32 channels = ma_engine_get_channels(&s_Engine), rate = ma_engine_get_sample_rate(&s_Engine);
    ma_node* last = (ma_node*)&n.group;
    auto link = [&](ma_node* next) { ma_node_attach_output_bus(last, 0, next, 0); last = next; };
    if (g.lowpass > 0) { n.lpf = MakeScope<ma_lpf_node>(); auto c = ma_lpf_node_config_init(channels, rate, std::clamp((double)g.lowpass, 10.0, rate * 0.45), 2); if (ma_lpf_node_init(graph, &c, nullptr, n.lpf.get()) == MA_SUCCESS) link((ma_node*)n.lpf.get()); else n.lpf.reset(); }
    if (g.highpass > 0) { n.hpf = MakeScope<ma_hpf_node>(); auto c = ma_hpf_node_config_init(channels, rate, std::clamp((double)g.highpass, 10.0, rate * 0.45), 2); if (ma_hpf_node_init(graph, &c, nullptr, n.hpf.get()) == MA_SUCCESS) link((ma_node*)n.hpf.get()); else n.hpf.reset(); }
    if (g.echo > 0) {
        n.delay = MakeScope<ma_delay_node>();
        auto c = ma_delay_node_config_init(channels, rate, (ma_uint32)(rate * std::clamp(g.echoDelay, 0.01f, 4.0f)), std::clamp(g.echoFeedback, 0.0f, 0.95f));
        if (ma_delay_node_init(graph, &c, nullptr, n.delay.get()) == MA_SUCCESS) { ma_delay_node_set_wet(n.delay.get(), std::clamp(g.echo, 0.0f, 1.0f)); ma_delay_node_set_dry(n.delay.get(), 1.0f); link((ma_node*)n.delay.get()); } else n.delay.reset();
    }
    if (g.reverb > 0 && channels <= 2) {
        n.reverb = MakeScope<ma_reverb_node>();
        auto c = ma_reverb_node_config_init(channels, rate);
        c.wetVolume = std::clamp(g.reverb, 0.0f, 1.0f) * 3.0f; c.dryVolume = 1.0f; c.roomSize = std::clamp(g.reverbRoom, 0.0f, 1.0f);
        if (ma_reverb_node_init(graph, &c, nullptr, n.reverb.get()) == MA_SUCCESS) link((ma_node*)n.reverb.get()); else n.reverb.reset();
    }
    ma_node_attach_output_bus(last, 0, Lower(g.name) == "master" ? ma_engine_get_endpoint(&s_Engine) : InputOf(g.parent.empty() ? "Master" : g.parent), 0);
    n.built = g;
}
void ApplyValues(const AudioMixerGroup& g, GroupNode& n) {
    const bool anySolo = std::any_of(s_Groups.begin(), s_Groups.end(), [](auto& x) { return x.solo; });
    const bool silent = g.mute || (anySolo && !g.solo && Lower(g.name) != "master");
    ma_sound_group_set_volume(&n.group, silent ? 0.0f : ma_volume_db_to_linear(std::clamp(g.volume, -80.0f, 20.0f)));
    ma_sound_group_set_pitch(&n.group, std::clamp(g.pitch, 0.1f, 4.0f));
    const ma_uint32 channels = ma_engine_get_channels(&s_Engine), rate = ma_engine_get_sample_rate(&s_Engine);
    if (n.lpf && std::abs(n.built.lowpass - g.lowpass) > 0.5f) { auto c = ma_lpf_config_init(ma_format_f32, channels, rate, std::clamp((double)g.lowpass, 10.0, rate * 0.45), 2); ma_lpf_node_reinit(&c, n.lpf.get()); n.built.lowpass = g.lowpass; }
    if (n.hpf && std::abs(n.built.highpass - g.highpass) > 0.5f) { auto c = ma_hpf_config_init(ma_format_f32, channels, rate, std::clamp((double)g.highpass, 10.0, rate * 0.45), 2); ma_hpf_node_reinit(&c, n.hpf.get()); n.built.highpass = g.highpass; }
    if (n.delay) { ma_delay_node_set_wet(n.delay.get(), std::clamp(g.echo, 0.0f, 1.0f)); ma_delay_node_set_decay(n.delay.get(), std::clamp(g.echoFeedback, 0.0f, 0.95f)); }
    if (n.reverb) { verblib_set_wet(&n.reverb->reverb, std::clamp(g.reverb, 0.0f, 1.0f) * 3.0f); verblib_set_room_size(&n.reverb->reverb, std::clamp(g.reverbRoom, 0.0f, 1.0f)); }
}
void ParseGroup(const nlohmann::json& j, AudioMixerGroup& g) {
    g.name = j.value("name", g.name); g.parent = j.value("parent", g.parent);
    g.volume = j.value("volume", g.volume); g.mute = j.value("mute", g.mute); g.solo = j.value("solo", g.solo); g.pitch = j.value("pitch", g.pitch);
    g.lowpass = j.value("lowpass", g.lowpass); g.highpass = j.value("highpass", g.highpass);
    g.reverb = j.value("reverb", g.reverb); g.reverbRoom = j.value("reverbRoom", g.reverbRoom);
    g.echo = j.value("echo", g.echo); g.echoDelay = j.value("echoDelay", g.echoDelay); g.echoFeedback = j.value("echoFeedback", g.echoFeedback);
}
nlohmann::json GroupJson(const AudioMixerGroup& g) {
    return { { "name", g.name }, { "parent", g.parent }, { "volume", g.volume }, { "mute", g.mute }, { "solo", g.solo }, { "pitch", g.pitch },
             { "lowpass", g.lowpass }, { "highpass", g.highpass }, { "reverb", g.reverb }, { "reverbRoom", g.reverbRoom },
             { "echo", g.echo }, { "echoDelay", g.echoDelay }, { "echoFeedback", g.echoFeedback } };
}
void EnsureMaster() {
    if (!FindGroup("Master")) { AudioMixerGroup m; m.name = "Master"; s_Groups.insert(s_Groups.begin(), m); }
    for (auto& g : s_Groups) if (Lower(g.name) == "master") g.parent.clear();
}
}

std::vector<AudioMixerGroup>& AudioEngine::MixerGroups() { return s_Groups; }
std::vector<AudioMixerSnapshot>& AudioEngine::MixerSnapshots() { return s_Snapshots; }

void AudioEngine::LoadMixer() {
    s_Groups.clear(); s_Snapshots.clear(); s_Transition.active = false;
    auto path = AssetManager::ToAbsolute("Settings/AudioMixer.json");
    if (FileSystem::Exists(path)) {
        auto j = nlohmann::json::parse(FileSystem::ReadText(path), nullptr, false);
        if (j.is_object()) {
            for (auto& g : j.value("groups", nlohmann::json::array())) if (g.is_object()) { AudioMixerGroup group; ParseGroup(g, group); if (!group.name.empty()) s_Groups.push_back(group); }
            for (auto& s : j.value("snapshots", nlohmann::json::array())) {
                if (!s.is_object()) continue;
                AudioMixerSnapshot snapshot; snapshot.name = s.value("name", "");
                for (auto& g : s.value("groups", nlohmann::json::array())) if (g.is_object()) { AudioMixerGroup group; ParseGroup(g, group); snapshot.groups.push_back(group); }
                if (!snapshot.name.empty()) s_Snapshots.push_back(std::move(snapshot));
            }
        }
    }
    EnsureMaster();
    ApplyMixer();
}

bool AudioEngine::SaveMixer() {
    nlohmann::json j = { { "groups", nlohmann::json::array() }, { "snapshots", nlohmann::json::array() } };
    for (auto& g : s_Groups) j["groups"].push_back(GroupJson(g));
    for (auto& s : s_Snapshots) { nlohmann::json groups = nlohmann::json::array(); for (auto& g : s.groups) groups.push_back(GroupJson(g)); j["snapshots"].push_back({ { "name", s.name }, { "groups", groups } }); }
    return FileSystem::WriteText(AssetManager::ToAbsolute("Settings/AudioMixer.json"), j.dump(2));
}

// Creates missing groups, rebuilds changed effect chains and applies the values
void AudioEngine::ApplyMixer() {
    if (!s_Ready) return;
    EnsureMaster();
    // Parents first, so a child attaches to an existing group
    std::vector<const AudioMixerGroup*> order;
    std::function<void(const AudioMixerGroup&, int)> visit = [&](const AudioMixerGroup& g, int depth) {
        if (depth > 16 || std::find(order.begin(), order.end(), &g) != order.end()) return;
        if (!g.parent.empty()) if (auto* p = FindGroup(g.parent); p && p != &g) visit(*p, depth + 1);
        order.push_back(&g);
    };
    for (auto& g : s_Groups) visit(g, 0);
    for (const AudioMixerGroup* g : order) {
        auto& slot = s_Nodes[Lower(g->name)];
        bool fresh = false;
        if (!slot) {
            slot = std::make_unique<GroupNode>();
            if (ma_sound_group_init(&s_Engine, 0, nullptr, &slot->group) != MA_SUCCESS) { slot.reset(); continue; }
            slot->groupReady = true; fresh = true;
        }
        if (fresh || NeedsRebuild(slot->built, *g)) BuildChain(*g, *slot);
        ApplyValues(*g, *slot);
    }
}

void* AudioEngine::MixerGroupNode(const std::string& name) {
    if (name.empty() || !s_Ready) return nullptr;
    auto it = s_Nodes.find(Lower(name));
    if (it == s_Nodes.end()) {
        // A group used by a source but not in the mixer file: created under Master
        if (!FindGroup(name)) { AudioMixerGroup g; g.name = name; g.parent = "Master"; s_Groups.push_back(g); }
        ApplyMixer();
        it = s_Nodes.find(Lower(name));
        if (it == s_Nodes.end()) return nullptr;
    }
    return &it->second->group;
}

bool AudioEngine::SetMixerParameter(const std::string& group, const std::string& parameter, float value) {
    auto* g = FindGroup(group);
    if (!g) return false;
    const std::string p = Lower(parameter);
    if (p == "volume") g->volume = value; else if (p == "pitch") g->pitch = value; else if (p == "lowpass") g->lowpass = value;
    else if (p == "highpass") g->highpass = value; else if (p == "reverb") g->reverb = value; else if (p == "reverbroom") g->reverbRoom = value;
    else if (p == "echo") g->echo = value; else if (p == "echodelay") g->echoDelay = value; else if (p == "echofeedback") g->echoFeedback = value;
    else if (p == "mute") g->mute = value != 0; else if (p == "solo") g->solo = value != 0;
    else return false;
    ApplyMixer();
    return true;
}

float AudioEngine::GetMixerParameter(const std::string& group, const std::string& parameter) {
    auto* g = FindGroup(group);
    if (!g) return 0.0f;
    const std::string p = Lower(parameter);
    return p == "volume" ? g->volume : p == "pitch" ? g->pitch : p == "lowpass" ? g->lowpass : p == "highpass" ? g->highpass : p == "reverb" ? g->reverb :
           p == "reverbroom" ? g->reverbRoom : p == "echo" ? g->echo : p == "echodelay" ? g->echoDelay : p == "echofeedback" ? g->echoFeedback :
           p == "mute" ? (g->mute ? 1.0f : 0.0f) : p == "solo" ? (g->solo ? 1.0f : 0.0f) : 0.0f;
}

bool AudioEngine::TransitionToSnapshot(const std::string& name, float seconds) {
    auto it = std::find_if(s_Snapshots.begin(), s_Snapshots.end(), [&](auto& s) { return Lower(s.name) == Lower(name); });
    if (it == s_Snapshots.end()) return false;
    s_Transition.from = s_Groups; s_Transition.to = s_Groups;
    for (auto& g : s_Transition.to) for (auto& v : it->groups) if (Lower(v.name) == Lower(g.name)) { auto parent = g.parent; g = v; g.parent = parent; }
    s_Transition.time = 0; s_Transition.duration = std::max(0.0f, seconds); s_Transition.active = true;
    UpdateMixer(0.0f);
    return true;
}

void AudioEngine::UpdateMixer(float dt) {
    if (!s_Transition.active) return;
    s_Transition.time += dt;
    const float t = s_Transition.duration <= 0 ? 1.0f : std::clamp(s_Transition.time / s_Transition.duration, 0.0f, 1.0f);
    for (size_t i = 0; i < s_Groups.size() && i < s_Transition.to.size(); ++i) {
        auto& a = s_Transition.from[i]; auto& b = s_Transition.to[i]; auto& g = s_Groups[i];
        auto mix = [&](float x, float y) { return x + (y - x) * t; };
        g.volume = mix(a.volume, b.volume); g.pitch = mix(a.pitch, b.pitch); g.lowpass = t < 1 && (a.lowpass == 0) != (b.lowpass == 0) ? (b.lowpass == 0 ? a.lowpass : b.lowpass) : mix(a.lowpass, b.lowpass);
        g.highpass = mix(a.highpass, b.highpass); g.reverb = mix(a.reverb, b.reverb); g.echo = mix(a.echo, b.echo);
        if (t >= 1) { g.mute = b.mute; g.lowpass = b.lowpass; }
    }
    ApplyMixer();
    if (t >= 1) s_Transition.active = false;
}

void AudioEngine::ShutdownMixer() {
    for (auto& [name, n] : s_Nodes) { TearDownChain(*n); if (n->groupReady) ma_sound_group_uninit(&n->group); }
    s_Nodes.clear();
}

} // namespace Stardust
