using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
namespace Stardust.Editor
{
    [AttributeUsage(AttributeTargets.Class)] public sealed class CustomEditor : Attribute {public Type InspectedType {get;}public CustomEditor(Type type){InspectedType=type;}}
    public abstract class Editor {
        public ScriptBehaviour target {get;internal set;}
        public virtual void OnInspectorGUI(){DrawDefaultInspector();}
        /// <summary>Called every editor frame while the target is selected: read <see cref="SceneView"/> input and draw with <see cref="Handles"/>.
        /// Editors are created anew for each call, so keep tool state in static fields.</summary>
        public virtual void OnSceneGUI(){}
        public void DrawDefaultInspector()=>EditorGUI.Command("default","");
    }
    public abstract class EditorWindow {
        internal static readonly Dictionary<string,EditorWindow> Open=new();
        public string title {get;set;}
        public abstract void OnGUI();
        public static T GetWindow<T>(string title=null) where T:EditorWindow,new(){var key=typeof(T).FullName;if(!Open.TryGetValue(key,out var value)){value=new T();Open[key]=value;}value.title=title??typeof(T).Name;return(T)value;}
        public void Close()=>Open.Remove(GetType().FullName);
        public void Repaint(){} // Windows are drawn every editor frame.
    }
    internal static class EditorGUI {
        internal static JsonArray Commands=new();internal static JsonObject Events=new();static int id;
        internal static void Begin(JsonObject events){Commands=new();Events=events??new();id=0;}
        internal static JsonNode Command(string kind,string label,JsonNode value=null){string key=(id++).ToString();JsonNode result=Events[key]?.DeepClone()??value?.DeepClone();Commands.Add(new JsonObject{["id"]=key,["kind"]=kind,["label"]=label,["value"]=result?.DeepClone()});return result;}
    }
    public static class EditorGUILayout {
        public static void LabelField(string text)=>EditorGUI.Command("label",text);
        public static void HelpBox(string text)=>EditorGUI.Command("help",text);
        public static float FloatField(string label,float value)=>EditorGUI.Command("float",label,JsonValue.Create(value)).GetValue<float>();
        public static int IntField(string label,int value)=>EditorGUI.Command("int",label,JsonValue.Create(value)).GetValue<int>();
        public static string TextField(string label,string value)=>EditorGUI.Command("text",label,JsonValue.Create(value??"")).GetValue<string>();
        public static bool Toggle(string label,bool value)=>EditorGUI.Command("bool",label,JsonValue.Create(value)).GetValue<bool>();
        public static Vector3 Vector3Field(string label,Vector3 value){var v=EditorGUI.Command("vector3",label,new JsonArray(value.x,value.y,value.z));return new(v[0].GetValue<float>(),v[1].GetValue<float>(),v[2].GetValue<float>());}
        public static Color ColorField(string label,Color value){var v=EditorGUI.Command("color",label,new JsonArray(value.r,value.g,value.b,value.a));return new(v[0].GetValue<float>(),v[1].GetValue<float>(),v[2].GetValue<float>(),v[3].GetValue<float>());}
        public static void Space()=>EditorGUI.Command("space","");
        /// <summary>Section title with a line, like a header in the Inspector.</summary>
        public static void Header(string text)=>EditorGUI.Command("header",text??"");
        public static float Slider(string label,float value,float min,float max)=>
            EditorGUI.Command("slider",label,new JsonObject{["value"]=value,["min"]=min,["max"]=max})["value"]?.GetValue<float>()??value;
        /// <summary>Asset path picker with drag and drop from the Project window; <paramref name="extensions"/> filters the files (".png", ".json"...).</summary>
        public static string AssetField(string label,string path,params string[] extensions)=>EditorGUI.Command("asset",label,new JsonObject{["path"]=path??"",["extensions"]=new JsonArray(extensions.Select(e=>(JsonNode)JsonValue.Create(e)).ToArray())})["path"]?.GetValue<string>()??"";
        public static Vector2 Vector2Field(string label,Vector2 value){var v=EditorGUI.Command("vector2",label,new JsonArray(value.x,value.y));return new(v[0].GetValue<float>(),v[1].GetValue<float>());}
        /// <summary>Drop-down list; returns the chosen index.</summary>
        public static int Popup(string label,int index,params string[] options)=>EditorGUI.Command("popup",label,new JsonObject{["index"]=index,["options"]=new JsonArray(options.Select(o=>(JsonNode)JsonValue.Create(o)).ToArray())})["index"]?.GetValue<int>()??index;
    }
    public static class GUILayout {
        public static bool Button(string text)=>EditorGUI.Command("button",text,JsonValue.Create(false)).GetValue<bool>();
        /// <summary>A toggle drawn as a button, highlighted while on.</summary>
        public static bool Toggle(string text,bool on)=>EditorGUI.Command("togglebutton",text,JsonValue.Create(on)).GetValue<bool>();
        /// <summary>Puts the next control on the same line, optionally <paramref name="spacing"/> pixels after the previous one.</summary>
        public static void SameLine(float spacing=-1)=>EditorGUI.Command("sameline","",JsonValue.Create(spacing));
        /// <summary>Square button with a built-in icon: select, brush, eraser, box, bucket, picker, flipx, flipy, rotate, grid,
        /// palette, plus, minus, play, eye. Drawn filled while <paramref name="selected"/>. Returns true when clicked.</summary>
        public static bool IconButton(string icon,bool selected=false,string tooltip="")=>
            EditorGUI.Command("iconbutton",icon,new JsonObject{["selected"]=selected,["tooltip"]=tooltip??"",["clicked"]=false})["clicked"]?.GetValue<bool>()??false;
        /// <summary>Row of equal buttons filling the width with one selected (a segmented control); returns the selected index.</summary>
        public static int Toolbar(int selected,params string[] options)=>
            EditorGUI.Command("toolbar","",new JsonObject{["index"]=selected,["options"]=new JsonArray(options.Select(o=>(JsonNode)JsonValue.Create(o)).ToArray())})["index"]?.GetValue<int>()??selected;
        /// <summary>A texture cut into tiles, drawn in its own layout with a checkerboard under transparent pixels. Click picks a tile,
        /// dragging selects a rectangle of tiles; the result tells which (and whether Shift or Ctrl was held).</summary>
        public static TileGridEvent TileGrid(TileGridOptions o){
            var value=new JsonObject{["columns"]=o.columns,["rows"]=o.rows,["tileWidth"]=o.tileWidth,["tileHeight"]=o.tileHeight,["margin"]=o.margin,["spacing"]=o.spacing,
                ["size"]=o.cellSize,["height"]=o.maxHeight,["selected"]=new JsonArray((o.selected??Array.Empty<int>()).Select(i=>(JsonNode)i).ToArray()),
                ["highlighted"]=new JsonArray((o.highlighted??Array.Empty<int>()).Select(i=>(JsonNode)i).ToArray())};
            var e=EditorGUI.Command("tilegrid",o.texture??"",value)["event"] as JsonObject;
            if(e==null)return new TileGridEvent{clicked=-1,rectStart=-1,rectEnd=-1};
            return new TileGridEvent{clicked=e["clicked"]?.GetValue<int>()??-1,rectStart=e["rectStart"]?.GetValue<int>()??-1,rectEnd=e["rectEnd"]?.GetValue<int>()??-1,
                shift=e["shift"]?.GetValue<bool>()??false,control=e["ctrl"]?.GetValue<bool>()??false};
        }
        /// <summary>Square button showing part of a texture (pixel rectangle from the top-left). Returns true when clicked.</summary>
        public static bool TextureButton(string texture,float rectX,float rectY,float rectWidth,float rectHeight,float size,bool selected=false)=>
            EditorGUI.Command("texturebutton",texture,new JsonObject{["rect"]=new JsonArray(rectX,rectY,rectWidth,rectHeight),["size"]=size,["selected"]=selected,["clicked"]=false})["clicked"]?.GetValue<bool>()??false;
        public static void Label(string text)=>EditorGUILayout.LabelField(text);
    }
    /// <summary>Layout of a <see cref="GUILayout.TileGrid"/>: the texture and how it is cut, plus which tiles to mark.</summary>
    public struct TileGridOptions {
        public string texture;
        public int columns, rows, tileWidth, tileHeight, margin, spacing;
        /// <summary>Wanted cell size in pixels (cells shrink to fit the width).</summary>
        public float cellSize;
        /// <summary>Largest height before the grid scrolls; 0 shows every row.</summary>
        public float maxHeight;
        /// <summary>Tiles filled in blue, and tiles outlined in amber.</summary>
        public int[] selected, highlighted;
    }
    /// <summary>What happened in a <see cref="GUILayout.TileGrid"/> last frame: a click (clicked) or a dragged rectangle (rectStart, rectEnd); -1 when not.</summary>
    public struct TileGridEvent { public int clicked, rectStart, rectEnd; public bool shift, control; public bool any => clicked >= 0 || rectStart >= 0; }
    /// <summary>Scene view input for <see cref="Editor.OnSceneGUI"/>. Positions are on the target's local XY plane.</summary>
    public static class SceneView {
        internal static JsonObject Input=new();internal static bool Captured;
        internal static void Begin(JsonObject input){Input=input??new();Captured=false;}
        static Vector3 Vec(string key){var a=Input[key] as JsonArray;return a!=null&&a.Count==3?new Vector3(a[0].GetValue<float>(),a[1].GetValue<float>(),a[2].GetValue<float>()):default;}
        static bool Bit(string key,int button){var a=Input[key] as JsonArray;return a!=null&&button>=0&&button<a.Count&&a[button].GetValue<bool>();}
        /// <summary>The mouse is over the Scene view.</summary>
        public static bool mouseOver=>Input["hovered"]?.GetValue<bool>()??false;
        /// <summary>The mouse ray meets the target's local XY plane (in front of the camera).</summary>
        public static bool mouseOnPlane=>Input["hit"]?.GetValue<bool>()??false;
        /// <summary>Mouse position on the target's local XY plane, in its local units.</summary>
        public static Vector3 mouseLocal=>Vec("local");
        public static Vector3 mouseWorld=>Vec("world");
        /// <summary>0 left, 1 right, 2 middle.</summary>
        public static bool GetMouseButton(int button)=>Bit("down",button);
        public static bool GetMouseButtonDown(int button)=>Bit("pressed",button);
        public static bool GetMouseButtonUp(int button)=>Bit("released",button);
        public static bool shift=>Input["shift"]?.GetValue<bool>()??false;
        public static bool control=>Input["ctrl"]?.GetValue<bool>()??false;
        public static bool alt=>Input["alt"]?.GetValue<bool>()??false;
        public static float scrollDelta=>Input["scroll"]?.GetValue<float>()??0;
        /// <summary>Keeps the left mouse button for this tool this frame: no selection clicks and no transform gizmo.</summary>
        public static void TakeMouse()=>Captured=true;
    }
    /// <summary>Lines, rectangles and labels drawn over the Scene view, in the target's local coordinates.</summary>
    public static class Handles {
        internal static JsonArray Commands=new();
        public static Color color=new(1,1,1,1);
        internal static void Begin(){Commands=new();color=new(1,1,1,1);}
        static JsonArray C()=>new(color.r,color.g,color.b,color.a);
        public static void DrawLine(Vector3 a,Vector3 b,float thickness=1.5f)=>Commands.Add(new JsonObject{["kind"]="line",["a"]=new JsonArray(a.x,a.y,a.z),["b"]=new JsonArray(b.x,b.y,b.z),["color"]=C(),["width"]=thickness});
        /// <summary>Rectangle on the local XY plane, outlined or filled.</summary>
        public static void DrawRect(Vector2 min,Vector2 max,bool filled=false,float thickness=1.5f)=>Commands.Add(new JsonObject{["kind"]="rect",["min"]=new JsonArray(min.x,min.y),["max"]=new JsonArray(max.x,max.y),["color"]=C(),["fill"]=filled,["width"]=thickness});
        /// <summary>Part of a texture drawn on the plane (a preview of what a tool will place).</summary>
        public static void DrawTexture(Vector2 min,Vector2 max,string texture,float rectX,float rectY,float rectWidth,float rectHeight)=>Commands.Add(new JsonObject{["kind"]="texture",["min"]=new JsonArray(min.x,min.y),["max"]=new JsonArray(max.x,max.y),["texture"]=texture??"",["rect"]=new JsonArray(rectX,rectY,rectWidth,rectHeight),["color"]=C()});
        public static void Label(Vector3 position,string text)=>Commands.Add(new JsonObject{["kind"]="label",["at"]=new JsonArray(position.x,position.y,position.z),["text"]=text??"",["color"]=C()});
    }
    [AttributeUsage(AttributeTargets.Method)]
    public sealed class MenuItem : Attribute
    {
        public string Path { get; }
        public MenuItem(string path) { Path = path; }
    }
}
