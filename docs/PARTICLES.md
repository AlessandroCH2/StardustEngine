# Particle System

The **Particle System** component emits, moves and draws particles. It is organised in a **Main** section and
modules that each have an enable box. Particles are simulated on the CPU and drawn as one transparent draw per system,
with an unlit look.

Create one with **GameObject > Effects > Particle System** (Hierarchy context menu), or add the component to any object
with **Add Component > Particle System**.

## Preview in the editor

Outside Play mode, the selected object and its children play their particles in the Scene view. The controls at the
top of the component pause, restart and stop the preview and set its speed; the line below shows the particle count and
the time in the current cycle. In Play mode the same buttons drive the running system.

The emitter shape is drawn in light blue when the object is selected, and the collision plane in orange.

## Modules

| Module | Settings |
| --- | --- |
| Main | Duration, Looping, Prewarm, Start Delay, Start Lifetime, Start Speed, Start Size, Start Rotation (constant or random between two values: the **C/R** button), two Start Colors (a random colour between them), Gravity Modifier, Simulation Space (Local: particles follow the object; World: they stay behind), Simulation Speed, Play On Awake, Max Particles, Random Seed (0 = different every run) |
| Emission | Rate over Time, Rate over Distance, Bursts (time, count, cycles with 0 = for ever, interval, chance) |
| Shape | Sphere, Hemisphere, Cone, Box, Circle, Edge, Point; Radius, Radius Thickness (0 = surface, 1 = volume), Angle, Arc, Box Size, Position, Randomize Direction |
| Velocity over Lifetime | Constant velocity added to each particle |
| Force over Lifetime | Acceleration (adds up over time) |
| Limit Velocity | Speed Limit and Drag |
| Color over Lifetime | Gradient of up to 8 keys (click the bar to add one, drag the markers), multiplied with the start colour, alpha included |
| Size over Lifetime | Curve of size multipliers |
| Rotation over Lifetime | Angular velocity in degrees per second |
| Noise | Turbulence: Strength, Frequency, Scroll Speed |
| Collision | Bounce on a horizontal world plane: Plane Height, Bounce, Dampen, Lifetime Loss |
| Texture Sheet Animation | Tiles X/Y of a sprite sheet, Frames per Second (0 = over the particle's life, Cycles times), Random Start Frame |
| Renderer | Texture (empty = soft round particle), Material (optional custom shader), Blend Mode (Alpha or Additive), Render Mode (Billboard, Stretched, Horizontal, Vertical), Intensity (above 1 glows with bloom), Sort by Distance, Sorting Order |

The emitter's **up axis (+Y)** is its forward: a cone opens upwards and a box or an edge emits upwards. Rotate the object
to aim the effect.

## 2D

Particles face the camera, so they work with an orthographic camera too. Transparent objects are ordered by **Sorting
Order** first (Sprite Renderers, Sprite Batches, world-space UI and particles together), then by distance: give a
Particle System a higher Sorting Order than the sprites it must appear over.

## Scripting

```csharp
using Stardust;

public class Explosion : ScriptBehaviour {
    void OnCreate() {
        var particles = GetComponent<ParticleSystem>();
        particles.loop = false;
        particles.rateOverTime = 0;
        particles.bursts = new[] { new ParticleBurst(0, 80) };
        particles.startSpeed = new MinMax(4, 9);
        particles.SetStartColors(new Color(1, 0.8f, 0.3f), new Color(1, 0.3f, 0.1f));
        particles.colorOverLifetime = new[] { new GradientColorKey(0, Color.white), new GradientColorKey(1, new Color(1, 1, 1, 0)) };
        particles.blendMode = ParticleBlendMode.Additive;
        particles.Restart();
    }
}
```

| Member | Description |
| --- | --- |
| `Play()`, `Pause()`, `Stop(clear)`, `Clear()`, `Restart()` | Playback. `Stop()` stops emitting and lets the living particles finish; `Stop(true)` removes them too |
| `Emit(count)` | Emits particles now, even when the system is stopped |
| `isPlaying`, `isPaused`, `isEmitting`, `isStopped`, `particleCount`, `time` | State |
| `duration`, `loop`, `prewarm`, `playOnAwake`, `startDelay`, `startLifetime`, `startSpeed`, `startSize`, `startRotation`, `startColor`, `SetStartColors(a, b)`, `gravityModifier`, `simulationSpeed`, `simulationSpace`, `maxParticles`, `randomSeed` | Main module. `MinMax` converts from a `float` for a constant |
| `emissionEnabled`, `rateOverTime`, `rateOverDistance`, `bursts` | Emission |
| `shapeEnabled`, `shape`, `radius`, `radiusThickness`, `angle`, `arc`, `boxSize`, `shapePosition`, `randomizeDirection` | Shape |
| `velocityOverLifetime`, `forceOverLifetime`, `speedLimit`, `drag`, `colorOverLifetime`, `sizeOverLifetime`, `angularVelocity`, `noiseStrength`, `noiseFrequency`, `noiseScrollSpeed`, `collisionPlaneHeight`, `bounce`, `dampen`, `lifetimeLoss` | Over-lifetime modules, each with an `...Enabled` switch (setting the colour or size keys enables their module) |
| `textureSheetEnabled`, `tilesX`, `tilesY`, `sheetFps`, `sheetCycles`, `randomStartFrame` | Texture Sheet Animation |
| `texture`, `material`, `blendMode`, `renderMode`, `lengthScale`, `speedScale`, `intensity`, `sortByDistance`, `sortingOrder` | Renderer |

Rate over Time, Simulation Speed, Gravity Modifier, Intensity and the two Start Colors can also be keyed in the
Animation window.

## Limits

- Particles are simulated on the CPU: a few thousand per system stay cheap; Max Particles caps each system (up to 100,000).
- Particles are unlit and do not cast shadows.
- Collision is with one horizontal plane, not with physics colliders. Sub-emitters and trails are not available yet.
