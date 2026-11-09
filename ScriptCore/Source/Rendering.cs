using System;
using System.Linq;
using System.Text.Json.Nodes;
using P = Stardust.ComponentProperties;
namespace Stardust;

public partial class Camera {
    public uint cullingMask {get=>P.Value(this,6,"cullingMask",uint.MaxValue);set=>P.Property(this,6,"cullingMask",JsonValue.Create(value));}
    public RenderTexture targetTexture {get=>new(P.Value(this,6,"targetTexture",""),targetWidth,targetHeight);set{P.Property(this,6,"targetTexture",JsonValue.Create(value?.path??""));if(value!=null){targetWidth=value.width;targetHeight=value.height;}}}
    public int targetWidth {get=>P.Value(this,6,"targetWidth",512);set=>P.Property(this,6,"targetWidth",JsonValue.Create(value));}
    public int targetHeight {get=>P.Value(this,6,"targetHeight",512);set=>P.Property(this,6,"targetHeight",JsonValue.Create(value));}
    public void SetRenderTarget(string name,int width,int height) {targetTexture=new RenderTexture(name,width,height);}
}
public sealed class SpriteRenderer : Component {
    public Sprite sprite {get=>Assets.Load<Sprite>(P.Value(this,22,"sprite",""));set=>P.Property(this,22,"sprite",JsonValue.Create(value?.path??""));}
    public string material {get=>P.Value(this,22,"material","");set=>P.Property(this,22,"material",JsonValue.Create(value));}
    public Color color {get=>P.ColorValue(this,22,"color");set=>P.Property(this,22,"color",new JsonArray(value.r,value.g,value.b,value.a));}
    public float pixelsPerUnit {get=>P.Value(this,22,"pixelsPerUnit",100f);set=>P.Property(this,22,"pixelsPerUnit",JsonValue.Create(value));}
    public bool flipX {get=>P.Value(this,22,"flipX",false);set=>P.Property(this,22,"flipX",JsonValue.Create(value));}
    public bool flipY {get=>P.Value(this,22,"flipY",false);set=>P.Property(this,22,"flipY",JsonValue.Create(value));}
    public int sortingOrder {get=>P.Value(this,22,"sortingOrder",0);set=>P.Property(this,22,"sortingOrder",JsonValue.Create(value));}
}
public unsafe partial class MeshRenderer {
    // Per-renderer overrides detach the shared material and preserve the remaining properties.
    void SetShaderProperty(string name,JsonNode value){var material=P.Get(this,1)["embeddedMaterial"]?.DeepClone() as JsonObject??new JsonObject();var properties=material["properties"] as JsonObject;if(properties==null){properties=new JsonObject();material["properties"]=properties;}properties[name]=value;P.Property(this,1,"embeddedMaterial",material);}
    public void SetFloat(string name,float value)=>SetShaderProperty(name,JsonValue.Create(value));
    public void SetColor(string name,Color value)=>SetShaderProperty(name,new JsonArray(value.r,value.g,value.b,value.a));
    public void SetVector(string name,Vector2 value)=>SetShaderProperty(name,new JsonArray(value.x,value.y));
    public void SetVector(string name,Vector3 value)=>SetShaderProperty(name,new JsonArray(value.x,value.y,value.z));
    public void SetTexture(string name,string path)=>SetShaderProperty(name,JsonValue.Create(path));
    public float roughness {get=>P.Get(this,1)["embeddedMaterial"]?["roughness"]?.GetValue<float>()??.5f;set=>P.Property(this,1,"embeddedMaterial",new JsonObject{["roughness"]=value});}
    public float metallic {get=>P.Get(this,1)["embeddedMaterial"]?["metallic"]?.GetValue<float>()??0;set=>P.Property(this,1,"embeddedMaterial",new JsonObject{["metallic"]=value});}
    public void SetTexture(string asset)=>P.Property(this,1,"embeddedMaterial",new JsonObject{["albedoMap"]=asset});
    public void SetShader(string shader)=>P.Property(this,1,"embeddedMaterial",new JsonObject{["shader"]=shader});
    public void SetEmission(Color color,float intensity=1)=>P.Property(this,1,"embeddedMaterial",new JsonObject{["emissive"]=new JsonArray(color.r,color.g,color.b),["emissiveIntensity"]=intensity});

    public string mesh {get=>P.Value(this,1,"mesh","");set=>P.Property(this,1,"mesh",JsonValue.Create(value));}
    public string material {get=>P.Value(this,1,"material","");set=>P.Property(this,1,"material",JsonValue.Create(value));}
    public string[] materials {get=>P.Get(this,1)["materials"]?.AsArray().Select(n=>n.GetValue<string>()).ToArray()??Array.Empty<string>();set=>P.Property(this,1,"materials",new JsonArray(value.Select(s=>(JsonNode)JsonValue.Create(s)).ToArray()));}
    public bool castShadows {get=>P.Value(this,1,"castShadows",true);set=>P.Property(this,1,"castShadows",JsonValue.Create(value));}
    public bool receiveShadows {get=>P.Value(this,1,"receiveShadows",true);set=>P.Property(this,1,"receiveShadows",JsonValue.Create(value));}
    public void SetMesh(Vector3[] vertices,int[] triangles,Vector2[] uv=null) {
        ArgumentNullException.ThrowIfNull(vertices);ArgumentNullException.ThrowIfNull(triangles);
        if(vertices.Length==0||triangles.Length==0||triangles.Length%3!=0||triangles.Any(i=>i<0||i>=vertices.Length))throw new ArgumentException("Invalid indexed triangle mesh");
        if(uv!=null&&uv.Length!=vertices.Length)throw new ArgumentException("UV count must match vertex count");
        var v=new JsonArray();for(int i=0;i<vertices.Length;i++)v.Add(new JsonArray(vertices[i].x,vertices[i].y,vertices[i].z,uv?[i].x??0,uv?[i].y??0));
        P.Set(this,1,new JsonObject{["vertices"]=v,["triangles"]=new JsonArray(triangles.Select(i=>(JsonNode)JsonValue.Create(i)).ToArray())});
    }
}
public sealed class Environment : Component {
    public float shadowStrength {get=>P.Value(this,23,"shadowStrength",.85f);set=>P.Property(this,23,"shadowStrength",JsonValue.Create(value));}
    public float minimumAmbient {get=>P.Value(this,23,"minimumAmbient",.1f);set=>P.Property(this,23,"minimumAmbient",JsonValue.Create(value));}

    public float exposure {get=>P.Value(this,23,"exposure",1f);set=>P.Property(this,23,"exposure",JsonValue.Create(value));}
    public float ambientIntensity {get=>P.Value(this,23,"ambientIntensity",1f);set=>P.Property(this,23,"ambientIntensity",JsonValue.Create(value));}
    public float bloomIntensity {get=>P.Value(this,23,"bloomIntensity",.04f);set=>P.Property(this,23,"bloomIntensity",JsonValue.Create(value));}
    public float contrast {get=>P.Value(this,23,"contrast",1f);set=>P.Property(this,23,"contrast",JsonValue.Create(value));}
    public float saturation {get=>P.Value(this,23,"saturation",1f);set=>P.Property(this,23,"saturation",JsonValue.Create(value));}
    public float vignette {get=>P.Value(this,23,"vignette",.1f);set=>P.Property(this,23,"vignette",JsonValue.Create(value));}
    public float fogDensity {get=>P.Value(this,23,"fogDensity",0f);set=>P.Property(this,23,"fogDensity",JsonValue.Create(value));}
    public bool fxaa {get=>P.Value(this,23,"fxaa",true);set=>P.Property(this,23,"fxaa",JsonValue.Create(value));}
    public int skyMode {get=>P.Value(this,23,"skyMode",0);set=>P.Property(this,23,"skyMode",JsonValue.Create(value));}
    public Color skyColor {get=>P.ColorValue(this,23,"skyColor");set=>P.Property(this,23,"skyColor",new JsonArray(value.r,value.g,value.b,value.a));}
    public string skyMaterial {get=>P.Value(this,23,"skyMaterial","");set=>P.Property(this,23,"skyMaterial",JsonValue.Create(value));}
}


public sealed class RenderTexture : Asset {
    public int width {get;set;}=512;
    public int height {get;set;}=512;
    public RenderTexture(){}
    public RenderTexture(string name,int width=512,int height=512){path=name;this.width=width;this.height=height;}
    public static implicit operator string(RenderTexture value)=>value?.path??"";
    public static implicit operator RenderTexture(string path)=>new(path);
}

public sealed class VideoPlayer : Component {
    public string clip {get=>P.Value(this,26,"clip","");set=>P.Property(this,26,"clip",JsonValue.Create(value));}
    public string targetTexture {get=>P.Value(this,26,"targetTexture","");set=>P.Property(this,26,"targetTexture",JsonValue.Create(value));}
    public bool isPlaying {get=>P.Value(this,26,"playing",false);set=>P.Property(this,26,"playing",JsonValue.Create(value));}
    public bool loop {get=>P.Value(this,26,"loop",true);set=>P.Property(this,26,"loop",JsonValue.Create(value));}
    public float time {get=>P.Value(this,26,"time",0f);set=>P.Property(this,26,"time",JsonValue.Create(value));}
    public float length=>P.Value(this,26,"duration",0f);
    public float playbackSpeed {get=>P.Value(this,26,"speed",1f);set=>P.Property(this,26,"speed",JsonValue.Create(value));}
    public void Play()=>isPlaying=true;
    public void Pause()=>isPlaying=false;
    public void Stop(){isPlaying=false;time=0;}
}
public static unsafe class LayerMask {
    public static int NameToLayer(string name){var data=JsonNode.Parse(Stardust.Interop.Native.FromUtf8(Stardust.Interop.Native.Api.GetComponentJson(0,101)));var layers=data?["layers"]?.AsArray();if(layers==null)return -1;for(int i=0;i<layers.Count;i++)if(layers[i]?.GetValue<string>()==name)return i;return -1;}
    public static uint GetMask(params string[] names){uint mask=0;foreach(var name in names){int layer=NameToLayer(name);if(layer>=0)mask|=1u<<layer;}return mask;}
}

public abstract class Collider : Component {
    int Type=>Component.TypeIdOf(GetType());
    public bool isTrigger {get=>P.Value(this,Type,"isTrigger",false);set=>P.Property(this,Type,"isTrigger",JsonValue.Create(value));}
    public float friction {get=>P.Value(this,Type,"friction",.5f);set=>P.Property(this,Type,"friction",JsonValue.Create(value));}
    public float restitution {get=>P.Value(this,Type,"restitution",.1f);set=>P.Property(this,Type,"restitution",JsonValue.Create(value));}
    public Vector3 center {get {var a=P.Get(this,Type)["center"]?.AsArray();return a==null?Vector3.zero:new(a[0].GetValue<float>(),a[1].GetValue<float>(),a[2].GetValue<float>());}set=>P.Property(this,Type,"center",new JsonArray(value.x,value.y,value.z));}
}
public partial class BoxCollider {
    public Vector3 size {get {var a=P.Get(this,3)["size"].AsArray();return new(a[0].GetValue<float>(),a[1].GetValue<float>(),a[2].GetValue<float>());}set=>P.Property(this,3,"size",new JsonArray(value.x,value.y,value.z));}
}
public partial class SphereCollider {public float radius {get=>P.Value(this,4,"radius",.5f);set=>P.Property(this,4,"radius",JsonValue.Create(value));}}
public partial class CapsuleCollider {
    public float radius {get=>P.Value(this,5,"radius",.5f);set=>P.Property(this,5,"radius",JsonValue.Create(value));}
    public float height {get=>P.Value(this,5,"height",2f);set=>P.Property(this,5,"height",JsonValue.Create(value));}
}
public partial class MeshCollider {public bool convex {get=>P.Value(this,9,"convex",true);set=>P.Property(this,9,"convex",JsonValue.Create(value));}}

public partial class DirectionalLight : Component {
    public float intensity {get=>P.Value(this,7,"intensity",10f);set=>P.Property(this,7,"intensity",JsonValue.Create(value));}
    public float shadowDistance {get=>P.Value(this,7,"shadowDistance",120f);set=>P.Property(this,7,"shadowDistance",JsonValue.Create(value));}
    public bool castShadows {get=>P.Value(this,7,"castShadows",true);set=>P.Property(this,7,"castShadows",JsonValue.Create(value));}
    public Color color {get {var a=P.Get(this,7)["color"].AsArray();return new(a[0].GetValue<float>(),a[1].GetValue<float>(),a[2].GetValue<float>());}set=>P.Property(this,7,"color",new JsonArray(value.r,value.g,value.b));}
}

public partial class SpotLight : Component {
    public float intensity {get=>P.Value(this,25,"intensity",40f);set=>P.Property(this,25,"intensity",JsonValue.Create(value));}
    public float range {get=>P.Value(this,25,"range",20f);set=>P.Property(this,25,"range",JsonValue.Create(value));}
    public float innerAngle {get=>P.Value(this,25,"innerAngle",20f);set=>P.Property(this,25,"innerAngle",JsonValue.Create(value));}
    public float outerAngle {get=>P.Value(this,25,"outerAngle",30f);set=>P.Property(this,25,"outerAngle",JsonValue.Create(value));}
    public Color color {get {var a=P.Get(this,25)["color"].AsArray();return new(a[0].GetValue<float>(),a[1].GetValue<float>(),a[2].GetValue<float>());}set=>P.Property(this,25,"color",new JsonArray(value.r,value.g,value.b));}
}

public unsafe partial class AudioSource {
 public string clip {get=>P.Value(this,11,"clip","");set=>P.Property(this,11,"clip",JsonValue.Create(value));}
 public bool loop {get=>P.Value(this,11,"loop",false);set=>P.Property(this,11,"loop",JsonValue.Create(value));}
 public bool spatial {get=>P.Value(this,11,"spatial",true);set=>P.Property(this,11,"spatial",JsonValue.Create(value));}
}

public enum TerrainBrushMode { Raise=1,Smooth,Flatten,SetHeight,Paint,Trees }
public sealed class Terrain : Component {
    public float size=>P.Value(this,24,"size",2048f);
    public int resolution=>P.Value(this,24,"resolution",1024);
    public bool collider {get=>P.Value(this,24,"collider",true);set=>P.Property(this,24,"collider",JsonValue.Create(value));}
    public float GetHeight(float localX,float localZ){P.Set(this,24,new JsonObject{["query"]=new JsonArray(localX,localZ)});return P.Value(this,24,"height",0f);}
    public float SampleHeight(Vector3 world){var origin=Entity.transform.position;return origin.y+GetHeight(world.x-origin.x,world.z-origin.z);}
    public void MakeFlat(float height)=>P.Set(this,24,new JsonObject{["flat"]=height});
    public void RebuildCollider()=>P.Set(this,24,new JsonObject{["rebuildCollider"]=true});
    public void ApplyBrush(TerrainBrushMode mode,float x,float z,float radius,float strength=1,float height=0,int layer=0,bool rebuildCollider=true)=>P.Set(this,24,new JsonObject{["brush"]=new JsonObject{["mode"]=(int)mode,["x"]=x,["z"]=z,["radius"]=radius,["strength"]=strength,["height"]=height,["layer"]=layer},["rebuildCollider"]=rebuildCollider});
}
