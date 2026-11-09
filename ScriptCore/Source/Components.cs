using Stardust.Interop;

namespace Stardust
{
    public abstract class Component
    {
        public Entity Entity { get; internal set; }
        public unsafe bool enabled {
            get {int type=TypeIdOf(GetType());var values=System.Text.Json.Nodes.JsonNode.Parse(Native.FromUtf8(Native.Api.GetComponentJson(Entity.ID,103)))?["disabled"]?.AsArray();if(values!=null)foreach(var value in values)if(value.GetValue<int>()==type)return false;return true;}
            set {int type=TypeIdOf(GetType());if(type<=0||type==14)throw new System.NotSupportedException("This structural component cannot be disabled.");fixed(byte* p=Native.Utf8("{\"type\":"+type+",\"enabled\":"+(value?"true":"false")+"}"))Native.Api.SetComponentJson(Entity.ID,103,p);}
        }
        public bool isActiveAndEnabled => enabled && Entity.activeInHierarchy;


        internal static int TypeId<T>() where T : Component => TypeIdOf(typeof(T));

        internal static int TypeIdOf(System.Type t)
        {
            if (t == typeof(Transform)) return 0;
            if (t == typeof(MeshRenderer)) return 1;
            if (t == typeof(Rigidbody)) return 2;
            if (t == typeof(BoxCollider)) return 3;
            if (t == typeof(SphereCollider)) return 4;
            if (t == typeof(CapsuleCollider)) return 5;
            if (t == typeof(Camera)) return 6;
            if (t == typeof(DirectionalLight)) return 7;
            if (t == typeof(MeshCollider)) return 9;
            if (t == typeof(Light)) return 10;
            if (t == typeof(AudioSource)) return 11;
            if (t == typeof(Animator)) return 12;
            if (t == typeof(UI.Canvas)) return 13;
            if (t == typeof(UI.RectTransform)) return 14;
            if (t == typeof(UI.Image)) return 15;
            if (t == typeof(UI.Text)) return 16;
            if (t == typeof(UI.Button)) return 17;
            if(t==typeof(UI.CanvasGroup))return 18;
            if(t==typeof(UI.RectMask2D))return 19;
            if(t==typeof(UI.LayoutGroup))return 20;
            if(t==typeof(UI.InputField))return 21;
            if(t==typeof(SpriteRenderer))return 22;
            if(t==typeof(Environment))return 23;
            if(t==typeof(VideoPlayer))return 26;
            if(t==typeof(Terrain))return 24;
            if(t==typeof(SpotLight))return 25;
            return -1;
        }
    }

    public enum Space { World, Self }

    // Unity semantics: position / eulerAngles are world space, localPosition / localEulerAngles are relative to the parent.
    public unsafe class Transform : Component
    {
        public Transform() { }
        internal Transform(Entity e) { Entity = e; }

        ulong Id => Entity.ID;

        public Vector3 position
        {
            get { Vector3 v; Native.Api.GetWorldPosition(Id, &v); return v; }
            set => Native.Api.SetWorldPosition(Id, &value);
        }
        public Vector3 localPosition
        {
            get { Vector3 v; Native.Api.GetPosition(Id, &v); return v; }
            set => Native.Api.SetPosition(Id, &value);
        }
        public Vector3 eulerAngles
        {
            get { Vector3 v; Native.Api.GetWorldEuler(Id, &v); return v; }
            set => Native.Api.SetWorldEuler(Id, &value);
        }
        public Vector3 localEulerAngles
        {
            get { Vector3 v; Native.Api.GetRotation(Id, &v); return v; }
            set => Native.Api.SetRotation(Id, &value);
        }
        public Vector3 localScale
        {
            get { Vector3 v; Native.Api.GetScale(Id, &v); return v; }
            set => Native.Api.SetScale(Id, &value);
        }
        [System.Obsolete("Use position (world) - kept for older scripts")]
        public Vector3 worldPosition => position;
        public Vector3 forward { get { Vector3 v; Native.Api.GetForward(Id, &v); return v; } }
        public Vector3 right { get { Vector3 v; Native.Api.GetRight(Id, &v); return v; } }
        public Vector3 up { get { Vector3 v; Native.Api.GetUp(Id, &v); return v; } }

        public Transform parent
        {
            get { var p = Entity.parent; return p?.transform; }
            set => Entity.parent = value?.Entity;
        }
        public int childCount => Native.Api.GetChildCount(Id);
        public Transform GetChild(int index)
        {
            ulong c = Native.Api.GetChild(Id, index);
            return c == 0 ? null : new Entity(c).transform;
        }

        // Converts a direction from local space to world space
        public Vector3 TransformDirection(Vector3 d) => right * d.x + up * d.y - forward * d.z;

        public void Translate(Vector3 delta, Space relativeTo = Space.Self)
        {
            position = position + (relativeTo == Space.Self ? TransformDirection(delta) : delta);
        }
        public void Rotate(Vector3 eulerDelta) => localEulerAngles = localEulerAngles + eulerDelta;
        public void LookAt(Vector3 target) => Native.Api.LookAt(Id, &target);
        public void LookAt(Transform target) { if (target != null) LookAt(target.position); }
    }

    public enum ForceMode { Force = 0, Impulse = 1, VelocityChange = 2, Acceleration = 3 }

    public enum RigidbodyType { Static = 0, Dynamic = 1, Kinematic = 2 }

    // Forces are frame-rate independent: AddForce(ForceMode.Force) works the same from OnUpdate and OnFixedUpdate.
    public unsafe class Rigidbody : Component
    {
        ulong Id => Entity.ID;
        public Vector3 velocity
        {
            get { Vector3 v; Native.Api.GetVelocity(Id, &v); return v; }
            set => Native.Api.SetVelocity(Id, &value);
        }
        public Vector3 angularVelocity
        {
            get { Vector3 v; Native.Api.GetAngularVelocity(Id, &v); return v; }
            set => Native.Api.SetAngularVelocity(Id, &value);
        }
        public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force) => Native.Api.AddForce(Id, &force, (int)mode);
        public void AddRelativeForce(Vector3 force, ForceMode mode = ForceMode.Force) => AddForce(Entity.transform.TransformDirection(force), mode);
        public void AddForceAtPosition(Vector3 force, Vector3 position, ForceMode mode = ForceMode.Force) => Native.Api.AddForceAtPosition(Id, &force, &position, (int)mode);
        public void AddTorque(Vector3 torque) => Native.Api.AddTorque(Id, &torque);
        public void MovePosition(Vector3 position) => Entity.transform.position = position;
        public void MoveRotation(Vector3 eulerAngles) => Entity.transform.eulerAngles = eulerAngles;

        RigidbodyNative Get() { RigidbodyNative r; Native.Api.GetRigidbody(Id, &r); return r; }
        void Set(RigidbodyNative r) => Native.Api.SetRigidbody(Id, &r);

        public float mass { get => Get().Mass; set { var r = Get(); r.Mass = value; Set(r); } }
        public float drag { get => Get().Drag; set { var r = Get(); r.Drag = value; Set(r); } }
        public float angularDrag { get => Get().AngularDrag; set { var r = Get(); r.AngularDrag = value; Set(r); } }
        public bool useGravity { get => Get().UseGravity != 0; set { var r = Get(); r.UseGravity = value ? 1 : 0; Set(r); } }
        public bool freezeRotation { get => Get().FreezeRotation != 0; set { var r = Get(); r.FreezeRotation = value ? 1 : 0; Set(r); } }
        public bool isKinematic
        {
            get => Get().Type == (int)RigidbodyType.Kinematic;
            set { var r = Get(); r.Type = (int)(value ? RigidbodyType.Kinematic : RigidbodyType.Dynamic); Set(r); }
        }
    }

    public unsafe partial class MeshRenderer : Component
    {
        // Setting the colour switches the object to its own (embedded) material
        public Color color
        {
            get { Color c; Native.Api.GetColor(Entity.ID, &c); return c; }
            set => Native.Api.SetColor(Entity.ID, &value);
        }
    }

    public unsafe class Light : Component
    {
        public Color color
        {
            get { Color c; float r; Native.Api.GetLight(Entity.ID, &c, &r); return new Color(c.r, c.g, c.b); }
            set { Color c; float r; Native.Api.GetLight(Entity.ID, &c, &r); Color n = new Color(value.r, value.g, value.b, c.a); Native.Api.SetLight(Entity.ID, &n, r); }
        }
        public float intensity
        {
            get { Color c; float r; Native.Api.GetLight(Entity.ID, &c, &r); return c.a; }
            set { Color c; float r; Native.Api.GetLight(Entity.ID, &c, &r); c.a = value; Native.Api.SetLight(Entity.ID, &c, r); }
        }
        public float range
        {
            get { Color c; float r; Native.Api.GetLight(Entity.ID, &c, &r); return r; }
            set { Color c; float r; Native.Api.GetLight(Entity.ID, &c, &r); Native.Api.SetLight(Entity.ID, &c, value); }
        }
    }

    public unsafe partial class AudioSource : Component
    {
        public void Play() => Native.Api.AudioPlay(Entity.ID);
        public void Stop() => Native.Api.AudioStop(Entity.ID);
        public bool isPlaying => Native.Api.AudioIsPlaying(Entity.ID) != 0;
        public float volume {get=>ComponentProperties.Value(this,11,"volume",1f);set=>Native.Api.AudioSetVolume(Entity.ID,value);}
        public float pitch {get=>ComponentProperties.Value(this,11,"pitch",1f);set=>Native.Api.AudioSetPitch(Entity.ID,value);}

        public static void PlayClipAtPoint(string clip, Vector3 position, float volume = 1f)
        {
            fixed (byte* p = Native.Utf8(clip)) Native.Api.PlayClipAtPoint(p, &position, volume);
        }
    }

    public partial class BoxCollider : Collider { }
    public partial class SphereCollider : Collider { }
    public partial class CapsuleCollider : Collider { }
    public partial class MeshCollider : Collider { }
    public partial class Camera : Component {
        public bool orthographic {get=>ComponentProperties.Value(this,6,"orthographic",false);set=>ComponentProperties.Property(this,6,"orthographic",System.Text.Json.Nodes.JsonValue.Create(value));}
        public float orthographicSize {get=>ComponentProperties.Value(this,6,"orthographicSize",5f);set=>ComponentProperties.Property(this,6,"orthographicSize",System.Text.Json.Nodes.JsonValue.Create(value));}
        public float fieldOfView {get=>ComponentProperties.Value(this,6,"fieldOfView",60f);set=>ComponentProperties.Property(this,6,"fieldOfView",System.Text.Json.Nodes.JsonValue.Create(value));}
        public float nearClipPlane {get=>ComponentProperties.Value(this,6,"nearClipPlane",.1f);set=>ComponentProperties.Property(this,6,"nearClipPlane",System.Text.Json.Nodes.JsonValue.Create(value));}
        public float farClipPlane {get=>ComponentProperties.Value(this,6,"farClipPlane",1000f);set=>ComponentProperties.Property(this,6,"farClipPlane",System.Text.Json.Nodes.JsonValue.Create(value));}
    }
    public partial class DirectionalLight : Component { }
}
