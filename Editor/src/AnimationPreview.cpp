// Editor preview of clips and controllers. Runs on a copy of the scene, so nothing is saved by accident.
#include "EditorApp.h"
#include "Animation/AnimatorSystem.h"
#include "Core/FileSystem.h"
#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>

namespace Stardust {

void EditorApp::StartAnimationPreview(EntityID root, const std::string& asset) {
    if (m_State != PlayState::Edit || !ActiveScene().Reg().Valid(root)) return;
    StopAnimationPreview();
    auto uuid = ActiveScene().Reg().Get<IDComponent>(root).uuid;
    m_AnimationPreviewScene = Scene::Copy(m_ActiveScene);
    m_AnimationPreviewRoot = m_AnimationPreviewScene->FindByUUID(uuid);
    m_AnimationPreviewAsset = asset;
    m_AnimationPreviewPlaying = true;
    auto& reg = m_AnimationPreviewScene->Reg();
    // Only the previewed object animates
    reg.Each<AnimatorComponent>([&](EntityID e, AnimatorComponent& a) { if (e != m_AnimationPreviewRoot) a.playing = false; });
    if (asset.ends_with(".controller")) {
        auto& animator = reg.Has<AnimatorComponent>(m_AnimationPreviewRoot) ? reg.Get<AnimatorComponent>(m_AnimationPreviewRoot)
                                                                             : reg.Add<AnimatorComponent>(m_AnimationPreviewRoot);
        animator.controller = asset;
        animator.state.clear();
    }
    if (auto* animator = reg.TryGet<AnimatorComponent>(m_AnimationPreviewRoot)) {
        animator->time = 0;
        animator->previousState.clear();
        m_AnimationPreviewSpeed = animator->speed;
    }
}

void EditorApp::StopAnimationPreview() {
    m_AnimationRecording = false;
    m_AnimationRecordTime = -1;
    m_AnimationPreviewScene.reset();
    m_AnimationPreviewRoot = 0;
    m_AnimationPreviewTime = 0;
    m_AnimationPreviewState.clear();
    m_AnimationPreviewJson.clear();
    m_AnimationPreviewAsset.clear();
    m_AnimationPreviewPlaying = false;
}

void EditorApp::UpdateAnimationPreview(float dt) {
    if (!m_AnimationPreviewScene) return;
    if (m_State != PlayState::Edit) { StopAnimationPreview(); return; }
    auto& scene = *m_AnimationPreviewScene;
    auto root = m_AnimationPreviewRoot;
    if (!scene.Reg().Valid(root)) { StopAnimationPreview(); return; }
    try {
        if (m_AnimationPreviewAsset.ends_with(".anim")) {
            static std::string parsedText;
            static nlohmann::json clip;
            if (parsedText != m_AnimationPreviewJson || !clip.is_object()) {
                parsedText = m_AnimationPreviewJson;
                clip = nlohmann::json::parse(parsedText.empty() ? FileSystem::ReadText(AssetManager::ToAbsolute(m_AnimationPreviewAsset)) : parsedText);
            }
            if (m_AnimationPreviewPlaying) {
                float duration = std::max(0.001f, clip.value("duration", 1.0f));
                m_AnimationPreviewTime = std::fmod(std::max(0.0f, m_AnimationPreviewTime + dt * m_AnimationPreviewSpeed), duration);
            }
            // While recording and paused, the user's edits on the preview must not be overwritten
            if (m_AnimationRecording && !m_AnimationPreviewPlaying && m_AnimationRecordTime == m_AnimationPreviewTime) return;
            m_AnimationRecordTime = m_AnimationPreviewTime;
            AnimatorSystem::SampleClip(scene, root, clip, m_AnimationPreviewTime, m_AnimationPreviewPlaying);
            return;
        }
        // Controllers: the real state machine, including blends. The editors autosave, so it reads current data.
        auto* animator = scene.Reg().TryGet<AnimatorComponent>(root);
        if (!animator) { StopAnimationPreview(); return; }
        if (!m_AnimationPreviewState.empty() && m_AnimationPreviewState != animator->state) {
            animator->state = m_AnimationPreviewState;
            animator->previousState.clear();
        }
        animator->playing = true;
        animator->speed = m_AnimationPreviewSpeed;
        animator->time = m_AnimationPreviewTime;
        AnimatorSystem::Update(scene, m_AnimationPreviewPlaying ? dt : 0.0f);
        m_AnimationPreviewTime = animator->time;
        m_AnimationPreviewState = animator->state;
        if (!animator->playing) m_AnimationPreviewPlaying = false;
    } catch (const std::exception& e) {
        SD_WARN("Animation preview: {}", e.what());
        m_AnimationPreviewPlaying = false;
    }
}

} // namespace Stardust
