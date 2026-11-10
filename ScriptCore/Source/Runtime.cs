using Stardust.Interop;
using System;
using System.Runtime.InteropServices;
namespace Stardust
{
    // Values match GLFW key codes
    public enum KeyCode
    {
        Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
        Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Escape = 256, Enter, Tab, Backspace, Insert, Delete, RightArrow, LeftArrow, DownArrow, UpArrow,
        F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        LeftShift = 340, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt,
    }

    public enum MouseButton { Left = 0, Right = 1, Middle = 2 }

    public static unsafe partial class Input
    {
        public static bool GetKey(KeyCode k) => Native.Api.IsKeyDown((int)k) != 0;
        public static bool GetKeyDown(KeyCode k) => Native.Api.IsKeyPressed((int)k) != 0;
        public static bool GetMouseButton(MouseButton b) => Native.Api.IsMouseDown((int)b) != 0;
        public static bool GetMouseButtonDown(MouseButton b) => Native.Api.IsMousePressed((int)b) != 0;
        public static Vector2 mousePosition { get { Vector2 v; Native.Api.GetMousePosition(&v); return v; } }
        public static Vector2 mouseDelta { get { Vector2 v; Native.Api.GetMouseDelta(&v); return v; } }
        public static float mouseScroll => Native.Api.GetScroll();
        public static bool cursorLocked
        {
            get => System.Text.Json.Nodes.JsonNode.Parse(Native.FromUtf8(Native.Api.GetComponentJson(0,104)))?["cursorLocked"]?.GetValue<bool>()??false;
            set { Native.Api.SetCursorLocked(value ? 1 : 0); }
        }
        // WASD / arrows as a -1..1 axis, like Unity's "Horizontal" / "Vertical"
        public static float GetAxis(string axis) => InputManager.GetAxis(axis);
        /// <summary>The axis without smoothing: -1, 0 or 1 for keys.</summary>
        public static float GetAxisRaw(string axis) => InputManager.GetAxisRaw(axis);

    }

    public static unsafe class Time
    {
        public static float deltaTime { get; internal set; }
        public static float time { get; internal set; }
        public static float fixedDeltaTime { get; internal set; } = 1f / 60f;
        public static float timeScale { get => Native.Api.GetTimeScale(); set => Native.Api.SetTimeScale(value); }
    }

    public sealed unsafe class SceneLoadOperation {
        readonly ulong id;readonly System.Threading.Tasks.TaskCompletionSource<bool> completion=new();
        internal SceneLoadOperation(ulong value){id=value;SceneLoadEvents.pending.Add(this);}
        public float progress=>Native.Api.SceneLoadProgress(id);
        public bool isDone=>Native.Api.SceneLoadStatus(id)!=0;
        public string error=>id==0?"Scene is unavailable or another load is in progress":Native.FromUtf8(Native.Api.SceneLoadError(id));
        bool activation=true;public bool allowSceneActivation {get=>activation;set{activation=value;Native.Api.SceneLoadActivation(id,value?1:0);}}
        public System.Runtime.CompilerServices.TaskAwaiter<bool> GetAwaiter()=>completion.Task.GetAwaiter();
        internal bool Poll(){if(!isDone)return false;if(Native.Api.SceneLoadStatus(id)>0)completion.TrySetResult(true);else completion.TrySetException(new System.InvalidOperationException(error));return true;}
    }
    internal static class SceneLoadEvents {internal static readonly System.Collections.Generic.List<SceneLoadOperation> pending=new();internal static void Tick(){foreach(var op in pending.ToArray())if(op.Poll())pending.Remove(op);}}
    public static unsafe class SceneManager
    {
        // Name (e.g. "Level2"), asset path or build index as text; the scene must be in Scenes In Build (File > Build Settings)
        public static bool LoadScene(string nameOrPath) { fixed (byte* p = Native.Utf8(nameOrPath)) return Native.Api.LoadScene(p) != 0; }
        public static bool LoadScene(int buildIndex) => LoadScene(buildIndex.ToString());
        public static SceneLoadOperation LoadSceneAsync(string nameOrPath){fixed(byte* p=Native.Utf8(nameOrPath))return new SceneLoadOperation(Native.Api.LoadSceneAsync(p));}
        public static SceneLoadOperation LoadSceneAsync(int index)=>LoadSceneAsync(index.ToString());
        public static void DontDestroyOnLoad(Entity entity)=>Entity.DontDestroyOnLoad(entity);
        public static string activeSceneName => Native.FromUtf8(Native.Api.GetActiveSceneName());
    }

    public static unsafe class Application
    {
        public static void Quit()
            => Native.Api.Quit();

        public static string GetVersion()
            => MarshalString(Native.Api.GetVersion());

        public static string GetProductName()
            => MarshalString(Native.Api.GetProductName());

        public static string GetCompanyName()
            => MarshalString(Native.Api.GetCompanyName());

        public static string GetDataPath()
            => MarshalString(Native.Api.GetDataPath());

        public static string GetPersistentDataPath()
            => MarshalString(Native.Api.GetPersistentDataPath());

        public static string GetProjectPath()
            => MarshalString(Native.Api.GetProjectPath());

        public static bool IsPlaying
            => Native.Api.IsPlaying() != 0;

        private static string MarshalString(IntPtr ptr)
        => Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
        
    }

    public static unsafe class Screen
    {
        public static int width { get { Vector2 v; Native.Api.GetScreenSize(&v); return (int)v.x; } }
        public static int height { get { Vector2 v; Native.Api.GetScreenSize(&v); return (int)v.y; } }
    }

    public static class Random
    {
        static readonly System.Random s_Rng = new System.Random();
        public static float value => (float)s_Rng.NextDouble();
        public static float Range(float min, float max) => min + (max - min) * value;
        public static int Range(int min, int max) => s_Rng.Next(min, max);
        public static Vector3 insideUnitSphere
        {
            get { Vector3 v; do { v = new Vector3(Range(-1f, 1f), Range(-1f, 1f), Range(-1f, 1f)); } while (v.sqrMagnitude > 1f); return v; }
        }
        public static Vector2 insideUnitCircle
        {
            get { Vector2 v; do { v = new Vector2(Range(-1f, 1f), Range(-1f, 1f)); } while (v.x * v.x + v.y * v.y > 1f); return v; }
        }
        public static Color ColorHSV() => Color.HSV(value, 0.7f, 1f);
    }

    public static class Debug
    {
        public static void Log(object message) => Native.Log(1, message?.ToString() ?? "null");
        public static void LogWarning(object message) => Native.Log(2, message?.ToString() ?? "null");
        public static void LogError(object message) => Native.Log(3, message?.ToString() ?? "null");
    }

    public struct RaycastHit
    {
        public Entity entity;
        public Vector3 point;
        public Vector3 normal;
        public float distance;
    }

    public static unsafe class Physics
    {
        // ignore: skip this entity's own collider (e.g. ground checks from inside a character capsule)
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1000f, Entity ignore = null)
        {
            RaycastHitNative h;
            hit = default;
            if (Native.Api.Raycast(&origin, &direction, maxDistance, ignore?.ID ?? 0, &h) == 0) return false;
            hit.entity = h.Entity != 0 ? new Entity(h.Entity) : null;
            hit.point = h.Point;
            hit.normal = h.Normal;
            hit.distance = h.Distance;
            return true;
        }
    }
}
