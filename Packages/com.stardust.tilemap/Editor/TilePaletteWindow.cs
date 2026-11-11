using Stardust;
using Stardust.Editor;

namespace Stardust.Tilemaps.Editor {

/// <summary>Tools, brushes and tiles of the selected Tilemap's tileset, in a window of its own.</summary>
public class TilePaletteWindow : EditorWindow {
    [MenuItem("Tilemap/Tile Palette")]
    static void Open() => GetWindow<TilePaletteWindow>("Tile Palette");

    static bool s_Properties;

    public override void OnGUI() {
        var set = Tileset.Load(TileTools.tileset);
        if (string.IsNullOrEmpty(TileTools.tileset)) { EditorGUILayout.HelpBox("Select an object with a Tilemap to paint on it."); return; }
        TileTools.DrawToolbar(set);
        EditorGUILayout.Header(System.IO.Path.GetFileNameWithoutExtension(TileTools.tileset));
        TileTools.paletteZoom = EditorGUILayout.Slider("Zoom", TileTools.paletteZoom, 16, 96);
        TileTools.DrawPalette(set, 420);
        GUILayout.SameLine();
        if (GUILayout.IconButton("grid", s_Properties, "Tile properties: collider and animation")) s_Properties = !s_Properties;
        if (s_Properties) { EditorGUILayout.Header("Tile Properties"); TileTools.DrawTileProperties(set); }
    }
}

}
