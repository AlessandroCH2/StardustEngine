#include "EditorApp.h"
#include "Scene/PrefabSystem.h"
#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "Scripting/ScriptEngine.h"
namespace Stardust {
void EditorApp::OpenPrefab(const std::filesystem::path& path){
    if(m_State!=PlayState::Edit)Stop();if(!m_PrefabAsset.empty())ExitPrefabStage();
    auto scene=SceneSerializer::Load(path);if(!scene)return;
    m_PrefabReturnScene=m_EditorScene;m_PrefabReturnPath=m_ScenePath;m_PrefabReturnSelection=m_EditorScene->Reg().Valid(m_Selected)?m_EditorScene->Reg().Get<IDComponent>(m_Selected).uuid:0;
    m_EditorScene=scene;m_ActiveScene=scene;m_PrefabAsset=path;m_ScenePath.clear();m_Selected=scene->GetRoots().empty()?0:scene->GetRoots()[0];m_InspectAsset=false;m_Undo.clear();m_Redo.clear();m_UndoSnapshot.clear();m_SceneDirty=false;scene->SetName("Prefab: "+path.stem().string());ScriptEngine::SetEditorScene(scene.get());FocusSelected();UpdateTitle();
}
void EditorApp::SavePrefabStage(){
    if(m_PrefabAsset.empty()||!m_EditorScene)return;auto roots=m_EditorScene->GetRoots();
    if(roots.size()!=1){SD_ERROR("A prefab needs exactly one root object");return;}
    if(SceneSerializer::SavePrefab(*m_EditorScene,roots[0],m_PrefabAsset)){m_SceneDirty=false;if(m_PrefabReturnScene)PrefabSystem::Refresh(m_PrefabReturnScene);}
}
void EditorApp::ExitPrefabStage(){
    if(m_PrefabAsset.empty())return;SavePrefabStage();m_EditorScene=m_PrefabReturnScene;m_ActiveScene=m_EditorScene;m_ScenePath=m_PrefabReturnPath;m_Selected=m_EditorScene->FindByUUID(m_PrefabReturnSelection);m_PrefabAsset.clear();m_PrefabReturnScene.reset();m_Undo.clear();m_Redo.clear();m_UndoSnapshot.clear();m_SceneDirty=true;ScriptEngine::SetEditorScene(m_EditorScene.get());UpdateTitle();
}
void EditorApp::UpdatePrefabs(){
    if(m_State!=PlayState::Edit||!m_EditorScene)return;
    if(std::exchange(m_PrefabClosePending,false)&&!m_PrefabAsset.empty()){
        if(m_EditorScene->GetRoots().size()!=1)SD_ERROR("A prefab needs exactly one root object before it can be closed");
        else{ExitPrefabStage();return;}
    }
    if(m_PrefabAction){auto e=m_EditorScene->FindByUUID(m_PrefabTarget);int action=std::exchange(m_PrefabAction,0);if(e)if(auto* link=m_EditorScene->Reg().TryGet<PrefabInstanceComponent>(e)){
        auto asset=link->asset;
        if(action==1){OpenPrefab(AssetManager::ToAbsolute(asset));return;}
        if(action==2)PrefabSystem::Revert(*m_EditorScene,e);
        if(action==3)SceneSerializer::SavePrefab(*m_EditorScene,e,AssetManager::ToAbsolute(asset));
        if(action==4){m_EditorScene->Reg().Remove<PrefabInstanceComponent>(e);m_EditorScene->Reg().Get<NameComponent>(e).prefab.clear();}
    }}
    if(!m_PrefabAsset.empty())return;m_PrefabPoll+=DeltaTime();if(m_PrefabPoll<.5f)return;m_PrefabPoll=0;
    u64 selected=m_EditorScene->Reg().Valid(m_Selected)?m_EditorScene->Reg().Get<IDComponent>(m_Selected).uuid:0;
    if(PrefabSystem::Refresh(m_EditorScene)){m_ActiveScene=m_EditorScene;m_Selected=m_EditorScene->FindByUUID(selected);m_SceneDirty=true;}
}
}
