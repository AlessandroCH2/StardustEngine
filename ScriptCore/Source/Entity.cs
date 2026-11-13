using System;
using System.Runtime.InteropServices;
using Stardust.Interop;

namespace Stardust
{
    public enum PrimitiveType { Cube = 0, Sphere = 1, Plane = 2, Cylinder = 3 }

    // A scene object. Entities are referenced by their 64-bit UUID.
    public unsafe class Entity
    {
        public ulong ID { get; internal set; }

        protected Entity() { }
        internal Entity(ulong id) { ID = id; }

        internal string PrefabAssetPath;
        public bool IsAsset => !string.IsNullOrEmpty(PrefabAssetPath);
        public string AssetPath => PrefabAssetPath ?? "";
        public bool IsValid => !IsAsset && ID != 0 && Native.Api.EntityExists(ID) != 0;

        public string name
        {
            get => Native.FromUtf8(Native.Api.GetName(ID));
            set { fixed (byte* p = Native.Utf8(value)) Native.Api.SetName(ID, p); }
        }

        public Transform transform => new Transform(this);

        public int layer { get { var j=System.Text.Json.Nodes.JsonNode.Parse(Native.FromUtf8(Native.Api.GetComponentJson(ID,99)));return j?["layer"]?.GetValue<int>()??0;} set { if(value<0||value>31)throw new ArgumentOutOfRangeException(nameof(value));fixed(byte* p=Native.Utf8("{\"layer\":"+value+"}"))Native.Api.SetComponentJson(ID,99,p); } }
        public string tag
        {
            get => Native.FromUtf8(Native.Api.GetTag(ID));
            set { fixed (byte* p = Native.Utf8(value)) Native.Api.SetTag(ID, p); }
        }
        public bool CompareTag(string t) => tag == t;
        public bool activeSelf => Native.Api.GetActive(ID) != 0;
        public bool activeInHierarchy => Native.Api.GetActiveInHierarchy(ID) != 0;
        public void SetActive(bool value) => Native.Api.SetActive(ID, value ? 1 : 0);

        public Entity parent
        {
            get { ulong p = Native.Api.GetParent(ID); return p == 0 ? null : new Entity(p); }
            set => Native.Api.SetParent(ID, value?.ID ?? 0);
        }

        public bool HasComponent<T>() where T : class
        {
            if (typeof(T).IsSubclassOf(typeof(ScriptBehaviour))) return GetComponent<T>() != null;
            return typeof(Component).IsAssignableFrom(typeof(T)) && Native.Api.HasComponent(ID, Component.TypeIdOf(typeof(T))) != 0;
        }

        public T GetComponent<T>() where T : class
        {
            if (typeof(T).IsSubclassOf(typeof(ScriptBehaviour))) {
                fixed (byte* p = Native.Utf8(typeof(T).FullName)) {
                    IntPtr h = Native.Api.GetScriptInstanceByType(ID,p);
                    return h == IntPtr.Zero ? null : GCHandle.FromIntPtr(h).Target as T;
                }
            }
            if (!HasComponent<T>()) return null;
            var component = (Component)Activator.CreateInstance(typeof(T));
            component.Entity = this;
            return component as T;
        }

        public T AddComponent<T>() where T : Component, new()
        {
            Native.Api.AddComponent(ID, Component.TypeId<T>());
            return new T { Entity = this };
        }

        public void RemoveComponent<T>() where T : Component => Native.Api.RemoveComponent(ID, Component.TypeId<T>());

        // The script instance attached to this entity, if it is of type T
        public T As<T>() where T : ScriptBehaviour
        {
            fixed (byte* p = Native.Utf8(typeof(T).FullName)) {
                IntPtr h = Native.Api.GetScriptInstanceByType(ID, p);
                return h == IntPtr.Zero ? null : GCHandle.FromIntPtr(h).Target as T;
            }
        }

        public static Entity Find(string name)
        {
            fixed (byte* p = Native.Utf8(name))
            {
                ulong id = Native.Api.FindEntityByName(p);
                return id == 0 ? null : new Entity(id);
            }
        }

        public static Entity Create(string name = "Entity")
        {
            fixed (byte* p = Native.Utf8(name)) return new Entity(Native.Api.CreateEntity(p));
        }

        public static Entity CreatePrimitive(PrimitiveType type, string name = null)
        {
            fixed (byte* p = Native.Utf8(name ?? type.ToString())) return new Entity(Native.Api.CreatePrimitive((int)type, p));
        }

        public static void Destroy(Entity e) { if (e != null) Native.Api.DestroyEntity(e.ID); }

        public static Entity FindWithTag(string tag)
        {
            fixed (byte* p = Native.Utf8(tag))
            {
                ulong id = Native.Api.FindWithTag(p);
                return id == 0 ? null : new Entity(id);
            }
        }

        // Copies an entity (with children and all components) at runtime
        public static Entity Instantiate(Entity original) => original == null ? null : original.IsAsset?Instantiate(original.AssetPath,Vector3.zero):new Entity(Native.Api.Instantiate(original.ID, null));
        public static Entity Instantiate(Entity original, Vector3 position)
        {
            if (original == null) return null;
            if(original.IsAsset)return Instantiate(original.AssetPath,position);
            return new Entity(Native.Api.Instantiate(original.ID, &position));
        }
        internal static T TypedInstance<T>(Entity spawned) where T:class {
            if(spawned==null)return null;var typed=typeof(T)==typeof(Entity)?spawned as T:spawned.GetComponent<T>();
            if(typed==null){Destroy(spawned);throw new InvalidOperationException("Prefab root is missing "+typeof(T).FullName);}return typed;
        }
        public static T Instantiate<T>(T original) where T:class =>TypedInstance<T>(Instantiate(original as Entity??(original as Component)?.Entity));
        public static T Instantiate<T>(T original,Vector3 position) where T:class =>TypedInstance<T>(Instantiate(original as Entity??(original as Component)?.Entity,position));
        public static void DontDestroyOnLoad(Entity entity){if(entity!=null&&entity.IsValid)Native.Api.DontDestroyOnLoad(entity.ID);}

        // Spawns a prefab asset, e.g. Entity.Instantiate("Prefabs/Enemy.prefab", pos)
        public static Entity Instantiate(string prefabPath, Vector3 position)
        {
            fixed (byte* p = Native.Utf8(prefabPath))
            {
                ulong id = Native.Api.InstantiatePrefab(p, &position);
                return id == 0 ? null : new Entity(id);
            }
        }

        public override bool Equals(object obj) => obj is Entity e && (IsAsset||e.IsAsset ? IsAsset&&e.IsAsset&&AssetPath==e.AssetPath : e.ID==ID);
        public override int GetHashCode() => IsAsset?AssetPath.GetHashCode():ID.GetHashCode();
        public override string ToString() => IsAsset?$"Prefab({AssetPath})":$"Entity({name})";
    }

    // Base class for game scripts. Optional methods, found by name:
    //   void OnCreate()  void OnUpdate(float dt)  void OnFixedUpdate(float dt)
    //   void OnCollisionEnter(Entity other)  void OnDestroy()
    // Public fields of type float/int/bool/string/Vector2/Vector3/Color are editable in the Inspector.
    public abstract class ScriptBehaviour : Entity
    {
        internal int ScriptSlotIndex;
        public unsafe bool enabled {
            get {var slots=System.Text.Json.Nodes.JsonNode.Parse(Native.FromUtf8(Native.Api.GetComponentJson(ID,102)))?["slots"]?.AsArray();return slots==null||ScriptSlotIndex>=slots.Count||slots[ScriptSlotIndex].GetValue<bool>();}
            set {fixed(byte* p=Native.Utf8("{\"slot\":"+ScriptSlotIndex+",\"enabled\":"+(value?"true":"false")+"}"))Native.Api.SetComponentJson(ID,102,p);}
        }
        public bool isActiveAndEnabled => enabled && activeInHierarchy;
        public Entity entity => this;
    }
}
