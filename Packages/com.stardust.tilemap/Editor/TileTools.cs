using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using Stardust;
using Stardust.Editor;

namespace Stardust.Tilemaps.Editor {

public enum TileTool { Select, Paint, Erase, Box, Fill, Pick }
/// <summary>What Paint, Box and Fill place: one tile, a block of tiles, a random tile of a set, or an auto-tiled terrain.</summary>
public enum BrushMode { Single, Stamp, Random, AutoTile }

/// <summary>State shared by the Tilemap inspector, the Scene view tool and the Tile Palette window.
/// Editors are recreated every frame, so everything lives here.</summary>
public static class TileTools {
    public static TileTool tool = TileTool.Paint;
    public static BrushMode brush = BrushMode.Single;
    public static int selectedTile;
    /// <summary>Stamp: rectangle of the palette (in tile columns/rows), from the first tile picked to the second.</summary>
    public static int stampStart = -1, stampEnd = -1;
    /// <summary>Random: the tiles picked at random (with equal weight), unless a named random brush of the tileset is used.</summary>
    public static readonly List<int> randomTiles = new();
    public static int randomBrush = -1, autoTile;
    public static bool flipX, flipY;
    public static int quarterTurns;
    public static float paletteZoom = 40;
    /// <summary>The tileset of the Tilemap selected last: the Tile Palette shows it.</summary>
    public static string tileset = "";

    internal static (int x, int y) lastCell = (int.MinValue, int.MinValue);
    internal static (int x, int y) boxStart;
    internal static bool drawingBox;
    static readonly System.Random s_Random = new();

    public static TileFlags flags => (flipX ? TileFlags.FlipX : 0) | (flipY ? TileFlags.FlipY : 0) | (TileFlags)((quarterTurns & 3) << 2);

    static readonly (string icon, string tip)[] s_Tools = {
        ("select", "Select: normal clicks in the Scene view"), ("brush", "Paint (Shift erases)"), ("eraser", "Erase"),
        ("box", "Box fill: drag a rectangle (Shift clears)"), ("bucket", "Flood fill (Shift clears)"), ("picker", "Pick a tile from the map"),
    };
    static readonly string[] s_BrushNames = { "Single", "Stamp", "Random", "Auto-tile" };

    /// <summary>Tools as icons, then flips and rotation; below, the kind of brush and its options.</summary>
    public static void DrawToolbar(Tileset set) {
        for (int i = 0; i < s_Tools.Length; ++i) {
            if (i > 0) GUILayout.SameLine(2);
            if (GUILayout.IconButton(s_Tools[i].icon, tool == (TileTool)i, s_Tools[i].tip)) tool = (TileTool)i;
        }
        GUILayout.SameLine(14);
        if (GUILayout.IconButton("flipx", flipX, "Flip X")) flipX = !flipX;
        GUILayout.SameLine(2);
        if (GUILayout.IconButton("flipy", flipY, "Flip Y")) flipY = !flipY;
        GUILayout.SameLine(2);
        if (GUILayout.IconButton("rotate", quarterTurns != 0, $"Rotate 90 degrees (now {quarterTurns * 90})")) quarterTurns = (quarterTurns + 1) & 3;

        int chosen = GUILayout.Toolbar((int)brush, s_BrushNames);
        if (chosen != (int)brush) { brush = (BrushMode)chosen; if (tool is TileTool.Select or TileTool.Pick or TileTool.Erase) tool = TileTool.Paint; }
        if (brush == BrushMode.AutoTile) {
            if (set == null || set.autoTiles.Count == 0) EditorGUILayout.HelpBox("This tileset has no auto-tile terrains (\"autotiles\" in the .tileset).");
            else autoTile = Math.Clamp(EditorGUILayout.Popup("Terrain", autoTile, set.autoTiles.Select(a => a.name).ToArray()), 0, set.autoTiles.Count - 1);
        }
        if (brush == BrushMode.Random && set != null && set.randomBrushes.Count > 0) {
            var names = new[] { "Tiles picked below" }.Concat(set.randomBrushes.Select(r => r.name)).ToArray();
            randomBrush = EditorGUILayout.Popup("Random Set", randomBrush + 1, names) - 1;
        }
    }

    static IEnumerable<int> StampTiles(Tileset set) {
        if (stampStart < 0 || set.columns <= 0) yield break;
        int c = set.columns, end = stampEnd < 0 ? stampStart : stampEnd;
        for (int row = Math.Min(stampStart / c, end / c); row <= Math.Max(stampStart / c, end / c); ++row)
            for (int col = Math.Min(stampStart % c, end % c); col <= Math.Max(stampStart % c, end % c); ++col) yield return row * c + col;
    }

    /// <summary>The tileset's tiles in their texture layout. Click picks a tile, dragging picks a block (a stamp),
    /// Ctrl + click adds or removes a tile of the random set.</summary>
    public static void DrawPalette(Tileset set, float maxHeight = 0) {
        if (set == null || !set.isValid) { EditorGUILayout.HelpBox("Choose a tileset to see its tiles."); return; }
        int[] selected = brush switch {
            BrushMode.Stamp => StampTiles(set).ToArray(),
            BrushMode.Random => randomBrush >= 0 && randomBrush < set.randomBrushes.Count ? set.randomBrushes[randomBrush].tiles : randomTiles.ToArray(),
            BrushMode.AutoTile => Array.Empty<int>(),
            _ => new[] { selectedTile },
        };
        int[] highlighted = brush == BrushMode.AutoTile && autoTile < set.autoTiles.Count ? set.autoTiles[autoTile].tiles : Array.Empty<int>();
        var e = GUILayout.TileGrid(new TileGridOptions {
            texture = set.texture, columns = set.columns, rows = set.rows, tileWidth = set.tileWidth, tileHeight = set.tileHeight,
            margin = set.margin, spacing = set.spacing, cellSize = paletteZoom, maxHeight = maxHeight, selected = selected, highlighted = highlighted,
        });
        if (e.rectStart >= 0) { // a dragged block becomes the stamp
            brush = BrushMode.Stamp; stampStart = e.rectStart; stampEnd = e.rectEnd; selectedTile = e.rectStart;
        } else if (e.clicked >= 0) {
            selectedTile = e.clicked;
            if (e.control || brush == BrushMode.Random) {
                brush = BrushMode.Random; randomBrush = -1;
                if (!randomTiles.Remove(e.clicked)) randomTiles.Add(e.clicked);
            } else if (brush == BrushMode.Stamp) { stampStart = e.clicked; stampEnd = e.clicked; }
            else if (brush != BrushMode.AutoTile) brush = BrushMode.Single;
        }
        if (e.any && tool is TileTool.Select or TileTool.Erase or TileTool.Pick) tool = TileTool.Paint;
        string info = brush switch {
            BrushMode.Stamp => $"Stamp {StampSize(set).w} x {StampSize(set).h}",
            BrushMode.Random => randomBrush >= 0 ? $"Random: {set.randomBrushes[randomBrush].name}" : $"Random: {randomTiles.Count} tiles (Ctrl + click to add or remove)",
            BrushMode.AutoTile => "Auto-tile: neighbours join up as you paint",
            _ => $"Tile {selectedTile}",
        };
        EditorGUILayout.LabelField($"{info}  |  {set.columns} x {set.rows} tiles, {set.tileWidth}x{set.tileHeight} px  |  drag for a stamp");
    }

    internal static (int w, int h) StampSize(Tileset set) {
        if (stampStart < 0 || set.columns <= 0) return (1, 1);
        int c = set.columns, end = stampEnd < 0 ? stampStart : stampEnd;
        return (Math.Abs(end % c - stampStart % c) + 1, Math.Abs(end / c - stampStart / c) + 1);
    }

    /// <summary>The tile the brush places at a cell (stamp tiles repeat every stamp size, random tiles are drawn per cell).</summary>
    public static int TileFor(Tileset set, int x, int y, (int x, int y) origin) {
        switch (brush) {
        case BrushMode.Stamp when stampStart >= 0 && set.columns > 0: {
            int c = set.columns, end = stampEnd < 0 ? stampStart : stampEnd;
            int left = Math.Min(stampStart % c, end % c), top = Math.Min(stampStart / c, end / c);
            var (w, h) = StampSize(set);
            int dx = ((x - origin.x) % w + w) % w, dy = ((y - origin.y) % h + h) % h;
            // Palette rows go down, map rows go up: the stamp's top row lands on the highest cell
            return (top + (h - 1 - dy)) * c + left + dx;
        }
        case BrushMode.Random: {
            int[] tiles; float[] weights = Array.Empty<float>();
            if (randomBrush >= 0 && randomBrush < set.randomBrushes.Count) { tiles = set.randomBrushes[randomBrush].tiles; weights = set.randomBrushes[randomBrush].weights; }
            else tiles = randomTiles.ToArray();
            if (tiles.Length == 0) return selectedTile;
            if (weights.Length != tiles.Length) return tiles[s_Random.Next(tiles.Length)];
            float roll = (float)s_Random.NextDouble() * weights.Sum();
            for (int i = 0; i < tiles.Length; ++i) { roll -= weights[i]; if (roll <= 0) return tiles[i]; }
            return tiles[^1];
        }
        default:
            return selectedTile;
        }
    }

    // ------------------------------------------------------------------ tileset editing

    /// <summary>Writes a tileset's JSON back to its file.</summary>
    public static void Save(Tileset set) {
        if (set == null || string.IsNullOrEmpty(set.path)) return;
        string file = Path.Combine(set.path.StartsWith("Packages/") ? Application.GetProjectPath() : Application.GetDataPath(), set.path);
        File.WriteAllText(file, set.json.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
    }

    static string s_FramesText = "";
    static int s_FramesTile = -1;

    /// <summary>Collider and animation of the selected tile, saved into the tileset.</summary>
    public static void DrawTileProperties(Tileset set) {
        if (set == null || !set.isValid) return;
        int tile = Math.Clamp(selectedTile, 0, set.tileCount - 1);
        EditorGUILayout.LabelField($"Tile {tile}");
        var r = set.TileRect(tile);
        GUILayout.TextureButton(set.texture, r.x, r.y, r.width, r.height, 64, true);

        // Collider: none, full cell, or a custom box
        var collider = set.ColliderOf(tile);
        int kind = !collider.solid ? 0 : collider.isFull ? 1 : 2;
        int newKind = EditorGUILayout.Popup("Collider", kind, "None", "Full Cell", "Custom Box");
        var box = newKind == 2 ? (kind == 2 ? collider : new TileCollider { solid = true, x = 0, y = 0.5f, width = 1, height = 0.5f }) : collider;
        if (newKind == 2) {
            var pos = EditorGUILayout.Vector2Field("Box Position", new Vector2(box.x, box.y));
            var size = EditorGUILayout.Vector2Field("Box Size", new Vector2(box.width, box.height));
            box.x = Math.Clamp(pos.x, 0, 1); box.y = Math.Clamp(pos.y, 0, 1);
            box.width = Math.Clamp(size.x, 0.01f, 1 - box.x); box.height = Math.Clamp(size.y, 0.01f, 1 - box.y);
            EditorGUILayout.HelpBox("Fractions of the cell, from its bottom-left corner.");
        }
        if (newKind != kind || (newKind == 2 && (box.x != collider.x || box.y != collider.y || box.width != collider.width || box.height != collider.height))) {
            var colliders = set.json["colliders"] as JsonObject ?? new JsonObject();
            set.json["colliders"] = colliders;
            colliders.Remove(tile.ToString());
            bool solidByDefault = set.solid == null || set.solid.Contains(tile);
            if (newKind == 0 && solidByDefault) colliders[tile.ToString()] = new JsonArray();
            if (newKind == 1 && !solidByDefault) colliders[tile.ToString()] = new JsonArray(0, 0, 1, 1);
            if (newKind == 2) colliders[tile.ToString()] = new JsonArray(box.x, box.y, box.width, box.height);
            Save(set);
        }

        // Animation: frames as tile numbers
        EditorGUILayout.Space();
        set.animations.TryGetValue(tile, out var animation);
        if (s_FramesTile != tile) { s_FramesTile = tile; s_FramesText = animation != null ? string.Join(", ", animation.frames) : ""; }
        s_FramesText = EditorGUILayout.TextField("Animation Frames", s_FramesText);
        float fps = EditorGUILayout.FloatField("Frames Per Second", animation?.fps ?? 8);
        EditorGUILayout.HelpBox("Tile numbers shown in turn, e.g. \"8, 24, 25\". Empty = not animated.");
        var frames = s_FramesText.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                                 .Select(t => int.TryParse(t, out int v) ? v : -1).Where(v => v >= 0 && v < set.tileCount).ToArray();
        bool framesChanged = animation == null ? frames.Length > 0 : !frames.SequenceEqual(animation.frames) || Math.Abs(fps - animation.fps) > 1e-4f;
        if (framesChanged && GUILayout.Button("Apply Animation")) {
            var animations = set.json["animations"] as JsonObject ?? new JsonObject();
            set.json["animations"] = animations;
            animations.Remove(tile.ToString());
            if (frames.Length > 0) animations[tile.ToString()] = new JsonObject { ["frames"] = new JsonArray(frames.Select(f => (JsonNode)f).ToArray()), ["fps"] = Math.Max(0.1f, fps) };
            Save(set);
        }
    }
}

}
