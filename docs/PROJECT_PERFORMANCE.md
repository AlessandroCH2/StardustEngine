# Project loading and scene submission

New default scenes use FlyCamera, Rotator and ColorPulse from `Packages/com.stardust.core/Scripts`, under the Stardust.Engine namespace. They no longer depend on matching files in a project's Assets/Scripts folder. The core package updater supplies these files when opening a project. Existing authored scenes and project scripts are not renamed or rewritten.

Loose OBJ imports have a versioned binary cache in `<project>/Library/ImportedMeshes`. Subsequent opens can reuse vertices, indices and submesh slots instead of reparsing text and regenerating tangents. Source OBJ and MTL file timestamps/sizes invalidate the cache. Corrupt/truncated cache entries fall back to source import. Paks keep their existing virtual-file path and are not extracted for this cache. The first import still parses the source, and GPU resource uploads remain necessary on each process startup. Library is excluded by the .gitignore generated for new projects.

Inspector activation goes through Scene.SetActive, including multi-selection and render-list activation checkboxes. Runtime collider bodies follow the entire subtree on activation/deactivation and when reparenting between active/inactive parents. Disabling only MeshRenderer does not disable its Collider: component activation and GameObject activation remain separate.

Submission caches active hierarchy state and skips world-matrix work beneath inactive roots. Reverse-Z frustum culling includes near/far planes and handles infinite projections. Meshes are tested using transformed axis-aligned boxes rather than oversized spheres; each shadow cascade also tests its own light frustum. Offscreen casters outside every shadow volume are discarded before material preparation. Offscreen meshes skip material/texture preparation where possible; offscreen opaque shadow casters remain available to shadow passes. Custom shader render-state rules and particle bounds retain their special handling. Frustum culling is not occlusion culling: an object behind a building can still be submitted if it lies in the camera frustum.

Large undo snapshots are deferred until input has settled for 0.75 seconds. Merely changing selection no longer schedules multiple rebasing snapshots, and there is no periodic full-scene snapshot while nothing changes. Snapshot serialization itself still takes time on very large scenes.

Windows editor double-clicks use original mouse-message times and positions, plus the OS double-click interval. This recognizes a fast pair even if both arrive during one slow rendered frame, without treating physically slow clicks as a double click. Regular ImGui press/release event trickling remains enabled.

To run the CPU submission benchmark on Portvale, build StardustRuntimeTests with STARDUST_BUILD_TESTS=ON and set STARDUST_BENCH_PROJECT to the .sdproject path. It reports cold import/submission time and average CPU submission with City active/inactive, using a fixed camera and excluding terrain. This is a CPU resource test, not an end-to-end FPS measurement.

Project opening now displays the project name, current loading stage and progress. Mesh/material preparation is split across frames with an 8 ms batch budget, then pending texture decodes are pumped without waiting for unfinished jobs. Individual scene parsing, model import or terrain generation operations can still take longer than that budget. This also applies when opening a project from the command line. Texture sampler metadata is cached between frames (refresh within 0.5 seconds), instead of reading .meta files again for every visible view every frame.

## Shared transforms, batching and physics

Editor Scene/Game views and render-texture cameras share a transform snapshot during one render batch. The player does the same. Transform queries cache local inputs and parent revisions; direct position/rotation/scale edits, reparenting, reused entity IDs and reactivation after an ancestor changed are detected. Static geometry no longer rebuilds identical matrices. Standalone SubmitToRenderer calls still refresh transforms by default.

Repeated opaque meshes using built-in Standard, Unlit or Toon shaders are instanced by mesh/submesh and pipeline. Color, textures and entity IDs remain per instance, so different paint colors and editor picking are preserved. Custom shaders and transparent/UI draws keep their individual submission and ordering. Shadow casters sharing a mesh/submesh also instance within each cascade. Skinned instances with separate deformed meshes remain separate. The Stats window reports color instances, shadow instances and actual shadow draw calls.

Physics compares transforms before decomposing them or writing them to Jolt. Unchanged static city colliders require neither operation; dynamic bodies are ordered by hierarchy depth only when writing their results back. Static collider children still follow a moved/rotated/scaled parent, including a parent moved by physics.

Measured on the local AMD Radeon RX 6600, Release Vulkan build, Portvale:

| Measurement | Result |
| --- | --- |
| CPU submission benchmark, fixed camera, terrain excluded | 3.52 ms (previous measurement: 5.96 ms) |
| Runtime, 1280 x 720, 51 vehicles and 38 pedestrians | 14.99 ms mean / 66.7 FPS |
| Runtime frame time, 95th percentile | 19.42 ms |
| Physics profiler average in runtime sample | 2.65 ms |
| Transform preparation profiler average in runtime sample | 0.12 ms |

The runtime sample includes the game's UI and render-texture cameras, without editor panels or a second Scene view. It uses 180 warmup frames then 180 measured frames at a fixed 1/60 simulation step, with VSync and Vulkan validation disabled. It is a stationary camera sample, not a minimum-FPS guarantee for the whole map. Run it with STARDUST_GPU_BENCH_PROJECT set to the Portvale .sdproject and STARDUST_GPU_BENCH_RUNTIME=1 when launching StardustGpuFeatureTests. Avoid concurrent builds/benchmarks when comparing timings.
