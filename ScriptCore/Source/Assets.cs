using System;
using System.IO;
using System.Text;
using Stardust.Interop;
namespace Stardust {
    // Asset references contain only a virtual path. Creating one never opens a file or mounts a pak.
    public class Asset {
        public string path {get; internal set;} = "";
        public bool isAvailable => !string.IsNullOrEmpty(path) && Assets.Exists(path);
        public override string ToString()=>path;
    }
    public sealed class Sprite : Asset {
        public Sprite() {}
        public Sprite(string path){this.path=path??"";}
        public static Sprite FromPath(string path)=>string.IsNullOrEmpty(path)?null:new Sprite(path);
        public static implicit operator Sprite(string path)=>FromPath(path);
        public static implicit operator string(Sprite sprite)=>sprite?.path??"";
    }
    public sealed class Prefab : Asset {
        public Prefab() {}
        public Prefab(string path){this.path=path??"";}
        public Entity Instantiate(Vector3 position=default)=>Entity.Instantiate(path,position);
        public T Instantiate<T>(Vector3 position=default) where T:class =>Entity.TypedInstance<T>(Instantiate(position));
    }
    public sealed class TextAsset : Asset {
        public string text=>Assets.ReadAllText(path);
        public byte[] bytes=>Assets.ReadAllBytes(path);
    }
    public static unsafe class Assets {
        public static bool Exists(string path){fixed(byte* p=Native.Utf8(path))return Native.Api.AssetSize(p)>=0;}
        public static long GetSize(string path){fixed(byte* p=Native.Utf8(path))return Native.Api.AssetSize(p);}
        public static byte[] ReadAllBytes(string path){
            long length=GetSize(path);if(length<0)throw new FileNotFoundException("Asset is not available in the project or mounted pak namespace",path);
            if(length>Array.MaxLength)throw new IOException("Asset is too large for a managed byte array");
            var result=new byte[(int)length];if(length==0)return result;
            fixed(byte* p=Native.Utf8(path))fixed(byte* buffer=result)if(Native.Api.ReadAsset(p,0,buffer,(ulong)length)!=length)throw new IOException("Asset read failed: "+path);
            return result;
        }
        /// <summary>Pixel size of a texture asset (loads it if needed); zero when it cannot be read.</summary>
        public static Vector2 GetTextureSize(string path){var size=Native.Query(new System.Text.Json.Nodes.JsonObject{["kind"]="textureSize",["path"]=path??""});return new Vector2(size["width"]?.GetValue<float>()??0,size["height"]?.GetValue<float>()??0);}
        public static string ReadAllText(string path)=>Encoding.UTF8.GetString(ReadAllBytes(path)).TrimStart('\uFEFF');
        public static T Load<T>(string path) where T:class {
            if(string.IsNullOrEmpty(path))return null;
            var type=typeof(T);
            if(typeof(Asset).IsAssignableFrom(type)){var asset=(Asset)Activator.CreateInstance(type);asset.path=path;return asset as T;}
            if(type==typeof(Entity)||type.IsSubclassOf(typeof(ScriptBehaviour))){var entity=(Entity)Activator.CreateInstance(type,true);entity.PrefabAssetPath=path;return entity as T;}
            if(type.IsSubclassOf(typeof(Component))){var component=(Component)Activator.CreateInstance(type);component.Entity=Load<Entity>(path);return component as T;}
            throw new ArgumentException("Unsupported asset reference type: "+type.FullName);
        }
    }
}
