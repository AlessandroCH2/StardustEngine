#include "Physics/PhysicsWorld.h"
#include "MemoryDevice.h"
#include "Animation/ModelSkeleton.h"
#include "Renderer/Nature/TerrainNeighbors.h"
#include "Project/Project.h"
#include "Project/BuildCooker.h"
#include "Scene/SceneManager.h"
#include <thread>
#include "Scene/Scene.h"
#include "Core/FileSystem.h"
#include "Core/Input.h"
#include "Scene/ComponentState.h"
#include "Core/Process.h"
#include "Core/WindowIcon.h"
#include <windows.h>
#undef near
#undef far
#include "Assets/Pak.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptEngine.h"
#include "Animation/AnimatorSystem.h"
#include "Project/PackageManager.h"
#include "Scene/PrefabSystem.h"
#include "UI/CanvasRenderer.h"
#include "UI/RectTransformUtils.h"
#include "UI/UIAssets.h"
#include "UI/VideoPlayer.h"
#include "Assets/TextureImport.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <ufbx.h>

using namespace Stardust;

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// All fixtures live in an isolated build workspace, never beside shipped binaries.
static std::filesystem::path s_TestRoot;
static const std::filesystem::path& TestRoot() { return s_TestRoot; }
struct TestWorkspace {
    TestWorkspace() {
        auto parent=std::filesystem::absolute(FileSystem::EngineRoot()/"build/TestWork").lexically_normal();
        s_TestRoot=parent/("RuntimeTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(s_TestRoot);
    }
    ~TestWorkspace() {
        std::error_code ec;
        auto parent=std::filesystem::absolute(FileSystem::EngineRoot()/"build/TestWork").lexically_normal();
        if(s_TestRoot.parent_path()==parent&&s_TestRoot.filename().string().starts_with("RuntimeTests-"))
            std::filesystem::remove_all(s_TestRoot,ec);
        if(ec)std::cerr<<"Test workspace cleanup failed: "<<ec.message()<<'\n';
    }
};

static void TestScenes() {
    auto file = TestRoot() / "runtime-tests.sdproject";
    FileSystem::WriteText(file, R"({"scenes":[{"path":"Scenes/Disabled.scene","enabled":false},"Scenes/Start.scene","Scenes/Next.scene"]})");
    Check(Project::Load(file), "project load failed");
    Check(Project::EnabledScenes().size() == 2, "disabled scene included");
    Check(Project::ResolveScene("0") == "Scenes/Start.scene", "startup scene index wrong");
    Check(Project::ResolveScene("Next") == "Scenes/Next.scene", "scene name resolution failed");
    Check(Project::ResolveScene("Scenes/Next.scene") == "Scenes/Next.scene", "scene path resolution failed");
    Check(Project::ResolveScene("Disabled").empty(), "disabled scene resolved");
    Check(Project::ResolveScene("9999999999999999999999999999999").empty(), "overflowing scene index accepted");
    FileSystem::WriteText(file, R"({"player":{"width":"invalid"}})");
    Check(!Project::Load(file), "malformed settings accepted");
    Check(Project::ResolveScene("0") == "Scenes/Start.scene", "failed load replaced active settings");
    std::filesystem::remove(file);
}

static void TestForces() {
    Scene scene;
    auto entity = scene.CreateEntity("Force test");
    auto& rb = scene.Reg().Add<RigidbodyComponent>(entity);
    rb.mass = 2.0f;
    rb.useGravity = false;
    scene.Reg().Add<BoxColliderComponent>(entity);
    PhysicsWorld physics(scene);
    Check(physics.HasBody(entity), "rigidbody not created");
    const ForceMode modes[] = { ForceMode::Force, ForceMode::Impulse, ForceMode::VelocityChange, ForceMode::Acceleration };
    const float expected[] = { 0.5f, 5.0f, 10.0f, 1.0f };
    for (int i = 0; i < 4; ++i) {
        physics.SetVelocity(entity, glm::vec3(0));
        physics.AddForce(entity, {10, 0, 0}, modes[i], 0.1f);
        Check(std::abs(physics.GetVelocity(entity).x - expected[i]) < 0.001f, "AddForce mass/time scaling wrong");
        physics.SetVelocity(entity, glm::vec3(0));
        physics.AddForceAtPosition(entity, {10, 0, 0}, {0, 0, 0}, modes[i], 0.1f);
        Check(std::abs(physics.GetVelocity(entity).x - expected[i]) < 0.001f, "AddForceAtPosition mass/time scaling wrong");
    }
    physics.SetVelocity(entity, glm::vec3(0));
    for (int i = 0; i < 10; ++i) physics.AddForce(entity, {10, 0, 0}, ForceMode::Force, 0.01f);
    Check(std::abs(physics.GetVelocity(entity).x - 0.5f) < 0.001f, "force depends on caller frame rate");
}

static void TestHierarchyColliders() {
    Scene scene;
    auto parent=scene.CreateEntity("Parent");
    auto child=scene.CreateEntity("Child Collider");
    scene.SetParent(child,parent,false);
    scene.Reg().Get<TransformComponent>(child).position={2,0,0};
    scene.Reg().Add<BoxColliderComponent>(child).size={2,1,1};
    PhysicsWorld physics(scene);
    RaycastHit hit;
    Check(physics.Raycast({2,3,0},{0,-1,0},10,hit) && hit.entity==child,"initial child collider missing");
    auto& transform=scene.Reg().Get<TransformComponent>(parent);
    transform.position={10,0,0}; transform.SetEuler({0,90,0});
    physics.Step(1.0f/60);
    Check(physics.Raycast({10,3,-2},{0,-1,0},10,hit) && hit.entity==child,"child collider did not inherit translation/rotation");
    Check(!physics.Raycast({2,3,0},{0,-1,0},10,hit),"child collider stayed at old position");
    transform.scale={2,2,2}; physics.Step(1.0f/60);
    Check(physics.Raycast({10,3,-5.5f},{0,-1,0},10,hit) && hit.entity==child,"child collider did not inherit scale");
}

static void TestSerialization() {
    Scene scene; auto root=scene.CreateEntity("UI"); auto child=scene.CreateEntity("Label");
    scene.SetParent(child,root,false);
    scene.Reg().Add<CanvasComponent>(root);
    scene.Reg().Add<RectTransformComponent>(child).anchoredPosition={30,40};
    scene.Reg().Add<UITextComponent>(child).text="UI survives serialization";
    auto& button=scene.Reg().Add<UIButtonComponent>(child);UIButtonAction action;action.target=scene.Reg().Get<IDComponent>(root).uuid;action.method="Test(Stardust.Entity,System.String,System.String)";action.argumentTypes={"Entity","string","string"};action.argumentsJson=nlohmann::json::array({std::format("{:016x}",scene.Reg().Get<IDComponent>(child).uuid),"ciao mondo",std::format("{:016x}",scene.Reg().Get<IDComponent>(root).uuid)}).dump();button.actions.push_back(action);
    scene.Reg().Add<AnimatorComponent>(root).controller="Animation.controller";
    auto& scripts=scene.Reg().Add<ScriptComponent>(root,"TestA");
    scripts.fieldsJson=nlohmann::json({{"Target",std::format("{:016x}",scene.Reg().Get<IDComponent>(child).uuid)}}).dump();
    scripts.additional.push_back({"TestB","{\"Value\":42}"});
    auto loaded=SceneSerializer::FromJson(SceneSerializer::ToJson(scene));
    Check(loaded && loaded->Reg().Get<ScriptComponent>(loaded->FindByName("UI")).Count()==2,"multiple scripts lost in serialization");
    Check(loaded->Reg().Get<UITextComponent>(loaded->FindByName("Label")).text=="UI survives serialization","canvas text lost");
    auto copy=scene.DuplicateEntity(root); auto duplicateChild=scene.GetChildren(copy).front();
    auto& copiedAction=scene.Reg().Get<UIButtonComponent>(duplicateChild).actions.at(0);Check(copiedAction.target==scene.Reg().Get<IDComponent>(copy).uuid&&nlohmann::json::parse(copiedAction.argumentsJson)[0]==std::format("{:016x}",scene.Reg().Get<IDComponent>(duplicateChild).uuid),"duplicated button action references point at original objects");
    Check(nlohmann::json::parse(copiedAction.argumentsJson)[1]=="ciao mondo"&&nlohmann::json::parse(copiedAction.argumentsJson)[2]==std::format("{:016x}",scene.Reg().Get<IDComponent>(root).uuid),"button string arguments incorrectly remapped as UUIDs");
    auto fields=nlohmann::json::parse(scene.Reg().Get<ScriptComponent>(copy).fieldsJson);
    Check(fields["Target"]==std::format("{:016x}",scene.Reg().Get<IDComponent>(duplicateChild).uuid),"prefab script references point at original objects");
}

static void TestPaks() {
    auto directory=TestRoot()/"PakTest";
    auto root=directory/"Source"; auto mount=directory/"Virtual";
    FileSystem::WriteText(root/"Scenes/Test.scene","mounted content");
    auto cjk=std::filesystem::path(u8"Textures/日本語中文.txt");FileSystem::WriteText(root/cjk,"UTF-8 asset");
    FileSystem::WriteText(root/"Scripts/Runtime.cs","exclude");
    FileSystem::WriteText(root/"Nested/Editor/Secret.txt","exclude");
    auto archive=directory/"content.pak";
    Check(Pak::Build(root,archive),"pak build failed");
    Check(Pak::Mount(archive,mount),"pak mount failed");auto extracted=Pak::Resolve(mount/"Scenes/Test.scene");Check(!std::filesystem::exists(extracted)&&Pak::Contains(extracted),"pak mount extracted an asset to disk");char slice[7]{};Check(Pak::ReadRange(extracted,8,slice,7)==7&&std::string(slice,7)=="content","pak range read failed");
    Project::SetRuntimeLayout(mount,{});
    Check(Project::ResolveScene("Scenes/Test.scene")=="Scenes/Test.scene","downloaded scene did not resolve");
    FileSystem::WriteText(mount/"Scenes/Loose.scene","loose scene");Check(Project::ResolveScene("Scenes/Loose.scene")=="Scenes/Loose.scene","loose path outside build list was rejected");
    Project::Config().scenes={"Scenes/Loose.scene"};Project::Config().scenesEnabled={false};Check(Project::ResolveScene("Scenes/Loose.scene")=="Scenes/Loose.scene","explicit path to disabled build entry rejected");
    Check(FileSystem::ReadText(mount/"Scenes/Test.scene")=="mounted content","mounted scene cannot be read");
    Check(FileSystem::ReadText(mount/cjk)=="UTF-8 asset","Unicode pak entry failed");
    Check(FileSystem::ReadText(mount/"Scripts/Runtime.cs").empty(),"source scripts included in pak");
    Check(FileSystem::ReadText(mount/"Nested/Editor/Secret.txt").empty(),"Editor content included in pak");
    FileSystem::WriteText(root/"Scenes/Test.scene","patched content");
    Check(Pak::Build(root,directory/"patch.pak") && Pak::Mount(directory/"patch.pak",mount),"patch pak failed");
    Check(FileSystem::ReadText(mount/"Scenes/Test.scene")=="patched content","mount precedence wrong");
    Check(Pak::Unmount(directory/"patch.pak")&&FileSystem::ReadText(mount/"Scenes/Test.scene")=="mounted content","unmount did not restore previous archive priority");Check(Pak::Mount(directory/"patch.pak",mount),"patch remount failed");
    auto corrupt=FileSystem::ReadBinary(archive); corrupt.back()^=0xff;
    { std::ofstream out(directory/"corrupt.pak",std::ios::binary); out.write((const char*)corrupt.data(),corrupt.size()); }
    Check(Pak::Mount(directory/"corrupt.pak",directory/"CorruptVirtual"),"valid index rejected before payload read");bool rejected=false;try{FileSystem::ReadText(directory/"CorruptVirtual"/cjk);}catch(...){rejected=true;}Check(rejected,"corrupt payload accepted on demand");
    Check(FileSystem::ReadText(mount/"Scenes/Test.scene")=="patched content","unrelated mount damaged patched content");
    Pak::UnmountAll();Check(!std::filesystem::exists(extracted),"pak cache survived unmount");
}

static void TestManagedScripts() {
    auto root=TestRoot()/"ScriptTest";
    FileSystem::WriteText(root/"Test.sdproject",R"({"name":"Test","assemblyName":"RuntimeFixture"})");
    Check(Project::Load(root/"Test.sdproject"),"fixture project failed");
    Check(PackageManager::EnsureDefault(),"default engine package failed");
    Check(!PackageManager::Installed().empty(),"engine package not discovered");
    Project::WriteScriptProject(Project::ScriptProjectFile(),"RuntimeFixture");
    Project::WriteEditorScriptProject();
    FileSystem::WriteText(root/"Assets/Scripts/Test.cs",R"(using Stardust;
public class TestRendering : ScriptBehaviour {
 public int Verified;public int Ticks;void OnUpdate(float dt){Ticks++;}
 void OnCreate(){
  layer=7;var camera=AddComponent<Camera>();camera.cullingMask=1u<<7;camera.SetRenderTarget("runtime:apitest",128,64);
  var sprite=AddComponent<SpriteRenderer>();sprite.sprite="UI/Test.sprite";sprite.flipX=true;sprite.sortingOrder=5;
  var mesh=AddComponent<MeshRenderer>();mesh.SetMesh(new[]{new Vector3(0,0,0),new Vector3(1,0,0),new Vector3(0,1,0)},new[]{0,1,2});mesh.roughness=.2f;mesh.castShadows=false;mesh.SetFloat("CustomGain",2.5f);mesh.SetColor("CustomTint",new Color(.2f,.3f,.4f));mesh.enabled=false;bool rendererWasDisabled=!mesh.enabled;mesh.enabled=true;
  var box=AddComponent<BoxCollider>();box.size=new Vector3(2,3,4);box.center=new Vector3(1,0,0);box.isTrigger=true;
  var environment=AddComponent<Stardust.Environment>();environment.exposure=2;environment.shadowStrength=.4f;
  var video=AddComponent<VideoPlayer>();video.loop=false;video.playbackSpeed=.5f;
  InputManager.Bind("Custom",new InputBinding{Positive=KeyCode.Space,GamepadButton=0});
  Verified=rendererWasDisabled&&layer==7&&camera.targetWidth==128&&camera.targetHeight==64&&camera.cullingMask==(1u<<7)&&sprite.flipX&&sprite.sortingOrder==5&&box.isTrigger&&box.size.y==3&&environment.exposure==2&&!video.loop&&InputManager.Bindings.ContainsKey("Custom")?1:0;
 }
}
public class TestA : ScriptBehaviour { public int Number; public string Message; public void CliccaQui(int numero,string stringa){Number=numero;Message=stringa;} public void CliccaQui(string altro){Message="wrong overload";} public int Counter; public string SceneAtCreate; void OnCreate() { Counter=10;SceneAtCreate=SceneManager.activeSceneName; } void OnUpdate(float dt) { Counter++; } void OnClick() { Counter+=100; } }
public class TestCollections : ScriptBehaviour {public System.Collections.Generic.List<int> Numbers=new(){1,2};public System.Collections.Generic.List<Entity> Peers=new();public System.Collections.Generic.Dictionary<string,Vector3> Positions=new(){{"Spawn",new Vector3(1,2,3)}};public System.Collections.Generic.Dictionary<int,System.Collections.Generic.List<TestA>> Groups=new();public System.Collections.Generic.Dictionary<string,int> Keys=new();public int References;void OnCreate(){References=Peers.Count>0&&Peers[0]!=null&&Groups.ContainsKey(7)&&Groups[7][0]!=null?1:0;}}
public class TestPersistent : ScriptBehaviour { public int Creates; public int Observed; private int secret=100; public TestA Template; public Sprite Future; public int Stage; public int SpawnNumber; public string Content; public string FilePath; void OnCreate(){Creates++;Entity.DontDestroyOnLoad(this);var image=AddComponent<Stardust.UI.Image>();image.sprite=Future;}void OnUpdate(float dt){Observed=++secret;if(Stage==1){var spawned=Entity.Instantiate(Template,new Vector3(2,3,4));SpawnNumber=spawned.Number;Content=Assets.ReadAllText(FilePath);Stage=2;}}}
public class TestB : ScriptBehaviour { public TestA Peer; public int Observed; void OnCreate() { Observed=Peer==null ? -1 : Peer.Counter; } }
public class TestUI : ScriptBehaviour { public string InputValue; public int Submitted; public int Events; void OnCreate() { var input=AddComponent<Stardust.UI.InputField>();input.text="start";input.onValueChanged.AddListener(value=>InputValue=value);input.onEndEdit.AddListener(value=>Submitted++);var text=AddComponent<Stardust.UI.Text>();text.font="Fonts/Custom.ttf";text.color=new Color(.25f,.5f,.75f,1);var rect=AddComponent<Stardust.UI.RectTransform>();rect.anchoredPosition=new Vector2(12,34);var button=AddComponent<Stardust.UI.Button>();button.onClick.AddListener(()=>Events++); } }
)");
    FileSystem::WriteText(root/"Assets/Editor/Commands.cs",R"(using Stardust.Editor;
public static class Commands { [MenuItem("Tests/Editor command")] static void Run() { Stardust.Debug.Log("Editor command executed"); } }
public class EditorBehaviour : Stardust.ScriptBehaviour { }
[CustomEditor(typeof(TestA))] public class TestInspector : Editor {public override void OnInspectorGUI(){var t=(TestA)target;t.Number=EditorGUILayout.IntField("Number",t.Number);if(GUILayout.Button("Reset"))t.Number=42;}}
public class TestWindow : EditorWindow {public int Count;public override void OnGUI(){Count=EditorGUILayout.IntField("Count",Count);if(GUILayout.Button("Increment"))Count++;} [MenuItem("Tests/Open Window")] static void OpenTool()=>GetWindow<TestWindow>("Test Tool");}

)");
    auto build=[&](const std::filesystem::path& project) {
        return RunProcess(L"dotnet build \""+project.wstring()+L"\" -c Release -o \""+(root/"bin").wstring()+L"\" -p:StardustBin=\""+FileSystem::ExecutableDir().wstring()+L"\" --nologo -v:q",root,[](const std::string& line){std::cout<<line<<'\n';});
    };
    Check(build(Project::ScriptProjectFile())==0,"runtime script compilation failed");
    Check(build(root/"RuntimeFixture.Editor.csproj")==0,"Editor Only compilation failed");
    ScriptEngine::SetEditorMode(true);
    Check(ScriptEngine::Init(),"managed ABI/hosting failed");
    auto assembly=Project::ScriptAssembly();auto compiled=std::filesystem::last_write_time(assembly);std::filesystem::last_write_time(assembly,compiled-std::chrono::hours(24));Check(ScriptEngine::NeedsBuild(),"outdated ScriptCore consumer was not scheduled for rebuild");std::filesystem::last_write_time(assembly,compiled);
    Check(!ScriptEngine::FindClass("EditorBehaviour"),"Editor Only class leaked into runtime behaviours");
    Check(ScriptEngine::EditorCommands().size()==2,"Editor Only menu discovery failed");
    ScriptEngine::RunEditorCommand("Tests/Editor command");
    auto custom=nlohmann::json::parse(ScriptEngine::DrawEditorUI(R"({"kind":"inspector","class":"TestA","entity":"0000000000000001","fields":{"Number":7},"events":{"0":11}})"));Check(custom.value("handled",false)&&custom["fields"]["Number"]==11,"custom inspector field edit failed");
    custom=nlohmann::json::parse(ScriptEngine::DrawEditorUI(R"({"kind":"inspector","class":"TestA","entity":"0000000000000001","fields":{"Number":11},"events":{"1":true}})"));Check(custom["fields"]["Number"]==42,"custom inspector button failed");
    ScriptEngine::RunEditorCommand("Tests/Open Window");auto windows=nlohmann::json::parse(ScriptEngine::DrawEditorUI(R"({"kind":"windows"})"));Check(windows["windows"].size()==1,"custom editor window did not open");
    auto tool=nlohmann::json::parse(ScriptEngine::DrawEditorUI(R"({"kind":"window","id":"TestWindow","events":{"0":9}})"));Check(tool["commands"][0]["value"]==9,"custom window field events failed");
    ScriptEngine::DrawEditorUI(R"({"kind":"window","id":"TestWindow","close":true})");
    {Tests::MemoryDevice device;AssetManager::Init(device,Project::AssetsDir());Scene api;auto object=api.CreateEntity("API probe");api.Reg().Add<ScriptComponent>(object).className="TestRendering";api.OnRuntimeStart();auto fields=nlohmann::json::parse(ScriptEngine::GetInstanceFields(object));Check(fields["Verified"]==1,"extended C# component APIs failed");auto mesh=AssetManager::GetMesh(api.Reg().Get<MeshRendererComponent>(object).mesh);Check(mesh&&mesh->VertexCount()==3&&mesh->IndexCount()==3,"custom C# mesh upload failed");auto customValues=nlohmann::json::parse(api.Reg().Get<MeshRendererComponent>(object).material.propertiesJson);Check(customValues["CustomGain"]==2.5f&&std::abs(customValues["CustomTint"][1].get<float>()-.3f)<.00001f,"C# shader property overrides ignored");api.Reg().Get<ScriptComponent>(object).enabled=false;ScriptEngine::Update(.02f);Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(object))["Ticks"]==0,"disabled script updated");auto snapshot=SceneSerializer::FromJson(SceneSerializer::ToJson(api));Check(!snapshot->Reg().Get<ScriptComponent>(snapshot->FindByUUID(api.Reg().Get<IDComponent>(object).uuid)).enabled,"disabled script lost during serialization");api.Reg().Get<ScriptComponent>(object).enabled=true;ScriptEngine::Update(.02f);Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(object))["Ticks"]==1,"re-enabled script did not resume");api.OnRuntimeStop();AssetManager::Shutdown();}

    Scene scene; auto entity=scene.CreateEntity("Scripted");
    auto& scripts=scene.Reg().Add<ScriptComponent>(entity,"TestA");
    scripts.additional.push_back({"TestB",nlohmann::json({{"Peer",std::format("{:016x}",scene.Reg().Get<IDComponent>(entity).uuid)}}).dump()});
    auto collections=scene.CreateEntity("Collections");auto idText=std::format("{:016x}",scene.Reg().Get<IDComponent>(entity).uuid);auto& collectionScript=scene.Reg().Add<ScriptComponent>(collections,"TestCollections");collectionScript.fieldsJson=nlohmann::json({{"Peers",nlohmann::json::array({idText})},{"Groups",nlohmann::json::array({{{"key",7},{"value",nlohmann::json::array({idText})}}})},{"Keys",nlohmann::json::array({{{"key",idText},{"value",42}}})}}).dump();
    auto ui=scene.CreateEntity("Scripted UI");scene.Reg().Add<ScriptComponent>(ui,"TestUI");
    scene.OnRuntimeStart();
    auto collectionValues=nlohmann::json::parse(ScriptEngine::GetInstanceFields(collections));Check(collectionValues["References"]==1&&collectionValues["Numbers"].size()==2&&collectionValues["Positions"][0]["value"][2]==3,"collection values / typed reference resolution failed");ScriptEngine::SetInstanceField(collections,"Numbers","[9,8,7]");Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(collections))["Numbers"][2]==7,"list edit did not reach managed instance");auto metadata=ScriptEngine::FindClass("TestCollections");Check(metadata&&std::any_of(metadata->fields.begin(),metadata->fields.end(),[](auto& f){return f.name=="Groups"&&nlohmann::json::parse(f.schemaJson)["value"]["kind"]=="List";}),"nested dictionary schema missing");auto mapping=nlohmann::json({{idText,"000000000000ffff"}});auto remapped=nlohmann::json::parse(ScriptEngine::RemapFieldReferences("TestCollections",scene.Reg().Get<ScriptComponent>(collections).fieldsJson,mapping.dump()));Check(remapped["Peers"][0]=="000000000000ffff"&&remapped["Groups"][0]["value"][0]=="000000000000ffff"&&remapped["Keys"][0]["key"]==idText,"collection references or text dictionary keys remapped incorrectly");
    Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(entity,1))["Observed"]==10,"typed reference to additional script failed");
    scene.OnUpdate(0.02f);
    Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(entity,0))["Counter"]==11,"multiple-script update failed");
    ScriptEngine::OnUIButton(entity);
    Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(entity,0))["Counter"]==111,"UI button callback failed");
    Check(scene.Reg().Get<UITextComponent>(ui).font=="Fonts/Custom.ttf"&&std::abs(scene.Reg().Get<UITextComponent>(ui).color.g-.5f)<.001f,"C# UI font/colour properties failed");
    Check(scene.Reg().Get<RectTransformComponent>(ui).anchoredPosition.y==34,"C# RectTransform properties failed");
    ScriptEngine::OnUIButton(ui);Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(ui))["Events"]==1,"Button.AddListener callback failed");
    scene.Reg().Get<UIButtonComponent>(ui).target=scene.Reg().Get<IDComponent>(entity).uuid;
    ScriptEngine::OnUIButton(ui);Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(entity))["Counter"]==211,"button target method dispatch failed");
    ScriptEngine::OnUIInput(ui,"Unicode: è ciao",false);ScriptEngine::OnUIInput(ui,"Unicode: è ciao",true);auto inputFields=nlohmann::json::parse(ScriptEngine::GetInstanceFields(ui));Check(inputFields["InputValue"]=="Unicode: è ciao"&&inputFields["Submitted"]==1&&scene.Reg().Get<UIInputFieldComponent>(ui).text=="start","input field C# properties / events failed");
    auto methods=ScriptEngine::ActionMethods("Script","TestA");Check(std::any_of(methods.begin(),methods.end(),[](auto& m){return m.signature=="CliccaQui(System.Int32,System.String)"&&m.parameters.size()==2;}),"button method metadata missing mixed arguments / overloads");
    auto& button=scene.Reg().Get<UIButtonComponent>(ui);UIButtonAction action;action.target=scene.Reg().Get<IDComponent>(entity).uuid;action.method="CliccaQui(System.Int32,System.String)";action.argumentsJson=R"([42,"ciao mondo"] )";button.actions.push_back(action);
    UIButtonAction native;native.component="Text";native.target=scene.Reg().Get<IDComponent>(ui).uuid;native.method="set_text(System.String)";native.argumentsJson=R"(["Changed by Button"])";button.actions.push_back(native);
    auto disabled=action;disabled.enabled=false;disabled.argumentsJson=R"([999,"disabled"])";button.actions.push_back(disabled);
    ScriptEngine::OnUIButton(ui);auto values=nlohmann::json::parse(ScriptEngine::GetInstanceFields(entity));Check(values["Number"]==42&&values["Message"]=="ciao mondo","typed multi-argument button call or overload resolution failed");Check(scene.Reg().Get<UITextComponent>(ui).text=="Changed by Button","button native component setter failed");
    auto saved=SceneSerializer::FromJson(SceneSerializer::ToJson(scene));Check(saved->Reg().Get<UIButtonComponent>(saved->FindByName("Scripted UI")).actions.size()==3,"persistent button actions lost");
    auto legacyImage=scene.CreateEntity("Legacy sprite action");scene.Reg().Add<UIImageComponent>(legacyImage);auto& legacyButton=scene.Reg().Add<UIButtonComponent>(legacyImage);UIButtonAction legacyAction;legacyAction.component="Image";legacyAction.method="set_sprite(System.String)";legacyAction.argumentsJson=R"(["UI/Future.sprite"])";legacyButton.actions.push_back(legacyAction);ScriptEngine::OnUIButton(legacyImage);Check(scene.Reg().Get<UIImageComponent>(legacyImage).sprite=="UI/Future.sprite","legacy string sprite action broke after introducing Sprite");
    scene.OnRuntimeStop();
    {
      Tests::MemoryDevice device;
      Scene templateScene;auto templateRoot=templateScene.CreateEntity("EnemyTemplate");auto& slot=templateScene.Reg().Add<ScriptComponent>(templateRoot);slot.className="TestA";slot.fieldsJson=R"({"Number":42})";auto prefab=root/"Assets/Prefabs/Enemy.prefab";Check(SceneSerializer::SavePrefab(templateScene,templateRoot,prefab),"typed prefab fixture failed");
      auto content=root/"Content";FileSystem::WriteText(content/"Config/Runtime.json","mounted file");FileSystem::WriteText(content/"UI/Future.png","sprite fixture");auto archive=root/"runtime.pak";Check(Pak::Build(content,archive),"runtime assets pack failed");
      auto icon=FileSystem::Utf8(root/"Assets/UI/Future.png"),text=FileSystem::Utf8(root/"Assets/Config/Runtime.json");
      Scene current;auto manager=current.CreateEntity("Persistent manager");current.Reg().Add<RigidbodyComponent>(manager).useGravity=false;current.Reg().Add<SphereColliderComponent>(manager);auto child=current.CreateEntity("Persistent child");current.SetParent(child,manager,false);auto disposable=current.CreateEntity("Old level");auto oldUuid=current.Reg().Get<IDComponent>(disposable).uuid;auto& script=current.Reg().Add<ScriptComponent>(manager);script.className="TestPersistent";script.fieldsJson=nlohmann::json{{"Template",{{"asset",FileSystem::Utf8(prefab)}}},{"Future",{{"asset",icon}}},{"FilePath",text}}.dump();auto managerUuid=current.Reg().Get<IDComponent>(manager).uuid;
      current.OnRuntimeStart();current.Physics()->SetVelocity(manager,{1,2,3});current.OnUpdate(.01f);auto fields=nlohmann::json::parse(ScriptEngine::GetInstanceFields(manager));Check(fields["Creates"]==1&&fields["Template"]["asset"]==FileSystem::Utf8(prefab)&&fields["Future"]["asset"]==icon,"lazy typed asset references failed to serialize");Check(current.Reg().Get<UIImageComponent>(manager).sprite==icon&&!FileSystem::Exists(root/"Assets/UI/Future.png"),"sprite assignment required a mounted pak");
      Check(Pak::Mount(archive,root/"Assets"),"runtime mount failed");ScriptEngine::SetInstanceField(manager,"Stage","1");current.OnUpdate(.01f);fields=nlohmann::json::parse(ScriptEngine::GetInstanceFields(manager));Check(fields["SpawnNumber"]==42&&fields["Content"]=="mounted file","typed prefab instantiate or virtual file read failed");
      Scene next;next.SetName("Next level");auto newObject=next.CreateEntity("New level");next.Reg().Add<ScriptComponent>(newObject).className="TestA";int before=fields["Observed"];current.ReplaceContents(next,device);Check(glm::length(current.Physics()->GetVelocity(manager)-glm::vec3(1,2,3))<.01f,"persistent rigidbody velocity was reset");Check(current.FindByUUID(managerUuid)==manager&&current.Reg().Valid(child)&&!current.FindByUUID(oldUuid),"persistent hierarchy lost or old level survived");current.OnUpdate(.01f);fields=nlohmann::json::parse(ScriptEngine::GetInstanceFields(manager));Check(fields["Creates"]==1&&fields["Observed"].get<int>()==before+1,"DontDestroyOnLoad recreated managed script or lost private state");Check(current.FindByName("New level")!=0,"new scene not activated");Check(nlohmann::json::parse(ScriptEngine::GetInstanceFields(current.FindByName("New level")))["SceneAtCreate"]=="Next level","OnCreate observed previous scene name");
      Check(Pak::Unmount(archive)&&!FileSystem::Exists(root/"Assets/UI/Future.png"),"pak unmount did not remove virtual entries");current.OnRuntimeStop();
    }
    ScriptEngine::Shutdown();
}

static void TestFbx() {
    auto path=FileSystem::EngineRoot()/"SandboxProject/Assets/X Bot@Idle.fbx";
    ufbx_load_opts options{}; options.evaluate_skinning=true;
    auto* scene=ufbx_load_file(path.string().c_str(),&options,nullptr);
    Check(scene!=nullptr,"existing FBX failed to import");
    std::cout<<"Existing FBX: "<<scene->meshes.count<<" meshes, "<<scene->anim_stacks.count<<" animation takes\n";
    if (scene->anim_stacks.count) {
        auto* evaluated=ufbx_evaluate_scene(scene,scene->anim_stacks.data[0]->anim,0.5,nullptr,nullptr);
        Check(evaluated!=nullptr,"FBX animation evaluation failed"); ufbx_free_scene(evaluated);
    }
    ufbx_free_scene(scene);
}

static void TestAnimator() {
    auto root=TestRoot()/"AnimatorTest";
    auto clip=root/"Clip.anim"; auto controller=root/"Test.controller";
    FileSystem::WriteText(clip,R"({"duration":1,"tracks":[{"path":"Child","position":[[0,0,0,0],[1,10,0,0]]}]})");
    FileSystem::WriteText(controller,nlohmann::json({{"defaultState","Move"},{"states",{{"Move",{{"clip",clip.generic_string()},{"loop",true}}}}}}).dump());
    Scene scene; auto parent=scene.CreateEntity("Animated"); auto child=scene.CreateEntity("Child"); scene.SetParent(child,parent,false);
    auto& animator=scene.Reg().Add<AnimatorComponent>(parent); animator.controller=controller.generic_string();
    AnimatorSystem::Update(scene,0.5f);
    Check(std::abs(scene.Reg().Get<TransformComponent>(child).position.x-5)<0.001f,"Animator hierarchy track interpolation failed");
    AnimatorSystem::Update(scene,0.75f);
    Check(std::abs(scene.Reg().Get<TransformComponent>(child).position.x-2.5f)<0.001f,"Animator clip looping failed");
          nlohmann::json graph={{"defaultState","Idle"},{"states",{{"Idle",nlohmann::json::object()},{"Run",nlohmann::json::object()}}},{"parameters",nlohmann::json::array({{{"name","Speed"},{"type","Float"},{"default",0.f}},{{"name","Jump"},{"type","Trigger"},{"default",0.f}}})},{"transitions",nlohmann::json::array({{{"from","Idle"},{"to","Run"},{"hasExitTime",true},{"exitTime",.5f},{"conditions",nlohmann::json::array({{{"parameter","Speed"},{"mode",">"},{"threshold",1.f}},{{"parameter","Jump"},{"mode","If"}}})}}})}};
      AnimatorComponent probe;probe.parameters["Speed"]=2;probe.parameters["Jump"]=1;probe.time=.25f;Check(!AnimatorSystem::AdvanceController(probe,graph,1),"exit time ignored");probe.time=.75f;Check(AnimatorSystem::AdvanceController(probe,graph,1)&&probe.state=="Run"&&probe.parameters["Jump"]==0,"typed conditions or trigger consumption failed");
      Scene multi;auto one=multi.CreateEntity("One"),two=multi.CreateEntity("Two"),nested=multi.CreateEntity("Nested");multi.SetParent(nested,one,false);auto selection=SceneSerializer::SelectionToJson(multi,{one,nested,two});auto pasted=SceneSerializer::InstantiateSelection(multi,selection);Check(pasted.size()==2&&multi.Reg().AllEntities().size()==6&&multi.GetChildren(pasted[0]).size()==1,"multi-selection clipboard duplicated children or lost roots");
animator.playing=false; AnimatorSystem::Update(scene,0.5f);
    Check(std::abs(scene.Reg().Get<TransformComponent>(child).position.x-2.5f)<0.001f,"paused Animator changed transform");
}

static void TestPrefabLinks(){
    auto file=TestRoot()/"PrefabTests/Linked.prefab";
    Scene source;auto root=source.CreateEntity("Linked Root");auto child=source.CreatePrimitive(PrimitiveType::Cube,"Child");source.SetParent(child,root,false);source.Reg().Get<MeshRendererComponent>(child).material.roughness=.4f;
    Check(SceneSerializer::SavePrefab(source,root,file),"cannot save prefab fixture");
    auto scene=MakeRef<Scene>();auto a=SceneSerializer::InstantiatePrefab(*scene,file),b=SceneSerializer::InstantiatePrefab(*scene,file);auto ac=scene->GetChildren(a)[0],bc=scene->GetChildren(b)[0];
    u64 aId=scene->Reg().Get<IDComponent>(a).uuid,acId=scene->Reg().Get<IDComponent>(ac).uuid,bcId=scene->Reg().Get<IDComponent>(bc).uuid;
    scene->Reg().Get<TransformComponent>(a).position={5,2,1};scene->Reg().Get<MeshRendererComponent>(ac).material.color={1,0,0,1};
    source.Reg().Get<NameComponent>(child).name="Updated Child";source.Reg().Get<MeshRendererComponent>(child).material.roughness=.8f;
    SceneSerializer::SavePrefab(source,root,file);Check(PrefabSystem::Refresh(scene),"prefab change not propagated");
    ac=scene->FindByUUID(acId);bc=scene->FindByUUID(bcId);a=scene->FindByUUID(aId);
    Check(ac&&bc&&a,"prefab update changed stable entity UUIDs");
    Check(scene->Reg().Get<NameComponent>(ac).name=="Updated Child","prefab name change missing");
    Check(scene->Reg().Get<MeshRendererComponent>(ac).material.color.r==1&&scene->Reg().Get<MeshRendererComponent>(ac).material.color.g==0,"prefab update lost an instance override");
    Check(std::abs(scene->Reg().Get<MeshRendererComponent>(ac).material.roughness-.8f)<.001f,"unmodified property did not follow prefab");
    PrefabSystem::Revert(*scene,a);Check(PrefabSystem::Refresh(scene),"prefab revert failed");a=scene->FindByUUID(aId);ac=scene->FindByUUID(acId);
    Check(a&&ac&&scene->Reg().Get<TransformComponent>(a).position.x==5,"prefab revert lost root placement or UUIDs");
    Check(scene->Reg().Get<MeshRendererComponent>(ac).material.color.g==1,"prefab override was not reverted");
    auto before=nlohmann::json::parse(FileSystem::ReadText(file));SceneSerializer::SavePrefab(*scene,a,file);auto after=nlohmann::json::parse(FileSystem::ReadText(file));
    Check(before["entities"][1]["uuid"]==after["entities"][1]["uuid"],"Apply prefab changed source child IDs");
    auto loaded=SceneSerializer::FromJson(SceneSerializer::ToJson(*scene));Check(loaded&&loaded->Reg().Has<PrefabInstanceComponent>(loaded->FindByUUID(aId)),"scene lost prefab reference");
}

static void TestCanvasAndRichText(){
    TextStyle style;auto text=ParseRichText("<b>A<color=#ff0000>B</color>C</b>D",style,true);
    Check(text.size()==4&&text[0].style.bold&&text[1].style.color.g==0&&text[2].style.color.g==1&&!text[3].style.bold,"nested rich text styles failed");
    auto literal=ParseRichText("<b>A</b>",style,false);Check(literal.size()==8,"rich text disable did not preserve literal tags");
    Scene scene;auto canvas=scene.CreateEntity("Canvas");auto& c=scene.Reg().Add<CanvasComponent>(canvas);c.referenceResolution={1000,500};scene.Reg().Get<TransformComponent>(canvas).position={2,3,0};
    auto parent=scene.CreateEntity("Panel");scene.SetParent(parent,canvas,false);scene.Reg().Add<RectTransformComponent>(parent).sizeDelta={400,200};scene.Reg().Add<CanvasGroupComponent>(parent).alpha=.5f;scene.Reg().Add<RectMaskComponent>(parent);
    auto child=scene.CreateEntity("Text");scene.SetParent(child,parent,false);scene.Reg().Add<RectTransformComponent>(child).sizeDelta={200,100};scene.Reg().Add<UITextComponent>(child).font="Fonts/Test.ttf";
    auto layout=CanvasRenderer::Layout(scene,{800,600},true);auto found=std::find_if(layout.begin(),layout.end(),[&](auto& r){return r.entity==child;});Check(found!=layout.end()&&found->masks.size()==1&&found->alpha==.5f,"UI mask/group inheritance failed");
    auto world=CanvasRenderer::CanvasWorld(scene,canvas)*found->localToCanvas*glm::vec4(found->size*.5f,0,1);Check(glm::length(glm::vec3(world)-glm::vec3(2,3,0))<.001f,"canvas world-space rectangle placement failed");
    auto loaded=SceneSerializer::FromJson(SceneSerializer::ToJson(scene));Check(loaded&&loaded->Reg().Get<UITextComponent>(loaded->FindByName("Text")).font=="Fonts/Test.ttf","custom font reference lost");
    RectTransformComponent rect;rect.anchorMin={.2f,.3f};rect.anchorMax={.7f,.8f};rect.pivot={.3f,.6f};rect.anchoredPosition={12,34};rect.sizeDelta={-20,-10};rect.rotation=31;rect.scale={1.2f,.8f};glm::vec2 parentSize(800,600);auto size=RectTransformUtils::Size(rect,parentSize);auto original=rect;auto point=parentSize*glm::mix(rect.anchorMin,rect.anchorMax,rect.pivot)+rect.anchoredPosition;RectTransformUtils::SetAnchors(rect,parentSize,{0,0},{1,1});Check(glm::length(RectTransformUtils::Size(rect,parentSize)-size)<.001f&&glm::length(parentSize*glm::mix(rect.anchorMin,rect.anchorMax,rect.pivot)+rect.anchoredPosition-point)<.001f,"anchor preset moved or resized rectangle");
    auto corner=[&](const RectTransformComponent& r){return parentSize*glm::mix(r.anchorMin,r.anchorMax,r.pivot)+r.anchoredPosition+glm::vec2(glm::rotate(glm::mat4(1),glm::radians(r.rotation),glm::vec3(0,0,1))*glm::vec4(-r.pivot*RectTransformUtils::Size(r,parentSize)*r.scale,0,0));};auto before=corner(rect);RectTransformUtils::SetPivot(rect,parentSize,{1,0});Check(glm::length(corner(rect)-before)<.001f,"pivot edit moved rectangle");auto lower=RectTransformUtils::OffsetMin(rect),upper=RectTransformUtils::OffsetMax(rect);auto position=rect.anchoredPosition;RectTransformUtils::SetOffsets(rect,lower,upper);Check(glm::length(position-rect.anchoredPosition)<.001f,"stretch offsets roundtrip failed");
    TerrainComponent terrain;Check(!terrain.settings.proceduralAppearance&&!terrain.settings.grass,"manual terrain defaults still procedural");
    SpriteAsset sprite;sprite.texture="Sheet.png";sprite.rect={8,16,64,32};sprite.border={3,4,5,6};auto path=(TestRoot()/"SpriteTest.sprite").generic_string();Check(sprite.Save(path),"sprite save failed");auto restored=SpriteAsset::Load(path);Check(restored.rect.x==8&&restored.border.w==6,"sprite metadata lost");
}

static void TestCookingAndAsync(){
 auto root=TestRoot()/"CookTests";auto source=root/"Project",out=root/"Export";auto assets=source/"Assets";
 Scene scene;auto cube=scene.CreatePrimitive(PrimitiveType::Cube,"Cooked");scene.Reg().Get<MeshRendererComponent>(cube).materials={"Materials/Used.mat"};auto imported=scene.CreateEntity("External Model");scene.Reg().Add<MeshRendererComponent>(imported).mesh="Models/Test.gltf";FileSystem::WriteText(assets/"Models/Test.gltf",R"({"asset":{"version":"2.0"},"buffers":[{"uri":"geo.bin","byteLength":4}],"images":[{"uri":"albedo.png"}]})");FileSystem::WriteText(assets/"Models/geo.bin","data");FileSystem::WriteText(assets/"Models/albedo.png","model texture");FileSystem::WriteText(assets/"albedo.png","wrong sibling");auto input=scene.CreateEntity("Input");scene.Reg().Add<UIInputFieldComponent>(input).placeholder="Name";scene.Reg().Add<ScriptComponent>(input).fieldsJson=R"({"Future":{"asset":"DLC/Optional.png"}})";auto terrain=scene.CreateEntity("Terrain");auto& settings=scene.Reg().Add<TerrainComponent>(terrain).settings;for(int i=1;i<7;++i)settings.layers.push_back({"Custom "+std::to_string(i)});
 auto unicodeMaterial=std::filesystem::path(u8"Materials/日本語.mat");scene.Reg().Get<MeshRendererComponent>(cube).materials.push_back(FileSystem::Utf8(unicodeMaterial));FileSystem::WriteText(assets/unicodeMaterial,R"({"albedoMap":"Textures/Used.png"})");
 Check(SceneSerializer::Save(scene,assets/"Scenes/Main.scene"),"cook fixture scene save failed");
 FileSystem::WriteText(assets/"Materials/Used.mat",R"({"albedoMap":"Textures/Used.png","normalMap":"DLC/Required.png","customPixelShader":"Shaders/Custom.spv"})");FileSystem::WriteText(assets/"Textures/Used.png","used");FileSystem::WriteText(assets/"Textures/Unused.png","unused");FileSystem::WriteText(assets/"DLC/Required.png","required by included scene");FileSystem::WriteText(assets/"DLC/Optional.png","optional");FileSystem::WriteText(assets/"DLC/Editor/Secret.txt","editor");
 ProjectConfig config;config.name="Cook fixture";config.scenes={"Scenes/Main.scene"};config.scenesEnabled={true};config.contentPacks.push_back({"chapter2","DLC"});config.icon="DLC/GameIcon.png";
 std::filesystem::copy_file(FileSystem::EngineRoot()/"engineIcon.png",assets/config.icon,std::filesystem::copy_options::overwrite_existing);
 std::filesystem::create_directories(out);std::filesystem::copy_file(FileSystem::ExecutableDir()/"StardustPlayer.exe",out/"Cook fixture.exe",std::filesystem::copy_options::overwrite_existing);CookReport report;
 FileSystem::WriteText(assets/"Shaders/Custom.spv","test shader");FileSystem::WriteText(assets/"Shaders/Custom.spv.properties.json",R"({"properties":[{"name":"Pattern","type":"texture","default":"Textures/SchemaDefault.png"}]})");FileSystem::WriteText(assets/"Textures/SchemaDefault.png","default texture");Check(BuildCooker::Cook(source,out,config,&report),"dependency cooking failed");Check(report.mainAssets.contains("Shaders/Custom.spv.properties.json")&&report.mainAssets.contains("Textures/SchemaDefault.png"),"shader schema/default texture omitted from pak");Check(report.mainAssets.contains(config.icon),"game icon excluded from main pak");Check(report.mainAssets.contains(FileSystem::Utf8(unicodeMaterial))&&report.mainAssets.contains("Textures/Used.png")&&report.mainAssets.contains("DLC/Required.png")&&!report.mainAssets.contains("Textures/Unused.png")&&!report.mainAssets.contains("DLC/Optional.png")&&report.mainAssets.contains("Models/geo.bin")&&report.mainAssets.contains("Models/albedo.png")&&!report.mainAssets.contains("albedo.png"),"main pak dependency closure incorrect");
 auto module=LoadLibraryExW((out/"Cook fixture.exe").c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
 Check(module!=nullptr,"cannot inspect exported executable icon");
 auto resource=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(14));
 Check(resource!=nullptr,"exported executable lacks icon group");
 auto group=(const unsigned char*)LockResource(LoadResource(module,resource));
 Check(group&&group[4]==6,"exported icon does not contain six resolutions");
 for(int i=0;i<6;++i){auto icon=FindResourceW(module,MAKEINTRESOURCEW(201+i),MAKEINTRESOURCEW(3));Check(icon!=nullptr,"custom icon frame missing");}
 FreeLibrary(module);
 Check(Pak::Mount(out/"Data/game.pak",out/"Data/Assets")&&Project::LoadRuntime(out/"Data"),"binary runtime metadata cannot load from pak");Check(Project::Config().icon==config.icon,"runtime icon setting lost");Check(Project::Config().name=="Cook fixture"&&!std::filesystem::exists(out/"Data/game.sdproject"),"player still requires editable project file");
 auto loaded=SceneSerializer::FromJson(FileSystem::ReadText(out/"Data/Assets/Scenes/Main.scene"));Check(loaded&&loaded->Reg().Get<TerrainComponent>(loaded->FindByName("Terrain")).settings.layers.size()==7,"custom terrain palette lost");Check(loaded->Reg().Get<UIInputFieldComponent>(loaded->FindByName("Input")).placeholder=="Name","input field serialization failed");
 auto id=SceneManager::LoadAsync((out/"Data/Assets/Scenes/Main.scene").generic_string());SceneManager::AllowActivation(id,false);for(int i=0;i<1000&&SceneManager::LoadProgress(id)<.9f;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));Check(SceneManager::LoadProgress(id)>=.9f&&SceneManager::LoadStatus(id)==0,"async scene preparation did not reach activation barrier");bool activated=false;SceneManager::UpdateAsync([&](Ref<Scene> next){activated=true;return true;});Check(!activated,"async activation gate ignored");SceneManager::AllowActivation(id,true);SceneManager::UpdateAsync([&](Ref<Scene> next){activated=next->FindByName("Cooked")!=0;return activated;});Check(activated&&SceneManager::LoadStatus(id)==1&&SceneManager::LoadProgress(id)==1,"async scene activation / completion failed");
 auto bad=SceneManager::LoadAsync((assets/"missing.scene").generic_string());SceneManager::AllowActivation(bad,false);for(int i=0;i<1000&&SceneManager::LoadStatus(bad)==0;++i){SceneManager::UpdateAsync([](Ref<Scene>){return true;});std::this_thread::sleep_for(std::chrono::milliseconds(1));}Check(SceneManager::LoadStatus(bad)==-1&&!SceneManager::LoadError(bad).empty(),"async load failure was not surfaced");SceneManager::CancelLoads();
 Check(Pak::Mount(out/"Data/Optional/chapter2.pak",out/"Data/Assets"),"optional content pak cannot mount");Check(FileSystem::ReadText(out/"Data/Assets/DLC/Optional.png")=="optional","optional asset paths changed");Pak::UnmountAll();
}

static void TestEditorSystems(){
 Scene scene;auto camera=scene.CreateEntity("Ortho Camera");auto& c=scene.Reg().Add<CameraComponent>(camera);c.orthographic=true;c.orthographicSize=3;c.nearPlane=.1f;c.farPlane=100;RenderCamera render;Check(scene.GetPrimaryCamera(render,2)&&render.orthographic,"orthographic camera missing");auto near=render.projection*glm::vec4(6,0,-.1f,1),far=render.projection*glm::vec4(0,0,-100,1);Check(std::abs(near.x-1)<.001f&&std::abs(near.z-1)<.001f&&std::abs(far.z)<.001f,"orthographic camera bounds / reverse Z failed");auto env=scene.CreateEntity("Sky");auto& sky=scene.Reg().Add<EnvironmentComponent>(env);sky.skyMode=3;sky.skyFaces={"r.png","l.png","t.png","b.png","f.png","k.png"};auto loaded=SceneSerializer::FromJson(SceneSerializer::ToJson(scene));Check(loaded->Reg().Get<EnvironmentComponent>(loaded->FindByName("Sky")).skyFaces[5]=="k.png"&&loaded->Reg().Get<CameraComponent>(loaded->FindByName("Ortho Camera")).orthographic,"skybox / 2D camera serialization failed");
 auto a=scene.CreateEntity("A"),b=scene.CreateEntity("B"),d=scene.CreateEntity("D");scene.SetParent(a,0,true,(int)scene.GetRoots().size()-1);Check(scene.GetRoots().back()==a,"hierarchy ordering failed");scene.MoveInParent(d,0);Check(scene.GetRoots().front()==d,"first sibling ordering failed");auto ordered=SceneSerializer::FromJson(SceneSerializer::ToJson(scene));Check(ordered->Reg().Get<NameComponent>(ordered->GetRoots().front()).name=="D","scene lost hierarchy ordering");
 Tests::MemoryDevice device;AssetManager::Init(device,FileSystem::EngineRoot()/"SandboxProject/Assets");
 {
  auto file=(FileSystem::EngineRoot()/"build/_deps/ufbx-src/data/blender_293_half_skinned_7400_binary.fbx").generic_string();auto bones=AssetManager::GetFbxSkeleton(file);Check(!bones.empty(),"FBX bone hierarchy missing");Scene rig;auto root=rig.CreateEntity("Rig");Check(ModelSkeleton::Import(rig,root,file),"FBX skeleton instantiation failed");auto pose=ModelSkeleton::Pose(rig,root);auto before=AssetManager::GetPosedFbx(file,1,pose);Check(before&&!before->Positions().empty(),"skinned FBX has no vertices");for(auto& [id,matrix]:pose)matrix=glm::translate(glm::mat4(1),glm::vec3(1,0,0))*matrix;auto after=AssetManager::GetPosedFbx(file,1,pose);bool moved=false;for(size_t i=0;i<before->Positions().size();++i)if(glm::length(after->Positions()[i]-before->Positions()[i])>.01f){moved=true;break;}Check(moved,"bone pose does not deform FBX vertices");auto copy=rig.DuplicateEntity(root);auto copyUuid=rig.Reg().Get<IDComponent>(copy).uuid;bool linked=false;rig.Reg().Each<BoneComponent>([&](EntityID bone,BoneComponent& bind){if(rig.IsDescendantOf(bone,copy)){linked=true;Check(bind.root==copyUuid,"duplicated bone points at original rig");}});Check(linked,"duplicated rig has no bones");
 }
 {
  TerrainSettings settings;settings.resolution=64;settings.size=64;while(settings.layers.size()<6)settings.layers.push_back({"Custom"});TerrainData first(device,settings),second(device,settings);first.MakeFlat(0,64);second.MakeFlat(10,64);Check(first.StitchEdge(second,1),"compatible terrain seam rejected");Check(std::abs(first.HeightAt(32,0)-5)<.01f&&std::abs(second.HeightAt(-32,0)-5)<.01f,"neighbor seam heights differ");TerrainBrushStamp brush;brush.shape=1;first.ApplyBrush(TerrainBrush::Paint,{0,0},10,1,.9f,0,5,false,1,brush);Check(first.LayerWeightAt(0,0,5)>.9f,"painting high terrain layer failed");auto file=TestRoot()/"Terrain16Test.terrain";Check(first.Save(file)&&second.Load(file)&&second.LayerWeightAt(0,0,5)>.9f,"terrain weights did not survive persistence");second.RemoveLayer(5);Check(second.LayerWeightAt(0,0,0)>.9f,"terrain layer removal lost normalized weights");Check(TerrainData::BrushWeight({.9f,0},brush)==1,"hard brush mask failed");brush.shape=4;Check(TerrainData::BrushWeight({0,0},brush)==1&&TerrainData::BrushWeight({.95f,0},brush)==0,"star brush mask failed");
  Scene tiles;auto group=tiles.CreateEntity("Tiles");auto left=tiles.CreateEntity("Left"),right=tiles.CreateEntity("Right");tiles.SetParent(left,group,false);tiles.SetParent(right,group,false);tiles.Reg().Add<TerrainComponent>(left).settings=settings;tiles.Reg().Add<TerrainComponent>(right).settings=settings;tiles.Reg().Get<TransformComponent>(right).position.x=64;TerrainNeighbors::AutoConnect(tiles);Check(tiles.Reg().Get<TerrainComponent>(left).neighbors[1]==tiles.Reg().Get<IDComponent>(right).uuid,"terrain neighbor auto-connect failed");SceneRenderer renderer(device);renderer.BeginScene({}, {}, 0);tiles.SubmitToRenderer(renderer,device);Check(renderer.TerrainCount()==2,"only the last terrain was submitted");auto clone=tiles.DuplicateEntity(group);auto children=tiles.GetChildren(clone);Check(tiles.Reg().Get<TerrainComponent>(children[0]).neighbors[1]==tiles.Reg().Get<IDComponent>(children[1]).uuid,"neighbor reference duplication failed");TerrainData::ClearCache();
 }
 {
  auto source=TestRoot()/"PackedImporterSource",virtualRoot=TestRoot()/"PackedImporterVirtual",archive=TestRoot()/"importers.pak";
  std::filesystem::create_directories(source);std::filesystem::copy_file(FileSystem::EngineRoot()/"build/_deps/ufbx-src/data/blender_293_half_skinned_7400_binary.fbx",source/"Skin.fbx",std::filesystem::copy_options::overwrite_existing);
  FileSystem::WriteText(source/"Triangle.obj","mtllib Triangle.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");FileSystem::WriteText(source/"Triangle.mtl","newmtl Surface\nKd 1 0 0\n");
  float vertices[]={0,0,0,1,0,0,0,1,0};{std::ofstream out(source/"geo.bin",std::ios::binary);out.write((char*)vertices,sizeof(vertices));}
  FileSystem::WriteText(source/"Triangle.gltf",R"({"asset":{"version":"2.0"},"buffers":[{"uri":"geo.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
  Check(Pak::Build(source,archive)&&Pak::Mount(archive,virtualRoot),"packed importer fixture failed");
  Check(AssetManager::GetMesh((virtualRoot/"Skin.fbx").generic_string())!=nullptr,"FBX memory import from pak failed");auto obj=AssetManager::GetMesh((virtualRoot/"Triangle.obj").generic_string());Check(obj&&obj->Positions().size()==3,"OBJ/MTL virtual import failed");auto gltf=AssetManager::GetMesh((virtualRoot/"Triangle.gltf").generic_string());Check(gltf&&gltf->Positions().size()==3,"glTF external buffer in pak failed");Check(!std::filesystem::exists(virtualRoot),"importer extracted files to disk");Pak::UnmountAll();
 }
 AssetManager::Shutdown();
}

static void TestTerrainSceneTransitions() {
    Tests::MemoryDevice device;
    auto root=TestRoot()/"TerrainSceneTests";
    AssetManager::Init(device,root);
    TerrainSettings settings;settings.resolution=64;settings.size=64;
    TerrainData data(device,settings);data.MakeFlat(7,64);
    std::filesystem::create_directories(root);
    auto heightFile=root/"Raised.terrain";
    Check(data.Save(heightFile),"terrain transition height data save failed");
    Scene incoming;
    auto ground=incoming.CreateEntity("Raised Ground");
    auto& terrain=incoming.Reg().Add<TerrainComponent>(ground);
    terrain.settings=settings;terrain.dataPath=heightFile.generic_string();
    incoming.Reg().Get<TransformComponent>(ground).position={0,3,0};
    auto tile=incoming.CreateEntity("Second Ground");
    incoming.Reg().Add<TerrainComponent>(tile).settings=settings;
    incoming.Reg().Get<TransformComponent>(tile).position={100,-2,0};
    auto disabled=incoming.CreateEntity("No Collider");
    auto& off=incoming.Reg().Add<TerrainComponent>(disabled);off.settings=settings;off.collider=false;
    incoming.Reg().Get<TransformComponent>(disabled).position={200,0,0};
    auto inactive=incoming.CreateEntity("Inactive Ground");
    incoming.Reg().Add<TerrainComponent>(inactive).settings=settings;
    incoming.Reg().Get<NameComponent>(inactive).active=false;
    Check(SceneSerializer::Save(incoming,root/"Ground.scene"),"terrain transition scene save failed");
    Scene current;current.OnRuntimeStart();
    // Same async activation path used by C# LoadSceneAsync in editor and player.
    auto load=SceneManager::LoadAsync((root/"Ground.scene").generic_string());
    for(int i=0;i<2000&&SceneManager::LoadStatus(load)==0;++i) {
        SceneManager::UpdateAsync([&](Ref<Scene> next){current.ReplaceContents(*next,device);return true;});
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(SceneManager::LoadStatus(load)==1,"terrain scene async load failed");
    auto loaded=current.FindByName("Raised Ground"),second=current.FindByName("Second Ground");
    Check(current.Physics()->HasBody(loaded),"script-loaded terrain has no collider");
    Check(current.Physics()->HasBody(second),"second script-loaded terrain has no collider");
    Check(!current.Physics()->HasBody(current.FindByName("No Collider"))&&!current.Physics()->HasBody(current.FindByName("Inactive Ground")),"disabled terrain collider was created");
    RaycastHit hit;
    Check(current.Physics()->Raycast({0,30,0},{0,-1,0},60,hit)&&hit.entity==loaded&&std::abs(hit.point.y-10)<.05f,"loaded terrain collider height / offset incorrect");
    Check(current.Physics()->Raycast({100,30,0},{0,-1,0},60,hit)&&hit.entity==second&&std::abs(hit.point.y+2)<.05f,"second terrain collider height incorrect");
    auto ball=current.CreatePrimitive(PrimitiveType::Sphere,"Falling sphere");
    current.Reg().Get<TransformComponent>(ball).position={0,16,0};
    current.Reg().Add<RigidbodyComponent>(ball);
    current.Reg().Add<SphereColliderComponent>(ball);
    current.Physics()->CreateBody(ball);
    for(int i=0;i<180;++i)current.OnUpdate(1.f/60);
    float y=current.GetWorldMatrix(ball)[3].y;
    Check(y>10.35f&&y<10.7f,"rigidbody fell through script-loaded terrain");
    current.Physics()->RecreateBody(loaded);
    Check(current.Physics()->HasBody(loaded),"terrain collider recreation lost body");
    current.SetActive(second,false);Check(!current.Physics()->HasBody(second),"terrain disable retained collider");
    current.SetActive(second,true);Check(current.Physics()->HasBody(second),"terrain re-enable lost collider");
    Scene empty;current.ReplaceContents(empty,device);
    Check(!current.Physics()->Raycast({100,30,0},{0,-1,0},60,hit),"old terrain collider survived scene unload");
    auto reloaded=SceneSerializer::Load(root/"Ground.scene");current.ReplaceContents(*reloaded,device);
    Check(current.Physics()->HasBody(current.FindByName("Raised Ground")),"repeated scene load lost terrain collider");
    current.OnRuntimeStop();SceneManager::CancelLoads();TerrainData::ClearCache();AssetManager::Shutdown();
}

static void TestComponentEnableAndMouse(){
    Input::ResetMouseTracking();Input::SampleMousePosition({100,100});Check(Input::MouseDelta()==glm::vec2(0),"first mouse sample jumped");Input::SampleMousePosition({104,98});Check(Input::MouseDelta()==glm::vec2(4,-2),"mouse movement lost");Input::ResetMouseTracking();Input::SampleMousePosition({-10000,3000});Check(Input::MouseDelta()==glm::vec2(0),"cursor mode warp leaked into look delta");Input::SampleMousePosition({-9997,3001});Check(Input::MouseDelta()==glm::vec2(3,1),"locked mouse delta rebounded");
    Tests::MemoryDevice device;AssetManager::Init(device,TestRoot());MeshLibrary::Init(device);Scene scene;auto object=scene.CreatePrimitive(PrimitiveType::Cube,"Disabled Renderer");scene.Reg().Add<BoxColliderComponent>(object);auto camera=scene.CreateEntity("Camera");scene.Reg().Add<CameraComponent>(camera);RenderCamera view;scene.GetCamera(camera,view,1);SceneRenderer renderer(device);scene.Reg().SetEnabled<MeshRendererComponent>(object,false);renderer.BeginScene(view,{},0);scene.SubmitToRenderer(renderer,device);Check(renderer.SubmittedDrawCount()==0,"disabled mesh renderer still rendered");scene.Reg().SetEnabled<CameraComponent>(camera,false);Check(!scene.GetPrimaryCamera(view,1),"disabled camera remained primary");Registry copied=scene.Reg();Check(!copied.IsEnabled<MeshRendererComponent>(object),"registry copy lost disabled state");auto duplicate=scene.DuplicateEntity(object);Check(!scene.Reg().IsEnabled<MeshRendererComponent>(duplicate),"duplicate lost disabled component state");auto saved=SceneSerializer::FromJson(SceneSerializer::ToJson(scene));auto loaded=saved->FindByName("Disabled Renderer");Check(!saved->Reg().IsEnabled<MeshRendererComponent>(loaded),"component enabled state lost in scene serialization");scene.Reg().SetEnabled<MeshRendererComponent>(object,true);renderer.BeginScene(view,{},0);scene.SubmitToRenderer(renderer,device);Check(renderer.SubmittedDrawCount()==1,"renderer did not re-enable");scene.Reg().SetEnabled<BoxColliderComponent>(object,false);scene.OnRuntimeStart();Check(!scene.Physics()->HasBody(object),"disabled collider created physics body");scene.Reg().SetEnabled<BoxColliderComponent>(object,true);scene.RefreshComponentState(object,3);Check(scene.Physics()->HasBody(object),"collider did not re-enable");scene.Reg().SetEnabled<BoxColliderComponent>(object,false);scene.RefreshComponentState(object,3);Check(!scene.Physics()->HasBody(object),"disabled collider retained physics body");scene.OnRuntimeStop();MeshLibrary::Shutdown();AssetManager::Shutdown();
}

static void TestRenderingSystems(){
    Tests::MemoryDevice device;auto root=TestRoot()/"Rendering";std::filesystem::create_directories(root);AssetManager::Init(device,root);MeshLibrary::Init(device);
    std::filesystem::copy_file(FileSystem::EngineRoot()/"engineIcon.png",root/"Image.png",std::filesystem::copy_options::overwrite_existing);
    TextureImportSettings settings;settings.maxSize=64;settings.srgb=false;settings.mipmaps=false;settings.filter=0;settings.wrap=0;Check(settings.Save("Image.png"),"texture import save failed");auto texture=AssetManager::GetTexture("Image.png",true);Check(texture&&texture->Desc().width==64&&texture->Desc().height==64&&texture->Desc().mipLevels==1&&texture->Desc().format==RHI::Format::RGBA8_UNORM,"texture import settings ignored");Check(TextureImportSettings::Load("Image.png").Sampler()==RHI::SamplerIndex::PointClamp,"point/clamp import ignored");
    FileSystem::WriteText(root/"Custom.spv.properties.json",R"({"properties":[{"name":"Amount","type":"float","default":2},{"name":"Tint","type":"color","default":[1,0,0,1]},{"name":"Pattern","type":"texture","default":"Image.png"}]})");
    Material custom;custom.customPixelShader="Custom.spv";custom.propertiesJson=R"({"Amount":3.5,"Tint":[0.1,0.6,0.9,1]})";auto propertyValues=custom.PropertyValues();Check(propertyValues.size()==3&&propertyValues[0].x==3.5f&&propertyValues[1].y==.6f&&propertyValues[2].y==(float)RHI::SamplerIndex::PointClamp,"custom material property slots/defaults ignored");nlohmann::json serialized;custom.ToJson(serialized);Material restoredMaterial;restoredMaterial.FromJson(serialized);Check(nlohmann::json::parse(restoredMaterial.propertiesJson)["Amount"]==3.5f,"custom material properties lost in serialization");Material water;water.shader=ShaderType::Water;water.propertiesJson=R"({"WaveSpeed":0,"WindDirection":[-1,0]})";auto waterValues=water.PropertyValues();Check(waterValues.size()==14&&waterValues[2].x==0&&waterValues[3].x==-1,"water speed/direction overrides ignored");
    Scene scene;auto camera=scene.CreateEntity("Map camera");auto& c=scene.Reg().Add<CameraComponent>(camera);c.orthographic=true;c.cullingMask=1;c.targetTexture="Map.rendertexture";c.targetWidth=256;c.targetHeight=128;
    auto visible=scene.CreatePrimitive(PrimitiveType::Cube,"Visible");auto hidden=scene.CreatePrimitive(PrimitiveType::Cube,"Hidden");scene.Reg().Get<NameComponent>(hidden).layer=3;
    RenderCamera view;Check(scene.GetCamera(camera,view,2)&&view.cullingMask==1,"camera layer mask not carried to renderer");SceneRenderer renderer(device);renderer.BeginScene(view,{},0);scene.SubmitToRenderer(renderer,device);Check(renderer.SubmittedDrawCount()==1,"camera culling mask did not filter objects");
    auto sprite=scene.CreateEntity("Sprite");auto& sr=scene.Reg().Add<SpriteRendererComponent>(sprite);sr.sprite="Image.png";sr.flipX=true;sr.sortingOrder=4;sr.pixelsPerUnit=32;renderer.BeginScene(view,{},0);scene.SubmitToRenderer(renderer,device);Check(renderer.SubmittedDrawCount()==2,"sprite renderer did not submit geometry");
    auto roundtrip=SceneSerializer::FromJson(SceneSerializer::ToJson(scene));auto& restored=roundtrip->Reg().Get<CameraComponent>(roundtrip->FindByName("Map camera"));Check(restored.targetTexture=="Map.rendertexture"&&restored.targetWidth==256&&restored.targetHeight==128&&restored.cullingMask==1,"camera render target serialization failed");Check(roundtrip->Reg().Get<NameComponent>(roundtrip->FindByName("Hidden")).layer==3&&roundtrip->Reg().Get<SpriteRendererComponent>(roundtrip->FindByName("Sprite")).flipX,"sprite / layer serialization failed");
    auto clip=nlohmann::json::parse(R"({"duration":1,"tracks":[{"path":"","interpolation":"Constant","position":[[0,0,0,0],[1,10,0,0]]}]})");AnimatorSystem::SampleClip(scene,visible,clip,.5f,false);Check(scene.Reg().Get<TransformComponent>(visible).position.x==0,"constant animation interpolation failed");clip["tracks"][0]["interpolation"]="Cubic";AnimatorSystem::SampleClip(scene,visible,clip,.25f,false);Check(std::abs(scene.Reg().Get<TransformComponent>(visible).position.x-1.5625f)<.001f,"cubic animation interpolation failed");
    auto source=root/"VideoSource";std::filesystem::create_directories(source);std::filesystem::copy_file(FileSystem::EngineRoot()/"Tests/Fixtures/VideoTest.mpg",source/"Video.mpg",std::filesystem::copy_options::overwrite_existing);auto archive=root/"video.pak";Check(Pak::Build(source,archive)&&Pak::Mount(archive,root/"Mounted"),"video pak fixture failed");
    {Scene video;auto object=video.CreateEntity("UI Video");video.Reg().Add<RectTransformComponent>(object);auto& player=video.Reg().Add<VideoPlayerComponent>(object);player.clip=(root/"Mounted/Video.mpg").generic_string();player.targetTexture="runtime:test-video";for(int i=0;i<10;i++)VideoPlayerSystem::Update(video,.04f);auto frame=AssetManager::GetTexture(player.targetTexture,false);Check(frame&&frame->Desc().width==64&&frame->Desc().height==64&&player.time>.2f,"archive-backed video did not decode frames");Check(video.Reg().Has<UIImageComponent>(object),"video did not attach its UI image");auto& pixels=static_cast<Tests::MemoryTexture*>(frame.get())->pixels;Check(pixels.size()==64*64*4&&pixels[3]==255&&pixels.back()==255,"decoded video was transparent");player.playing=false;float time=player.time;VideoPlayerSystem::Update(video,.1f);Check(player.time==time,"video pause ignored");Check(!std::filesystem::exists(root/"Mounted"),"video extracted pak to disk");VideoPlayerSystem::Clear(video);}
    Pak::UnmountAll();MeshLibrary::Shutdown();AssetManager::Shutdown();
}

int main() {
    try {
        TestWorkspace workspace;
        TestScenes();
        TestForces();
        TestHierarchyColliders();
        TestSerialization();
        TestPaks();
        TestManagedScripts();
        TestFbx();
        TestAnimator();
        TestPrefabLinks();
        TestCanvasAndRichText();
        TestCookingAndAsync();
        TestEditorSystems();
        TestTerrainSceneTransitions();
        TestComponentEnableAndMouse();
        TestRenderingSystems();
        PhysicsWorld::ShutdownJolt();
        std::cout << "Runtime tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        PhysicsWorld::ShutdownJolt();
        return 1;
    }
}
