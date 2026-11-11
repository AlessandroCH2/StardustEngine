using System;
using System.Collections.Generic;
using System.Linq;
using Stardust;

namespace Stardust.Tilemaps {

/// <summary>Flips and quarter turns of a placed tile (same bits as <see cref="SpriteQuad.flags"/>).</summary>
[Flags]
public enum TileFlags { None = 0, FlipX = 1, FlipY = 2, Rotate90 = 4, Rotate180 = 8, Rotate270 = 12 }

/// <summary>
/// A grid of tiles on the entity's local XY plane. The tiles live in the Sprite Batch on the same object (one quad per
/// cell), which draws them in a few large meshes and saves them with the scene. Cell (0, 0) covers local (0, 0) to
/// <see cref="CellSize"/>; x grows right and y grows up. Use several Tilemaps for layers, with different Sorting Orders.
/// Change cells with the methods below, then call <see cref="Refresh"/> once to send the changes to the batch.
/// </summary>
public class Tilemap : ScriptBehaviour {
    /// <summary>The ".tileset" asset the tiles come from.</summary>
    public string Tileset = "";
    public Vector2 CellSize = new(1, 1);
    public Color Tint = new(1, 1, 1, 1);
    public int SortingOrder;
    /// <summary>Builds 2D colliders over the solid tiles when the game starts (full tiles merged into as few boxes as possible).</summary>
    public bool Colliders;
    public float Friction = 0.4f;
    public float Bounciness;
    /// <summary>Tiles saved by version 1.0 of the package; moved into the Sprite Batch the first time the map loads.</summary>
    [HideInInspector] public string Cells = "";

    // ------------------------------------------------------------------ data
    // One cache per Tilemap object, kept across the editor's per-frame script copies and refreshed when the batch changes
    sealed class MapData {
        public string version = "", tilesetPath = "";
        public Vector2 cellSize;
        public Tileset tileset;
        public readonly Dictionary<long, int> cells = new();   // cell -> tile | flags << 24
        public readonly Dictionary<long, int> slotOf = new();  // cell -> quad index in the batch
        public readonly List<SpriteQuad> quads = new();
        public readonly Stack<int> free = new();
        public readonly SortedSet<int> dirty = new();
        public bool rebuild;                                  // send everything (textures, animations, quads)
        public bool boundsValid; public int minX, minY, maxX, maxY;
        public Dictionary<int, int> animationOf = new();      // tile -> animation index in the batch
    }
    static readonly Dictionary<ulong, MapData> s_Maps = new();

    static long Key(int x, int y) => (long)x << 32 | (uint)y;
    static (int x, int y) Unkey(long key) => ((int)(key >> 32), (int)(uint)key);
    const int IndexMask = 0xFFFFFF;

    // Resolved once per script instance: editors get a fresh instance every frame, the running game keeps one
    MapData m_Data;
    MapData Data => m_Data ??= Resolve();
    /// <summary>Forgets cached tiles and reads them again from the Sprite Batch (after changing it from elsewhere).</summary>
    public void Reload() { s_Maps.Remove(ID); m_Data = null; }
    /// <summary>True when cells changed since the last <see cref="Refresh"/>.</summary>
    public bool hasPendingChanges => Data.rebuild || Data.dirty.Count > 0;

    MapData Resolve() {
        {
            var batch = GetComponent<SpriteBatch>();
            s_Maps.TryGetValue(ID, out var map);
            string version = batch?.version ?? "";
            var tileset = Tilemaps.Tileset.Load(Tileset);
            bool settingsChanged = map != null && (map.tilesetPath != Tileset || map.cellSize.x != CellSize.x || map.cellSize.y != CellSize.y || !ReferenceEquals(map.tileset, tileset));
            if (map != null && map.version == version && !settingsChanged) return map;
            if (map != null && settingsChanged && map.version == version) { // same tiles, new tileset or cell size: redraw them all
                map.tilesetPath = Tileset; map.cellSize = CellSize; map.tileset = tileset;
                RebuildQuads(map);
                return map;
            }
            map = new MapData { tilesetPath = Tileset, cellSize = CellSize, tileset = tileset, version = version };
            s_Maps[ID] = map;
            if (!string.IsNullOrEmpty(Cells)) { LoadLegacyCells(map); return map; }
            if (batch != null && tileset != null && tileset.isValid) {
                // The batch's quads are the saved map: cell from the position, tile from the texture rectangle
                var quads = batch.GetQuads();
                for (int i = 0; i < quads.Length; ++i) {
                    var q = quads[i];
                    map.quads.Add(q);
                    if (q.isEmpty) { map.free.Push(i); continue; }
                    int tile = tileset.TileAt(q.rectX, q.rectY);
                    if (tile < 0) { map.free.Push(i); continue; }
                    int x = (int)MathF.Round(q.position.x / Math.Max(1e-4f, CellSize.x)), y = (int)MathF.Round(q.position.y / Math.Max(1e-4f, CellSize.y));
                    map.cells[Key(x, y)] = tile | (q.flags & 15) << 24;
                    map.slotOf[Key(x, y)] = i;
                }
                BuildAnimationTable(map);
            }
            return map;
        }
    }

    void LoadLegacyCells(MapData map) {
        try {
            var bytes = Convert.FromBase64String(Cells);
            for (int i = 0; i + 12 <= bytes.Length; i += 12)
                map.cells[Key(BitConverter.ToInt32(bytes, i), BitConverter.ToInt32(bytes, i + 4))] = BitConverter.ToInt32(bytes, i + 8);
        } catch (FormatException) { Debug.LogWarning("Tilemap: unreadable tile data was cleared"); }
        Cells = "";
        RebuildQuads(map);
    }

    void BuildAnimationTable(MapData map) {
        map.animationOf.Clear();
        if (map.tileset == null) return;
        foreach (var tile in map.tileset.animations.Keys.OrderBy(t => t)) map.animationOf[tile] = map.animationOf.Count;
    }

    SpriteQuad MakeQuad(MapData map, int x, int y, int value) {
        int tile = value & IndexMask;
        var rect = map.tileset != null ? map.tileset.TileRect(tile) : default;
        return new SpriteQuad {
            position = new Vector2(x * CellSize.x, y * CellSize.y), size = CellSize,
            rectX = rect.x, rectY = rect.y, rectWidth = rect.width, rectHeight = rect.height,
            color = 0xFFFFFFFF, texture = 0,
            flags = (ushort)((value >> 24) & 15 | (map.animationOf.TryGetValue(tile, out int animation) ? (animation + 1) << 4 : 0)),
        };
    }

    // Every cell gets a fresh, compact slot
    void RebuildQuads(MapData map) {
        BuildAnimationTable(map);
        map.quads.Clear(); map.slotOf.Clear(); map.free.Clear(); map.dirty.Clear();
        foreach (var (key, value) in map.cells) {
            if (map.tileset != null && (value & IndexMask) >= map.tileset.tileCount) continue;
            var (x, y) = Unkey(key);
            map.slotOf[key] = map.quads.Count;
            map.quads.Add(MakeQuad(map, x, y, value));
        }
        map.rebuild = true; map.boundsValid = false;
    }

    void Write(MapData map, int x, int y, int value) {
        long key = Key(x, y);
        if (value < 0) {
            if (!map.cells.Remove(key)) return;
            if (map.slotOf.Remove(key, out int slot)) { map.quads[slot] = default; map.free.Push(slot); map.dirty.Add(slot); }
        } else {
            map.cells[key] = value;
            if (!map.slotOf.TryGetValue(key, out int slot)) {
                slot = map.free.Count > 0 ? map.free.Pop() : map.quads.Count;
                if (slot == map.quads.Count) map.quads.Add(default);
                map.slotOf[key] = slot;
            }
            map.quads[slot] = MakeQuad(map, x, y, value);
            map.dirty.Add(slot);
        }
        map.boundsValid = false;
    }
    static int Pack(int tile, TileFlags flags) => tile < 0 ? -1 : (tile & IndexMask) | ((int)flags & 15) << 24;

    // ------------------------------------------------------------------ tiles

    /// <summary>The tile at a cell, or -1 when empty.</summary>
    public int GetTile(int x, int y) => Data.cells.TryGetValue(Key(x, y), out var v) ? v & IndexMask : -1;
    public TileFlags GetTileFlags(int x, int y) => Data.cells.TryGetValue(Key(x, y), out var v) ? (TileFlags)(v >> 24) : TileFlags.None;
    public bool HasTile(int x, int y) => Data.cells.ContainsKey(Key(x, y));
    public int tileCount => Data.cells.Count;
    /// <summary>Every placed cell as (x, y, tile).</summary>
    public IEnumerable<(int x, int y, int tile)> GetTiles() { foreach (var (key, value) in Data.cells) { var (x, y) = Unkey(key); yield return (x, y, value & IndexMask); } }

    /// <summary>Places a tile (-1 clears the cell).</summary>
    public void SetTile(int x, int y, int tile, TileFlags flags = TileFlags.None) => Write(Data, x, y, Pack(tile, flags));
    public void ClearTile(int x, int y) => SetTile(x, y, -1);
    public void ClearAllTiles() { var map = Data; map.cells.Clear(); RebuildQuads(map); }

    /// <summary>Fills the rectangle between two cells (inclusive) with a tile, or clears it with -1.</summary>
    public void BoxFill(int x0, int y0, int x1, int y1, int tile, TileFlags flags = TileFlags.None) {
        var map = Data;
        for (int y = Math.Min(y0, y1); y <= Math.Max(y0, y1); ++y)
            for (int x = Math.Min(x0, x1); x <= Math.Max(x0, x1); ++x) Write(map, x, y, Pack(tile, flags));
    }

    /// <summary>Replaces the connected area of cells that hold the same tile as (x, y). An empty area stops at
    /// <paramref name="limit"/> cells, since it has no edge. <paramref name="pick"/> chooses the tile per cell (random brushes).</summary>
    public int FloodFill(int x, int y, int tile, TileFlags flags = TileFlags.None, int limit = 10000, Func<int, int, int> pick = null) {
        var map = Data;
        int from = GetTile(x, y);
        if (from >= 0 && pick == null && map.cells[Key(x, y)] == Pack(tile, flags)) return 0;
        var open = new Stack<(int, int)>();
        var seen = new HashSet<long>();
        open.Push((x, y));
        int changed = 0;
        while (open.Count > 0 && changed < limit) {
            var (cx, cy) = open.Pop();
            if (!seen.Add(Key(cx, cy)) || GetTile(cx, cy) != from) continue;
            Write(map, cx, cy, Pack(pick != null ? pick(cx, cy) : tile, flags));
            ++changed;
            open.Push((cx + 1, cy)); open.Push((cx - 1, cy)); open.Push((cx, cy + 1)); open.Push((cx, cy - 1));
        }
        return changed;
    }

    /// <summary>Paints a terrain cell and re-picks it and its four neighbours from the auto-tile set.</summary>
    public void SetAutoTile(int x, int y, AutoTile terrain, bool erase = false) {
        if (terrain == null) return;
        var map = Data;
        if (erase) Write(map, x, y, -1); else Write(map, x, y, terrain.tiles[15]);
        foreach (var (dx, dy) in new[] { (0, 0), (0, 1), (1, 0), (0, -1), (-1, 0) }) UpdateAutoTile(map, x + dx, y + dy, terrain);
    }
    void UpdateAutoTile(MapData map, int x, int y, AutoTile terrain) {
        int tile = GetTile(x, y);
        if (tile < 0 || !terrain.Contains(tile)) return;
        bool Same(int cx, int cy) { int t = GetTile(cx, cy); return t >= 0 && terrain.Contains(t); }
        int mask = (Same(x, y + 1) ? 1 : 0) | (Same(x + 1, y) ? 2 : 0) | (Same(x, y - 1) ? 4 : 0) | (Same(x - 1, y) ? 8 : 0);
        if (terrain.tiles[mask] != tile) Write(map, x, y, terrain.tiles[mask]);
    }

    /// <summary>Smallest and largest occupied cells; false when the map is empty.</summary>
    public bool GetBounds(out int minX, out int minY, out int maxX, out int maxY) {
        var map = Data;
        if (!map.boundsValid) {
            map.minX = map.minY = int.MaxValue; map.maxX = map.maxY = int.MinValue;
            foreach (var key in map.cells.Keys) { var (x, y) = Unkey(key); map.minX = Math.Min(map.minX, x); map.minY = Math.Min(map.minY, y); map.maxX = Math.Max(map.maxX, x); map.maxY = Math.Max(map.maxY, y); }
            map.boundsValid = true;
        }
        minX = map.minX; minY = map.minY; maxX = map.maxX; maxY = map.maxY;
        return map.cells.Count > 0;
    }
    /// <summary>Quads in the batch, empty slots included (the batch is compacted by <see cref="Compact"/>).</summary>
    public int slotCount => Data.quads.Count;
    /// <summary>Drops the empty slots left by erased tiles.</summary>
    public void Compact() { var map = Data; RebuildQuads(map); Refresh(); }

    // ------------------------------------------------------------------ coordinates

    /// <summary>The cell under a point on the local XY plane.</summary>
    public (int x, int y) LocalToCell(Vector3 local) =>
        ((int)MathF.Floor(local.x / Math.Max(1e-4f, CellSize.x)), (int)MathF.Floor(local.y / Math.Max(1e-4f, CellSize.y)));
    /// <summary>The cell under a world position (the map may be moved, turned or scaled).</summary>
    public (int x, int y) WorldToCell(Vector3 world) {
        var p = world - transform.position;
        var s = transform.localScale;
        return LocalToCell(new Vector3(p.x / (s.x == 0 ? 1 : s.x), p.y / (s.y == 0 ? 1 : s.y), 0));
    }
    /// <summary>Bottom-left corner of a cell on the local XY plane.</summary>
    public Vector2 CellToLocal(int x, int y) => new(x * CellSize.x, y * CellSize.y);
    public Vector3 CellCenterLocal(int x, int y) => new((x + 0.5f) * CellSize.x, (y + 0.5f) * CellSize.y, 0);

    // ------------------------------------------------------------------ drawing

    /// <summary>Sends the changes to the Sprite Batch (adds one if the object has none): only the quads that changed,
    /// or everything after the tileset or cell size changed.</summary>
    public void Refresh() {
        var map = Data;
        var batch = GetComponent<SpriteBatch>() ?? AddComponent<SpriteBatch>();
        if (map.tileset == null || !map.tileset.isValid) { if (batch.quadCount > 0) batch.Clear(); map.version = batch.version; return; }
        if (batch.quadCount != map.quads.Count && !map.rebuild && map.dirty.Count == 0) map.rebuild = true;
        if (map.rebuild || map.dirty.Count > map.quads.Count / 3) {
            batch.SetAnimations(map.animationOf.OrderBy(p => p.Value).Select(p => new SpriteAnimation {
                fps = map.tileset.animations[p.Key].fps,
                frames = map.tileset.animations[p.Key].frames.Select(map.tileset.TileRect).ToArray(),
            }).ToArray());
            batch.SetQuads(new[] { map.tileset.texture }, map.quads.ToArray());
        } else if (map.dirty.Count > 0) {
            // Runs of nearby slots go in one update each
            var all = map.quads.ToArray();
            int start = -1, end = -1;
            foreach (int slot in map.dirty) {
                if (start >= 0 && slot - end > 64) { batch.SetQuadRange(start, all.AsSpan(start, end - start + 1)); start = -1; }
                if (start < 0) start = slot;
                end = slot;
            }
            if (start >= 0) batch.SetQuadRange(start, all.AsSpan(start, end - start + 1));
        }
        map.rebuild = false; map.dirty.Clear();
        if (batch.sortingOrder != SortingOrder) batch.sortingOrder = SortingOrder;
        var tint = batch.color;
        if (tint.r != Tint.r || tint.g != Tint.g || tint.b != Tint.b || tint.a != Tint.a) batch.color = Tint;
        map.version = batch.version;
    }

    // ------------------------------------------------------------------ colliders

    void OnCreate() {
        if (!string.IsNullOrEmpty(Cells)) Refresh();
        if (Colliders) BuildColliders();
    }

    /// <summary>Creates child objects with Box Collider 2D components over the solid tiles: full tiles are merged into
    /// rectangles, tiles with a custom box get their own.</summary>
    public void BuildColliders() {
        var map = Data;
        var full = new HashSet<long>();
        var custom = new List<(int x, int y, TileCollider box)>();
        foreach (var (key, value) in map.cells) {
            var box = map.tileset != null ? map.tileset.ColliderOf(value & IndexMask) : TileCollider.Full;
            if (!box.solid) continue;
            var (x, y) = Unkey(key);
            if (box.isFull) full.Add(key); else custom.Add((x, y, box));
        }
        if (full.Count == 0 && custom.Count == 0) return;
        var root = Entity.Create("Tilemap Colliders");
        root.parent = this;
        root.transform.localPosition = new Vector3(0, 0, 0);
        void AddBox(float cx, float cy, float w, float h) {
            var child = Entity.Create("Collider");
            child.parent = root;
            child.transform.localPosition = new Vector3(cx, cy, 0);
            var collider = child.AddComponent<BoxCollider2D>();
            collider.size = new Vector2(w, h);
            collider.friction = Friction;
            collider.bounciness = Bounciness;
        }
        if (full.Count > 0 && GetBounds(out int minX, out int minY, out int maxX, out int maxY))
            // Greedy rectangles: grow right along a row, then up while the whole span is solid
            for (int y = minY; y <= maxY; ++y)
                for (int x = minX; x <= maxX; ++x) {
                    if (!full.Contains(Key(x, y))) continue;
                    int width = 1;
                    while (full.Contains(Key(x + width, y))) ++width;
                    int height = 1;
                    for (bool grow = true; grow;) {
                        for (int i = 0; i < width; ++i) if (!full.Contains(Key(x + i, y + height))) { grow = false; break; }
                        if (grow) ++height;
                    }
                    for (int j = 0; j < height; ++j) for (int i = 0; i < width; ++i) full.Remove(Key(x + i, y + j));
                    AddBox((x + width * 0.5f) * CellSize.x, (y + height * 0.5f) * CellSize.y, width * CellSize.x, height * CellSize.y);
                }
        foreach (var (x, y, box) in custom)
            AddBox((x + box.x + box.width * 0.5f) * CellSize.x, (y + box.y + box.height * 0.5f) * CellSize.y, box.width * CellSize.x, box.height * CellSize.y);
    }
}

}
