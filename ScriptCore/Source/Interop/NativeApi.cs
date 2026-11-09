using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Stardust.Interop
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RaycastHitNative
    {
        public ulong Entity;
        public Vector3 Point;
        public Vector3 Normal;
        public float Distance;
    }

    // Function table filled by the engine. The field order must match Engine/src/Scripting/ScriptEngine.cpp :: NativeApi.
    [StructLayout(LayoutKind.Sequential)]
    public struct RigidbodyNative
    {
        public int Type;
        public float Mass;
        public float Drag;
        public float AngularDrag;
        public int UseGravity;
        public int FreezeRotation;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeApi
    {
        public delegate* unmanaged<int, byte*, void> Log;
        public delegate* unmanaged<byte*, ulong> FindEntityByName;
        public delegate* unmanaged<ulong, int> EntityExists;
        public delegate* unmanaged<ulong, byte*> GetName;
        public delegate* unmanaged<ulong, byte*, void> SetName;
        public delegate* unmanaged<byte*, ulong> CreateEntity;
        public delegate* unmanaged<int, byte*, ulong> CreatePrimitive;
        public delegate* unmanaged<ulong, void> DestroyEntity;
        public delegate* unmanaged<ulong, int, int> HasComponent;
        public delegate* unmanaged<ulong, int, void> AddComponent;
        public delegate* unmanaged<ulong, int, void> RemoveComponent;
        public delegate* unmanaged<ulong, ulong> GetParent;
        public delegate* unmanaged<ulong, ulong, void> SetParent;
        public delegate* unmanaged<ulong, Vector3*, void> GetPosition;
        public delegate* unmanaged<ulong, Vector3*, void> SetPosition;
        public delegate* unmanaged<ulong, Vector3*, void> GetRotation;
        public delegate* unmanaged<ulong, Vector3*, void> SetRotation;
        public delegate* unmanaged<ulong, Vector3*, void> GetScale;
        public delegate* unmanaged<ulong, Vector3*, void> SetScale;
        public delegate* unmanaged<ulong, Vector3*, void> GetWorldPosition;
        public delegate* unmanaged<ulong, Vector3*, void> GetForward;
        public delegate* unmanaged<ulong, Vector3*, void> GetRight;
        public delegate* unmanaged<ulong, Vector3*, void> GetUp;
        public delegate* unmanaged<ulong, Vector3*, void> LookAt;
        public delegate* unmanaged<int, int> IsKeyDown;
        public delegate* unmanaged<int, int> IsKeyPressed;
        public delegate* unmanaged<int, int> IsMouseDown;
        public delegate* unmanaged<int, int> IsMousePressed;
        public delegate* unmanaged<Vector2*, void> GetMousePosition;
        public delegate* unmanaged<Vector2*, void> GetMouseDelta;
        public delegate* unmanaged<float> GetScroll;
        public delegate* unmanaged<int, void> SetCursorLocked;
        public delegate* unmanaged<ulong, Vector3*, int, void> AddForce;
        public delegate* unmanaged<ulong, Vector3*, void> AddTorque;
        public delegate* unmanaged<ulong, Vector3*, void> GetVelocity;
        public delegate* unmanaged<ulong, Vector3*, void> SetVelocity;
        public delegate* unmanaged<ulong, Vector3*, void> GetAngularVelocity;
        public delegate* unmanaged<ulong, Vector3*, void> SetAngularVelocity;
        public delegate* unmanaged<Vector3*, Vector3*, float, ulong, RaycastHitNative*, int> Raycast;
        public delegate* unmanaged<ulong, Color*, void> GetColor;
        public delegate* unmanaged<ulong, Color*, void> SetColor;
        public delegate* unmanaged<ulong, IntPtr> GetScriptInstance;
        // ---- phase 5 additions
        public delegate* unmanaged<ulong, Vector3*, void> GetWorldEuler;
        public delegate* unmanaged<ulong, Vector3*, void> SetWorldPosition;
        public delegate* unmanaged<ulong, Vector3*, void> SetWorldEuler;
        public delegate* unmanaged<ulong, Vector3*, Vector3*, int, void> AddForceAtPosition;
        public delegate* unmanaged<ulong, RigidbodyNative*, void> GetRigidbody;
        public delegate* unmanaged<ulong, RigidbodyNative*, void> SetRigidbody;
        public delegate* unmanaged<ulong, byte*> GetTag;
        public delegate* unmanaged<ulong, byte*, void> SetTag;
        public delegate* unmanaged<byte*, ulong> FindWithTag;
        public delegate* unmanaged<ulong, int> GetActive;
        public delegate* unmanaged<ulong, int, void> SetActive;
        public delegate* unmanaged<ulong, int> GetActiveInHierarchy;
        public delegate* unmanaged<ulong, Vector3*, ulong> Instantiate;
        public delegate* unmanaged<byte*, Vector3*, ulong> InstantiatePrefab;
        public delegate* unmanaged<byte*, int> LoadScene;
        public delegate* unmanaged<byte*> GetActiveSceneName;
        public delegate* unmanaged<void> Quit;
        public delegate* unmanaged<float, void> SetTimeScale;
        public delegate* unmanaged<float> GetTimeScale;
        public delegate* unmanaged<ulong, void> AudioPlay;
        public delegate* unmanaged<ulong, void> AudioStop;
        public delegate* unmanaged<ulong, int> AudioIsPlaying;
        public delegate* unmanaged<ulong, float, void> AudioSetVolume;
        public delegate* unmanaged<ulong, float, void> AudioSetPitch;
        public delegate* unmanaged<byte*, Vector3*, float, void> PlayClipAtPoint;
        public delegate* unmanaged<ulong, Color*, float*, void> GetLight;
        public delegate* unmanaged<ulong, Color*, float, void> SetLight;
        public delegate* unmanaged<ulong, int> GetChildCount;
        public delegate* unmanaged<ulong, int, ulong> GetChild;
        public delegate* unmanaged<Vector2*, void> GetScreenSize;
        //Application addition
        public delegate* unmanaged<IntPtr> GetVersion;
        public delegate* unmanaged<IntPtr> GetProductName;
        public delegate* unmanaged<IntPtr> GetCompanyName;
        public delegate* unmanaged<IntPtr> GetDataPath;
        public delegate* unmanaged<IntPtr> GetPersistentDataPath;
        public delegate* unmanaged<IntPtr> GetProjectPath;
        public delegate* unmanaged<int> IsPlaying;
        public delegate* unmanaged<byte*, int> MountPak;
        public delegate* unmanaged<ulong, byte*, IntPtr> GetScriptInstanceByType;
        public delegate* unmanaged<ulong, int, byte*> GetComponentJson;
        public delegate* unmanaged<ulong, int, byte*, void> SetComponentJson;
        public delegate* unmanaged<byte*, ulong> LoadSceneAsync;
        public delegate* unmanaged<ulong, float> SceneLoadProgress;
        public delegate* unmanaged<ulong, int> SceneLoadStatus;
        public delegate* unmanaged<ulong, byte*> SceneLoadError;
        public delegate* unmanaged<ulong, int, void> SceneLoadActivation;
        public delegate* unmanaged<ulong, void> DontDestroyOnLoad;
        public delegate* unmanaged<byte*, int> UnmountPak;
        public delegate* unmanaged<byte*, long> AssetSize;
        public delegate* unmanaged<byte*, ulong, byte*, ulong, long> ReadAsset;
    }

    internal static unsafe class Native
    {
        public static NativeApi Api;

        public static byte[] Utf8(string s) => Encoding.UTF8.GetBytes((s ?? string.Empty) + "\0");
        public static string FromUtf8(byte* p) => p == null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)p) ?? string.Empty;

        public static void Log(int level, string message)
        {
            if (Api.Log == null) { Console.WriteLine(message); return; }
            fixed (byte* p = Utf8(message)) Api.Log(level, p);
        }
    }
}
