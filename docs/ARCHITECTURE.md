# Stardust Engine — Architettura

Engine 3D custom in **C++20** con editor visuale e scripting in **C#** (.NET 8 hostato nativamente).
Obiettivo visivo: paesaggi naturali realistici (prato, alberi, cielo con nuvole) a 60+ fps su GPU di fascia media (target: RX 6600 @ 1080p, < 8 ms GPU).

## Toolchain

| Strumento | Versione | Uso |
|---|---|---|
| MSVC (VS 2022) | 14.44 | Compilatore C++20 |
| CMake | 4.x | Build system (generatore Visual Studio 17 2022) |
| .NET SDK | 8.0 | ScriptCore + script di gioco (target `net8.0`) |
| Vulkan SDK | 1.4 (target API 1.3) | Backend grafico principale, validation layer, DXC |
| DXC | 1.9 | Shader HLSL → SPIR-V (Vulkan) e DXIL (D3D12) |
| Windows SDK | 10.0.26100 | Backend Direct3D 12 (fase successiva) |

Dipendenze scaricate automaticamente da CMake (`FetchContent`): GLFW 3.4, GLM 1.0.1, Vulkan Memory Allocator 3.1,
Dear ImGui 1.91.8 (docking), ImGuizmo, stb_image, nlohmann/json. `nethost` viene preso dall'SDK .NET installato.

## Struttura cartelle

```
GameEngineCustom/
├─ CMakeLists.txt            root
├─ cmake/Dependencies.cmake  FetchContent + nethost
├─ Engine/                   libreria statica StardustEngine
│  ├─ src/Core/              Application, Window, Input, Time, Log, Assert
│  ├─ src/RHI/               interfaccia RHI astratta + backend Vulkan (RHI/Vulkan), D3D12 in arrivo
│  ├─ src/Renderer/          Mesh, Camera, SceneRenderer
│  ├─ src/Renderer/Nature/   Terrain, Grass (GPU), Trees (procedurali), Sky, Clouds
│  ├─ src/Scene/             Scene, Entity, Components, SceneSerializer (JSON)
│  └─ src/Scripting/         ScriptEngine (.NET hosting), internal calls
├─ Editor/                   StardustEditor.exe (ImGui docking)
│  └─ src/Panels/            Hierarchy, Inspector, SceneView, GameView, Project, Console
├─ Player/                   StardustPlayer.exe (runtime standalone, carica una scena)
├─ ScriptCore/               Stardust.ScriptCore.dll (API C# per gli script)
├─ Assets/Shaders/           HLSL (*.vs/ps/cs.hlsl) compilati a build-time da DXC
└─ SandboxProject/           progetto di esempio: Assets/Scenes, Assets/Scripts (.csproj di gioco)
```

## Moduli

### Core
- `Application` con loop fisso: input → script update → scene update → render → UI.
- `Window` su GLFW, contesto GL 4.6 core, vsync configurabile.
- `Input` (tastiera/mouse, stato per frame), `Time` (delta, tempo totale), `Log` (console + buffer per il pannello Console).

### Scene (ECS leggero)
- `Scene` possiede le entità in array contigui per tipo di componente (sparse set semplice, ID a 64 bit stabili = UUID).
- Componenti: `Tag`, `Transform` (gerarchia padre/figli), `MeshRenderer` (primitivi + materiale PBR),
  `Camera`, `DirectionalLight` (sole → guida cielo e ombre), `Environment` (ora del giorno, nuvole, nebbia, esposizione),
  `Terrain`, `Grass`, `TreeScatter`, `Script` (classe C# + campi pubblici).
- `SceneSerializer`: formato `.scene` JSON leggibile.
- Play mode: la scena viene copiata (snapshot) al Play e ripristinata allo Stop.

### RHI (Render Hardware Interface)
- API astratta in stile Vulkan/D3D12 (`RHI/RHI.h`): Device, CommandList, Buffer, Texture, Pipeline.
- **Bindless completo**: ogni texture/buffer ha un indice globale; gli shader lo ricevono via push constants
  (`Bindless.hlsli` mappa gli indici su array Vulkan oppure su `ResourceDescriptorHeap` di D3D12 SM 6.6).
- **Vertex pulling**: niente vertex layout, le mesh si leggono da storage buffer.
- Barriere automatiche sulle texture (stato tracciato), dynamic rendering, synchronization2, 2 frame in flight,
  distruzione differita delle risorse, timer GPU per pass, picking tramite readback dell'ID entità.
- Convenzioni comuni ai due backend: clip space Y-up (viewport Vulkan con altezza negativa), **reverse-Z** con proiezione infinita.
- **Vulkan 1.3** è il backend attuale; **Direct3D 12** riusa gli stessi shader HLSL compilati in DXIL.

### Renderer — pipeline deferred HDR
Ordine dei pass per frame:
1. **Culling** CPU per chunk di terreno / istanze alberi (frustum); culling GPU per l'erba (compute).
2. **Shadow pass**: Cascaded Shadow Maps, 4 cascate 2048², texel snapping anti-shimmer, PCF 3×3.
   L'erba proietta ombra solo nella prima cascata.
3. **G-Buffer**: albedo+AO (RGBA8), normale (RGBA16F), materiale roughness/metallic/translucency (RGBA8), depth 32F.
4. **Lighting** full-screen: PBR Cook-Torrance GGX, sole + ambient dal cielo (SH ordine 2 calcolata dal cielo),
   subsurface/trasmissione per foglie ed erba.
5. **Sky**: atmosfera fisica (Rayleigh + Mie + ozono) via LUT di trasmittanza (una volta) e Sky-View LUT (ogni frame, 192×108).
6. **Nuvole volumetriche**: raymarching cumuli a ¼ di risoluzione con rumore 3D Perlin-Worley (generato via compute all'avvio),
   blue-noise + riproiezione temporale, upsample bilaterale.
7. **Aerial perspective / nebbia**: dispersione atmosferica in funzione della distanza e altezza (montagne azzurrate in lontananza).
8. **Post**: bloom (catena down/up sample), auto-esposizione, tonemapping ACES, color grading leggero, FXAA.

### Natura — dove si gioca il realismo con poca spesa
- **Terreno**: heightmap procedurale (fBm + domain warp) o importata; mesh a chunk 64×64 con LOD per distanza e
  "skirt" per nascondere le crepe; texturing procedurale nello shader (erba/terra/roccia per pendenza e quota) con dettaglio normale da rumore.
- **Erba GPU**: compute shader genera i fili per tile attorno alla camera → frustum + distance culling → SSBO + `glDrawElementsIndirect`.
  Fili curvi a 7 vertici, densità decrescente con la distanza (LOD continuo), vento da rumore scorrevole + raffiche,
  variazione di colore/altezza, normali "arrotondate", trasmissione della luce. Fiori gialli come seconda specie istanziata.
  Budget: ~1–2 milioni di fili visibili.
- **Alberi**: generati proceduralmente (tronco/rami ricorsivi + cluster di foglie alpha-tested), 3–4 varianti,
  instancing con 3 LOD (mesh piena → mesh ridotta → impostor billboard), animazione vento sulle foglie.

### Scripting C# (.NET 8 hosting)
- L'engine carica `hostfxr` tramite `nethost`, inizializza il runtime con `Stardust.ScriptCore.runtimeconfig.json`
  e ottiene puntatori a metodi `[UnmanagedCallersOnly]` di `Stardust.Interop.Bridge`.
- **Native → C#**: `Initialize(nativeApiTable)`, `LoadGameAssembly(path)`, `CreateInstance(entityId, className)`,
  `InvokeOnCreate/OnUpdate/OnDestroy`, `GetClassFields(className)` (JSON per l'Inspector), `Get/SetField`.
- **C# → Native**: tabella di function pointer (`delegate* unmanaged`) passata in `Initialize`: Log, Transform get/set,
  Input, FindEntityByName, componenti luce/ambiente, ecc.
- Gli script di gioco stanno in un `AssemblyLoadContext` collezionabile → **hot reload** quando la DLL cambia
  (l'editor offre "Build Scripts" che lancia `dotnet build`).
- API a componenti:
  ```csharp
  public class Rotator : ScriptBehaviour {
      public float Speed = 45f;
      void OnUpdate(float dt) { transform.Rotate(Vector3.Up * Speed * dt); }
  }
  ```

### Editor
- Layout dockable di default: **Hierarchy** a sinistra, **Scene/Game** al centro, **Inspector** a destra,
  **Project** e **Console** in basso; toolbar centrale Play / Pause / Step.
- Scene view: camera fly con tasto destro + WASD/QE, rotella per zoom, `F` per focus, gizmo W/E/R (ImGuizmo),
  selezione con click (picking tramite buffer ID entità).
- Inspector con "Add Component", campi script esposti automaticamente.
- Tema scuro, salvataggio layout in `imgui.ini`.

## Ottimizzazioni chiave
- Draw call minime: instancing ovunque, indirect draw per l'erba, mesh terreno condivise per LOD.
- Pass costosi a risoluzione ridotta (nuvole ¼, sky-view LUT minuscola) con upsample.
- Un solo pipeline layout e un solo descriptor set bindless: niente cambi di descriptor tra draw.
- Nessuna allocazione per frame nel loop principale; UBO per-frame unico.
- Profiler GPU con timer query per pass, visibile nell'editor (`Stats`).

## Fasi di sviluppo
1. ✅ Core + RHI Vulkan + editor ImGui (Hierarchy, Inspector, Scene/Game, Project, Console, gizmo, picking).
2. ✅ Scena/ECS, serializzazione JSON (.scene), gerarchia padre/figli con drag & drop, gizmo in world space, ombre CSM, griglia editor.
3. ✅ Cielo fisico (LUT Hillaire), nuvole volumetriche con scattering multiplo, terreno CDLOD procedurale,
   erba GPU (compute + indirect, ~1M fili), alberi procedurali instanziati con 2 LOD, bloom/ACES/grading/FXAA.
   Nota: la pipeline è **forward** con un solo sole (più semplice e altrettanto veloce del deferred previsto,
   dato che l'erba è geometria opaca); il G-Buffer resta un'opzione se serviranno molte luci.
4. ✅ Scripting C# (.NET 8 via hostfxr, ALC collezionabile con hot reload, API a componenti), fisica Jolt
   (Rigidbody, Box/Sphere/Capsule/Mesh Collider, heightfield del terreno, raycast, OnCollisionEnter),
   editor del terreno (Raise/Smooth/Flatten/Set Height/Paint/Trees, terreno piatto o procedurale, 4 layer con texture,
   file .terrain), asset pipeline (texture PNG/JPG, mesh OBJ/glTF/GLB, materiali .mat con shader
   Stardust/Standard, Unlit, Toon, Transparent).
5. **In corso, Vulkan**: Project Hub e progetti `.sdproject`, scene abilitate e ordinabili in Build Settings,
   player standalone con layout `Data/Assets` e `Data/Managed`, cambio scena da C#, audio e impostazioni finestra.
   Export con compilazione Release degli script, controllo scene mancanti e diagnostica degli errori.
   Riferimenti a Entity/Component/script trascinabili dall'Hierarchy e campi privati `[SerializeField]`.
   Direct3D 12 rimandato; profiler e rifinitura visiva restano da completare.

## Uso della fase 5

Per package, Editor Only, Canvas, Animator, shader custom e distribuzione `.pak`, consultare
[PHASE5_SYSTEMS.md](PHASE5_SYSTEMS.md), che documenta anche i limiti delle prime implementazioni.

- Avviare `build/bin/Release/StardustEditor.exe`: il Project Hub permette di creare o aprire progetti.
- In **File > Build Settings**, aggiungere le scene, abilitarle e ordinarle. La prima abilitata è la scena iniziale.
- **Build** esporta la cartella del gioco; **Build And Run** la avvia. Distribuire l'intera cartella.
  Il PC di destinazione richiede Vulkan e il runtime .NET 8; il runtime .NET non è incluso nell'export.
- `SceneManager.LoadScene(0)` oppure `SceneManager.LoadScene("Next")` carica una scena abilitata della lista.
  `Application.Quit()` chiude il player e interrompe il Play nell'editor.
- I campi `public Entity`, `public Transform`, `public Rigidbody` e riferimenti agli script accettano oggetti
  dell'Hierarchy compatibili. I campi privati supportano `[SerializeField]`; `[HideInInspector]` nasconde un campo.
- `Rigidbody.AddForce` e `AddForceAtPosition` supportano Force, Impulse, VelocityChange e Acceleration.
  Le forze continue usano il tempo del callback chiamante (Update o FixedUpdate).
- Il controller di esempio modificato dall'utente si trova in `SandboxProject/Assets/Scripts/FlyCamera.cs`.

Verifiche senza interfaccia grafica: compilare il target `StardustRuntimeTests` in Release ed eseguire
`build/bin/Release/StardustRuntimeTests.exe`. Controlla scene disabilitate, indici fuori intervallo,
impostazioni non valide e tutti i modi di forza su un corpo Jolt di massa 2.
