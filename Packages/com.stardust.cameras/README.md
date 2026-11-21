# Virtual Cameras

Game cameras without camera code. Each **Virtual Camera** describes a shot (follow the player, orbit around
it, ride a rail, frame a door); the **Camera Director** on the real camera shows the one with the highest
priority and blends when it changes.

- `Scripts/` — runtime: `VirtualCamera`, `CameraDirector`, `CameraShake`, `CameraMath` (namespace `Stardust.Cameras`).
- `Editor/` — Virtual Camera inspector and Scene view gizmos.

Install it from **File → Project Settings → Package Manager**. Full guide: `docs/CAMERAS.md` in the engine repository.
