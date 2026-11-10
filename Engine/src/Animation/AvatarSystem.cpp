#include "Assets/AssetMeta.h"
#include "Animation/AvatarSystem.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace Stardust {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const float kBakeRate = 30.0f;

glm::quat RotationOf(const glm::mat4& m) {
    glm::mat3 r(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])));
    return glm::normalize(glm::quat_cast(r));
}
glm::quat RotationBetween(glm::vec3 a, glm::vec3 b) {
    float w = 1.0f + glm::dot(a, b);
    if (w < 1e-5f) {
        glm::vec3 axis = std::abs(a.x) < 0.8f ? glm::cross(a, glm::vec3(1, 0, 0)) : glm::cross(a, glm::vec3(0, 1, 0));
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    glm::vec3 v = glm::cross(a, b);
    return glm::normalize(glm::quat(w, v.x, v.y, v.z));
}
json ToJson(const glm::quat& q) { return json::array({ q.x, q.y, q.z, q.w }); }
glm::quat QuatFrom(const json& j) { return glm::quat(j.at(3).get<float>(), j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()); }

std::vector<std::string> BonePaths(const std::vector<FbxBoneDesc>& bones) {
    std::vector<std::string> paths(bones.size());
    for (size_t i = 0; i < bones.size(); ++i)
        paths[i] = bones[i].parent >= 0 ? paths[bones[i].parent] + "/" + bones[i].name : bones[i].name;
    return paths;
}

std::string SafeName(std::string name) {
    for (char& c : name) if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
    return name;
}

// ---------------------------------------------------------------- name matching
struct Parsed { std::string core; char side = 0; };

Parsed ParseBoneName(std::string name) {
    if (auto colon = name.rfind(':'); colon != std::string::npos) name = name.substr(colon + 1);
    std::vector<std::string> tokens;
    std::string token;
    auto flush = [&] { if (!token.empty()) { tokens.push_back(token); token.clear(); } };
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char c = name[i];
        if (!std::isalnum(c)) { flush(); continue; }
        // camelCase boundaries turn "LeftUpLeg" into left, up, leg
        if (std::isupper(c) && i > 0 && std::islower((unsigned char)name[i - 1])) flush();
        token += (char)std::tolower(c);
    }
    flush();
    Parsed out;
    std::string core;
    for (const std::string& t : tokens) {
        if (t == "mixamorig" || t == "bip001" || t == "bip01" || t == "bip" || t == "def" || t == "jnt" || t == "bone" || t == "armature") continue;
        if (t == "left" || t == "l") { out.side = 'L'; continue; }
        if (t == "right" || t == "r") { out.side = 'R'; continue; }
        core += t;
    }
    out.core = core;
    return out;
}

const std::map<std::string, std::vector<std::string>>& Aliases() {
    static const std::map<std::string, std::vector<std::string>> table = {
        { "Hips", { "hips", "hip", "pelvis" } },
        { "Spine", { "spine", "spine01", "spine0" } },
        { "Chest", { "spine1", "spine02", "chest", "spine001" } },
        { "UpperChest", { "spine2", "spine03", "upperchest", "spine002" } },
        { "Neck", { "neck", "neck01" } },
        { "Head", { "head" } },
        { "Shoulder", { "shoulder", "clavicle", "collar" } },
        { "UpperArm", { "arm", "upperarm", "uparm" } },
        { "LowerArm", { "forearm", "lowerarm", "elbow" } },
        { "Hand", { "hand", "wrist" } },
        { "UpperLeg", { "upleg", "upperleg", "thigh" } },
        { "LowerLeg", { "leg", "lowerleg", "calf", "shin", "knee" } },
        { "Foot", { "foot", "ankle" } },
        { "Toes", { "toebase", "toe", "toes", "ball", "toe0" } },
    };
    return table;
}

// "LeftThumbProximal" and friends from names like handthumb1, thumb01, thumb_proximal
std::string MatchFinger(std::string core) {
    if (core.starts_with("hand")) core = core.substr(4);
    static const std::regex pattern("(thumb|index|middle|ring|pinky|little)(proximal|intermediate|distal|0?[123])");
    std::smatch m;
    if (!std::regex_match(core, m, pattern)) return {};
    std::string finger = m[1];
    finger[0] = (char)std::toupper(finger[0]);
    if (finger == "Pinky") finger = "Little";
    std::string joint = m[2];
    if (joint.back() == '1' || joint == "proximal") joint = "Proximal";
    else if (joint.back() == '2' || joint == "intermediate") joint = "Intermediate";
    else joint = "Distal";
    return finger + joint;
}

// MikuMikuDance bone names, stored as UTF-8 bytes so the source stays ASCII
std::string MatchMmd(const std::string& name) {
    static const std::map<std::string, std::string> body = {
        { "\xE3\x82\xBB\xE3\x83\xB3\xE3\x82\xBF\xE3\x83\xBC", "Hips" },
        { "\xE8\x85\xB0", "Hips" },
        { "\xE4\xB8\x8B\xE5\x8D\x8A\xE8\xBA\xAB", "Hips" },
        { "\xE4\xB8\x8A\xE5\x8D\x8A\xE8\xBA\xAB", "Spine" },
        { "\xE4\xB8\x8A\xE5\x8D\x8A\xE8\xBA\xAB" "2" "", "Chest" },
        { "\xE4\xB8\x8A\xE5\x8D\x8A\xE8\xBA\xAB" "3" "", "UpperChest" },
        { "\xE9\xA6\x96", "Neck" },
        { "\xE9\xA0\xAD", "Head" },
    };
    static const std::map<std::string, std::string> sided = {
        { "\xE8\x82\xA9", "Shoulder" },
        { "\xE8\x85\x95", "UpperArm" },
        { "\xE3\x81\xB2\xE3\x81\x98", "LowerArm" },
        { "\xE6\x89\x8B\xE9\xA6\x96", "Hand" },
        { "\xE8\xB6\xB3", "UpperLeg" },
        { "\xE3\x81\xB2\xE3\x81\x96", "LowerLeg" },
        { "\xE8\xB6\xB3\xE9\xA6\x96", "Foot" },
        { "\xE3\x81\xA4\xE3\x81\xBE\xE5\x85\x88", "Toes" },
        { "\xE4\xBA\xBA\xE6\x8C\x87" "1" "", "IndexProximal" },
        { "\xE4\xBA\xBA\xE6\x8C\x87\xEF\xBC\x91", "IndexProximal" },
        { "\xE4\xBA\xBA\xE6\x8C\x87" "2" "", "IndexIntermediate" },
        { "\xE4\xBA\xBA\xE6\x8C\x87\xEF\xBC\x92", "IndexIntermediate" },
        { "\xE4\xBA\xBA\xE6\x8C\x87" "3" "", "IndexDistal" },
        { "\xE4\xBA\xBA\xE6\x8C\x87\xEF\xBC\x93", "IndexDistal" },
        { "\xE4\xB8\xAD\xE6\x8C\x87" "1" "", "MiddleProximal" },
        { "\xE4\xB8\xAD\xE6\x8C\x87\xEF\xBC\x91", "MiddleProximal" },
        { "\xE4\xB8\xAD\xE6\x8C\x87" "2" "", "MiddleIntermediate" },
        { "\xE4\xB8\xAD\xE6\x8C\x87\xEF\xBC\x92", "MiddleIntermediate" },
        { "\xE4\xB8\xAD\xE6\x8C\x87" "3" "", "MiddleDistal" },
        { "\xE4\xB8\xAD\xE6\x8C\x87\xEF\xBC\x93", "MiddleDistal" },
        { "\xE8\x96\xAC\xE6\x8C\x87" "1" "", "RingProximal" },
        { "\xE8\x96\xAC\xE6\x8C\x87\xEF\xBC\x91", "RingProximal" },
        { "\xE8\x96\xAC\xE6\x8C\x87" "2" "", "RingIntermediate" },
        { "\xE8\x96\xAC\xE6\x8C\x87\xEF\xBC\x92", "RingIntermediate" },
        { "\xE8\x96\xAC\xE6\x8C\x87" "3" "", "RingDistal" },
        { "\xE8\x96\xAC\xE6\x8C\x87\xEF\xBC\x93", "RingDistal" },
        { "\xE5\xB0\x8F\xE6\x8C\x87" "1" "", "LittleProximal" },
        { "\xE5\xB0\x8F\xE6\x8C\x87\xEF\xBC\x91", "LittleProximal" },
        { "\xE5\xB0\x8F\xE6\x8C\x87" "2" "", "LittleIntermediate" },
        { "\xE5\xB0\x8F\xE6\x8C\x87\xEF\xBC\x92", "LittleIntermediate" },
        { "\xE5\xB0\x8F\xE6\x8C\x87" "3" "", "LittleDistal" },
        { "\xE5\xB0\x8F\xE6\x8C\x87\xEF\xBC\x93", "LittleDistal" },
        { "\xE8\xA6\xAA\xE6\x8C\x87" "0" "", "ThumbProximal" },
        { "\xE8\xA6\xAA\xE6\x8C\x87\xEF\xBC\x90", "ThumbProximal" },
        { "\xE8\xA6\xAA\xE6\x8C\x87" "1" "", "ThumbIntermediate" },
        { "\xE8\xA6\xAA\xE6\x8C\x87\xEF\xBC\x91", "ThumbIntermediate" },
        { "\xE8\xA6\xAA\xE6\x8C\x87" "2" "", "ThumbDistal" },
        { "\xE8\xA6\xAA\xE6\x8C\x87\xEF\xBC\x92", "ThumbDistal" },
    };
    if (auto found = body.find(name); found != body.end()) return found->second;
    const char* side = name.starts_with("\xE5\xB7\xA6") ? "Left" : name.starts_with("\xE5\x8F\xB3") ? "Right" : nullptr;
    if (!side) return {};
    if (auto found = sided.find(name.substr(3)); found != sided.end()) return side + found->second;
    return {};
}

// ---------------------------------------------------------------- runtime data
struct RuntimeBone { std::string name, path; glm::quat tpose{ 1, 0, 0, 0 }; };
struct RuntimeAvatar {
    std::vector<RuntimeBone> bones;
    glm::quat basis{ 1, 0, 0, 0 };
    float hipsHeight = 1;
    std::string hipsPath;
    fs::file_time_type stamp{};
};

const RuntimeAvatar* LoadAvatar(const std::string& path) {
    static std::unordered_map<std::string, RuntimeAvatar> cache;
    std::error_code ec;
    auto absolute = AssetManager::ToAbsolute(path);
    auto stamp = fs::last_write_time(absolute, ec);
    auto found = cache.find(path);
    if (found != cache.end() && (ec || found->second.stamp == stamp)) return &found->second;
    json data = json::parse(FileSystem::ReadText(absolute), nullptr, false);
    if (!data.is_object() || !data.contains("tpose")) return nullptr;
    RuntimeAvatar avatar;
    avatar.stamp = stamp;
    avatar.basis = QuatFrom(data.at("basis"));
    avatar.hipsHeight = std::max(1e-4f, data.value("hipsHeight", 1.0f));
    const json& mapped = data.at("bones");
    for (const std::string& name : AvatarSystem::HumanBones()) {
        if (!mapped.contains(name) || !data["tpose"].contains(name)) continue;
        avatar.bones.push_back({ name, mapped[name].get<std::string>(), QuatFrom(data["tpose"][name]) });
    }
    avatar.hipsPath = mapped.value("Hips", std::string());
    return &(cache[path] = std::move(avatar));
}

struct HumanTrack { std::string bone; std::vector<float> times; std::vector<glm::quat> values; };
struct HumanClip {
    std::vector<HumanTrack> tracks;
    std::unordered_map<std::string, int> byBone;
    std::vector<float> hipsTimes;
    std::vector<glm::vec3> hips;
};

const HumanClip& CompileClip(const json& clip) {
    static std::unordered_map<std::string, HumanClip> cache;
    std::string id = clip.value("bakeId", std::string());
    auto found = cache.find(id);
    if (found != cache.end() && !id.empty()) return found->second;
    HumanClip compiled;
    for (const json& entry : clip.value("human", json::array())) {
        HumanTrack track;
        track.bone = entry.value("bone", std::string());
        for (const json& key : entry.at("rotation")) {
            track.times.push_back(key[0].get<float>());
            track.values.push_back(glm::quat(key[4].get<float>(), key[1].get<float>(), key[2].get<float>(), key[3].get<float>()));
        }
        compiled.byBone[track.bone] = (int)compiled.tracks.size();
        compiled.tracks.push_back(std::move(track));
    }
    for (const json& key : clip.value("hips", json::array())) {
        compiled.hipsTimes.push_back(key[0].get<float>());
        compiled.hips.push_back({ key[1].get<float>(), key[2].get<float>(), key[3].get<float>() });
    }
    return cache[id] = std::move(compiled);
}

template<typename T, typename Lerp>
T SampleKeys(const std::vector<float>& times, const std::vector<T>& values, float t, Lerp&& lerp) {
    auto after = std::upper_bound(times.begin(), times.end(), t);
    if (after == times.begin()) return values.front();
    if (after == times.end()) return values.back();
    size_t b = after - times.begin(), a = b - 1;
    return lerp(values[a], values[b], (t - times[a]) / std::max(1e-6f, times[b] - times[a]));
}

EntityID Resolve(Scene& scene, EntityID root, const std::string& path) {
    EntityID target = root;
    std::stringstream parts(path);
    std::string segment;
    while (target && std::getline(parts, segment, '/')) {
        if (segment.empty()) continue;
        EntityID child = kNullEntity;
        for (EntityID c : scene.GetChildren(target))
            if (scene.Reg().Get<NameComponent>(c).name == segment) { child = c; break; }
        target = child;
    }
    return target;
}

// Keeps only the keys that interpolation cannot reproduce within `error(first, last, between)`
template<typename Error>
std::vector<int> ReduceKeys(int count, Error&& exceeds) {
    std::vector<int> kept;
    if (count == 0) return kept;
    kept.push_back(0);
    int anchor = 0;
    for (int candidate = 2; candidate < count; ++candidate) {
        bool bad = false;
        for (int between = anchor + 1; between < candidate && !bad; ++between) bad = exceeds(anchor, candidate, between);
        if (bad) { anchor = candidate - 1; kept.push_back(anchor); }
    }
    if (count > 1) kept.push_back(count - 1);
    return kept;
}

json BakeVectorChannel(const std::vector<float>& times, const std::vector<glm::vec3>& values, float tolerance) {
    auto indices = ReduceKeys((int)values.size(), [&](int a, int b, int mid) {
        float k = (times[mid] - times[a]) / std::max(1e-6f, times[b] - times[a]);
        return glm::length(glm::mix(values[a], values[b], k) - values[mid]) > tolerance;
    });
    json keys = json::array();
    for (int i : indices) keys.push_back({ times[i], values[i].x, values[i].y, values[i].z });
    return keys;
}

} // namespace

// ---------------------------------------------------------------- settings
FbxImportSettings FbxImportSettings::Load(const std::string& asset) {
    FbxImportSettings s;
    json j = AssetMeta::Read(asset, "model");
    if (j.empty()) return s;
    s.animationType = j.value("animationType", (int)Generic);
    s.avatar = j.value("avatar", std::string());
    s.inPlace = j.value("inPlace", false);
    if (j.contains("loop")) for (auto& [take, value] : j["loop"].items()) s.loop[take] = value.get<bool>();
    return s;
}
bool FbxImportSettings::Save(const std::string& asset) const {
    json loops = json::object();
    for (auto& [take, value] : loop) loops[take] = value;
    return AssetMeta::Write(asset, "model", json({ { "animationType", animationType }, { "avatar", avatar }, { "inPlace", inPlace }, { "loop", loops } }));
}

// ---------------------------------------------------------------- avatar authoring
const std::vector<std::string>& AvatarSystem::HumanBones() {
    static const std::vector<std::string> bones = [] {
        std::vector<std::string> list = { "Hips", "Spine", "Chest", "UpperChest", "Neck", "Head" };
        for (const char* side : { "Left", "Right" })
            for (const char* part : { "Shoulder", "UpperArm", "LowerArm", "Hand" }) list.push_back(std::string(side) + part);
        for (const char* side : { "Left", "Right" })
            for (const char* part : { "UpperLeg", "LowerLeg", "Foot", "Toes" }) list.push_back(std::string(side) + part);
        for (const char* side : { "Left", "Right" })
            for (const char* finger : { "Thumb", "Index", "Middle", "Ring", "Little" })
                for (const char* joint : { "Proximal", "Intermediate", "Distal" }) list.push_back(std::string(side) + finger + joint);
        return list;
    }();
    return bones;
}

bool AvatarSystem::IsRequired(const std::string& bone) {
    static const std::set<std::string> required = { "Hips", "Spine", "Head", "LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand",
                                                    "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot" };
    return required.count(bone) > 0;
}

std::vector<std::string> AvatarSystem::SkeletonPaths(const std::string& fbx) { return BonePaths(AssetManager::GetFbxSkeleton(fbx)); }

json AvatarSystem::AutoMap(const std::string& fbx) {
    auto bones = AssetManager::GetFbxSkeleton(fbx);
    auto paths = BonePaths(bones);
    json mapped = json::object();
    for (size_t i = 0; i < bones.size(); ++i) {
        if (std::string mmd = MatchMmd(bones[i].name); !mmd.empty()) { if (!mapped.contains(mmd)) mapped[mmd] = paths[i]; continue; }
        Parsed parsed = ParseBoneName(bones[i].name);
        std::string side = parsed.side == 'L' ? "Left" : parsed.side == 'R' ? "Right" : "";
        std::string human;
        for (auto& [name, aliases] : Aliases())
            if (std::find(aliases.begin(), aliases.end(), parsed.core) != aliases.end()) { human = name; break; }
        if (human.empty()) human = MatchFinger(parsed.core);
        if (human.empty()) continue;
        bool sided = human != "Hips" && human != "Spine" && human != "Chest" && human != "UpperChest" && human != "Neck" && human != "Head";
        if (sided == side.empty()) continue;
        std::string full = side + human;
        if (!mapped.contains(full)) mapped[full] = paths[i]; // the first match is the one nearest the root
    }
    return mapped;
}

bool AvatarSystem::Rebuild(json& avatar, std::string* error) {
    auto fail = [&](const std::string& message) { if (error) *error = message; return false; };
    auto bones = AssetManager::GetFbxSkeleton(avatar.value("source", std::string()));
    if (bones.empty()) return fail("The source model has no skeleton.");
    auto paths = BonePaths(bones);
    std::unordered_map<std::string, int> indexOfPath;
    for (int i = 0; i < (int)paths.size(); ++i) indexOfPath[paths[i]] = i;

    const json& mapped = avatar["bones"];
    std::unordered_map<std::string, int> index;
    for (const std::string& name : HumanBones()) {
        if (!mapped.contains(name) || !mapped[name].is_string()) { if (IsRequired(name)) return fail("Missing required bone: " + name); continue; }
        auto found = indexOfPath.find(mapped[name].get<std::string>());
        if (found == indexOfPath.end()) { if (IsRequired(name)) return fail("Bone not found in the model: " + name); continue; }
        index[name] = found->second;
    }
    auto position = [&](const std::string& n) { return glm::vec3(bones[index.at(n)].world[3]); };

    // Canonical frame: X towards the character's left, Y up the body, Z in front
    glm::vec3 x = glm::normalize(position("LeftUpperArm") - position("RightUpperArm"));
    glm::vec3 y = position("Head") - position("Hips");
    y = glm::normalize(y - x * glm::dot(y, x));
    glm::vec3 z = glm::cross(x, y);
    glm::quat basis = glm::normalize(glm::quat_cast(glm::transpose(glm::mat3(x, y, z))));

    std::unordered_map<int, std::string> nameOfIndex;
    for (auto& [name, i] : index) nameOfIndex[i] = name;
    auto mappedParent = [&](const std::string& name) -> std::string {
        for (int p = bones[index.at(name)].parent; p >= 0; p = bones[p].parent)
            if (auto found = nameOfIndex.find(p); found != nameOfIndex.end()) return found->second;
        return {};
    };

    // Straighten limbs so every avatar is measured from the same T-pose
    struct Segment { const char* child; glm::vec3 direction; };
    std::map<std::string, Segment> segments = {
        { "LeftUpperArm", { "LeftLowerArm", x } }, { "LeftLowerArm", { "LeftHand", x } }, { "LeftHand", { "LeftMiddleProximal", x } },
        { "RightUpperArm", { "RightLowerArm", -x } }, { "RightLowerArm", { "RightHand", -x } }, { "RightHand", { "RightMiddleProximal", -x } },
        { "LeftUpperLeg", { "LeftLowerLeg", -y } }, { "LeftLowerLeg", { "LeftFoot", -y } },
        { "RightUpperLeg", { "RightLowerLeg", -y } }, { "RightLowerLeg", { "RightFoot", -y } },
    };
    std::unordered_map<std::string, glm::quat> correction;
    std::unordered_map<std::string, glm::vec3> straightened;
    json tpose = json::object();
    for (const std::string& name : HumanBones()) {
        if (!index.count(name)) continue;
        std::string parent = mappedParent(name);
        glm::quat inherited = parent.empty() ? glm::quat(1, 0, 0, 0) : correction[parent];
        glm::vec3 p = parent.empty() ? position(name) : straightened[parent] + inherited * (position(name) - position(parent));
        straightened[name] = p;
        glm::quat fix = inherited;
        if (auto segment = segments.find(name); segment != segments.end() && index.count(segment->second.child)) {
            glm::vec3 toChild = inherited * (position(segment->second.child) - position(name));
            if (glm::length(toChild) > 1e-6f) fix = RotationBetween(glm::normalize(toChild), segment->second.direction) * inherited;
        }
        correction[name] = fix;
        tpose[name] = ToJson(glm::normalize(fix * RotationOf(bones[index.at(name)].world)));
    }
    float hipsHeight = (basis * position("Hips")).y;
    if (hipsHeight <= 1e-4f) hipsHeight = std::max(1e-3f, glm::length(position("Head") - position("Hips")));
    avatar["basis"] = ToJson(basis);
    avatar["hipsHeight"] = hipsHeight;
    avatar["tpose"] = std::move(tpose);
    return true;
}

std::string AvatarSystem::CreateFromFbx(const std::string& fbx, std::string* error) {
    json avatar = { { "source", fbx }, { "bones", AutoMap(fbx) } };
    if (!Rebuild(avatar, error)) return {};
    fs::path out = AssetManager::ToAbsolute(fbx);
    out.replace_extension(".avatar");
    FileSystem::WriteText(out, avatar.dump(2));
    return AssetManager::ToRelative(out);
}

// ---------------------------------------------------------------- clip baking
bool AvatarSystem::TakeHasMotion(const std::string& fbx, const FbxTakeDesc& take) {
    const int samples = 8;
    auto first = AssetManager::GetFbxSkeleton(fbx, 0.0f, take.name);
    for (int i = 1; i < samples; ++i) {
        auto pose = AssetManager::GetFbxSkeleton(fbx, take.duration * i / samples, take.name);
        if (pose.size() != first.size()) return true;
        for (size_t bone = 0; bone < pose.size(); ++bone) {
            const float reference = std::max(1e-4f, glm::length(glm::vec3(first[bone].local[3])));
            if (glm::length(glm::vec3(pose[bone].local[3]) - glm::vec3(first[bone].local[3])) > reference * 0.002f) return true;
            float dot = std::min(1.0f, std::abs(glm::dot(RotationOf(pose[bone].local), RotationOf(first[bone].local))));
            if (2.0f * std::acos(dot) > glm::radians(0.25f)) return true;
        }
    }
    return false;
}

std::vector<std::string> AvatarSystem::ImportClips(const std::string& fbx, std::string* error) {
    std::vector<std::string> written;
    auto fail = [&](const std::string& message) { if (error) *error = message; return written; };
    auto settings = FbxImportSettings::Load(fbx);
    auto takes = AssetManager::GetFbxTakes(fbx);
    if (takes.empty()) return fail("This model contains no animation.");
    auto bindBones = AssetManager::GetFbxSkeleton(fbx);
    if (bindBones.empty()) return fail("This model has no skeleton.");
    auto paths = BonePaths(bindBones);

    bool humanoid = settings.animationType == FbxImportSettings::Humanoid;
    json avatar;
    std::vector<std::pair<std::string, int>> humanBones;
    glm::quat basis(1, 0, 0, 0);
    float hipsHeight = 1;
    int hipsIndex = -1;
    if (humanoid) {
        if (settings.avatar.empty()) return fail("Create or assign an Avatar first.");
        avatar = json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(settings.avatar)), nullptr, false);
        if (!avatar.is_object() || !avatar.contains("tpose")) return fail("The Avatar asset is not valid.");
        basis = QuatFrom(avatar["basis"]);
        hipsHeight = avatar.value("hipsHeight", 1.0f);
        for (const std::string& name : HumanBones()) {
            if (!avatar["bones"].contains(name) || !avatar["tpose"].contains(name)) continue;
            auto path = avatar["bones"][name].get<std::string>();
            auto found = std::find(paths.begin(), paths.end(), path);
            if (found == paths.end()) continue;
            humanBones.push_back({ name, (int)(found - paths.begin()) });
            if (name == "Hips") hipsIndex = humanBones.back().second;
        }
        if (hipsIndex < 0) return fail("The Avatar has no Hips bone.");
    }

    std::vector<FbxTakeDesc> moving;
    for (const FbxTakeDesc& take : takes) if (TakeHasMotion(fbx, take)) moving.push_back(take);

    fs::path source = AssetManager::ToAbsolute(fbx);
    for (const FbxTakeDesc& take : moving.empty() ? takes : moving) {
        const int frames = std::max(2, (int)std::ceil(take.duration * kBakeRate) + 1);
        std::vector<float> times(frames);
        std::vector<std::vector<FbxBoneDesc>> poses(frames);
        for (int f = 0; f < frames; ++f) {
            times[f] = std::min(take.duration, f / kBakeRate);
            poses[f] = AssetManager::GetFbxSkeleton(fbx, std::min(take.duration - 1e-4f, times[f]), take.name);
            if (poses[f].size() != bindBones.size()) return fail("Could not evaluate take: " + take.name);
        }

        json clip = { { "sampleRate", (int)kBakeRate }, { "duration", std::max(1.0f / kBakeRate, take.duration) }, { "tracks", json::array() } };
        clip["loop"] = !settings.loop.count(take.name) || settings.loop[take.name];
        if (humanoid) {
            clip["humanoid"] = true;
            clip["sourceAvatar"] = settings.avatar;
            clip["bakeId"] = std::to_string(GenerateUUID());
            clip["human"] = json::array();
            const float angle = glm::radians(0.15f);
            for (auto& [name, bone] : humanBones) {
                glm::quat tpose = glm::normalize(QuatFrom(avatar["tpose"][name]));
                std::vector<glm::quat> values(frames);
                for (int f = 0; f < frames; ++f) {
                    glm::quat q = glm::normalize(basis * RotationOf(poses[f][bone].world) * glm::inverse(tpose) * glm::inverse(basis));
                    if (f && glm::dot(values[f - 1], q) < 0) q = -q;
                    values[f] = q;
                }
                auto indices = ReduceKeys(frames, [&](int a, int b, int mid) {
                    float k = (times[mid] - times[a]) / std::max(1e-6f, times[b] - times[a]);
                    float d = std::min(1.0f, std::abs(glm::dot(glm::slerp(values[a], values[b], k), values[mid])));
                    return 2.0f * std::acos(d) > angle;
                });
                json keys = json::array();
                for (int i : indices) keys.push_back({ times[i], values[i].x, values[i].y, values[i].z, values[i].w });
                clip["human"].push_back({ { "bone", name }, { "rotation", std::move(keys) } });
            }
            std::vector<glm::vec3> hips(frames);
            for (int f = 0; f < frames; ++f) hips[f] = (basis * glm::vec3(poses[f][hipsIndex].world[3])) / hipsHeight;
            if (settings.inPlace) { glm::vec3 start = hips[0]; for (auto& p : hips) { p.x -= start.x; p.z -= start.z; } }
            clip["hips"] = BakeVectorChannel(times, hips, 0.0005f);
        } else {
            for (size_t bone = 0; bone < bindBones.size(); ++bone) {
                std::vector<glm::vec3> position(frames), euler(frames), scale(frames);
                for (int f = 0; f < frames; ++f) {
                    const glm::mat4& m = poses[f][bone].local;
                    position[f] = glm::vec3(m[3]);
                    scale[f] = { glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) };
                    euler[f] = glm::degrees(glm::eulerAngles(RotationOf(m)));
                    if (f) for (int a = 0; a < 3; ++a) euler[f][a] += 360.0f * std::round((euler[f - 1][a] - euler[f][a]) / 360.0f);
                }
                json track = { { "path", paths[bone] } };
                // A channel can be dropped only if it never leaves the pose the bone already has when imported
                const glm::mat4& bind = bindBones[bone].local;
                const glm::quat bindRotation = RotationOf(bind);
                const glm::vec3 bindPosition(bind[3]), bindScale(glm::length(glm::vec3(bind[0])), glm::length(glm::vec3(bind[1])), glm::length(glm::vec3(bind[2])));
                float unit = std::max(1e-5f, glm::length(bindPosition) * 0.001f);
                bool movesPosition = false, movesRotation = false, movesScale = false;
                for (int f = 0; f < frames; ++f) {
                    const glm::mat4& m = poses[f][bone].local;
                    movesPosition |= glm::length(position[f] - bindPosition) > unit;
                    movesScale |= glm::length(scale[f] - bindScale) > 0.0005f;
                    movesRotation |= 2.0f * std::acos(std::min(1.0f, std::abs(glm::dot(RotationOf(m), bindRotation)))) > glm::radians(0.05f);
                }
                if (movesPosition) track["position"] = BakeVectorChannel(times, position, unit);
                if (movesRotation) track["rotation"] = BakeVectorChannel(times, euler, 0.1f);
                if (movesScale) track["scale"] = BakeVectorChannel(times, scale, 0.001f);
                if (track.size() > 1) clip["tracks"].push_back(std::move(track));
            }
        }

        std::string stem = FileSystem::Utf8(source.stem());
        std::string fileName = takes.size() == 1 ? stem : stem + " - " + SafeName(take.name);
        fs::path out = source.parent_path() / fs::u8path(fileName + ".anim");
        FileSystem::WriteText(out, clip.dump());
        std::string relative = AssetManager::ToRelative(out);
        AssetManager::Invalidate(relative);
        written.push_back(relative);
    }
    return written;
}

// ---------------------------------------------------------------- runtime
void AvatarSystem::Apply(Scene& scene, EntityID root, const json& clip, float time, bool loop) {
    auto* animator = scene.Reg().TryGet<AnimatorComponent>(root);
    if (!animator || animator->avatar.empty()) return;
    const RuntimeAvatar* avatar = LoadAvatar(animator->avatar);
    if (!avatar) return;
    const HumanClip& human = CompileClip(clip);
    const float duration = std::max(1e-4f, clip.value("duration", 1.0f));
    const float t = loop ? std::fmod(std::max(0.0f, time), duration) : std::clamp(time, 0.0f, duration);

    const glm::mat4 rootInverse = glm::inverse(scene.GetWorldMatrix(root));
    const glm::quat toModel = glm::inverse(avatar->basis);
    for (const RuntimeBone& bone : avatar->bones) {
        auto track = human.byBone.find(bone.name);
        if (track == human.byBone.end()) continue;
        const HumanTrack& keys = human.tracks[track->second];
        if (keys.values.empty()) continue;
        EntityID entity = Resolve(scene, root, bone.path);
        if (!entity) continue;
        glm::quat delta = SampleKeys(keys.times, keys.values, t, [](const glm::quat& a, const glm::quat& b, float k) { return glm::slerp(a, b, k); });
        glm::quat target = glm::normalize(toModel * delta * avatar->basis * bone.tpose);
        EntityID parent = scene.GetParent(entity);
        glm::mat4 parentModel = rootInverse * scene.GetWorldMatrix(parent);
        auto& transform = scene.Reg().Get<TransformComponent>(entity);
        transform.rotation = glm::normalize(glm::inverse(RotationOf(parentModel)) * target);
        transform.eulerHint = glm::degrees(glm::eulerAngles(transform.rotation));
        if (bone.name == "Hips" && !human.hips.empty()) {
            glm::vec3 normalized = SampleKeys(human.hipsTimes, human.hips, t, [](const glm::vec3& a, const glm::vec3& b, float k) { return glm::mix(a, b, k); });
            transform.position = glm::vec3(glm::inverse(parentModel) * glm::vec4(toModel * (normalized * avatar->hipsHeight), 1.0f));
        }
    }
}

} // namespace Stardust
