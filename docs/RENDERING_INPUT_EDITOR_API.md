# Stardust 0.6: rendering, input and editor APIs

This supplements [C# scripting](CSHARP_SCRIPTING.md). Names below are Stardust APIs. Use engine APIs on the main thread. Asset paths are relative to Assets, or begin with Packages/ for package assets. Files in mounted paks use the same namespace.

## Cameras, render textures and layers

```csharp
using Stardust;
public class MinimapSetup : ScriptBehaviour {
    public Entity MapCamera;
    void OnCreate() {
        var camera = MapCamera.GetComponent<Camera>();
        camera.orthographic = true;
        camera.orthographicSize = 40;
        camera.cullingMask = LayerMask.GetMask("Default", "Water");
        camera.targetTexture = new RenderTexture("runtime:minimap", 512, 512);
    }
}
```

`Camera` exposes `cullingMask`, `targetTexture`, `targetWidth`, `targetHeight`, `SetRenderTarget(name, width, height)`, orthographic/perspective and clip settings. Cameras with target textures render separately from the primary game camera. Assign the same virtual texture name to a UI Image texture to display the result. A `.rendertexture` asset stores dimensions; its asset dimensions take priority over the camera fields. Dimensions are limited to 16–8192 pixels.

`Entity.layer` accepts 0–31. `LayerMask.NameToLayer(name)` returns -1 for unknown names; `LayerMask.GetMask(params string[])` combines named layers. Names are edited in Project Settings → Layers. Camera masks also filter terrain, sprites, lights and canvas graphics.

## Sprites and texture imports

`SpriteRenderer` exposes `Sprite sprite`, `string material`, `Color color`, `float pixelsPerUnit`, `bool flipX`, `bool flipY`, `int sortingOrder`. Set pixelsPerUnit to zero to use the sprite asset value. Sprites support atlas rectangles and pivots. The default sprite shader is unlit and alpha blended; custom `.mat` materials can replace it.

Select an image in Project to edit import type (Default, Normal or Sprite), sRGB, maximum size, mipmaps, wrap and filtering. Settings are saved alongside the image as `.import.json` and included by the build cooker. The importer currently resizes with nearest-neighbor sampling; platform compression and cubemap import are not available. Sprite mode offers Create Sprite and the sprite editing window.

## Meshes and materials

```csharp
var mesh = GetComponent<MeshRenderer>();
mesh.SetMesh(
    new[] { new Vector3(-1,0,0), new Vector3(1,0,0), new Vector3(0,1,0) },
    new[] { 0, 1, 2 },
    new[] { new Vector2(0,0), new Vector2(1,0), new Vector2(.5f,1) });
mesh.roughness = .35f;
mesh.SetTexture("Textures/Stone.png");
```

`SetMesh(vertices, triangles, uv = null)` uploads indexed triangles and generates normals/tangents. Indices must be valid, triangle count divisible by three, and UV count must match vertices. Runtime meshes are transient; save source data separately if persistence is required. Large dynamic meshes should not be rebuilt each frame through JSON.

`MeshRenderer`: `mesh`, `material`, `materials`, `castShadows`, `receiveShadows`, `roughness`, `metallic`, `SetTexture`, `SetShader`, `SetEmission`. Embedded material setters detach external material references on that renderer, so they do not modify a shared material asset. Multiple materials correspond to imported submesh slots.

## Environment, lights, physics and terrain

`Stardust.Environment` avoids ambiguity with `System.Environment`:

| Component | Script properties / methods |
| --- | --- |
| Environment | exposure, ambientIntensity, shadowStrength, minimumAmbient, bloomIntensity, contrast, saturation, vignette, fogDensity, fxaa, skyMode, skyColor, skyMaterial |
| DirectionalLight | color, intensity, castShadows, shadowDistance |
| SpotLight | color, intensity, range, innerAngle, outerAngle |
| Collider | center, isTrigger, friction, restitution |
| BoxCollider | size |
| SphereCollider | radius |
| CapsuleCollider | radius, height |
| MeshCollider | convex |
| Terrain | size, resolution, collider, GetHeight, SampleHeight, MakeFlat, ApplyBrush, RebuildCollider |
| AudioSource | clip, volume, pitch, loop, spatial, Play, Pause, Stop |

Terrain `GetHeight` uses local X/Z. `SampleHeight` accounts for translation; rotated/scaled terrain sampling is not supported. Batch height edits before rebuilding a terrain collider.

## Input actions and gamepads

Project Settings → Input edits named actions, keyboard codes, controller index, axis/button, deadzone and inversion. Rename an action with its Name field and press Enter; scripts must use the new name. Settings live at `Assets/Settings/Input.json`, included in builds. A saved action file replaces the default action set, so removed/renamed defaults do not reappear at runtime.

```csharp
float horizontal = Input.GetAxis("Horizontal");
if (Input.GetButtonDown("Jump")) Debug.Log("Jump");
if (Input.IsGamepadConnected()) {
    float look = Input.GetGamepadAxis(GamepadAxis.RightX);
    bool fire = Input.GetGamepadButton(GamepadButton.RightBumper);
}
InputManager.Bind("Interact", new InputBinding {
    Positive = KeyCode.E, GamepadButton = (int)GamepadButton.West
});
```

`Input.GetButton`, `GetButtonDown`, `GetGamepadButton`, `GetGamepadButtonDown`, `GetGamepadAxis`, `IsGamepadConnected` are available. `InputManager.Reload()` reloads disk settings and `Bindings` exposes the current mapping. Bind changes the current session only. Controllers use GLFW standard gamepad mappings; up to 16 indices are accepted. Trigger axes are normalized to 0–1. Rumble and platform-specific adaptive triggers are not implemented. Editor input is scoped to the Game viewport during Play mode.

## Video and UI transitions

`VideoPlayer`: `clip`, `targetTexture`, `isPlaying`, `loop`, `time`, `length`, `playbackSpeed`, `renderMode`, `audioEnabled`, `volume`, `Play`, `Pause`, `Stop`.

- Formats: MPEG-1 `.mpg`/`.mpeg` on every platform; on Windows also `.mp4`, `.mov`, `.wmv` and `.avi` through Media Foundation (H.264/HEVC/... as far as the system has decoders). Linux players only play MPEG-1; `Tools/ImportVideo.py` converts with FFmpeg.
- Clips stream from loose files or from (encrypted) paks without extracting the movie; decoding runs on a worker thread.
- Embedded audio plays in sync with the picture when `audioEnabled` is on, at `volume`.
- `renderMode = VideoRenderMode.RenderTexture` writes into the `.rendertexture` asset in `targetTexture`; `VideoRenderMode.MaterialOverride` replaces the albedo of the object's MeshRenderer. With a Rect Transform the video shows as a UI Image.
- Play On Awake starts the clip when Play mode starts.

`Stardust.UI.Button.transition` selects None (0), ColorTint (1) or Animation (2). Set `fadeDuration`, `highlightedColor`, `pressedColor`, `disabledColor` for color fades. Animation mode uses state names configured in the button inspector and an Animator on the button object. Existing serialized actions and C# listeners support arguments; see the main scripting guide.

## Disabled scripts

Each script header has its own enable checkbox. `script.enabled = false` suspends OnUpdate and OnFixedUpdate for that slot; `isActiveAndEnabled` combines this flag with the object's hierarchy activation. Instances and serialized field values are retained. Explicit method calls and destruction callbacks remain available. There are no automatic OnEnable/OnDisable lifecycle callbacks.

## Custom inspectors and windows

Place editor code in an `Editor` directory. It is compiled into the editor assembly and excluded from player scripts.

```csharp
using Stardust.Editor;
[CustomEditor(typeof(MyMover))]
public class MoverInspector : Editor {
    public override void OnInspectorGUI() {
        var mover = (MyMover)target;
        mover.Speed = EditorGUILayout.FloatField("Speed", mover.Speed);
        if (GUILayout.Button("Reset Speed")) mover.Speed = 1;
        DrawDefaultInspector();
    }
}
public class MyTools : EditorWindow {
    string note = "Ready";
    [MenuItem("Tools/My Tools")]
    public static void Open() => GetWindow<MyTools>("My Tools");
    public override void OnGUI() {
        note = EditorGUILayout.TextField("Note", note);
        EditorGUILayout.HelpBox("An editor-only tool.");
    }
}
```

MyMover is a project script with a serializable float Speed field. Available controls: LabelField, HelpBox, FloatField, IntField, TextField, Toggle, Vector3Field, ColorField, Space, GUILayout.Button/Label. Windows can Close/Repaint. Controls use stable draw order to match events. Custom inspectors persist supported scalar/value fields; retain DrawDefaultInspector for entity/asset references and collections. This is a small editor GUI API: only the controls listed here are available.

See [Editor 0.6 workflows](EDITOR_06.md), [Linux export](LINUX_PLAYER.md), and `Examples/FeatureLab` for working project scripts.

## Shader-specific properties

See [Custom material properties](CUSTOM_SHADER_PROPERTIES.md) for shader schemas, HLSL property slots and MeshRenderer.SetFloat/SetColor/SetVector/SetTexture(name,path). Water uses the same per-material property storage, including WaveSpeed and WindDirection.

## Component activation

Most native components now expose `enabled` and `isActiveAndEnabled`, in addition to each script slot's existing enabled flag. For example:

```csharp
var mesh = GetComponent<MeshRenderer>();
mesh.enabled = false; // hides this renderer; retains its mesh/material settings
GetComponent<BoxCollider>().enabled = false; // removes its physics shape
GetComponent<Camera>().enabled = false; // excludes it from camera rendering
```

The inspector checkbox is next to the component icon. Activation is retained in scene/prefab serialization, duplication and Play-mode copies. Disabled renderers/lights/terrain/cameras stop contributing; animator and video updates pause; disabled UI components stop their own drawing, layout or interaction. Disabling a Canvas hides its canvas subtree. Audio sources stop, and cannot be played while disabled. Physics-related toggles recreate/remove their bodies; re-enabling a Rigidbody creates its body again rather than preserving its previous simulation velocity. Component fields remain editable while disabled.

Transform, RectTransform, prefab metadata, imported rig/bone metadata and entity identity/hierarchy components are structural and have no enable checkbox. Calling `enabled = false` on a structural C# transform throws NotSupportedException. Entity.SetActive remains the way to deactivate an entire hierarchy. There are no new OnEnable/OnDisable callbacks.

## Locked mouse in the editor

A locked Game cursor retains gameplay capture even when GLFW's virtual cursor coordinates leave the visible viewport. Escape releases capture; click in Game again to recapture it. Losing application focus also releases the lock. Mode changes discard the synthetic cursor-position jump, and repeated requests for the same mode do not reset tracking. `Input.cursorLocked` reads the current gameplay lock state. Gameplay cursor requests do not override Scene navigation while the game does not own mouse input.

## CPU profiler

The Stats window lists **CPU systems (average)**: the time each part of a frame takes on the main thread, averaged over about half a second: Animation, Particles, Scripts (FixedUpdate / contacts / Update), Physics, Scene upkeep, and per view the render submission ("submit") and command recording ("record"). Engine code can add sections with `SD_PROFILE("Name")` (Core/Profiler.h); sections with the same name add up within a frame.
