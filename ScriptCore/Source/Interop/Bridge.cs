using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Stardust.Interop
{
    // Game scripts live in a collectible context so they can be unloaded and reloaded (hot reload).
    internal sealed class GameLoadContext : AssemblyLoadContext
    {
        public GameLoadContext() : base("StardustGame", isCollectible: true) { }
        // Editor scripts reference the game assembly by name: hand back the one already loaded here
        protected override Assembly Load(AssemblyName name) =>
            name.Name == "Stardust.ScriptCore" ? typeof(Entity).Assembly : Assemblies.FirstOrDefault(a => a.GetName().Name == name.Name);
    }

    internal sealed class ScriptType
    {
        public Type Type;
        public MethodInfo OnCreate, OnUpdate, OnFixedUpdate, OnCollision, OnCollisionExit, OnTriggerEnter, OnTriggerExit, OnDestroy, OnClick;
        public List<FieldInfo> Fields = new();
    }

    // Entry points called by the native engine (Engine/src/Scripting/ScriptEngine.cpp)
    public static unsafe class Bridge
    {
        static GameLoadContext s_Context;
        static readonly Dictionary<string, ScriptType> s_Types = new();
        static readonly object[] s_OneArg = new object[1];
        static readonly Dictionary<string,MethodInfo> s_EditorCommands = new();
        static readonly Dictionary<string,Type> s_CustomEditors=new();

        static readonly HashSet<Type> s_FieldTypes = new()
        {
            typeof(float), typeof(double), typeof(int), typeof(bool), typeof(string), typeof(Vector2), typeof(Vector3), typeof(Color)
        };

        [UnmanagedCallersOnly]
        public static int Initialize(NativeApi* api, int size)
        {
            if (size != sizeof(NativeApi)) return 0;
            Native.Api = *api;
            return 1;
        }

        [UnmanagedCallersOnly]
        public static void SetTime(float dt, float time, float fixedDt)
        {
            if(time<Time.time)InputManager.Reset();
            Time.deltaTime = dt;
            Time.time = time;
            Time.fixedDeltaTime = fixedDt;
        }

        [UnmanagedCallersOnly]
        public static int LoadAssembly(byte* path)
        {
            try
            {
                string p = Native.FromUtf8(path);
                s_Context = new GameLoadContext();
                // Load from memory so the DLL can be rebuilt while the editor runs
                using var dll = new MemoryStream(File.ReadAllBytes(p));
                string pdbPath = Path.ChangeExtension(p, ".pdb");
                using var pdb = File.Exists(pdbPath) ? new MemoryStream(File.ReadAllBytes(pdbPath)) : null;
                Assembly game = s_Context.LoadFromStream(dll, pdb);
                s_Types.Clear();
                foreach (Type t in SafeTypes(game))
                {
                    if (!t.IsSubclassOf(typeof(ScriptBehaviour)) || t.IsAbstract) continue;
                    const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
                    var st = new ScriptType
                    {
                        Type = t,
                        OnCreate = t.GetMethod("OnCreate", flags, Type.EmptyTypes),
                        OnUpdate = t.GetMethod("OnUpdate", flags, new[] { typeof(float) }),
                        OnFixedUpdate = t.GetMethod("OnFixedUpdate", flags, new[] { typeof(float) }),
                        OnCollision = t.GetMethod("OnCollisionEnter", flags, new[] { typeof(Entity) }),
                        OnCollisionExit = t.GetMethod("OnCollisionExit", flags, new[] { typeof(Entity) }),
                        OnTriggerEnter = t.GetMethod("OnTriggerEnter", flags, new[] { typeof(Entity) }),
                        OnTriggerExit = t.GetMethod("OnTriggerExit", flags, new[] { typeof(Entity) }),
                        OnDestroy = t.GetMethod("OnDestroy", flags, Type.EmptyTypes),
                        OnClick = t.GetMethod("OnClick", flags, Type.EmptyTypes),
                    };
                    // Walk declared fields so private serialized fields inherited from a base script work too.
                    for (Type owner = t; owner != null && owner != typeof(ScriptBehaviour); owner = owner.BaseType)
                        foreach (var f in owner.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                            // [HideInInspector] fields are still saved, like Unity; only the inspector skips them
                            if (!f.IsInitOnly && !f.IsLiteral &&
                                (f.IsPublic || f.IsDefined(typeof(SerializeField))) &&
                                SupportedField(f.FieldType) &&
                                !st.Fields.Any(existing => existing.Name == f.Name)) st.Fields.Add(f);
                    s_Types[t.FullName ?? t.Name] = st;
                }
                return 1;
            }
            catch (Exception e)
            {
                Native.Log(3, $"Failed to load game assembly: {e}");
                return 0;
            }
        }

        static IEnumerable<Type> SafeTypes(Assembly a)
        {
            try { return a.GetTypes(); }
            catch (ReflectionTypeLoadException e) { return e.Types.Where(t => t != null); }
        }

        [UnmanagedCallersOnly]
        public static void UnloadAssembly()
        {
            Stardust.UI.UIEvents.Clear();
            s_EditorCommands.Clear();s_CustomEditors.Clear();Stardust.Editor.EditorWindow.Open.Clear();
            s_Types.Clear();
            if (s_Context == null) return;
            s_Context.Unload();
            s_Context = null;
            for (int i = 0; i < 3; ++i) { GC.Collect(); GC.WaitForPendingFinalizers(); }
        }

        [UnmanagedCallersOnly]
        public static IntPtr GetClasses()
        {
            var arr = new JsonArray();
            foreach (var (name, st) in s_Types.OrderBy(k => k.Key))
            {
                var fields = new JsonArray();
                object defaults = null;
                try { defaults = Activator.CreateInstance(st.Type); } catch { }
                foreach (var f in st.Fields)
                    fields.Add(new JsonObject
                    {
                        ["name"] = f.Name,
                        ["type"] = TypeName(f.FieldType),["schema"]=HiddenSchema(f),
                        ["default"] = defaults != null ? ToJson(f.GetValue(defaults)) : null,
                    });
                arr.Add(new JsonObject { ["name"] = name, ["fields"] = fields, ["methods"] = ActionMethods(st.Type,true) });
            }
            return Marshal.StringToCoTaskMemUTF8(arr.ToJsonString());
        }

        [UnmanagedCallersOnly]
        public static void FreeString(IntPtr p) => Marshal.FreeCoTaskMem(p);

        [UnmanagedCallersOnly]
        public static IntPtr CreateInstance(byte* className, ulong entityId, byte* fieldsJson)
        {
            string name = Native.FromUtf8(className);
            if (!s_Types.TryGetValue(name, out var st)) return IntPtr.Zero;
            try
            {
                var obj = (ScriptBehaviour)Activator.CreateInstance(st.Type);
                obj.ID = entityId;
                var json = JsonNode.Parse(Native.FromUtf8(fieldsJson)) as JsonObject;
                ((ScriptBehaviour)obj).ScriptSlotIndex=json?["__sdScriptSlot"]?.GetValue<int>()??0;
                if (json != null)
                    foreach (var f in st.Fields)
                        if (!NeedsReferences(f.FieldType) && json.TryGetPropertyValue(f.Name, out var v) && v != null) TrySet(obj, f, v);
                return GCHandle.ToIntPtr(GCHandle.Alloc(obj));
            }
            catch (Exception e)
            {
                Native.Log(3, $"{name}: constructor failed: {e.InnerException ?? e}");
                return IntPtr.Zero;
            }
        }

        // Second phase: references to other entities / components / scripts (all instances exist by now)
        [UnmanagedCallersOnly]
        public static void ResolveReferences(IntPtr handle, byte* fieldsJson)
        {
            var obj = GCHandle.FromIntPtr(handle).Target;
            if (obj == null || !s_Types.TryGetValue(obj.GetType().FullName ?? "", out var st)) return;
            try
            {
                var json = JsonNode.Parse(Native.FromUtf8(fieldsJson)) as JsonObject;
                if (json == null) return;
                foreach (var f in st.Fields)
                    if (NeedsReferences(f.FieldType) && json.TryGetPropertyValue(f.Name, out var v) && v != null) TrySet(obj, f, v);
            }
            catch (Exception e) { Native.Log(3, $"{st.Type.Name}: could not resolve references: {e.Message}"); }
        }

        [UnmanagedCallersOnly]
        public static void DestroyInstance(IntPtr handle)
        {
            if (handle != IntPtr.Zero) GCHandle.FromIntPtr(handle).Free();
        }

        static void Call(IntPtr handle, Func<ScriptType, MethodInfo> pick, object arg, bool hasArg)
        {
            var obj = GCHandle.FromIntPtr(handle).Target;
            if (obj == null || !s_Types.TryGetValue(obj.GetType().FullName ?? "", out var st)) return;
            var m = pick(st);
            if (m == null) return;
            try
            {
                if (hasArg) { s_OneArg[0] = arg; m.Invoke(obj, s_OneArg); }
                else m.Invoke(obj, null);
            }
            catch (TargetInvocationException e)
            {
                Native.Log(3, $"{st.Type.Name}.{m.Name}: {e.InnerException}");
            }
        }

        [UnmanagedCallersOnly] public static void InvokeCreate(IntPtr h) => Call(h, s => s.OnCreate, null, false);
        [UnmanagedCallersOnly] public static void InvokeUpdate(IntPtr h, float dt) {SceneLoadEvents.Tick();Call(h, s => s.OnUpdate, dt, true); }
        [UnmanagedCallersOnly] public static void InvokeFixedUpdate(IntPtr h, float dt) => Call(h, s => s.OnFixedUpdate, dt, true);
        [UnmanagedCallersOnly] public static void PumpSceneLoads()=>SceneLoadEvents.Tick();
        [UnmanagedCallersOnly] public static void DispatchUIInput(ulong id,byte* text,int submit)=>Stardust.UI.InputEvents.Fire(id,Native.FromUtf8(text),submit!=0);
        [UnmanagedCallersOnly] public static void ClearUIEvents() {Stardust.UI.UIEvents.Clear();Stardust.UI.InputEvents.Clear();}
        [UnmanagedCallersOnly] public static void DispatchUIButton(ulong entity) => Stardust.UI.UIEvents.Fire(entity);
        [UnmanagedCallersOnly] public static void InvokeNamed(IntPtr handle, byte* method) {
            try { var target=GCHandle.FromIntPtr(handle).Target; target?.GetType().GetMethod(Native.FromUtf8(method),BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic,null,Type.EmptyTypes,null)?.Invoke(target,null); }
            catch(Exception e){Native.Log(3,"UI callback: "+e.GetBaseException().Message);}
        }
        static bool ActionType(Type t) => t==typeof(float)||t==typeof(double)||t==typeof(int)||t==typeof(bool)||t==typeof(string)||t==typeof(Vector2)||t==typeof(Vector3)||t==typeof(Color)||t.IsEnum||IsReference(t);
        static string Signature(MethodInfo m) => m.Name+"("+string.Join(",",m.GetParameters().Select(p=>p.ParameterType.FullName))+")";
        static IEnumerable<MethodInfo> Actions(Type type,bool script) => type.GetMethods(BindingFlags.Public|BindingFlags.Instance).Where(m=>m.ReturnType==typeof(void)&&!m.IsGenericMethod&&!m.IsStatic&&(!m.IsSpecialName||m.Name.StartsWith("set_"))&&m.GetParameters().All(p=>!p.IsOut&&!p.ParameterType.IsByRef&&ActionType(p.ParameterType))&&(!script||m.DeclaringType!=typeof(Entity)&&m.DeclaringType!=typeof(ScriptBehaviour)&&m.DeclaringType!=typeof(object))).OrderBy(m=>m.Name);
        static JsonArray ActionMethods(Type type,bool script){var methods=new JsonArray();foreach(var m in Actions(type,script)){var args=new JsonArray();foreach(var p in m.GetParameters()){var t=p.ParameterType;JsonNode value= t==typeof(string)?JsonValue.Create(""):IsReference(t)?JsonValue.Create("0"):t.IsEnum?JsonValue.Create(Convert.ToInt32(Activator.CreateInstance(t))):ToJson(Activator.CreateInstance(t));if(p.HasDefaultValue)value=ToJson(p.DefaultValue);args.Add(new JsonObject{["name"]=p.Name,["type"]=t.IsEnum?"int":TypeName(t),["default"]=value});}methods.Add(new JsonObject{["name"]=m.Name,["signature"]=Signature(m),["parameters"]=args});}return methods;}
        static Type ActionComponent(string name)=>typeof(Entity).Assembly.GetTypes().FirstOrDefault(t=>t.Name==name&&(t==typeof(Entity)||t.IsSubclassOf(typeof(Component)))&&!t.IsAbstract);
        [UnmanagedCallersOnly] public static IntPtr GetComponentActions(byte* component){var type=ActionComponent(Native.FromUtf8(component));return Marshal.StringToCoTaskMemUTF8((type==null?new JsonArray():ActionMethods(type,false)).ToJsonString());}
        static object ActionValue(Type t,JsonNode v){if(v==null)return t.IsValueType?Activator.CreateInstance(t):null;if(typeof(Asset).IsAssignableFrom(t)){var asset=(Asset)Activator.CreateInstance(t);asset.path=v is JsonObject?v["asset"]?.GetValue<string>()??"":v.GetValue<string>();return asset;}if(IsReference(t)){if(v is JsonObject obj&&obj["asset"]!=null)return MakeAssetReference(t,obj["asset"].GetValue<string>());return ResolveReference(t,v.GetValue<string>());}if(t==typeof(Vector2))return new Vector2(v[0].GetValue<float>(),v[1].GetValue<float>());if(t==typeof(Vector3))return new Vector3(v[0].GetValue<float>(),v[1].GetValue<float>(),v[2].GetValue<float>());if(t==typeof(Color))return new Color(v[0].GetValue<float>(),v[1].GetValue<float>(),v[2].GetValue<float>(),v[3].GetValue<float>());if(t.IsEnum)return Enum.ToObject(t,v.GetValue<int>());return JsonSerializer.Deserialize(v.ToJsonString(),t);}
        [UnmanagedCallersOnly] public static void InvokeAction(IntPtr handle,ulong entity,byte* component,byte* signature,byte* arguments){try{object target=handle!=IntPtr.Zero?GCHandle.FromIntPtr(handle).Target:null;if(target==null){var type=ActionComponent(Native.FromUtf8(component));if(type==null)return;target=ResolveReference(type,entity.ToString("x16"));}if(target==null)return;string key=Native.FromUtf8(signature);if(target is Stardust.UI.Image image&&key=="set_sprite(System.String)"){var legacy=JsonNode.Parse(Native.FromUtf8(arguments)) as JsonArray;if(legacy?.Count==1)image.SetSprite(legacy[0]?.GetValue<string>()??"");return;}var method=Actions(target.GetType(),target is ScriptBehaviour).FirstOrDefault(m=>Signature(m)==key);if(method==null)return;var values=JsonNode.Parse(Native.FromUtf8(arguments)) as JsonArray;var pars=method.GetParameters();if(values==null||values.Count!=pars.Length)return;method.Invoke(target,pars.Select((p,i)=>ActionValue(p.ParameterType,values[i])).ToArray());}catch(Exception e){Native.Log(3,"UI action: "+e.GetBaseException().Message);}}
        [UnmanagedCallersOnly] public static void InvokeButton(IntPtr h) => Call(h, s => s.OnClick, null, false);
        [UnmanagedCallersOnly]
        public static void InvokeContact(IntPtr h, ulong other, int kind) => Call(h, s => kind switch
        {
            0 => s.OnCollision, 1 => s.OnCollisionExit, 2 => s.OnTriggerEnter, _ => s.OnTriggerExit,
        }, new Entity(other), true);
        [UnmanagedCallersOnly] public static void InvokeDestroy(IntPtr h) => Call(h, s => s.OnDestroy, null, false);

        [UnmanagedCallersOnly]
        public static IntPtr GetFields(IntPtr handle)
        {
            var obj = GCHandle.FromIntPtr(handle).Target;
            var o = new JsonObject();
            if (obj != null && s_Types.TryGetValue(obj.GetType().FullName ?? "", out var st))
                foreach (var f in st.Fields) o[f.Name] = ToJson(f.GetValue(obj));
            return Marshal.StringToCoTaskMemUTF8(o.ToJsonString());
        }

        [UnmanagedCallersOnly]
        public static void SetField(IntPtr handle, byte* name, byte* json)
        {
            var obj = GCHandle.FromIntPtr(handle).Target;
            if (obj == null || !s_Types.TryGetValue(obj.GetType().FullName ?? "", out var st)) return;
            string n = Native.FromUtf8(name);
            var f = st.Fields.FirstOrDefault(x => x.Name == n);
            var v = JsonNode.Parse(Native.FromUtf8(json));
            if (f != null && v != null) TrySet(obj, f, v);
        }

        // ---------------------------------------------------------------- field (de)serialization

        // Entity, any Component (Transform, Camera, Rigidbody...) or any script type
        static bool IsReference(Type t) => t == typeof(Entity) || t.IsSubclassOf(typeof(Component)) || t.IsSubclassOf(typeof(ScriptBehaviour));

        [UnmanagedCallersOnly]
        public static int LoadEditorAssembly(byte* path)
        {
            try {
                s_EditorCommands.Clear();s_CustomEditors.Clear();Stardust.Editor.EditorWindow.Open.Clear();
                using var stream = new MemoryStream(File.ReadAllBytes(Native.FromUtf8(path)));
                var assembly = s_Context.LoadFromStream(stream);
                foreach(var type in SafeTypes(assembly)){var custom=type.GetCustomAttribute<Stardust.Editor.CustomEditor>();if(custom!=null&&typeof(Stardust.Editor.Editor).IsAssignableFrom(type))s_CustomEditors[custom.InspectedType.FullName]=type;}
                foreach (var type in SafeTypes(assembly))
                    foreach (var method in type.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic)) {
                        var menu = method.GetCustomAttribute<Stardust.Editor.MenuItem>();
                        if (menu != null && method.GetParameters().Length == 0) s_EditorCommands[menu.Path] = method;
                    }
                return 1;
            } catch (Exception e) { Native.Log(3,"Editor scripts: " + e.Message); return 0; }
        }
        [UnmanagedCallersOnly] public static IntPtr GetEditorCommands() => Marshal.StringToCoTaskMemUTF8(JsonSerializer.Serialize(s_EditorCommands.Keys));
        [UnmanagedCallersOnly]
        public static void InvokeEditorCommand(byte* path)
        {
            try { if (s_EditorCommands.TryGetValue(Native.FromUtf8(path),out var method)) method.Invoke(null,null); }
            catch (Exception e) { Native.Log(3,"Editor command: " + e.GetBaseException().Message); }
        }

        [UnmanagedCallersOnly]
        public static IntPtr DrawEditorUI(byte* request){
            var response=new JsonObject();
            try{
                var q=JsonNode.Parse(Native.FromUtf8(request)).AsObject();var kind=q["kind"]?.GetValue<string>();
                if(kind=="windows"){var windows=new JsonArray();foreach(var pair in Stardust.Editor.EditorWindow.Open)windows.Add(new JsonObject{["id"]=pair.Key,["title"]=pair.Value.title});response["windows"]=windows;}
                else {
                    Stardust.Editor.EditorGUI.Begin(q["events"] as JsonObject);
                    if(kind=="window") {if(Stardust.Editor.EditorWindow.Open.TryGetValue(q["id"].GetValue<string>(),out var window)){if(q["close"]?.GetValue<bool>()==true)window.Close();else window.OnGUI();}}
                    if(kind=="scene"){
                        string name=q["class"].GetValue<string>();
                        if(s_CustomEditors.TryGetValue(name,out var custom)&&s_Types.TryGetValue(name,out var script)&&custom.GetMethod("OnSceneGUI")?.DeclaringType!=typeof(Stardust.Editor.Editor)){
                            var target=(ScriptBehaviour)Activator.CreateInstance(script.Type);target.ID=ulong.Parse(q["entity"].GetValue<string>(),System.Globalization.NumberStyles.HexNumber);
                            var fields=q["fields"] as JsonObject??new();foreach(var f in script.Fields)if(fields[f.Name]!=null)TrySet(target,f,fields[f.Name]);
                            Stardust.Editor.SceneView.Begin(q["input"] as JsonObject);Stardust.Editor.Handles.Begin();
                            var editor=(Stardust.Editor.Editor)Activator.CreateInstance(custom);editor.target=target;editor.OnSceneGUI();
                            var updated=new JsonObject();foreach(var f in script.Fields)if(!NeedsReferences(f.FieldType))updated[f.Name]=ToJson(f.GetValue(target));
                            response["fields"]=updated;response["handled"]=true;response["capture"]=Stardust.Editor.SceneView.Captured;response["handles"]=Stardust.Editor.Handles.Commands.DeepClone();
                        }
                    }
                    if(kind=="inspector"){
                        string name=q["class"].GetValue<string>();
                        if(s_CustomEditors.TryGetValue(name,out var custom)&&s_Types.TryGetValue(name,out var script)){
                            var target=(ScriptBehaviour)Activator.CreateInstance(script.Type);target.ID=ulong.Parse(q["entity"].GetValue<string>(),System.Globalization.NumberStyles.HexNumber);
                            var fields=q["fields"] as JsonObject??new();foreach(var f in script.Fields)if(fields[f.Name]!=null)TrySet(target,f,fields[f.Name]);
                            var editor=(Stardust.Editor.Editor)Activator.CreateInstance(custom);editor.target=target;editor.OnInspectorGUI();
                            var updated=new JsonObject();foreach(var f in script.Fields)if(!NeedsReferences(f.FieldType))updated[f.Name]=ToJson(f.GetValue(target));response["fields"]=updated;response["handled"]=true;
                        }
                    }
                    response["commands"]=Stardust.Editor.EditorGUI.Commands.DeepClone();
                }
            }catch(Exception e){Native.Log(3,"Custom editor: "+e.GetBaseException().Message);response["error"]=e.GetBaseException().Message;}
            return Marshal.StringToCoTaskMemUTF8(response.ToJsonString());
        }

        static bool ListType(Type t)=>t.IsArray&&t.GetArrayRank()==1||t.IsGenericType&&t.GetGenericTypeDefinition()==typeof(List<>);
        static bool DictionaryType(Type t)=>t.IsGenericType&&t.GetGenericTypeDefinition()==typeof(Dictionary<,>);
        static Type ElementType(Type t)=>t.IsArray?t.GetElementType():t.GetGenericArguments()[0];
        static bool SupportedField(Type t,int depth=0){if(depth>8)return false;if(s_FieldTypes.Contains(t)||t.IsEnum||IsReference(t)||typeof(Asset).IsAssignableFrom(t))return true;if(ListType(t))return SupportedField(ElementType(t),depth+1);if(DictionaryType(t)){var args=t.GetGenericArguments();return (args[0]==typeof(string)||args[0]==typeof(int)||args[0]==typeof(bool)||args[0].IsEnum)&&SupportedField(args[1],depth+1);}return false;}
        static bool NeedsReferences(Type t)=>IsReference(t)||ListType(t)&&NeedsReferences(ElementType(t))||DictionaryType(t)&&NeedsReferences(t.GetGenericArguments()[1]);
        static JsonObject HiddenSchema(FieldInfo f){var schema=FieldSchema(f.FieldType);if(f.IsDefined(typeof(HideInInspector)))schema["hidden"]=true;return schema;}
        static JsonObject FieldSchema(Type t){var result=new JsonObject{["kind"]=TypeName(t)};if(ListType(t))result["element"]=FieldSchema(ElementType(t));else if(DictionaryType(t)){result["key"]=FieldSchema(t.GetGenericArguments()[0]);result["value"]=FieldSchema(t.GetGenericArguments()[1]);}else if(t.IsEnum){result["names"]=new JsonArray(Enum.GetNames(t).Select(n=>(JsonNode)JsonValue.Create(n)).ToArray());result["values"]=new JsonArray(Enum.GetValues(t).Cast<object>().Select(v=>(JsonNode)JsonValue.Create(Convert.ToInt32(v))).ToArray());}result["default"]=ListType(t)||DictionaryType(t)?new JsonArray():t==typeof(string)?JsonValue.Create(""):IsReference(t)?JsonValue.Create("0"):t.IsEnum?JsonValue.Create(0):ToJson(Activator.CreateInstance(t));return result;}
        static object FieldValue(Type t,JsonNode node){if(node==null)return t.IsValueType?Activator.CreateInstance(t):null;if(ListType(t)){var element=ElementType(t);var values=node.AsArray();if(t.IsArray){var array=Array.CreateInstance(element,values.Count);for(int i=0;i<values.Count;++i)array.SetValue(FieldValue(element,values[i]),i);return array;}var list=(System.Collections.IList)Activator.CreateInstance(t);foreach(var v in values)list.Add(FieldValue(element,v));return list;}if(DictionaryType(t)){var args=t.GetGenericArguments();var dict=(System.Collections.IDictionary)Activator.CreateInstance(t);foreach(var pair in node.AsArray())dict.Add(FieldValue(args[0],pair["key"]),FieldValue(args[1],pair["value"]));return dict;}return ActionValue(t,node);}
        static string TypeName(Type t) =>
            typeof(Asset).IsAssignableFrom(t)?"Asset:"+t.Name:ListType(t)?"List":DictionaryType(t)?"Dictionary":t.IsEnum?"enum":
            t == typeof(float) || t == typeof(double) ? "float" : t == typeof(int) ? "int" : t == typeof(bool) ? "bool" :
            t == typeof(string) ? "string" : t == typeof(Vector2) ? "Vector2" : t == typeof(Vector3) ? "Vector3" :
            t == typeof(Color) ? "Color" : t == typeof(Entity) ? "Entity" :
            t.IsSubclassOf(typeof(Component)) ? "Component:" + t.Name : "Script:" + (t.FullName ?? t.Name);

        // References are stored as the target entity's UUID in hex ("0" = none)
        static ulong RefId(object v) => v switch { Entity e => e.ID, Component c => c.Entity?.ID ?? 0, _ => 0 };

        static JsonNode ToJson(object v) => v switch
        {
            float f => JsonValue.Create(f),
            double d => JsonValue.Create(d),
            int i => JsonValue.Create(i),
            bool b => JsonValue.Create(b),
            string s => JsonValue.Create(s),
            Vector2 v2 => new JsonArray(v2.x, v2.y),
            Vector3 v3 => new JsonArray(v3.x, v3.y, v3.z),
            Color c => new JsonArray(c.r, c.g, c.b, c.a),
            Asset asset => new JsonObject{["asset"]=asset.path},
            Entity e when e.IsAsset => new JsonObject{["asset"]=e.AssetPath},
            Component c when c.Entity?.IsAsset==true => new JsonObject{["asset"]=c.Entity.AssetPath},
            Entity or Component => JsonValue.Create(RefId(v).ToString("x16")),
            System.Collections.IDictionary dict => DictionaryJson(dict),
            System.Collections.IList list => new JsonArray(list.Cast<object>().Select(ToJson).ToArray()),
            Enum e => JsonValue.Create(Convert.ToInt32(e)),
            _ => null,
        };

        static JsonArray DictionaryJson(System.Collections.IDictionary dictionary){var values=new JsonArray();foreach(System.Collections.DictionaryEntry entry in dictionary)values.Add(new JsonObject{["key"]=ToJson(entry.Key),["value"]=ToJson(entry.Value)});return values;}
        static void TrySet(object obj, FieldInfo f, JsonNode v)
        {
            try
            {
                f.SetValue(obj,FieldValue(f.FieldType,v));
            }
            catch (Exception) { /* incompatible saved value (field type changed): keep the default */ }
        }

        static object MakeAssetReference(Type type,string path){if(string.IsNullOrEmpty(path))return null;if(type.IsSubclassOf(typeof(Component))){var component=(Component)Activator.CreateInstance(type);component.Entity=new Entity(0){PrefabAssetPath=path};return component;}var entity=(Entity)Activator.CreateInstance(type,true);entity.PrefabAssetPath=path;return entity;}

        static object ResolveReference(Type t, string hex)
        {
            if (string.IsNullOrEmpty(hex) || !ulong.TryParse(hex, System.Globalization.NumberStyles.HexNumber, null, out ulong id) || id == 0) return null;
            if (Native.Api.EntityExists(id) == 0) return null;
            if (t == typeof(Entity)) return new Entity(id);
            if (t.IsSubclassOf(typeof(ScriptBehaviour)))
            {
                IntPtr h;
                fixed (byte* p = Native.Utf8(t.FullName)) h = Native.Api.GetScriptInstanceByType(id, p);
                var target = h == IntPtr.Zero ? null : GCHandle.FromIntPtr(h).Target;
                return target != null && t.IsInstanceOfType(target) ? target : null;
            }
            // Component: only if the entity actually has it (Transform always exists)
            int typeId = Component.TypeIdOf(t);
            if (typeId < 0 || (typeId != 0 && Native.Api.HasComponent(id, typeId) == 0)) return null;
            var comp = (Component)Activator.CreateInstance(t);
            comp.Entity = new Entity(id);
            return comp;
        }
    }
}

