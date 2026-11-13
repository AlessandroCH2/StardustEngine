# Shaders, Shader Graph and the Script Editor

Stardust has three ways to write a material shader:

| Asset | What it is | Edited with |
| --- | --- | --- |
| `.shader` | A text shader: properties, render state and HLSL in one file | Script Editor (or any text editor) |
| `.shadergraph` | A node graph that becomes a `.shader` | Shader Graph window |
| `.ps.hlsl` | A raw pixel shader with a JSON property schema ([Custom material properties](CUSTOM_SHADER_PROPERTIES.md)) | any text editor |

`.shader` and `.shadergraph` files compile automatically when they are created, saved, or changed on disk
(the editor watches the Assets folder). The result is `<asset>.spv` plus `<asset>.spv.properties.json`
(properties and render state) next to the source. To use one, create a material and set its **Custom Shader** to the
`.shader` or `.shadergraph` (the inspector of a shader also has **Create Material**). Builds include the compiled shader.

Create them from the Project panel: **Create Shader > Standard Surface Shader / Unlit Shader / Lit Shader Graph /
Unlit Shader Graph**.

## .shader files

```hlsl
Shader "Custom/Rock"
{
    Properties
    {
        _Color ("Color", Color) = (1, 1, 1, 1)
        _MainTex ("Albedo", 2D) = "white" {}
        [Normal] _BumpMap ("Normal Map", 2D) = "bump" {}
        _Glossiness ("Smoothness", Range(0, 1)) = 0.5
        _Metallic ("Metallic", Range(0, 1)) = 0
    }
    SubShader
    {
        Tags { "Queue" = "Geometry" }
        Cull Back

        HLSLPROGRAM
        #pragma surface surf Standard

        void surf(SurfaceInput IN, inout SurfaceOutputStandard o)
        {
            float4 c = tex2D(_MainTex, IN.uv) * _Color;
            o.Albedo = c.rgb;
            o.Normal = UnpackNormal(tex2D(_BumpMap, IN.uv));
            o.Metallic = _Metallic;
            o.Smoothness = _Glossiness;
        }
        ENDHLSL
    }
}
```

### Properties

`_Name ("Label", Type) = default`, one per line. Types: `Float`, `Range(min, max)`, `Int`, `Color`, `Vector`, `2D`.
Texture defaults are `"white"`, `"black"`, `"gray"`, `"bump"` or an asset path. Attributes: `[Toggle]` (a check box),
`[Normal]` (linear normal map); others such as `[HDR]` are accepted and ignored. Up to 64 properties.

In the HLSL code a property is used by its name: `_Color` is a `float4`, `_Glossiness` a `float`, and a texture is a
slot read with `tex2D(_MainTex, uv)`. Materials show the properties in the Inspector, and scripts change them with
`MeshRenderer.SetFloat / SetColor / SetVector / SetTexture`.

### Render state

Written in the SubShader or Pass, outside the HLSL block:

| Command | Values |
| --- | --- |
| `Tags { "Queue" = "Transparent" }` | `Geometry` (default) or `Transparent` (sorted, drawn after opaque objects, no shadows cast) |
| `Blend` | `SrcAlpha OneMinusSrcAlpha` (alpha), `One One` and `SrcAlpha One` (additive), `One OneMinusSrcAlpha` (premultiplied), `Off` |
| `ZWrite` | `On`, `Off` (default Off when transparent) |
| `Cull` | `Back`, `Front`, `Off` |

Any `Blend` other than `Off` makes the shader transparent.

### Entry points

| Pragma | Function | Result |
| --- | --- | --- |
| `#pragma surface surf Standard` | `void surf(SurfaceInput IN, inout SurfaceOutputStandard o)` | Lit like Stardust/Standard: sun and shadows, ambient, point and spot lights, fog |
| `#pragma surface surf Unlit` | same | `Albedo + Emission`, no lighting |
| `#pragma fragment frag` | `float4 frag(SurfaceInput IN)` | the returned colour |

Options after a surface pragma: `alpha` / `alpha:fade` (transparent), `alpha:premul`, `alphatest:_Cutoff` (discards
pixels whose Alpha is below the property).

`SurfaceInput`: `worldPos`, `worldNormal`, `worldTangent`, `worldBitangent`, `viewDir` (towards the camera), `uv`,
`screenPos` (0..1), `time`, `frontFace`.
`SurfaceOutputStandard`: `Albedo`, `Normal` (tangent space), `Emission`, `Metallic`, `Smoothness`, `Occlusion`, `Alpha`.

### Porting existing shaders

Many existing surface shaders written in this block format work with few or no changes: `CGPROGRAM/ENDCG`, `fixed`/`half` types, `_Time`, `_SinTime`,
`_CosTime`, `_WorldSpaceCameraPos`, `UnpackNormal`, `TRANSFORM_TEX`, and a `struct Input` with members such as
`uv_MainTex`, `worldPos`, `worldNormal`, `viewDir`, `screenPos`, `worldRefl`. Redeclared property uniforms
(`sampler2D _MainTex; fixed4 _Color;`) and `#include` lines of `.cginc` headers are ignored. Vertex functions
(`#pragma vertex`, `vertex:vert`), multiple passes, grab passes and separate vertex/fragment programs are not supported:
use `#pragma fragment frag` with `SurfaceInput` instead.

Compile errors point at the line of the `.shader` file; the Script Editor shows the first error and jumps to it on click.

## Shader Graph

Double-click a `.shadergraph` to open it.

- **Blackboard** (left): the graph settings (Lit or Unlit, Opaque or Transparent, blend mode, Two Sided, Alpha Clipping)
  and the material properties. Add properties with **+**, then drag them onto the graph. The **Reference** is the name
  materials and scripts use.
- **Graph**: right-click (or drop a link on empty space) to add a node, with a search field. Drag from a pin to connect;
  dragging a connected input moves its link. Unconnected inputs are edited in place. Middle mouse or Alt+drag pans,
  the wheel zooms. Delete removes the selected node, Ctrl+D duplicates it, Ctrl+Z / Ctrl+Y undo and redo.
- **Save** (Ctrl+S) writes the graph and compiles it. **Show Code** shows the `.shader` it generates.

Nodes: Float, Vector 2/3/4, Color, Property, UV, Time, Position, Normal Vector, View Direction, Screen Position, Camera
Position; Sample Texture 2D, Sample Normal Map; Add, Subtract, Multiply, Divide, Power, Minimum, Maximum, Lerp, Clamp,
Step, Smoothstep, Modulo, Remap, Saturate, Absolute, Sine, Cosine, Fraction, Floor, Ceiling, Round, Square Root, Normalize,
One Minus, Negate; Dot Product, Cross Product, Length, Distance, Reflect; Split, Combine; Tiling And Offset, Panner,
Rotate; Simple Noise, Gradient Noise, Voronoi, Checkerboard; Fresnel Effect, Normal Strength.

Math nodes adapt to their inputs (float to float4); pin colours show the size. The Fragment Output takes Base Color,
Normal, Metallic, Smoothness, Emission, Ambient Occlusion, Alpha and Alpha Clip Threshold (the pins depend on the settings).

## Script Editor

Double-clicking a `.cs`, `.shader` or `.hlsl` file opens it in the Script Editor (**Window > Script Editor**);
**Open Externally** hands it to the system editor.

- C#, HLSL and `.shader` colouring, line numbers, several files in tabs, files reloaded when changed outside.
- Autocompletion while typing, or Ctrl+Space: the engine API and the game scripts (read from the compiled assemblies),
  language keywords, and the words of the file. After a dot it lists the members of the type: `Input.`, `Vector3.`,
  a variable declared in the file (`Rigidbody body` or `var body = GetComponent<Rigidbody>()`), or a member of the
  script such as `transform.`. Up/Down choose, Enter or Tab insert, Esc closes.
- Ctrl+S save, Ctrl+Z / Ctrl+Y undo and redo, Ctrl+F / F3 find, Ctrl+/ comment, Tab / Shift+Tab indent, Ctrl+D duplicate
  line, Ctrl+X / Ctrl+C on an empty selection cut or copy the line, automatic indentation and bracket closing.

Saving a C# script rebuilds the game scripts as before; saving a shader compiles it.
