using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using Stardust;

namespace Stardust.Tilemaps {

/// <summary>A tile's collision: none, the whole cell, or a box in cell fractions (from the bottom-left).</summary>
public struct TileCollider {
    public bool solid;
    public float x, y, width, height;
    public bool isFull => solid && x <= 0 && y <= 0 && width >= 1 && height >= 1;
    public static readonly TileCollider None = new() { solid = false };
    public static readonly TileCollider Full = new() { solid = true, x = 0, y = 0, width = 1, height = 1 };
}

/// <summary>Frames a tile cycles through, as tile indices of the same tileset.</summary>
public sealed class TileAnimation { public int[] frames = Array.Empty<int>(); public float fps = 8; }

/// <summary>Terrain that picks its tile from the neighbours: 16 tiles indexed by mask (1 up, 2 right, 4 down, 8 left
/// set when that neighbour is the same terrain).</summary>
public sealed class AutoTile { public string name = ""; public int[] tiles = new int[16]; public bool Contains(int tile) => Array.IndexOf(tiles, tile) >= 0; }

/// <summary>Named set of tiles painted at random, optionally weighted.</summary>
public sealed class RandomBrush { public string name = ""; public int[] tiles = Array.Empty<int>(); public float[] weights = Array.Empty<float>(); }

/// <summary>
/// A texture cut into a grid of tiles, stored as a ".tileset" JSON asset. Tiles are numbered left to right, top to bottom.
/// <code>
/// { "texture": "Tiles/Terrain.png", "tileWidth": 16, "tileHeight": 16, "margin": 0, "spacing": 0,
///   "solid": [0, 1, 5],                         // tiles with full colliders (missing = every tile)
///   "colliders": { "10": [0, 0.6, 1, 0.4] },    // custom boxes in cell fractions; [] = no collider
///   "animations": { "8": { "frames": [8, 24, 25], "fps": 4 } },
///   "autotiles": [ { "name": "Path", "tiles": [16 tiles by neighbour mask] } ],
///   "random": [ { "name": "Grass", "tiles": [0, 16], "weights": [3, 1] } ] }
/// </code>
/// </summary>
public sealed class Tileset {
    public string path = "";
    public string texture = "";
    public int tileWidth = 16, tileHeight = 16, margin, spacing;
    public int textureWidth, textureHeight;
    public HashSet<int> solid; // null: every tile
    public Dictionary<int, TileCollider> colliders = new();
    public Dictionary<int, TileAnimation> animations = new();
    public List<AutoTile> autoTiles = new();
    public List<RandomBrush> randomBrushes = new();
    /// <summary>The JSON as loaded (editors change it and save it back, keeping unknown keys).</summary>
    public JsonObject json = new();

    public int columns => tileWidth <= 0 ? 0 : Math.Max(0, (textureWidth - 2 * margin + spacing) / (tileWidth + spacing));
    public int rows => tileHeight <= 0 ? 0 : Math.Max(0, (textureHeight - 2 * margin + spacing) / (tileHeight + spacing));
    public int tileCount => columns * rows;
    public bool isValid => !string.IsNullOrEmpty(texture) && tileCount > 0;

    /// <summary>Pixel rectangle of a tile in the texture, from the top-left.</summary>
    public PixelRect TileRect(int tile) {
        int c = columns;
        if (c <= 0 || tile < 0) return default;
        return new PixelRect(margin + tile % c * (tileWidth + spacing), margin + tile / c * (tileHeight + spacing), tileWidth, tileHeight);
    }
    /// <summary>The tile whose rectangle starts at a pixel position (the inverse of <see cref="TileRect"/>), or -1.</summary>
    public int TileAt(float x, float y) {
        if (columns <= 0) return -1;
        int col = (int)MathF.Round((x - margin) / (tileWidth + spacing)), row = (int)MathF.Round((y - margin) / (tileHeight + spacing));
        return col < 0 || row < 0 || col >= columns || row >= rows ? -1 : row * columns + col;
    }
    public TileCollider ColliderOf(int tile) =>
        colliders.TryGetValue(tile, out var custom) ? custom : solid == null || solid.Contains(tile) ? TileCollider.Full : TileCollider.None;
    public bool IsSolid(int tile) => ColliderOf(tile).solid;

    // Loaded tilesets, refreshed when the file's text changes
    static readonly Dictionary<string, (string text, Tileset tileset)> s_Cache = new();

    /// <summary>Reads a ".tileset" asset; null when it is missing or invalid.</summary>
    public static Tileset Load(string assetPath) {
        if (string.IsNullOrEmpty(assetPath) || !Assets.Exists(assetPath)) return null;
        string text;
        try { text = Assets.ReadAllText(assetPath); } catch { return null; }
        if (s_Cache.TryGetValue(assetPath, out var cached) && cached.text == text && cached.tileset.textureWidth > 0) return cached.tileset;
        try {
            var json = JsonNode.Parse(text)?.AsObject();
            if (json == null) return null;
            var t = new Tileset {
                path = assetPath, json = json,
                texture = json["texture"]?.GetValue<string>() ?? "",
                tileWidth = Math.Max(1, json["tileWidth"]?.GetValue<int>() ?? 16),
                tileHeight = Math.Max(1, json["tileHeight"]?.GetValue<int>() ?? 16),
                margin = Math.Max(0, json["margin"]?.GetValue<int>() ?? 0),
                spacing = Math.Max(0, json["spacing"]?.GetValue<int>() ?? 0),
            };
            if (json["solid"] is JsonArray solid) t.solid = new HashSet<int>(solid.Select(v => v.GetValue<int>()));
            if (json["colliders"] is JsonObject colliders)
                foreach (var (key, value) in colliders)
                    if (int.TryParse(key, out int tile) && value is JsonArray box)
                        t.colliders[tile] = box.Count == 4
                            ? new TileCollider { solid = true, x = box[0].GetValue<float>(), y = box[1].GetValue<float>(), width = box[2].GetValue<float>(), height = box[3].GetValue<float>() }
                            : TileCollider.None;
            if (json["animations"] is JsonObject animations)
                foreach (var (key, value) in animations)
                    if (int.TryParse(key, out int tile) && value is JsonObject a && a["frames"] is JsonArray frames && frames.Count > 0)
                        t.animations[tile] = new TileAnimation { frames = frames.Select(f => f.GetValue<int>()).ToArray(), fps = a["fps"]?.GetValue<float>() ?? 8 };
            if (json["autotiles"] is JsonArray autoTiles)
                foreach (var node in autoTiles)
                    if (node is JsonObject a && a["tiles"] is JsonArray tiles && tiles.Count == 16)
                        t.autoTiles.Add(new AutoTile { name = a["name"]?.GetValue<string>() ?? "Terrain", tiles = tiles.Select(v => v.GetValue<int>()).ToArray() });
            if (json["random"] is JsonArray randoms)
                foreach (var node in randoms)
                    if (node is JsonObject r && r["tiles"] is JsonArray tiles && tiles.Count > 0)
                        t.randomBrushes.Add(new RandomBrush {
                            name = r["name"]?.GetValue<string>() ?? "Random", tiles = tiles.Select(v => v.GetValue<int>()).ToArray(),
                            weights = r["weights"] is JsonArray w ? w.Select(v => v.GetValue<float>()).ToArray() : Array.Empty<float>(),
                        });
            var size = Assets.GetTextureSize(t.texture);
            t.textureWidth = (int)size.x; t.textureHeight = (int)size.y;
            s_Cache[assetPath] = (text, t);
            return t;
        } catch (Exception e) {
            Debug.LogWarning($"Tileset {assetPath}: {e.Message}");
            return null;
        }
    }

    /// <summary>The JSON text of a new tileset for a texture.</summary>
    public static string Create(string texture, int tileWidth, int tileHeight, int margin = 0, int spacing = 0) =>
        new JsonObject { ["texture"] = texture, ["tileWidth"] = tileWidth, ["tileHeight"] = tileHeight, ["margin"] = margin, ["spacing"] = spacing }
            .ToJsonString(new JsonSerializerOptions { WriteIndented = true });
}

}
