# Virtual Cameras package

Official package `com.stardust.cameras`. Install it from **File → Project Settings → Package Manager**
(Stardust Packages → Virtual Cameras → Install). It is made of C# scripts, so it works in the player too.

## Setup

1. On the object with the real **Camera**, add the script **Camera Director** (`Stardust.Cameras.CameraDirector`).
2. Create an empty object, add the script **Virtual Camera**, set **Follow** (and **Look At**) to the player.
3. Press Play: the camera takes the shot of the live virtual camera.

Several virtual cameras can exist: the live one is the active one with the highest **Priority** (the most
recently activated wins a tie). Turning one on or off, or changing its priority, makes the director blend
to the new live camera over **Default Blend** seconds (or the camera's own **Blend Time**), with an
ease-in-out curve, a linear one, or a cut.

## Body: where the camera is

| Body | Use |
|---|---|
| Fixed | security cameras, menus: it stays where its object is |
| Follow | keeps **Follow Offset** from the target (World, Target local, or Target Yaw = behind it whatever its pitch), catching up in **Position Damping** seconds |
| Orbit | third person: mouse (or the **Look Action** of the input actions) and right stick turn it around **Pivot Offset**, the wheel changes **Distance**, walls pull it in (**Avoid Obstacles**), **Recenter** brings it back behind the target |
| Rail | slides along the Spline of the **Rail** object to the point closest to the target (corridors, racing replays) |

## Aim: where it looks

**Look At** (with **Look At Offset** and **Aim Damping**), **Follow Rotation** (the same way as the target,
for cockpits) or **None** (its object's forward; for Orbit, the orbit direction).

## Lens and feel

**Field Of View** is applied to the camera and blended too. **Noise Amplitude** / **Noise Frequency** add a
handheld sway. **Use Bounds** keeps the camera inside a box.

## Camera shake

```csharp
using Stardust.Cameras;
CameraShake.Shake(0.5f, 0.4f);                  // amplitude (1 = strong), seconds
CameraShake.ShakeAt(explosionPosition, 25, 1.2f); // fades with the distance from the camera
CameraShake.globalScale = settings.cameraShake;   // 0 turns every shake off
```

## From scripts

```csharp
var aim = Entity.Find("Aim Cam").As<VirtualCamera>();
aim.Priority = 20;                                // blends to it
director.onCameraChanged += (from, to) => Debug.Log($"{from?.name} -> {to?.name}");
var orbit = Entity.Find("Player Cam").As<VirtualCamera>();
orbit.yaw += 180;                                 // turn the orbit from code
```

`CameraDirector.current.live` is the camera shown now and `isBlending` tells whether a blend is running.
The director moves the camera after every script has updated, so targets moved in `OnUpdate` or by physics
are already in place.
