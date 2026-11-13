<!-- Updated for Stardust 0.6.0. -->
# C# Scripting Guide

This guide describes the scripting API implemented in Stardust's `Stardust.ScriptCore` assembly. Scripts target **.NET 8** and use the `Stardust` namespace; UI components use `Stardust.UI`. The examples use Stardust types and callback names.

## Contents

- [Create and attach a script](#create-and-attach-a-script)
- [Callbacks and execution order](#callbacks-and-execution-order)
- [Inspector fields and references](#inspector-fields-and-references)
- [Entities, transforms and prefabs](#entities-transforms-and-prefabs)
- [Input, time and math](#input-time-and-math)
- [Physics](#physics)
- [Scene loading and persistent objects](#scene-loading-and-persistent-objects)
- [Assets, sprites and pak files](#assets-sprites-and-pak-files)
- [UI](#ui)
- [Animation, cameras, lighting and audio](#animation-cameras-lighting-and-audio)
- [Editor-only scripts](#editor-only-scripts)
- [Troubleshooting and API boundaries](#troubleshooting-and-api-boundaries)

## Create and attach a script

1. Create a C# script from the Project context menu or **Scripts > New C# Script**. Keep runtime scripts under `Assets`, outside folders named `Editor`.
2. Derive a concrete class from `ScriptBehaviour`. Give it a parameterless constructor, or use the implicit default constructor.
3. Build with **Scripts > Build Scripts** (`Ctrl+B`). Saving a source file also schedules a build.
4. Add a script component to a GameObject and choose its class. Multiple script components can be attached to one object.
5. Set fields in the Inspector and enter Play mode.

The project-generated `.csproj` references `Stardust.ScriptCore.dll`. Open that project in your IDE for completion. Namespaces are supported; the Inspector identifies classes by their full names. Matching file and class names is a useful convention.

```csharp
using Stardust;

public class SpinExample : ScriptBehaviour
{
    public Vector3 DegreesPerSecond = new Vector3(0, 60, 0);

    void OnCreate()
    {
        Debug.Log("Created " + name);
    }

    void OnUpdate(float dt)
    {
        transform.Rotate(DegreesPerSecond * dt);
    }
}
```

Edit-mode scenes and play-mode scenes are separate. Live Inspector edits during Play are not saved back to the edited scene. An assembly rebuilt during Play is reloaded after stopping. Opening a project with a game assembly older than `ScriptCore` also schedules recompilation.

## Callbacks and execution order

Callbacks are optional instance methods discovered by name and parameter types. They can be public or private; they are not overrides. Use these exact signatures:

| Callback | When it runs |
| --- | --- |
| `void OnCreate()` | After creation and Inspector reference resolution. |
| `void OnUpdate(float dt)` | Once per runtime frame, with scaled frame time. |
| `void OnFixedUpdate(float dt)` | Before a physics step; normally `1 / 60` second. |
| `void OnCollisionEnter(Entity other)` | A physical contact begins. |
| `void OnCollisionExit(Entity other)` | A physical contact ends. |
| `void OnTriggerEnter(Entity other)` | A trigger contact begins. |
| `void OnTriggerExit(Entity other)` | A trigger contact ends. |
| `void OnDestroy()` | The script instance is destroyed or runtime stops. |
| `void OnClick()` | Legacy UI button dispatch when no persistent action list is configured. |

For an initial scene, script instances are created first, references are resolved next, and then `OnCreate` runs. A referenced script exists at that point, but its own `OnCreate` may not have run yet. Do not depend on ordering between different scripts.

A runtime frame updates animation, performs accumulated fixed updates and physics/contact callbacks, then calls `OnUpdate`. The fixed-step accumulator is capped at five steps. Inactive objects and disabled script slots do not receive normal update callbacks. Each ScriptBehaviour exposes `enabled` and `isActiveAndEnabled`; disabling one script keeps the other scripts on the object running. There are no `Awake`, `Start`, `Update`, `FixedUpdate`, `LateUpdate`, `OnEnable`, or coroutine callbacks in this API.

Use constructors and field initializers only for ordinary managed data. The editor may construct a script to inspect default field values, before it belongs to an entity. Read engine state in callbacks instead.

## Inspector fields and references

Public instance fields are exposed by default. `[SerializeField]` also exposes private fields; `[HideInInspector]` keeps a field saved with the scene but hides it from the Inspector. Properties, constants, readonly fields and static fields are not Inspector fields.

Supported field values:

| Category | Types |
| --- | --- |
| Scalars | `float`, `double`, `int`, `bool`, `string` |
| Math | `Vector2`, `Vector3`, `Color` |
| Enums | Enum values represented by 32-bit integers |
| Scene references | `Entity`, exposed `Component` subclasses, concrete `ScriptBehaviour` subclasses |
| Asset references | `Sprite`, `Prefab`, `TextAsset`, `Asset` |
| Collections | One-dimensional arrays, `List<T>`, `Dictionary<TKey,TValue>` |

Collection elements and dictionary values can use the supported types recursively, up to the Inspector's nesting limit. Dictionary keys must be `string`, `int`, `bool`, or an enum. Initialize collections to empty instances. Arbitrary custom serializable classes/structs, multidimensional arrays, sets and general collection interfaces are not currently supported.

```csharp
using Stardust;
using System.Collections.Generic;

public class InventoryFieldsExample : ScriptBehaviour
{
    [SerializeField] private int startingHealth = 100;
    public Entity Target;
    public Rigidbody Body;
    public List<Sprite> Icons = new List<Sprite>();
    public Dictionary<string, int> ItemCounts = new Dictionary<string, int>();
    public Dictionary<int, List<Entity>> Groups = new Dictionary<int, List<Entity>>();
    [HideInInspector] public int RuntimeCounter;

    void OnCreate() { Debug.Log(startingHealth); }
}
```

Drag a Hierarchy object into a compatible reference field. Component fields require that component on the object. Script fields require the corresponding script. Missing scene references resolve to `null`; an existing C# entity wrapper can outlive the native object, so check `IsValid` before using it.

### Project prefab references in script fields

`Entity`, component and script fields can also reference a **prefab asset from Project**. The selector contains Scene and Project Prefabs sections. The prefab **root** must match the requested type. A script on a prefab child does not satisfy a field requiring that script on the root.

A prefab reference is a template, not a live object. For an `Entity`/script template, `IsAsset` is true, `AssetPath` identifies the prefab, and `IsValid` is false because no scene instance exists. Instantiate it before reading or changing transforms or gameplay state.

```csharp
using Stardust;

public class EnemyExample : ScriptBehaviour
{
    public int Health = 100;
}

public class EnemySpawnerExample : ScriptBehaviour
{
    // Drag a prefab whose root has EnemyExample onto this field.
    public EnemyExample EnemyTemplate;

    public void Spawn()
    {
        if (EnemyTemplate == null) return;
        EnemyExample enemy = Entity.Instantiate(EnemyTemplate, transform.position);
        if (enemy != null) enemy.Health = 150;
    }
}
```

Typed instantiation returns the script/component on the spawned root. A missing requested type is reported instead of returning an unrelated object. Scene references use entity UUIDs; prefab and asset references use paths. Duplicating scene objects remaps internal scene references and preserves asset paths, including inside collections.

## Entities, transforms and prefabs

A `ScriptBehaviour` is also an `Entity`. `entity` refers to the same object, and `transform` accesses its Transform.

| API | Purpose |
| --- | --- |
| `Entity.Create(name)` | Create an empty entity. |
| `Entity.CreatePrimitive(type, name)` | Create `Cube`, `Sphere`, `Plane`, or `Cylinder`. |
| `Entity.Find(name)`, `Entity.FindWithTag(tag)` | Find one matching runtime entity, or return `null`. |
| `GetComponent<T>()`, `HasComponent<T>()` | Query an exposed native component or attached script. |
| `AddComponent<T>()`, `RemoveComponent<T>()` | Add/remove an exposed native component. `AddComponent<T>` is constrained to `Component`, not scripts. |
| `As<T>()` | Get an attached script of type `T`, or `null`. |
| `Entity.Instantiate(original)` | Copy an entity hierarchy and its components. |
| `Entity.Instantiate(original, position)` | Copy at a world position; typed script/component overloads are available. |
| `Entity.Instantiate(prefabPath, position)` | Spawn a `.prefab` asset. |
| `Entity.Destroy(entity)` | Destroy an entity and its children; runtime destruction may be deferred until the current update finishes. |
| `name`, `tag`, `CompareTag(tag)` | Name and tag access. |
| `SetActive(bool)`, `activeSelf`, `activeInHierarchy` | Activation and inherited activation. |
| `parent` | Get/set the parent entity; assigning `null` unparents it. |

`GetComponent<T>()` returns the matching script instance for script types. Prefer distinct script classes when several scripts are attached to one object; there is no indexed `GetComponents<T>()` API.

Transform members:

| Member | Space / meaning |
| --- | --- |
| `position`, `eulerAngles` | World position and Euler rotation in degrees. |
| `localPosition`, `localEulerAngles`, `localScale` | Relative to the parent. |
| `forward`, `right`, `up` | World directions. Stardust is right-handed, Y-up; forward is **-Z**. |
| `parent`, `childCount`, `GetChild(index)` | Hierarchy access. |
| `Translate(delta, Space.Self)` | Translation; default is self-relative. Use `Space.World` for world movement. |
| `Rotate(eulerDelta)` | Add degrees to local Euler rotation. |
| `LookAt(Vector3)` / `LookAt(Transform)` | Aim at a target. |
| `TransformDirection(Vector3)` | Convert a local direction into world space. |

Math values are structs. To edit one coordinate of a component property, read it, change it, then assign the struct back: `var p = transform.position; p.y = 2; transform.position = p;`.

The C# `Rigidbody.MovePosition` and `MoveRotation` methods currently set the entity's world transform. They do not provide a separate interpolated kinematic-target API.

## Input, time and math

| API | Meaning |
| --- | --- |
| `Input.GetKey(KeyCode.W)` | Key held. |
| `Input.GetKeyDown(KeyCode.Space)` | Key pressed this frame. |
| `Input.GetMouseButton(MouseButton.Left)` | Mouse button held. |
| `Input.GetMouseButtonDown(MouseButton.Left)` | Mouse button pressed this frame. |
| `Input.mousePosition`, `mouseDelta`, `mouseScroll` | Pointer position, movement and scroll. |
| `Input.cursorLocked` | Request normal/locked cursor mode. |
| `Input.GetAxis("Horizontal")` | D/right arrow minus A/left arrow, smoothed: moves toward the target at the axis Sensitivity and returns to zero at its Gravity. |
| `Input.GetAxis("Vertical")` | W/up arrow minus S/down arrow, smoothed the same way. |
| `Input.GetAxisRaw("Horizontal")` | The same axis without smoothing: exactly -1, 0 or 1 for keys (gamepad sticks keep their analog value). |

Axes are defined in the project's Input Manager settings (keys, gamepad axis, dead zone, Sensitivity, Gravity, Snap, Invert); names that are not defined return zero. There is no key-up callback yet. In editor Play mode, gameplay input is routed to the Game view; editor panel clicks are excluded. The editor can release cursor lock when leaving Game, so the managed `cursorLocked` request is not a guaranteed query of OS cursor state.

`Time.deltaTime` and callback `dt` are scaled by `Time.timeScale`. `Time.fixedDeltaTime` is normally `1f / 60f`; `Time.time` is accumulated script runtime time. `Time.timeScale = 0` stops fixed-step progression while frame callbacks still run with zero scaled `dt`. Persistent objects keep their script runtime time across scene replacement.

`Vector2` provides arithmetic, magnitude and normalization. `Vector3` also provides `Dot`, `Cross`, `Distance`, `Lerp`, `MoveTowards` and component-wise `Scale`. `Mathf` includes clamp/lerp, trig, min/max, square root, constants and `SmoothDamp(..., dt)`. `Color` provides RGBA, common colors, `Lerp` and `HSV`. `Stardust.Random` offers `value`, float/int `Range`, `insideUnitSphere`, `insideUnitCircle` and `ColorHSV`; qualify the namespace if also importing `System`.

## Physics

Add colliders and a Rigidbody in the Inspector. `BoxCollider`, `SphereCollider`, `CapsuleCollider`, and `MeshCollider` are exposed as component reference types, but their shape/trigger settings are configured in the Inspector.

Rigidbody members include `velocity`, `angularVelocity`, `mass`, `drag`, `angularDrag`, `useGravity`, `freezeRotation`, and `isKinematic`. Angular velocity is expressed in radians per second.

| Force API | Meaning |
| --- | --- |
| `AddForce(force, ForceMode.Force)` | Continuous force; mass-dependent. |
| `AddForce(force, ForceMode.Acceleration)` | Continuous acceleration; mass-independent. |
| `AddForce(force, ForceMode.Impulse)` | Instantaneous impulse; mass-dependent. |
| `AddForce(force, ForceMode.VelocityChange)` | Instantaneous velocity change. |
| `AddRelativeForce(force, mode)` | Transform a local force into world space. |
| `AddForceAtPosition(force, position, mode)` | Apply at a world point, including its rotational effect. |
| `AddTorque(torque)` | Apply torque. |

Do not multiply continuous forces by `dt` yourself: the native API receives the caller's time step. Avoid applying the same continuous force in both Update and FixedUpdate. Impulses are normally used once in response to an event.

```csharp
using Stardust;

public class ForceMovementExample : ScriptBehaviour
{
    public float Acceleration = 20;
    public float JumpSpeed = 5;
    private Rigidbody body;

    void OnCreate() { body = GetComponent<Rigidbody>(); }

    void OnUpdate(float dt)
    {
        if (body == null) return;
        Vector3 direction = new Vector3(Input.GetAxis("Horizontal"), 0,
                                      -Input.GetAxis("Vertical"));
        body.AddForce(direction.normalized * Acceleration, ForceMode.Acceleration);
        if (Input.GetKeyDown(KeyCode.Space) &&
            Physics.Raycast(transform.position, Vector3.down, out var hit, 1.2f, this))
        {
            body.AddForce(Vector3.up * JumpSpeed, ForceMode.VelocityChange);
        }
    }

    void OnCollisionEnter(Entity other) { Debug.Log("Hit " + other.name); }
}
```

### Vehicles

A car is a dynamic Rigidbody with colliders, a **Vehicle Controller** and a **Wheel Collider** on each wheel object (children of the car). The Vehicle Controller uses Jolt's wheeled vehicle: suspension, tyre friction, engine, automatic gearbox, differentials between the driven wheels and anti-roll bars. Wheels on the same axle are paired automatically; mark the driven ones with **Motor** and the rear ones with **Handbrake**, and give the front ones a **Max Steer Angle**. Place each wheel object at the wheel centre with the suspension fully extended: in play mode the engine moves it along the suspension and turns it for steering and rolling, so a wheel mesh (axle along X) can sit on it or below it.

| API | Meaning |
| --- | --- |
| `VehicleController.SetInput(throttle, steer, brake, handbrake)` | Driver input: throttle -1 (reverse) .. 1, steer -1 (left) .. 1 (right), brake and handbrake 0..1. Also as properties. |
| `VehicleController.speed`, `rpm`, `gear` | Forward speed (m/s, negative in reverse), engine RPM, gear (-1 reverse, 0 neutral, 1..). |
| `VehicleController.engineTorque`, `maxRPM`, `centerOfMassOffset`, `antiRollBars` | Settings; changing one rebuilds the vehicle. |
| `WheelCollider.radius`, `suspensionDistance`, `springFrequency`, `springDamping`, `maxSteerAngle`, `motor`, `handbrake`, `forwardFriction`, `sidewaysFriction` | Wheel settings. |
| `WheelCollider.isGrounded`, `compression`, `steerAngle`, `rotationAngle` | Wheel state in play mode. |

```csharp
using Stardust;

public class CarDriver : ScriptBehaviour
{
    VehicleController car;
    void OnCreate() { car = GetComponent<VehicleController>(); }
    void OnFixedUpdate(float dt)
    {
        float v = Input.GetAxisRaw("Vertical");
        // pressing against the direction of travel brakes first
        bool braking = (v < 0 && car.speed > 1) || (v > 0 && car.speed < -1);
        car.SetInput(braking ? 0 : v, Input.GetAxisRaw("Horizontal"), braking ? 1 : 0,
                     Input.GetKey(KeyCode.Space) ? 1 : 0);
    }
}
```

`Physics.Raycast(origin, direction, out RaycastHit hit, maxDistance = 1000f, ignore = null)` returns the closest hit. The result contains `entity`, `point`, `normal`, and `distance`. Use the optional `ignore` entity for checks originating inside your own collider.

## Scene loading and persistent objects

`SceneManager.LoadScene` accepts a build index, an enabled build-scene name, or an explicit `.scene` path. **An explicit path does not need to appear in Scenes In Build**, including a scene stored in a mounted optional pak. Relative paths are resolved against the asset namespace. Absolute loose-file paths are also accepted.

The file must still be available. Scenes In Build controls startup/build inclusion; it does not automatically package every dynamically named scene. Put additional levels in configured optional content folders or otherwise distribute their files. A disabled scene can be loaded explicitly by its path; disabled names/indices are not added to the enabled build list.

`LoadScene` returns whether the request was accepted. Actual loading/activation occurs between frames; acceptance is not a guarantee that deserialization will succeed.

`LoadSceneAsync` returns an awaitable `SceneLoadOperation`:

| Member | Meaning |
| --- | --- |
| `progress` | 0–1; reaches about 0.9 after background reading, before activation. |
| `isDone` | Success or failure has completed. |
| `error` | Failure text. |
| `allowSceneActivation` | Set false to hold at the activation barrier. |
| `GetAwaiter()` | Enables `await`; failure raises an exception. |

One async load can be active at a time. GPU, physics and entity activation still run on the main thread and can take a long frame. This is replacement loading, not additive scene loading.

```csharp
using Stardust;

public class LevelLoaderExample : ScriptBehaviour
{
    public string NextScene = "Scenes/Chapter2.scene";
    private SceneLoadOperation loading;

    public void BeginLoading()
    {
        if (loading != null && !loading.isDone) return;
        loading = SceneManager.LoadSceneAsync(NextScene);
        loading.allowSceneActivation = false;
    }

    void OnUpdate(float dt)
    {
        if (loading == null) return;
        if (loading.isDone)
        {
            if (!string.IsNullOrEmpty(loading.error)) Debug.LogError(loading.error);
            return;
        }
        Debug.Log("Loading: " + (loading.progress * 100).ToString("0") + "%");
        if (loading.progress >= 0.9f && Input.GetKeyDown(KeyCode.Enter))
            loading.allowSceneActivation = true;
    }

    public async void LoadImmediately()
    {
        try { await SceneManager.LoadSceneAsync(NextScene); }
        catch (System.Exception error) { Debug.LogError(error.Message); }
    }
}
```

### DontDestroyOnLoad

Call `Entity.DontDestroyOnLoad(entity)` or `SceneManager.DontDestroyOnLoad(entity)` during runtime. Calling it from a script with `this` is valid. The **top-level root and its entire hierarchy** are preserved, even when you pass a child.

Preserved objects retain their UUIDs, native entities, managed script instances and private managed state. Their `OnCreate` is not called again. Nonpersistent objects are destroyed, and incoming scene scripts receive normal initialization. Persistence lasts until explicit destruction, leaving Play, or quitting. Reloading a scene does not automatically suppress another copy of a manager authored in that scene; implement your singleton policy if needed.

```csharp
using Stardust;

public class PersistentSessionExample : ScriptBehaviour
{
    public int Score;
    private int visits;

    void OnCreate() { Entity.DontDestroyOnLoad(this); }

    public void AddScore(int points)
    {
        Score += points;
        visits++;
    }
}
```

References from a persistent object to destroyed scene objects become invalid. Reacquire level-specific targets after a load. An ordinary nonpersistent script can be destroyed during activation, so do not assume `this` is still valid after awaiting a scene load.

## Assets, sprites and pak files

Asset paths use `/` and are normally relative to `Assets`, for example `UI/Portrait.sprite` or `Prefabs/Enemy.prefab`. Do not include the physical extracted-cache path: pak assets are read directly from the archive. Package assets use the `Packages/...` namespace.

### Lazy references

`Assets.Load<T>(path)` creates a **reference** without opening the file. This allows a reference to exist before its optional pak is mounted. It supports asset classes and prefab-root entity/script/component types. It does not download or automatically mount anything.

| Type / API | Purpose |
| --- | --- |
| `Asset.path`, `Asset.isAvailable` | Virtual path and current availability. |
| `Sprite.FromPath(path)` / `new Sprite(path)` | Reference a `.sprite` or image file without loading it. |
| `Prefab.Instantiate(position)` | Spawn a referenced prefab as an Entity. |
| `Prefab.Instantiate<T>(position)` | Spawn and obtain a root script/component. |
| `TextAsset.text`, `TextAsset.bytes` | Read the referenced file on demand. |
| `Assets.Exists(path)` | Search the mounted virtual namespace and loose files. |
| `Assets.GetSize(path)` | File size, or -1 if unavailable. |
| `Assets.ReadAllBytes(path)` | Read all bytes; throws when missing/unreadable. |
| `Assets.ReadAllText(path)` | Read UTF-8 text; strips an initial UTF-8 BOM. |

`Image.sprite` is a `Sprite` property. `Image.SetSprite(string path)` also provides a string-based method for UI actions. Implicit conversion from/to a path string preserves simple older assignments such as `image.sprite = "UI/Portrait.sprite"`. Assigning a reference does not require the image to be present yet. Until its content becomes available, it cannot display the missing image.

```csharp
using Stardust;
using Stardust.UI;

public class PortraitExample : ScriptBehaviour
{
    // Can point into an optional pak which is not mounted on startup.
    public Sprite Portrait;
    private Image image;

    void OnCreate()
    {
        image = GetComponent<Image>();
        if (image != null) image.sprite = Portrait;
    }

    public void ChangePortrait(string path)
    {
        Portrait = Sprite.FromPath(path);
        if (image != null) image.sprite = Portrait;
    }
}
```

To keep a script asset reference optional in a build, place its asset under a configured optional-pak folder. A lazy reference stored in a script field does not force that folder's asset into the main pak. A hard rendering dependency, such as a scene Image's configured sprite, can still make the asset mandatory. Assets outside optional folders referenced by script fields are included normally.

### Mount, search and unmount

`AssetPacks.Mount(archiveFile)` mounts a local `.pak` into the asset namespace. `AssetPacks.Unmount(archiveFile)` removes mounts for that archive and returns whether any were removed. Use the same physical archive path; it is different from an asset's virtual path.

All mounted archives are searched automatically when resolving a virtual path. The most recently mounted entry wins over earlier entries with the same path; a mounted entry takes precedence over its loose-file fallback. Unmounting a patch exposes an earlier mounted or loose entry again. No filename-only search or automatic pak discovery is performed.

Mount/unmount changes refresh rendering asset caches on the main thread. Path references remain usable for a later mount; future requests for unavailable content fail or display a fallback. An already decoded byte array remains ordinary managed data. Audio streams already opened from an archive retain their file handle until playback resources are released.

```csharp
using Stardust;
using System.IO;

public class ContentPackExample : ScriptBehaviour
{
    public string PackFile = "chapter2.pak";

    public void MountChapter()
    {
        string file = Path.Combine(Application.GetPersistentDataPath(), PackFile);
        if (!AssetPacks.Mount(file))
        {
            Debug.LogError("Cannot mount " + file);
            return;
        }
        if (Assets.Exists("Config/Chapter2.json"))
            Debug.Log(Assets.ReadAllText("Config/Chapter2.json"));
        SceneManager.LoadScene("Scenes/Chapter2.scene");
    }

    public void UnmountChapter()
    {
        string file = Path.Combine(Application.GetPersistentDataPath(), PackFile);
        Debug.Log("Unmounted: " + AssetPacks.Unmount(file));
    }
}
```

`AssetPacks.DownloadAndMountAsync(url, destination, cancellationToken)` downloads via **HTTPS**, writes a uniquely named temporary download, moves the completed file to its final path, then mounts that final path. It returns `Task<bool>` and can throw HTTP, I/O or cancellation exceptions. The destination is selected by the game; it is not an automatic extraction directory. Prefer versioned archive filenames when replacing downloadable content. Pass a cancellation token and cancel it in `OnDestroy` when the operation should not outlive its owning script.

Use ordinary .NET tasks for I/O. There is no general engine synchronization-context/dispatcher API: do not access transforms, components, physics or rendering from `Task.Run` or an arbitrary task continuation. Poll task completion from `OnUpdate` before performing gameplay changes.

```csharp
using Stardust;
using System;
using System.IO;
using System.Threading.Tasks;

public class DownloadChapterExample : ScriptBehaviour
{
    private Task<bool> download;

    public void BeginDownload()
    {
        if (download != null) return;
        string file = Path.Combine(Application.GetPersistentDataPath(), "chapter2-v1.pak");
        download = AssetPacks.DownloadAndMountAsync("https://example.com/chapter2-v1.pak", file);
    }

    void OnUpdate(float dt)
    {
        if (download == null || !download.IsCompleted) return;
        try
        {
            if (download.GetAwaiter().GetResult())
                SceneManager.LoadScene("Scenes/Chapter2.scene");
        }
        catch (Exception error) { Debug.LogError(error.Message); }
        finally { download = null; }
    }
}
```

## UI

Add the relevant UI components and a Canvas hierarchy in the editor. Exposed properties are listed below; additional Inspector settings are not automatically available through C#.

| Component | C# properties / events |
| --- | --- |
| `Canvas` | `referenceResolution`, `renderMode`, `pixelsPerUnit`, `scaleWithScreen` |
| `RectTransform` | `anchoredPosition`, `sizeDelta`, `anchorMin`, `anchorMax`, `pivot`, `scale`, `rotation` |
| `Image` | `sprite`, `texture`, `material`, `color`, `type`, `fillAmount`, `preserveAspect`, `pixelsPerUnitMultiplier` |
| `Text` | `text`, `font`, `material`, `fontSize`, `color`, `outlineColor`, `outlineWidth`, `richText`, `wordWrap`, `autoSize`, `alignment` |
| `Button` | `interactable`, `onClick` |
| `InputField` | `text`, `placeholder`, `interactable`, `readOnly`, `password`, `multiline`, `characterLimit`, `onValueChanged`, `onEndEdit`, `SetTextWithoutNotify` |
| `Dropdown` | `options` (`DropdownOption` text + icon sprite; a string converts), `AddOptions`, `ClearOptions`, `value`, `SetValueWithoutNotify`, `captionText`, `onValueChanged` (int), `Show()`, `Hide()`, `isOpen`, `interactable`, and the look: `font`, `fontSize`, `textColor`, `placeholder`, `hoverTint`, `pressedTint`, `arrowColor`, `arrowSprite`, `direction`, `itemHeight`, `maxListHeight`, `listColor`, `listSprite`, `itemColor`, `itemHoverColor`, `itemSelectedColor`, `itemSprite`, `itemTextColor`, `showCheckmark`, `checkmarkColor` |
| `ScrollView` | `content`, `horizontal`, `vertical`, `movementType` (Unrestricted, Elastic, Clamped), `elasticity`, `inertia`, `decelerationRate`, `scrollSensitivity`, `dragToScroll`, `clipContent`, `normalizedPosition` / `horizontalNormalizedPosition` / `verticalNormalizedPosition` (1 = top), `ScrollToTop()`, `ScrollToBottom()`, `velocity`, `StopMovement()`, `onValueChanged` (Vector2), and the scroll bars: `scrollbars`, `scrollbarWidth`, `trackColor`, `handleColor`, `handleHoverColor`, `trackSprite`, `handleSprite` |
| `CanvasGroup` | `alpha`, `interactable`, `blocksRaycasts` |
| `RectMask2D` | Component presence; configure its hierarchy in the editor. |
| `LayoutGroup` | `mode`, `fitContent`, `spacing` |

`CanvasRenderMode` values are `ScreenSpaceOverlay`, `WorldSpace`, and `ScreenSpaceCamera`. `ImageType` values are `Simple`, `Sliced`, `Tiled`, and `Filled`. RectTransform rotation is in degrees. Anchors/pivot are normalized; anchored positions and sizes use Canvas layout units. Text alignment values are 0 left, 1 center, 2 right; layout mode values are 0 horizontal, 1 vertical, 2 grid.

Fonts are asset paths to TTF/OTF files. Rich text is enabled by `richText`; for example `<b>Bold</b>`, `<i>Italic</i>`, and `<color=#FFCC00>Gold</color>`. The font must contain the glyphs you need. The editor's system-font fallbacks do not automatically add fallback glyphs to a custom runtime UI font.

Create them with **GameObject > UI > Dropdown** and **GameObject > UI > Scroll View** (a view with a Content child that grows with a vertical Layout Group). A Scroll View clips its content and moves it through the content's RectTransform; mouse wheel, drag with inertia, and the scroll bars all work in Play mode. The Dropdown's list opens over the canvas and also works with the arrow keys and Enter.

`ButtonEvent`, `StringEvent`, `IntEvent` and `Vector2Event` expose `AddListener`, `RemoveListener`, and `RemoveAllListeners`. Prefer removing only your own listener when sharing a component with other scripts. Setting `InputField.text` can notify listeners; use `SetTextWithoutNotify` when updating a value without an event.

```csharp
using Stardust;
using Stardust.UI;

public class MenuEventsExample : ScriptBehaviour
{
    public Button Confirm;
    public InputField PlayerName;
    public Text Status;

    void OnCreate()
    {
        if (Confirm != null) Confirm.onClick.AddListener(ConfirmName);
        if (PlayerName != null) PlayerName.onEndEdit.AddListener(NameSubmitted);
    }

    public void ConfirmName()
    {
        if (Status != null && PlayerName != null)
            Status.text = "Hello, " + PlayerName.text;
    }

    void NameSubmitted(string value) { Debug.Log("Name submitted: " + value); }

    void OnDestroy()
    {
        if (Confirm != null && Confirm.Entity.IsValid) Confirm.onClick.RemoveListener(ConfirmName);
        if (PlayerName != null && PlayerName.Entity.IsValid)
            PlayerName.onEndEdit.RemoveListener(NameSubmitted);
    }

    // Also assignable through the Button's persistent On Click Inspector list.
    public void ChooseItem(int number, string label)
    {
        Debug.Log(number + ": " + label);
    }
}
```

Persistent button actions are configured in the Inspector: assign a scene target, choose its native component or script slot, choose a public instance `void` method, then set its arguments. Multiple arguments and overloads are supported. Supported argument kinds include primitives, vectors, colors, enums and entity/component/script references. Runtime `onClick` listeners are separate from that serialized list. The legacy `OnClick()` script callback applies when no persistent action list is set.

## Animation, cameras, lighting and audio

| Component | Exposed C# API |
| --- | --- |
| `Animator` | `Play(state)`, `SetFloat`, `SetInteger`, `SetBool`, `SetVector2`, `SetTrigger`, `ResetTrigger`, `speed`, `playing` |
| `Camera` | `orthographic`, `orthographicSize`, `fieldOfView`, `nearClipPlane`, `farClipPlane`, `cullingMask`, `targetTexture`, `SetRenderTarget(name, width, height)` |
| `Rigidbody2D`, `BoxCollider2D`, `CircleCollider2D`, `CapsuleCollider2D` | 2D physics on the XY plane: `velocity`, `AddForce(Vector2, ForceMode2D)`, `gravityScale`, `bodyType`...; `Physics2D.Raycast`. See [2D Tilemap package](TILEMAP.md#colliders-and-2d-physics) |
| `SpriteBatch` | `SetQuads(textures, quads, count)`, `SetQuadRange(start, quads)` (rebuilds only the touched chunks), `Resize`, `GetQuads()`, `SetAnimations`, `Clear()`, `quadCount`, `version`, `textures`, `color`, `material`, `sortingOrder` |
| `ParticleSystem` | `Play()`, `Pause()`, `Stop(clear)`, `Clear()`, `Restart()`, `Emit(count)`, `isPlaying`, `particleCount`, and every module setting (`rateOverTime`, `bursts`, `startSpeed`, `startColor`, `colorOverLifetime`, `shape`, `blendMode`...): see [Particle System](PARTICLES.md) |
| `QualitySettings` (static) | In-game graphics menus: `level` / `SetQualityLevel(0..3)` (Low, Medium, High, Custom), `renderScale`, `shadows`, `shadowResolution`, `shadowDistance`, `cloudDownscale`, `grassDensity`, `grassDistance`, `lodBias`, `bloom`, `antiAliasing`, `vSync`. Setting a single value switches to Custom. Not saved: store the choice yourself. Screen-space UI stays at full resolution whatever the render scale |
| `VideoPlayer` | `clip`, `Play()`, `Pause()`, `Stop()`, `isPlaying`, `loop`, `time`, `length`, `playbackSpeed`, `renderMode`, `targetTexture`, `audioEnabled`, `volume` |
| `MeshRenderer` | `color` |
| `Light` | `color`, `intensity`, `range` for supported local lights |
| `DirectionalLight` | Component reference only; configure in Inspector. |
| `AudioSource` | `Play()`, `Stop()`, `isPlaying`, write-only `volume`, write-only `pitch`, static `PlayClipAtPoint(clip, position, volume)` |

Animator parameter/state names must match the controller. Trigger parameters are consumed by a successful transition. Controller support currently includes one base state machine, Float/Int/Bool/Trigger/Vector2 parameters, conditions (Bool and Trigger conditions take no comparison), Any State, exit time, Mute, Can Transition To Self, timed transition blending, and 1D and 2D (Simple Directional) blend trees. `SetVector2("Move", v)` drives a 2D blend tree from a Vector2 parameter. Avatar masks and additional layers are not implemented.

Orthographic size is the half-height of the camera view. Camera field of view is in degrees. Changing Scene view's 2D mode does not automatically change the runtime Camera.

Setting `MeshRenderer.color` switches the renderer to its embedded material. There is no C# material-slot array API yet; configure mesh material slots in the Inspector.

```csharp
using Stardust;

public class CharacterAnimationExample : ScriptBehaviour
{
    private Animator animator;

    void OnCreate() { animator = GetComponent<Animator>(); }

    void OnUpdate(float dt)
    {
        if (animator == null) return;
        animator.SetFloat("Speed", Mathf.Abs(Input.GetAxis("Horizontal")));
        if (Input.GetKeyDown(KeyCode.Space)) animator.SetTrigger("Jump");
    }

}
```

## Editor-only scripts

Scripts anywhere under a folder named `Editor` are compiled into the project's editor assembly and excluded from player scripts. Add `[Stardust.Editor.MenuItem("path")]` to a parameterless static method. Commands appear in the editor's **Scripts** menu. Only the editor API actually exposed by ScriptCore is available; there is no selection API.

**Custom inspectors and Scene view tools.** `[CustomEditor(typeof(MyScript))]` on a class deriving from `Stardust.Editor.Editor` replaces the script's Inspector (`OnInspectorGUI`) and can add a Scene view tool (`OnSceneGUI`, called every frame while the object is selected, in edit mode). `target` is a copy of the script with the saved field values; field changes are written back after each call. Editors are created again for every call, so keep tool state in static fields.

| API | Use |
| --- | --- |
| `EditorGUILayout.LabelField`, `HelpBox`, `FloatField`, `IntField`, `TextField`, `Toggle`, `Vector2Field`, `Vector3Field`, `ColorField`, `Space` | Inspector rows |
| `EditorGUILayout.AssetField(label, path, ".png", ...)` | Asset path picker with drag and drop from the Project window |
| `EditorGUILayout.Popup(label, index, options...)` | Drop-down list |
| `GUILayout.Button`, `GUILayout.Toggle(text, on)`, `GUILayout.SameLine(spacing)` | Buttons, toggle buttons, several controls on one line |
| `GUILayout.IconButton(icon, selected, tooltip)` | Square button with a built-in icon (select, brush, eraser, box, bucket, picker, flipx, flipy, rotate, grid, palette, plus, minus, play, eye) |
| `GUILayout.Toolbar(selected, options...)` | Segmented control filling the width; returns the selected index |
| `GUILayout.TileGrid(TileGridOptions)` | A texture cut into tiles, in its own layout with zoom and scrolling; returns a `TileGridEvent` (clicked tile, or a dragged rectangle, with Shift/Ctrl) |
| `EditorGUILayout.Header(text)`, `EditorGUILayout.Slider(label, value, min, max)` | Section titles and sliders |
| `GUILayout.TextureButton(texture, x, y, w, h, size, selected)` | Button showing a pixel rectangle of a texture (palettes) |
| `SceneView.mouseOver`, `mouseOnPlane`, `mouseLocal`, `mouseWorld` | Mouse on the target's local XY plane |
| `SceneView.GetMouseButton/Down/Up(button)`, `shift`, `control`, `alt`, `scrollDelta` | Input in the Scene view |
| `SceneView.TakeMouse()` | Keeps the left mouse for the tool this frame (no selection clicks, no gizmo) |
| `Handles.color`, `DrawLine`, `DrawRect`, `DrawTexture`, `Label` | Overlays in the target's local coordinates |
| `EditorWindow` + `EditorWindow.GetWindow<T>(title)` | Dockable windows drawn with the same controls |

`Assets.GetTextureSize(path)` returns a texture's size in pixels. The optional 2D Tilemap package (`Packages/com.stardust.tilemap`) is a complete example of these APIs.

```csharp
using Stardust;
using Stardust.Editor;

public static class EditorCommandExample
{
    [MenuItem("Tools/Print Project Info")]
    static void PrintProjectInfo()
    {
        Debug.Log(Application.GetProductName());
        Debug.Log(Application.GetProjectPath());
    }
}
```

Keep runtime script types independent from editor-only classes so exported scripts do not require the editor assembly.

## Troubleshooting and API boundaries

| Symptom | Check |
| --- | --- |
| Script class is missing | Concrete `ScriptBehaviour` subclass, successful build, correct project, file outside `Editor`. |
| Callback never runs | Exact method name and parameters; active object; Play mode. |
| Inspector field is missing | Supported field type; public or `[SerializeField]`; not readonly/static/property. |
| Component lookup returns null | Component exists on that entity; script instances are runtime objects. |
| Prefab cannot be dropped on a script field | Required script is on the prefab root, and the script class matches the field type. |
| Template has no live transform | Instantiate the prefab reference first. |
| Sprite/file is unavailable | Correct virtual path, pak mounted, content distributed in the intended build/optional pack. |
| Scene path is rejected | `.scene` file exists in the loose or mounted namespace; check the path rather than only its short name. |
| Object disappears after a level change | Mark its root persistent during runtime; avoid keeping references to nonpersistent level objects. |
| Async gameplay access misbehaves | Perform engine access on the main thread; poll I/O tasks from `OnUpdate`. |

`Application.runInBackground` keeps a built game updating while its window is unfocused or minimized; it is off by default, so the Player pauses (audio included) in the background. `Application` also exposes `Quit()`, `IsPlaying`, `GetVersion()`, `GetProductName()`, `GetCompanyName()`, `GetDataPath()`, `GetPersistentDataPath()`, and `GetProjectPath()`. Use the persistent-data path for writable saves/downloads and `Assets` for virtual asset reads. `Application.Quit()` leaves Play mode in the editor. `Screen.width` and `Screen.height` refer to the gameplay viewport. Logging uses `Debug.Log`, `LogWarning`, and `LogError`.

Version 0.6 adds terrain editing, environment/post-processing, render textures, sprites, video, gamepad input, custom meshes and editor extension APIs. See [Rendering, input and editor APIs](RENDERING_INPUT_EDITOR_API.md) for the updated reference. Runtime script attachment, coroutines and a general main-thread task dispatcher remain outside this API.
