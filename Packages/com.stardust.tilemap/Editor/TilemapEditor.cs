using System;
using System.IO;
using Stardust;
using Stardust.Editor;

namespace Stardust.Tilemaps.Editor {

[CustomEditor(typeof(Tilemap))]
public class TilemapEditor : Stardust.Editor.Editor {
    static string s_NewTexture = "";
    static int s_NewTileWidth = 16, s_NewTileHeight = 16;
    static bool s_ConfirmClear;
    static int s_Tab; // Paint, Tile, Settings, Collider, Info
    static readonly string[] s_Tabs = { "Paint", "Tile", "Settings", "Collider", "Info" };

    Tilemap map => (Tilemap)target;

    public override void OnInspectorGUI() {
        var m = map;
        TileTools.tileset = m.Tileset;
        var set = Tileset.Load(m.Tileset);
        s_Tab = GUILayout.Toolbar(s_Tab, s_Tabs);
        EditorGUILayout.Space();
        if (set == null || !set.isValid) { DrawSettings(m); DrawNewTileset(m); Apply(m); return; }
        switch (s_Tab) {
        case 0:
            TileTools.DrawToolbar(set);
            TileTools.paletteZoom = EditorGUILayout.Slider("Zoom", TileTools.paletteZoom, 16, 96);
            TileTools.DrawPalette(set, 320);
            if (GUILayout.Button("Open Tile Palette")) EditorWindow.GetWindow<TilePaletteWindow>("Tile Palette");
            EditorGUILayout.HelpBox("Scene view: left click paints, Shift + click erases, Alt moves the camera. Drag in the palette to pick a block (stamp), Ctrl + click builds a random set.");
            break;
        case 1:
            TileTools.DrawPalette(set, 240);
            EditorGUILayout.Header("Selected Tile");
            TileTools.DrawTileProperties(set);
            break;
        case 2:
            DrawSettings(m);
            break;
        case 3:
            m.Colliders = EditorGUILayout.Toggle("Build Colliders", m.Colliders);
            m.Friction = Math.Max(0, EditorGUILayout.FloatField("Friction", m.Friction));
            m.Bounciness = Math.Clamp(EditorGUILayout.FloatField("Bounciness", m.Bounciness), 0, 1);
            EditorGUILayout.HelpBox("When the game starts, solid tiles get Box Collider 2D children: full tiles merged into rectangles, " +
                                    "custom boxes one per tile. Set each tile's collider in the Tile tab.");
            break;
        case 4:
            EditorGUILayout.LabelField($"{m.tileCount} tiles, {m.slotCount} batch slots");
            if (m.GetBounds(out int minX, out int minY, out int maxX, out int maxY)) EditorGUILayout.LabelField($"Cells ({minX}, {minY}) to ({maxX}, {maxY})");
            if (m.slotCount > m.tileCount && GUILayout.Button("Compact (drop empty slots)")) m.Compact();
            if (!s_ConfirmClear) { if (GUILayout.Button("Clear All")) s_ConfirmClear = true; }
            else {
                if (GUILayout.Button("Really clear every tile")) { m.ClearAllTiles(); s_ConfirmClear = false; }
                GUILayout.SameLine();
                if (GUILayout.Button("Cancel")) s_ConfirmClear = false;
            }
            break;
        }
        Apply(m);
    }

    static void Apply(Tilemap m) { if (m.hasPendingChanges) m.Refresh(); }

    static void DrawSettings(Tilemap m) {
        string tileset = EditorGUILayout.AssetField("Tileset", m.Tileset, ".tileset");
        Vector2 cell = EditorGUILayout.Vector2Field("Cell Size", m.CellSize);
        Color tint = EditorGUILayout.ColorField("Tint", m.Tint);
        int sorting = EditorGUILayout.IntField("Sorting Order", m.SortingOrder);
        bool redraw = tint.r != m.Tint.r || tint.g != m.Tint.g || tint.b != m.Tint.b || tint.a != m.Tint.a || sorting != m.SortingOrder;
        m.Tileset = tileset;
        m.CellSize = new Vector2(Math.Max(0.01f, cell.x), Math.Max(0.01f, cell.y));
        m.Tint = tint; m.SortingOrder = sorting;
        TileTools.tileset = m.Tileset;
        if (redraw) m.Refresh();
    }

    // No tileset yet: make one from a texture
    static void DrawNewTileset(Tilemap m) {
        EditorGUILayout.Space();
        EditorGUILayout.HelpBox("Pick a .tileset, or make one from a texture: choose the image and the size of one tile in pixels.");
        s_NewTexture = EditorGUILayout.AssetField("Texture", s_NewTexture, ".png", ".jpg", ".jpeg", ".tga", ".bmp");
        s_NewTileWidth = Math.Max(1, EditorGUILayout.IntField("Tile Width", s_NewTileWidth));
        s_NewTileHeight = Math.Max(1, EditorGUILayout.IntField("Tile Height", s_NewTileHeight));
        if (!string.IsNullOrEmpty(s_NewTexture) && GUILayout.Button("Create Tileset")) {
            string relative = Path.ChangeExtension(s_NewTexture, ".tileset").Replace('\\', '/');
            // Package paths start at the project folder, everything else at Assets
            string file = Path.Combine(relative.StartsWith("Packages/") ? Application.GetProjectPath() : Application.GetDataPath(), relative);
            if (!File.Exists(file)) File.WriteAllText(file, Tileset.Create(s_NewTexture, s_NewTileWidth, s_NewTileHeight));
            m.Tileset = relative;
            TileTools.tileset = relative;
            Debug.Log($"Tileset created: {relative}");
        }
    }

    public override void OnSceneGUI() {
        var m = map;
        TileTools.tileset = m.Tileset;
        var set = Tileset.Load(m.Tileset);
        DrawBounds(m);
        if (TileTools.tool == TileTool.Select || set == null || !set.isValid || !SceneView.mouseOnPlane || SceneView.alt) { Apply(m); return; }
        if (!SceneView.mouseOver && !TileTools.drawingBox) { Apply(m); return; }
        SceneView.TakeMouse();

        var (x, y) = m.LocalToCell(SceneView.mouseLocal);
        var tool = SceneView.shift && TileTools.tool == TileTool.Paint ? TileTool.Erase : TileTools.tool;
        bool pressed = SceneView.GetMouseButtonDown(0), held = SceneView.GetMouseButton(0), released = SceneView.GetMouseButtonUp(0);
        var brush = TileTools.brush;
        AutoTile terrain = brush == BrushMode.AutoTile && TileTools.autoTile < set.autoTiles.Count ? set.autoTiles[TileTools.autoTile] : null;
        var stampOrigin = TileTools.drawingBox ? TileTools.boxStart : (x, y);

        void Place(int cx, int cy, bool erase) {
            if (terrain != null) { m.SetAutoTile(cx, cy, terrain, erase); return; }
            if (erase) { m.ClearTile(cx, cy); return; }
            int tile = TileTools.TileFor(set, cx, cy, stampOrigin);
            m.SetTile(cx, cy, tile, brush is BrushMode.Single or BrushMode.Stamp ? TileTools.flags : TileFlags.None);
        }

        switch (tool) {
        case TileTool.Paint:
        case TileTool.Erase:
            if (held && (pressed || (x, y) != TileTools.lastCell)) {
                bool erase = tool == TileTool.Erase;
                // A stamp paints its whole block with the cursor at its bottom-left
                var (w, h) = brush == BrushMode.Stamp && !erase ? TileTools.StampSize(set) : (1, 1);
                stampOrigin = (x, y);
                for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) Place(x + i, y + j, erase);
                TileTools.lastCell = (x, y);
            }
            if (!held) TileTools.lastCell = (int.MinValue, int.MinValue);
            break;
        case TileTool.Box:
            if (pressed) { TileTools.boxStart = (x, y); TileTools.drawingBox = true; }
            if (TileTools.drawingBox && (released || !held)) {
                var (sx, sy) = TileTools.boxStart;
                stampOrigin = (Math.Min(sx, x), Math.Min(sy, y));
                for (int cy = Math.Min(sy, y); cy <= Math.Max(sy, y); ++cy)
                    for (int cx = Math.Min(sx, x); cx <= Math.Max(sx, x); ++cx) Place(cx, cy, SceneView.shift);
                TileTools.drawingBox = false;
            }
            break;
        case TileTool.Fill:
            if (pressed) {
                if (SceneView.shift) m.FloodFill(x, y, -1);
                else if (terrain != null) {
                    // Fill the area with terrain, then join it up
                    var cells = new System.Collections.Generic.List<(int, int)>();
                    m.FloodFill(x, y, terrain.tiles[15], TileFlags.None, 10000, (cx, cy) => { cells.Add((cx, cy)); return terrain.tiles[15]; });
                    foreach (var (cx, cy) in cells) m.SetAutoTile(cx, cy, terrain);
                } else m.FloodFill(x, y, TileTools.selectedTile, TileTools.flags, 10000, (cx, cy) => TileTools.TileFor(set, cx, cy, (x, y)));
            }
            break;
        case TileTool.Pick:
            if (pressed && m.HasTile(x, y)) {
                TileTools.selectedTile = m.GetTile(x, y);
                var f = m.GetTileFlags(x, y);
                TileTools.flipX = f.HasFlag(TileFlags.FlipX); TileTools.flipY = f.HasFlag(TileFlags.FlipY); TileTools.quarterTurns = ((int)f >> 2) & 3;
                TileTools.brush = BrushMode.Single;
                TileTools.tool = TileTool.Paint;
            }
            break;
        }
        Apply(m);
        DrawPreview(m, set, tool, x, y);
    }

    static void DrawBounds(Tilemap m) {
        if (!m.GetBounds(out int minX, out int minY, out int maxX, out int maxY)) return;
        Handles.color = new Color(1, 1, 1, 0.25f);
        Handles.DrawRect(m.CellToLocal(minX, minY), m.CellToLocal(maxX + 1, maxY + 1), false, 1);
    }

    static void DrawPreview(Tilemap m, Tileset set, TileTool tool, int x, int y) {
        // Faint grid around the cursor
        Handles.color = new Color(1, 1, 1, 0.12f);
        const int reach = 4;
        for (int i = -reach; i <= reach + 1; ++i) {
            Handles.DrawLine(new Vector3((x + i) * m.CellSize.x, (y - reach) * m.CellSize.y, 0), new Vector3((x + i) * m.CellSize.x, (y + reach + 1) * m.CellSize.y, 0), 1);
            Handles.DrawLine(new Vector3((x - reach) * m.CellSize.x, (y + i) * m.CellSize.y, 0), new Vector3((x + reach + 1) * m.CellSize.x, (y + i) * m.CellSize.y, 0), 1);
        }
        int x0 = x, y0 = y, x1 = x, y1 = y;
        if (tool == TileTool.Box && TileTools.drawingBox) {
            x0 = Math.Min(x, TileTools.boxStart.x); x1 = Math.Max(x, TileTools.boxStart.x);
            y0 = Math.Min(y, TileTools.boxStart.y); y1 = Math.Max(y, TileTools.boxStart.y);
        } else if (tool == TileTool.Paint && TileTools.brush == BrushMode.Stamp) {
            var (w, h) = TileTools.StampSize(set);
            x1 = x + w - 1; y1 = y + h - 1;
        }
        var lo = m.CellToLocal(x0, y0);
        var hi = m.CellToLocal(x1 + 1, y1 + 1);
        bool erasing = tool == TileTool.Erase || (SceneView.shift && tool is TileTool.Box or TileTool.Fill);
        var brush = TileTools.brush;
        // Tiles under the cursor; random and auto-tile brushes show their first tile
        if (!erasing && tool is TileTool.Paint or TileTool.Box && brush is BrushMode.Single or BrushMode.Stamp) {
            Handles.color = new Color(1, 1, 1, 0.6f);
            var origin = tool == TileTool.Box ? (x0, y0) : (x, y);
            for (int cy = y0; cy <= y1 && cy - y0 < 64; ++cy)
                for (int cx = x0; cx <= x1 && cx - x0 < 64; ++cx) {
                    int tile = TileTools.TileFor(set, cx, cy, origin);
                    if (tile >= set.tileCount) continue;
                    var r = set.TileRect(tile);
                    var a = m.CellToLocal(cx, cy);
                    var b = m.CellToLocal(cx + 1, cy + 1);
                    // Flips show by swapping the corners
                    if (TileTools.flipX) (a.x, b.x) = (b.x, a.x);
                    if (TileTools.flipY) (a.y, b.y) = (b.y, a.y);
                    Handles.DrawTexture(a, b, set.texture, r.x, r.y, r.width, r.height);
                }
        }
        Handles.color = erasing ? new Color(1, 0.35f, 0.3f, 1) : tool == TileTool.Pick ? new Color(1, 0.85f, 0.3f, 1) : new Color(0.3f, 0.65f, 1, 1);
        Handles.DrawRect(lo, hi, false, 2);
        // Brush position
        Handles.color = new Color(1, 1, 1, 0.9f);
        int under = m.GetTile(x, y);
        Handles.Label(new Vector3(hi.x + 0.1f * m.CellSize.x, hi.y, 0), under >= 0 ? $"({x}, {y})  tile {under}" : $"({x}, {y})");
    }
}

}
