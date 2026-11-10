using Stardust.Interop;
using System.Text.Json.Nodes;
using System;
using System.Collections.Generic;
namespace Stardust
{
    internal static unsafe class ComponentProperties
    {
        internal static T Value<T>(Component c,int type,string name,T fallback) { var v=Get(c,type)[name]; return v==null?fallback:v.GetValue<T>(); }
        internal static Color ColorValue(Component c,int type,string name) {var v=Get(c,type)[name];return v is JsonArray a && a.Count==4?new Color(a[0].GetValue<float>(),a[1].GetValue<float>(),a[2].GetValue<float>(),a[3].GetValue<float>()):new Color(1,1,1,1);}
        internal static Vector2 VectorValue(Component c,int type,string name) {var v=Get(c,type)[name];return v is JsonArray a && a.Count==2?new Vector2(a[0].GetValue<float>(),a[1].GetValue<float>()):new Vector2(0,0);}
        internal static void Property(Component c,int type,string name,JsonNode value) => Set(c,type,new JsonObject{[name]=value});
        internal static JsonObject Get(Component c, int type) => JsonNode.Parse(Native.FromUtf8(Native.Api.GetComponentJson(c.Entity.ID,type))).AsObject();
        internal static void Set(Component c, int type, JsonObject value) { fixed (byte* p=Native.Utf8(value.ToJsonString())) Native.Api.SetComponentJson(c.Entity.ID,type,p); }
    }
    public sealed class Animator : Component
    {
        public void Play(string state) => ComponentProperties.Set(this,12,new JsonObject { ["state"]=state, ["playing"]=true });
        public void SetFloat(string parameter,float value) => ComponentProperties.Set(this,12,new JsonObject { ["parameter"]=parameter,["value"]=value });
        public void SetBool(string parameter,bool value) => SetFloat(parameter,value ? 1 : 0);
        public void SetInteger(string parameter,int value) => SetFloat(parameter,value);
        public void SetTrigger(string parameter) => SetFloat(parameter,1);
        public void ResetTrigger(string parameter) => SetFloat(parameter,0);
        /// <summary>Sets a Vector2 parameter, for example the direction that drives a 2D Blend Tree.</summary>
        public void SetVector2(string parameter,Vector2 value) { SetFloat(parameter+".x",value.x); SetFloat(parameter+".y",value.y); }
        public float GetFloat(string parameter) => ComponentProperties.Get(this,12)["parameters"]?[parameter]?.GetValue<float>() ?? 0;
        public bool GetBool(string parameter) => GetFloat(parameter)!=0;
        public int GetInteger(string parameter) => (int)GetFloat(parameter);
        public Vector2 GetVector2(string parameter) => new Vector2(GetFloat(parameter+".x"),GetFloat(parameter+".y"));
        public float speed { get => ComponentProperties.Get(this,12)["speed"]?.GetValue<float>() ?? 1; set => ComponentProperties.Set(this,12,new JsonObject { ["speed"]=value }); }
        public bool playing { get => ComponentProperties.Get(this,12)["playing"]?.GetValue<bool>() ?? false; set => ComponentProperties.Set(this,12,new JsonObject { ["playing"]=value }); }
    }
}
namespace Stardust.UI
{
    public enum CanvasRenderMode { ScreenSpaceOverlay, WorldSpace, ScreenSpaceCamera }
    public enum ImageType { Simple, Sliced, Tiled, Filled }
    public sealed class ButtonEvent {
        readonly List<Action> listeners=new();
        public void AddListener(Action listener) { if(listener!=null&&!listeners.Contains(listener))listeners.Add(listener); }
        public void RemoveListener(Action listener)=>listeners.Remove(listener);
        public void RemoveAllListeners()=>listeners.Clear();
        internal void Invoke(){foreach(var listener in listeners.ToArray())try{listener();}catch(Exception e){Debug.LogError(e);}}
    }
    internal static class UIEvents {
        static readonly Dictionary<ulong,ButtonEvent> events=new();
        internal static ButtonEvent For(ulong entity){if(!events.TryGetValue(entity,out var result))events[entity]=result=new ButtonEvent();return result;}
        internal static void Fire(ulong entity){if(events.TryGetValue(entity,out var result))result.Invoke();}
        internal static void Clear()=>events.Clear();
    }
    public sealed class StringEvent {
        readonly List<Action<string>> listeners=new();public void AddListener(Action<string> callback){if(callback!=null&&!listeners.Contains(callback))listeners.Add(callback);}public void RemoveListener(Action<string> callback)=>listeners.Remove(callback);public void RemoveAllListeners()=>listeners.Clear();internal void Invoke(string value){foreach(var callback in listeners.ToArray())try{callback(value);}catch(Exception e){Debug.LogError(e);}}
    }
    internal static class InputEvents {
        static readonly Dictionary<(ulong,bool),StringEvent> events=new();internal static StringEvent For(ulong id,bool submit){if(!events.TryGetValue((id,submit),out var result))events[(id,submit)]=result=new();return result;}internal static void Fire(ulong id,string text,bool submit){if(events.TryGetValue((id,submit),out var result))result.Invoke(text);}internal static void Clear()=>events.Clear();
    }
    public sealed class InputField : Component {
        public string text {get=>ComponentProperties.Value(this,21,"text","");set{string previous=text;ComponentProperties.Property(this,21,"text",JsonValue.Create(value));if(previous!=value)InputEvents.Fire(Entity.ID,value,false);}}
        public void SetTextWithoutNotify(string value)=>ComponentProperties.Property(this,21,"text",JsonValue.Create(value));
        public string placeholder {get=>ComponentProperties.Value(this,21,"placeholder","");set=>ComponentProperties.Property(this,21,"placeholder",JsonValue.Create(value));}
        public bool interactable {get=>ComponentProperties.Value(this,21,"interactable",true);set=>ComponentProperties.Property(this,21,"interactable",JsonValue.Create(value));}
        public bool readOnly {get=>ComponentProperties.Value(this,21,"readOnly",false);set=>ComponentProperties.Property(this,21,"readOnly",JsonValue.Create(value));}
        public bool password {get=>ComponentProperties.Value(this,21,"password",false);set=>ComponentProperties.Property(this,21,"password",JsonValue.Create(value));}
        public bool multiline {get=>ComponentProperties.Value(this,21,"multiline",false);set=>ComponentProperties.Property(this,21,"multiline",JsonValue.Create(value));}
        public int characterLimit {get=>ComponentProperties.Value(this,21,"characterLimit",0);set=>ComponentProperties.Property(this,21,"characterLimit",JsonValue.Create(value));}
        public StringEvent onValueChanged=>InputEvents.For(Entity.ID,false);public StringEvent onEndEdit=>InputEvents.For(Entity.ID,true);
    }
    public sealed class Canvas : Component {
        public Vector2 referenceResolution { get=>ComponentProperties.VectorValue(this,13,"referenceResolution"); set=>ComponentProperties.Property(this,13,"referenceResolution",new JsonArray(value.x,value.y)); }
        public CanvasRenderMode renderMode { get=>(CanvasRenderMode)ComponentProperties.Value(this,13,"renderMode",0); set=>ComponentProperties.Property(this,13,"renderMode",JsonValue.Create((int)value)); }
        public float pixelsPerUnit { get=>ComponentProperties.Value(this,13,"pixelsPerUnit",100f); set=>ComponentProperties.Property(this,13,"pixelsPerUnit",JsonValue.Create(value)); }
        public bool scaleWithScreen { get=>ComponentProperties.Value(this,13,"scaleWithScreen",true); set=>ComponentProperties.Property(this,13,"scaleWithScreen",JsonValue.Create(value)); }
    }
    public sealed class RectTransform : Component {
        public Vector2 anchoredPosition { get=>ComponentProperties.VectorValue(this,14,"anchoredPosition"); set=>ComponentProperties.Property(this,14,"anchoredPosition",new JsonArray(value.x,value.y)); }
        public Vector2 sizeDelta { get=>ComponentProperties.VectorValue(this,14,"sizeDelta"); set=>ComponentProperties.Property(this,14,"sizeDelta",new JsonArray(value.x,value.y)); }
        public Vector2 anchorMin { get=>ComponentProperties.VectorValue(this,14,"anchorMin"); set=>ComponentProperties.Property(this,14,"anchorMin",new JsonArray(value.x,value.y)); }
        public Vector2 anchorMax { get=>ComponentProperties.VectorValue(this,14,"anchorMax"); set=>ComponentProperties.Property(this,14,"anchorMax",new JsonArray(value.x,value.y)); }
        public Vector2 pivot { get=>ComponentProperties.VectorValue(this,14,"pivot"); set=>ComponentProperties.Property(this,14,"pivot",new JsonArray(value.x,value.y)); }
        public Vector2 scale { get=>ComponentProperties.VectorValue(this,14,"scale"); set=>ComponentProperties.Property(this,14,"scale",new JsonArray(value.x,value.y)); }
        public float rotation { get=>ComponentProperties.Value(this,14,"rotation",0f); set=>ComponentProperties.Property(this,14,"rotation",JsonValue.Create(value)); }
    }
    public sealed class Image : Component {
        public void SetSprite(string path) => sprite=Sprite.FromPath(path);
        public float pixelsPerUnitMultiplier {get=>ComponentProperties.Value(this,15,"pixelsPerUnitMultiplier",1f);set=>ComponentProperties.Property(this,15,"pixelsPerUnitMultiplier",JsonValue.Create(value));}
        public Sprite sprite { get=>Sprite.FromPath(ComponentProperties.Value(this,15,"sprite","")); set=>ComponentProperties.Property(this,15,"sprite",JsonValue.Create(value?.path??"")); }
        public string texture { get=>ComponentProperties.Value(this,15,"texture",""); set=>ComponentProperties.Property(this,15,"texture",JsonValue.Create(value)); }
        public string material { get=>ComponentProperties.Value(this,15,"material",""); set=>ComponentProperties.Property(this,15,"material",JsonValue.Create(value)); }
        public Color color { get=>ComponentProperties.ColorValue(this,15,"color"); set=>ComponentProperties.Property(this,15,"color",new JsonArray(value.r,value.g,value.b,value.a)); }
        public ImageType type { get=>(ImageType)ComponentProperties.Value(this,15,"type",0); set=>ComponentProperties.Property(this,15,"type",JsonValue.Create((int)value)); }
        public float fillAmount { get=>ComponentProperties.Value(this,15,"fillAmount",1f); set=>ComponentProperties.Property(this,15,"fillAmount",JsonValue.Create(value)); }
        public bool preserveAspect { get=>ComponentProperties.Value(this,15,"preserveAspect",false); set=>ComponentProperties.Property(this,15,"preserveAspect",JsonValue.Create(value)); }
    }
    public sealed class Text : Component {
        public string text { get=>ComponentProperties.Value(this,16,"text",""); set=>ComponentProperties.Property(this,16,"text",JsonValue.Create(value)); }
        public string font { get=>ComponentProperties.Value(this,16,"font",""); set=>ComponentProperties.Property(this,16,"font",JsonValue.Create(value)); }
        public string material { get=>ComponentProperties.Value(this,16,"material",""); set=>ComponentProperties.Property(this,16,"material",JsonValue.Create(value)); }
        public float fontSize { get=>ComponentProperties.Value(this,16,"fontSize",24f); set=>ComponentProperties.Property(this,16,"fontSize",JsonValue.Create(value)); }
        public Color color { get=>ComponentProperties.ColorValue(this,16,"color"); set=>ComponentProperties.Property(this,16,"color",new JsonArray(value.r,value.g,value.b,value.a)); }
        public Color outlineColor { get=>ComponentProperties.ColorValue(this,16,"outlineColor"); set=>ComponentProperties.Property(this,16,"outlineColor",new JsonArray(value.r,value.g,value.b,value.a)); }
        public float outlineWidth { get=>ComponentProperties.Value(this,16,"outlineWidth",0f); set=>ComponentProperties.Property(this,16,"outlineWidth",JsonValue.Create(value)); }
        public bool richText { get=>ComponentProperties.Value(this,16,"richText",true); set=>ComponentProperties.Property(this,16,"richText",JsonValue.Create(value)); }
        public bool wordWrap { get=>ComponentProperties.Value(this,16,"wordWrap",true); set=>ComponentProperties.Property(this,16,"wordWrap",JsonValue.Create(value)); }
        public bool autoSize { get=>ComponentProperties.Value(this,16,"autoSize",false); set=>ComponentProperties.Property(this,16,"autoSize",JsonValue.Create(value)); }
        public int alignment { get=>ComponentProperties.Value(this,16,"alignment",1); set=>ComponentProperties.Property(this,16,"alignment",JsonValue.Create(value)); }
    }
    public sealed class Button : Component {
        public int transition {get=>ComponentProperties.Value(this,17,"transition",1);set=>ComponentProperties.Property(this,17,"transition",JsonValue.Create(value));}
        public float fadeDuration {get=>ComponentProperties.Value(this,17,"fadeDuration",.1f);set=>ComponentProperties.Property(this,17,"fadeDuration",JsonValue.Create(value));}
        public Color highlightedColor {get=>ComponentProperties.ColorValue(this,17,"highlightedColor");set=>ComponentProperties.Property(this,17,"highlightedColor",new JsonArray(value.r,value.g,value.b,value.a));}
        public Color pressedColor {get=>ComponentProperties.ColorValue(this,17,"pressedColor");set=>ComponentProperties.Property(this,17,"pressedColor",new JsonArray(value.r,value.g,value.b,value.a));}
        public Color disabledColor {get=>ComponentProperties.ColorValue(this,17,"disabledColor");set=>ComponentProperties.Property(this,17,"disabledColor",new JsonArray(value.r,value.g,value.b,value.a));}

        public bool interactable { get=>ComponentProperties.Value(this,17,"interactable",false); set=>ComponentProperties.Property(this,17,"interactable",JsonValue.Create(value)); }
        public ButtonEvent onClick=>UIEvents.For(Entity.ID);
    }
    public sealed class CanvasGroup : Component {
        public float alpha {get=>ComponentProperties.Value(this,18,"alpha",1f);set=>ComponentProperties.Property(this,18,"alpha",JsonValue.Create(value));}
        public bool interactable {get=>ComponentProperties.Value(this,18,"interactable",true);set=>ComponentProperties.Property(this,18,"interactable",JsonValue.Create(value));}
        public bool blocksRaycasts {get=>ComponentProperties.Value(this,18,"blocksRaycasts",true);set=>ComponentProperties.Property(this,18,"blocksRaycasts",JsonValue.Create(value));}
    }
    public sealed class RectMask2D : Component { }
    public sealed class LayoutGroup : Component {
        public int mode {get=>ComponentProperties.Value(this,20,"mode",0);set=>ComponentProperties.Property(this,20,"mode",JsonValue.Create(value));}
        public bool fitContent {get=>ComponentProperties.Value(this,20,"fitContent",false);set=>ComponentProperties.Property(this,20,"fitContent",JsonValue.Create(value));}
        public Vector2 spacing {get=>ComponentProperties.VectorValue(this,20,"spacing");set=>ComponentProperties.Property(this,20,"spacing",new JsonArray(value.x,value.y));}
    }
}
