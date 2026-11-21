# Gameplay and rendering systems (0.7)

Character movement, joints, navigation, coroutines, HTTP, input actions, audio mixing, line renderers,
splines, reflection probes, decals and the new post effects. Every component has an Inspector, is saved
with the scene and is scriptable from C#.

## Character Controller

A capsule moved by scripts. `Move` slides along walls, climbs steps up to **Step Offset** and slopes up to
**Slope Limit**, and pushes dynamic bodies. It needs no Rigidbody, and gravity is up to the script.
Triggers see it (`OnTriggerEnter`), and so do raycasts.

```csharp
public class Player : ScriptBehaviour {
    public float speed = 5, jump = 6;
    float fall;
    CharacterController cc;
    void OnCreate() => cc = GetComponent<CharacterController>();
    void OnUpdate(float dt) {
        var input = new Vector3(Input.GetAxis("Horizontal"), 0, -Input.GetAxis("Vertical"));
        if (cc.isGrounded) fall = Input.GetButtonDown("Jump") ? jump : -1;
        fall += Physics.gravity.y * dt;
        cc.Move((input * speed + new Vector3(0, fall, 0)) * dt);
    }
}
```

`SimpleMove(velocity)` is the short form: horizontal speed in metres per second, with gravity applied.
`collisionFlags` tells what the last move touched (`Sides`, `Above`, `Below`).

## Joints

**Add Component > Physics > Joint** connects this object's Rigidbody to another one, or to the world when
**Connected Body** is empty. Anchor and axis are in the object's local space.

| Type | Use | Limits |
|---|---|---|
| Fixed | glue two bodies | - |
| Hinge | doors, wheels, levers | min / max angle, motor (degrees per second) |
| Spring | ropes, suspension | min / max distance, frequency and damping |
| Slider | drawers, pistons | min / max position, motor (metres per second) |
| Ball | ragdoll limbs | cone (swing) and twist angles |

With **Break Force** or **Break Torque** above zero, the joint breaks when the force goes over it.
Scripts on the object receive `OnJointBreak(Entity)`, and `joint.Repair()` connects it again.
**Enable Collision** off (the default) keeps the two bodies from pushing each other apart.
Duplicating a hierarchy, or spawning a prefab, keeps the joints pointing inside the copy.

## Navigation

1. Add a **Nav Mesh Surface** (Add Component > Navigation) to any object and press **Bake**. It collects
   the Mesh Renderers (or the colliders) and terrains of the scene, on the layers in its mask, and writes
   `Assets/NavMesh/<scene>_<id>.navmesh`. **Show** draws it in the Scene view.
2. Give characters a **Nav Mesh Agent** and send them somewhere:

```csharp
using Stardust.AI;
public class Guard : ScriptBehaviour {
    public Entity target;
    NavMeshAgent agent;
    void OnCreate() => agent = GetComponent<NavMeshAgent>();
    void OnUpdate(float dt) {
        agent.SetDestination(target.transform.position);
        if (!agent.pathPending && agent.remainingDistance < 1.5f) Attack();
    }
    void Attack() { }
}
```

Agents avoid each other (lower **Avoidance Priority** pushes harder), brake before the destination
(**Auto Braking**) and turn towards where they walk. `NavMesh.CalculatePath`, `NavMesh.SamplePosition` and
`NavMesh.Raycast` query the mesh without an agent. `NavMeshSurface.BuildNavMesh()` bakes again while the
game runs (procedural levels), in memory only.

## Coroutines and Invoke

```csharp
IEnumerator Countdown() {
    for (int i = 3; i > 0; --i) { Debug.Log(i); yield return new WaitForSeconds(1); }
    yield return new WaitUntil(() => doorOpen);
    yield return Explode();          // another coroutine, run inside this one
}
void OnCreate() {
    StartCoroutine(Countdown());
    Invoke("Spawn", 2);              // once, after 2 seconds
    InvokeRepeating("Tick", 0, 0.5f); // every half second until CancelInvoke("Tick")
}
```

A coroutine can wait for `null` (next frame), `WaitForSeconds`, `WaitForSecondsRealtime`, `WaitUntil`,
`WaitWhile`, `WaitForFixedUpdate`, `WaitForEndOfFrame`, another `Coroutine`, a `Task`, an `Http` request or a
`SceneLoadOperation`. Coroutines stop when their object is destroyed or deactivated. `OnCreate` itself may be
a coroutine (`IEnumerator OnCreate()`).

`async`/`await` also works: the code after an `await` continues on the game's main thread, where the engine
API may be used. From your own threads, `MainThread.Run(() => ...)` queues work for the next frame.

## HTTP and HTTPS

```csharp
// await
var json = await Http.GetStringAsync("https://api.example.com/scores");
var scores = await Http.GetJsonAsync<Score[]>("https://api.example.com/scores");
var reply = await Http.PostJsonAsync("https://api.example.com/scores", new { name = "Ada", points = 120 });

// callback
Http.Get("https://api.example.com/motd", r => { if (r.success) banner.text = r.text; else Debug.LogWarning(r.error); });

// coroutine
var request = Http.Get(url);
yield return request;
Debug.Log(request.statusCode);
```

`Http.Send(method, url, body, headers)` covers every other case: `body` may be a string, bytes, a `JsonNode`
or any object (sent as JSON). `Http.defaultHeaders` adds a header to every request (an API key),
`Http.timeout` sets the seconds before a request gives up, `request.Abort()` cancels it and
`request.downloadProgress` reports the download. `Http.DownloadFileAsync(url, path)` saves to a file. Results
and callbacks always arrive on the main thread.

## Input Actions

**Project Settings > Input Actions** defines action maps ("Player", "Vehicle", "UI") with actions bound to
keys, mouse, gamepad or composites: a **2D Vector** (WASD), a **1D Axis** (Q/E) or a **button with modifier**
(Ctrl+S). **Listen** binds the next key you press. **Create Default Actions** adds Move, Look, Jump, Fire and
Sprint.

```csharp
using Stardust.InputSystem;
var actions = InputSystem.actions;                  // Settings/InputActions.json, enabled at start
Vector2 move = actions["Player/Move"].ReadValue<Vector2>();
if (actions["Player/Jump"].WasPressedThisFrame()) Jump();
actions["Player/Fire"].performed += ctx => Shoot();
actions.FindActionMap("Vehicle").Disable();
```

Action types: **Button** (started and performed on press, canceled on release), **Value** (performed while
it changes) and **Pass Through**. Interactions: **Hold** (performed after the hold time), **Tap** and
**Multi Tap**.

Players rebind at run time; the overrides save as text:

```csharp
var file = System.IO.Path.Combine(Application.GetPersistentDataPath(), "bindings.json");
actions["Player/Jump"].PerformInteractiveRebinding(0)
    .OnComplete(op => System.IO.File.WriteAllText(file, actions.SaveBindingOverridesAsJson()))
    .Start();
if (System.IO.File.Exists(file)) actions.LoadBindingOverridesFromJson(System.IO.File.ReadAllText(file));
```

The axes of **Input Manager** (`Input.GetAxis("Horizontal")`) keep working next to the actions.

## Audio Mixer

**Project Settings > Audio Mixer** holds the groups Audio Sources play into (**Output** in the Audio Source):
Master, Music, SFX... Each group has a volume in dB, mute and solo, pitch, and effects: low-pass and
high-pass filters, echo and reverb. A group feeds its parent, Master feeds the speakers. **Snapshots** store
every group's values, to blend to later (underwater, pause menu).

```csharp
AudioMixer.SetVolume("Music", -12);                 // dB
AudioMixer.SetVolumeLinear("SFX", optionsSlider);   // 0..1 slider
AudioMixer.SetFloat("SFX", "lowpass", 800);         // muffled
AudioMixer.TransitionToSnapshot("Underwater", 0.5f);
AudioSource.PlayClipAtPoint("Audio/Boom.wav", position, 1, "SFX");
```

## Line Renderer

A ribbon through a list of points, in local or world space: lasers, ropes, trajectories. Width and colour
go from start to end, **Corner Vertices** rounds the corners, **Loop** closes it. From C#:
`line.SetPositions(points)`, `line.positionCount`, `line.startColor`...

## Splines

A curve through knots, drawn in the Scene view: drag the yellow knots (and, in Bezier mode, the tangent
handles; hold Alt to move one side only). Modes: **Auto** (smooth through the knots), **Bezier** and
**Linear**. **Follow** moves another object along it in play mode (platforms, cameras on rails).

```csharp
var curve = GetComponent<Spline>().GetCurve();      // world space
transform.position = curve.EvaluateAtDistance(travelled);
float length = curve.length;
curve.Nearest(player.transform.position, out var closest);
```

## Reflection Probes

A probe captures the scene around it into a cube map. Objects inside its box use it for reflections and
for local ambient light, so indoors no longer reflect the sky. **Box Projection** makes reflections on
floors and walls line up with the room; **Blend Distance** fades between neighbouring probes. Modes:
**On Load** (captured once), **Realtime** (every **Interval** seconds) and **Via Scripting**
(`probe.RenderProbe()`). A capture takes six frames, one face each.

## Decals

A box that projects a texture along the object's -Z axis onto the opaque surfaces inside it: dirt, posters,
bullet holes, road markings. The decal is lit like the surface (sun, shadows, local lights) and can have a
normal map. **Angle Fade** hides it on surfaces turned away from the projector.

## Post effects

In the **Environment** component (Lighting window):

- **Ambient Occlusion**: darkens corners and contact points (screen space, from the depth buffer).
  **AO Radius** in metres.
- **Temporal Anti-aliasing**: sharper than FXAA on edges and thin lines, and it replaces it. It needs a few
  frames to settle.
- **Depth of Field**: blurs what is nearer or farther than **Focus Distance**; **Focus Range** is how quickly
  the blur grows, **Bokeh Radius** its size.
- **Motion Blur**: blurs the image along the camera movement.

The same values are on the C# `Environment` component (`ambientOcclusion`, `temporalAntiAliasing`,
`depthOfField`, `focusDistance`, `motionBlur`...). **Project Settings > Quality** can turn ambient occlusion,
temporal anti-aliasing and depth of field / motion blur off for slower machines (`QualitySettings` from C#).
