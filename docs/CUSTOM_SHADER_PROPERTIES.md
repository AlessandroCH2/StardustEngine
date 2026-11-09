# Custom material properties

A custom pixel shader declares its editable properties in a sidecar named `<compiled shader>.properties.json`. For example, `Shaders/Pulse.ps.hlsl.spv` uses `Shaders/Pulse.ps.hlsl.spv.properties.json`. Create Custom Shader generates a source file and a sample schema. The source inspector includes an editable Shader Properties section and Save Shader Properties.

```json
{
  "properties": [
    { "name": "Tint", "label": "Surface tint", "type": "color", "default": [0.1, 0.6, 1, 1] },
    { "name": "Brightness", "type": "float", "default": 1, "min": 0, "max": 5 },
    { "name": "Pattern", "type": "texture", "default": "Textures/Pattern.png", "srgb": true }
  ]
}
```

Assign the compiled shader to the material's Custom Shader field. The material inspector shows this shader's own property list instead of the built-in albedo/metallic/emission controls. Surface Pipeline selects the built-in rendering category (Standard, Transparent, UI, etc.); Double Sided remains a rasterization setting. Built-in shaders show their relevant controls, and Water has its own named surface properties.

Supported types are float, int, bool, vector2, vector3, vector4, color and texture. At most 64 unique named slots are supported. Scalar values occupy X of a float4. Texture slots contain the texture SRV index in X and sampler index in Y, both represented as floats. Missing material overrides use the schema defaults. Materials serialize overrides under `properties`; shader schemas and their default texture dependencies are included in cooked builds.

The array order defines the shader slot indices. Keep the order stable after authoring a shader; reordering the schema requires updating HLSL slot numbers. This is an explicit material interface, not automatic reflection of arbitrary HLSL local variables or cbuffer declarations.

```hlsl
#include "MeshCommon.hlsli"
MeshPSOut main(MeshVSOut i) {
    DrawData d = LoadDraw(g_Push.drawBuffer, g_Push.drawIndex);
    float4 tint = MaterialProperty(0, float4(1,1,1,1));
    float brightness = MaterialFloat(1, 1);
    float4 pattern = MaterialTexture(2, i.uv);
    MeshPSOut o;
    o.color = tint * pattern * brightness;
    o.entityId = d.ids.z;
    return o;
}
```

Compile with Compile Pixel Shader in the source inspector. The 144-byte DrawData layout is preserved; material slots use an additional storage buffer referenced by extended mesh push constants. Older shaders continue to use the initial push fields. New shaders using material properties must be compiled against the updated MeshCommon.hlsli.

Scripts can override a MeshRenderer's material instance:

```csharp
var renderer = GetComponent<Stardust.MeshRenderer>();
renderer.SetFloat("Brightness", 2);
renderer.SetColor("Tint", new Stardust.Color(.2f, .8f, 1));
renderer.SetVector("WindDirection", new Stardust.Vector2(1, 0));
renderer.SetTexture("Pattern", "Textures/Other.png");
```

These overrides detach shared material references from the renderer and preserve the current material's other properties. They do not edit a shared `.mat` asset. Use the inspector to save permanent shared material edits.

## Water controls

Select the Water `.mat` asset, or the material assigned to a water mesh. Its property list includes:

- Shallow Water / Deep Water: scattering colors.
- Wave Speed: zero freezes all surface motion; negative values reverse time.
- Wind Direction: X/Z travel direction; zero falls back to +X.
- Wave Height, Wavelength Scale, Choppiness and Small Ripples.
- Wave Direction Spread: narrow swells through a broader crossing spectrum.
- Foam Amount and Shore Foam Width.
- RGB Absorption, Surface Roughness and Refraction Strength.

The spectrum combines eight wavelengths with independent dispersion periods. Direction is relative to the chosen wind, and animation time is not wrapped into a short repeating loop. Reflections use visible scene geometry with a sky fallback. Screen-space limits remain: off-screen objects cannot appear in reflections, and there is no underwater pass or fluid simulation.

The same property schema and MaterialProperty/MaterialFloat/MaterialTexture helpers work in canvas custom shaders using MeshCommon.hlsli and sky shaders using SkyCommon.hlsli. Recompile those sources against the updated headers.
