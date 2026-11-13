# Editor 0.6 workflows

## Animation authoring

Select the animated object, then open an `.anim` asset from Project. The Animation window docks with the bottom Project/Console area on first use; drag its tab to change docking. Preview Selected samples a temporary scene copy visible in Scene. Stop Preview restores the authored scene. Physics and gameplay scripts do not run in preview.

Press Record Selected, move the playhead, and move/rotate/scale the selected object or its descendants in Scene. Changes create position, rotation and scale keys at the rounded sample frame. Inspector transform edits also record. At the first nonzero key, the original pose is inserted at time zero. Multiple selected descendants record their own tracks. Save writes the clip; leaving preview does not apply temporary transforms to the scene.

The window supports frame stepping, timeline scrubbing, movable keys, dope sheet/curves, Linear/Constant/Cubic interpolation and cubic in/out tangents. Animator controllers use the graph editor, states, parameters, conditions, exit time and transition duration. Animation clips and controllers are different assets/windows: record keyframes in Animation; configure state transitions in Animator. Animation events, blend trees, humanoid retargeting and layered masks are not implemented.

## Selection and multiple objects

Ctrl-click toggles selection in Hierarchy; Shift-click selects a range. The Scene gizmo applies a shared world delta to selected roots. Selected descendants of a selected parent are skipped to avoid applying the transform twice. Local/Global selects the gizmo orientation.

The Multiple Objects inspector section edits activation, layer, absolute transforms, mesh material references and script enable state across the selection. Ordinary script-field edits propagate changed fields to matching script classes on other selected objects. Other component inspectors continue to show the active object. Do not assume all heterogeneous component types support bulk editing.

In perspective Scene view, a mesh in front of a canvas takes selection priority over that canvas. Non-drawable canvas roots do not intercept the whole screen. In 2D, canvas graphics remain above scene geometry as requested.

## Views and settings

Move/Rotate/Scale, Local/Global, Grid, 2D and gizmo toggles live in Scene. The main toolbar contains Play/Pause/Step. Game offers free aspect, 16:9, 16:10, 4:3, square, portrait, or a fixed render resolution with letterboxing. Fixed resolution changes the render target, not just the image display dimensions.

Project Settings separates General, Packages, Input, Layers and Tags. Input actions have editable names; press Enter to commit a unique nonempty name. Update script action names after renaming. Build Settings separates Scenes, Content Paks, Player and Export.

## Water

The base package supplies a Water material and prefab. Water now renders after a snapshot of opaque color/depth, with depth-based refraction/absorption, shallow-edge foam, directional surface displacement, fine ripple normals, Fresnel reflection and screen-space reflection with sky fallback. Built-in planes use a denser water grid. The material exposes explicit Wave Speed, Wind Direction, Wave Height, Wavelength Scale, Choppiness, Small Ripples, Wave Direction Spread, foam, absorption and refraction settings. See [Custom material properties](CUSTOM_SHADER_PROPERTIES.md).

Screen-space reflections can only reflect geometry visible in the camera. There is no planar reflection camera, underwater renderer, fluid simulation or automatic shore geometry. Quality also depends on lighting, scene scale and material tuning. The water plane is intended to be horizontal.

Settings navigation now stays above its scrolling content. Inactive tabs use a darker fill, and Scene transform tools and component headers use vector icons.
