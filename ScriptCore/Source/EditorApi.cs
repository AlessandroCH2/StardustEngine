using System;
using System.Collections.Generic;
using System.Text.Json.Nodes;
namespace Stardust.Editor
{
    [AttributeUsage(AttributeTargets.Class)] public sealed class CustomEditor : Attribute {public Type InspectedType {get;}public CustomEditor(Type type){InspectedType=type;}}
    public abstract class Editor {
        public ScriptBehaviour target {get;internal set;}
        public virtual void OnInspectorGUI(){DrawDefaultInspector();}
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
    }
    public static class GUILayout {
        public static bool Button(string text)=>EditorGUI.Command("button",text,JsonValue.Create(false)).GetValue<bool>();
        public static void Label(string text)=>EditorGUILayout.LabelField(text);
    }
    [AttributeUsage(AttributeTargets.Method)]
    public sealed class MenuItem : Attribute
    {
        public string Path { get; }
        public MenuItem(string path) { Path = path; }
    }
}
