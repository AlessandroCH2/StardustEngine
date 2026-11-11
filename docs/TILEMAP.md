# 2D Tilemap package

`com.stardust.tilemap` is an official optional package for grid-based 2D levels. It is written in C# on top of engine features that any script can use: the **Sprite Batch** component, **2D physics**, and **Scene view tools** for custom editors (`OnSceneGUI`).

## Install

Official packages ship with the engine but are not added to projects automatically (only the core package is).

1. Open **Window → Package Manager** (also reachable from **File → Project Settings → Packages**).
2. Choose **Stardust Packages** in the Packages menu, select **2D Tilemap** and click **Install**.

The Package Manager lists the packages in the project and the official ones, with version, description, dependencies and samples (**Import** copies a sample into `Assets/Samples`). **Update** appears when the engine brings a newer version, **Remove** moves the package to the project's `Trash` folder. The **+** button adds a package from a folder on disk or from a git URL (`#tag` picks a version, `?path=folder` points inside the repository; Git must be installed); git packages get **Pull Latest**.

## Quick start

1. Create an empty object and add the **Stardust.Tilemaps.Tilemap** script with **Add Component**.
2. In the **Settings** tab, set **Tileset** to `Packages/com.stardust.tilemap/Samples/Tiles.tileset`, or make your own: choose a texture and the size of one tile in pixels, then **Create Tileset**.
3. Pick a tile in the **Paint** tab and paint in the Scene view. The **2D** button of the Scene view gives a flat view of the XY plane.

The Tilemap stores its tiles in a **Sprite Batch** on the same object (one quad per cell), which draws them and saves them with the scene. Maps made with version 1.0 are converted the first time they are opened.

## Inspector tabs

| Tab | Content |
| --- | --- |
| Paint | Tools, brush kind, flips and rotation, the palette, Open Tile Palette |
| Tile | Properties of the selected tile: collider and animation, saved into the tileset |
| Settings | Tileset, Cell Size, Tint, Sorting Order |
| Collider | Build Colliders, Friction, Bounciness |
| Info | Tile count, used area, batch slots, Compact, Clear All |

## Tools and brushes

| Tool | Scene view, left mouse |
| --- | --- |
| Select | Normal clicks: select objects, use the gizmo |
| Paint | Places the brush; drag to paint. Shift erases |
| Erase | Clears cells |
| Box | Drag a rectangle; it is filled on release. Shift clears it |
| Fill | Flood fills the connected area of the same tile (or the empty area, up to 10,000 cells). Shift clears |
| Pick | Takes the tile under the cursor, with its flips and rotation, and switches to Paint |

| Brush | What Paint, Box and Fill place |
| --- | --- |
| Single | The selected tile, with Flip X, Flip Y and Rotate (the icons next to the tools) |
| Stamp | A block of tiles: drag a rectangle in the palette. Paint places the whole block at the cursor; Box and Fill repeat it |
| Random | A random tile per cell: Ctrl + click tiles in the palette to add or remove them, or choose a weighted set of the tileset |
| Auto-tile | A terrain from the tileset: each cell picks the tile that joins its neighbours, and the neighbours update too |

The Scene view shows the cell and tile under the cursor next to the brush. Hold **Alt** to move the camera without painting. **Tilemap → Tile Palette** in the Scripts menu opens the palette in its own window (with tile properties). Each stroke is one undo step.

## Tilesets

A tileset is a `.tileset` JSON file. Tiles are numbered from 0, left to right and top to bottom.

```json
{
  "texture": "Tiles/Terrain.png",
  "tileWidth": 16, "tileHeight": 16, "margin": 0, "spacing": 0,
  "solid": [0, 1, 2, 5],
  "colliders": { "10": [0, 0.6, 1, 0.4], "12": [] },
  "animations": { "8": { "frames": [8, 24, 25, 24], "fps": 3 } },
  "autotiles": [ { "name": "Dirt Path", "tiles": [32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47] } ],
  "random": [ { "name": "Ground variants", "tiles": [3, 19], "weights": [4, 1] } ]
}
```

- `margin` is the border around the sheet and `spacing` the gap between tiles, in pixels.
- `solid` lists the tiles with a full-cell collider (leave it out to make every tile solid). `colliders` overrides single tiles: a box `[x, y, width, height]` in cell fractions from the bottom-left, or `[]` for none. The **Tile** tab edits both.
- `animations` cycle a tile through other tiles of the sheet at `fps` frames per second. Animated tiles share one small mesh that is rebuilt only when a frame changes, so they cost almost nothing. The **Tile** tab edits them.
- `autotiles` are terrains of 16 tiles indexed by which neighbours are the same terrain: 1 up, 2 right, 4 down, 8 left (tile 0 = alone, tile 15 = surrounded).
- `random` lists named sets for the Random brush, with optional weights.

The sample tileset has animated water, coins, a flower and lava, a dirt-path auto-tile set and two random sets. For pixel art set the texture's **Filter** to **Point** and turn **Mipmaps** off (the sample does this in `Tiles.png.meta`); bilinear textures work too, the batch stays half a texel inside each tile so neighbours never bleed in.

## Colliders and 2D physics

With **Build Colliders** on, the map creates a `Tilemap Colliders` child when the game starts: full tiles are merged into as few **Box Collider 2D** rectangles as possible, tiles with a custom box get their own. They use the map's **Friction** and **Bounciness**.

2D physics is part of the engine and works without the package:

| Component | Use |
| --- | --- |
| Rigidbody 2D | Body Type (Static, Dynamic, Kinematic), Mass, damping, Gravity Scale, Freeze Rotation. Moves on XY, turns around Z only |
| Box Collider 2D | Size and Offset |
| Circle Collider 2D | Radius and Offset |
| Capsule Collider 2D | Size (width, height) and Offset, vertical |

All 2D colliders have Friction, Bounciness and Is Trigger. Objects at different Z still meet, as in a 2D world. Collision and trigger callbacks (`OnCollision`, `OnTriggerEnter`...) are the same as in 3D.

```csharp
var body = GetComponent<Rigidbody2D>();
body.velocity = new Vector2(Input.GetAxis("Horizontal") * 6, body.velocity.y);
if (Input.GetKeyDown(KeyCode.Space) && Physics2D.Raycast(new Vector2(transform.position.x, transform.position.y), new Vector2(0, -1), 0.6f, this))
    body.AddForce(new Vector2(0, 7), ForceMode2D.Impulse);
```

`Rigidbody2D` has `velocity`, `angularVelocity` (degrees per second), `AddForce`, `AddForceAtPosition`, `AddTorque`, `MovePosition`, `bodyType`, `mass`, `drag`, `angularDrag`, `gravityScale` and `freezeRotation`. `Physics2D.Raycast(origin, direction, distance, ignore)` returns a `RaycastHit2D` (true when it hit) with `entity`, `point`, `normal` and `distance`.

## Scripting the map

```csharp
using Stardust;
using Stardust.Tilemaps;

public class LevelBuilder : ScriptBehaviour
{
    public Entity Map;

    void OnCreate()
    {
        var map = Map.As<Tilemap>();
        map.BoxFill(0, 0, 31, 1, 3);           // ground
        map.SetTile(10, 4, 14);                // a coin (animated by the tileset)
        map.SetTile(11, 4, 0, TileFlags.FlipX);
        map.Refresh();                         // send the changes once, after a batch of edits

        var (x, y) = map.WorldToCell(transform.position);
        if (map.GetTile(x, y) == 14) { map.ClearTile(x, y); map.Refresh(); }
    }
}
```

| Member | Use |
| --- | --- |
| `GetTile(x, y)`, `GetTileFlags(x, y)`, `HasTile(x, y)`, `tileCount`, `GetTiles()` | Read cells (`-1` = empty) |
| `SetTile(x, y, tile, flags)`, `ClearTile`, `ClearAllTiles` | Change cells |
| `BoxFill(x0, y0, x1, y1, tile, flags)`, `FloodFill(x, y, tile, flags, limit, pick)` | Fill areas (`tile = -1` clears; `pick` chooses a tile per cell) |
| `SetAutoTile(x, y, terrain, erase)` | Paint or erase auto-tiled terrain (`Tileset.autoTiles`) |
| `GetBounds(out minX, out minY, out maxX, out maxY)` | Occupied area |
| `LocalToCell`, `WorldToCell`, `CellToLocal`, `CellCenterLocal` | Coordinates |
| `Refresh()` | Sends pending changes to the Sprite Batch: only the changed quads, or everything after a tileset or cell size change |
| `Compact()`, `slotCount` | Erased tiles leave empty slots in the batch; Compact drops them |
| `BuildColliders()` | Creates the 2D collider boxes (done at start when Build Colliders is on) |
| `Reload()` | Reads the tiles again after the Sprite Batch was changed by another script |
| `Tileset.Load(path)` | `tileCount`, `columns`, `rows`, `TileRect`, `TileAt`, `ColliderOf`, `animations`, `autoTiles`, `randomBrushes` |

## Performance

- Tiles are not copied between the engine and C# every frame: the map keeps a cache per object and only checks the batch's version.
- Painting sends just the changed quads (`SpriteBatch.SetQuadRange`); the batch rebuilds only the 4096-quad chunks they belong to.
- Each chunk is drawn with one mesh per texture and colour; animated tiles share one small extra mesh rebuilt when a frame changes.

A 26,000-tile map stays at full frame rate in the editor while selected.

## Limits

- One tileset per Tilemap; use another Tilemap (a child object) for tiles from a different texture.
- 2D colliders are boxes, circles and capsules; there is no polygon collider yet, so slopes need small boxes.
- The C# editor API cannot read the keyboard in the Scene view yet: flips, rotation and brushes are chosen in the Inspector or the Tile Palette.
