# Trail Renderer

A ribbon left behind by a moving object: skid marks, contrails, a sword swing, a comet tail.
Add it with **Add Component → Trail Renderer**. It records the object's path in Play mode, and in the Scene view while you move the object in edit mode.

## How it works

- Every frame, after scripts and physics, the trail looks at its object's world position. Once the
  object has moved `Min Vertex Distance` metres from the last point, a new point is recorded.
- A point lives `Time` seconds. Width and colour go from **Start** (at the object) to **End**
  (a point about to disappear), so an end alpha of 0 fades the tail out.
- **Emitting** off ends the current strip: it stops growing and fades away. Turning it on again starts
  a new strip from where the object is (a car's skid marks are separate strips).
- The trail is drawn as one ribbon of quads, one draw per trail, through the particle pipelines.
  Colours are vertex colours, the same as particles. Without a material it is unlit.

## Settings

| Setting | |
|---|---|
| Time | seconds a point lives |
| Min Vertex Distance | metres between recorded points (larger is cheaper, smaller is smoother) |
| Start / End Width | width at the object and at the tail |
| Start / End Color | colour and alpha at the object and at the tail (Intensity multiplies the colour, for bloom) |
| Alignment | **View**: the ribbon faces the camera. **Flat**: it lies across the object's up axis (marks on the ground) |
| Texture Mode | **Stretch**: the texture is laid once along each strip. **Tile**: it repeats every `Tile Length` metres, fixed to the ground |
| Texture / Material | without a texture, a white strip that fades towards both edges |
| Blend Mode | Alpha or Additive |
| Max Points | the oldest points go beyond this |
| Sorting Order | among transparent draws |

## Scripting (C#)

```csharp
var trail = GetComponent<TrailRenderer>();          // or entity.AddComponent<TrailRenderer>()
trail.time = 12f;
trail.widthMultiplier = 0.2f;                        // start and end width
trail.startColor = new Color(0, 0, 0, 0.7f);
trail.endColor = new Color(0, 0, 0, 0);
trail.alignment = TrailAlignment.Flat;
trail.emitting = sliding;                            // set it only when it changes
int points = trail.positionCount;
trail.Clear();
```

Each property call goes through the component's JSON, so set what changes and avoid reading every frame.

The script ID of the component is 37, and it is saved as `trailRenderer`.
