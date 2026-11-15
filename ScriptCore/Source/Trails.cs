using System.Text.Json.Nodes;
using P = Stardust.ComponentProperties;
namespace Stardust;

public enum TrailAlignment { View, Flat }
public enum TrailTextureMode { Stretch, Tile }

/// <summary>
/// A ribbon left behind by the moving object. Points are recorded as it moves and fade out after
/// <see cref="time"/> seconds; width and colour go from start (at the object) to end (the oldest point).
/// Turning <see cref="emitting"/> off ends the current strip, turning it on again starts a new one.
/// </summary>
public sealed class TrailRenderer : Component {
    const int T = 37;
    float F(string name, float fallback) => P.Value(this, T, name, fallback);
    void Set(string name, JsonNode value) => P.Property(this, T, name, value);

    /// <summary>Seconds a point lives.</summary>
    public float time { get => F("time", 5); set => Set("time", value); }
    /// <summary>Metres the object moves before a new point is recorded.</summary>
    public float minVertexDistance { get => F("minVertexDistance", 0.1f); set => Set("minVertexDistance", value); }
    public float startWidth { get => F("startWidth", 1); set => Set("startWidth", value); }
    public float endWidth { get => F("endWidth", 0); set => Set("endWidth", value); }
    /// <summary>Sets both widths.</summary>
    public float widthMultiplier { set => P.Set(this, T, new JsonObject { ["startWidth"] = value, ["endWidth"] = value }); }
    public Color startColor { get => P.ColorValue(this, T, "startColor"); set => Set("startColor", new JsonArray(value.r, value.g, value.b, value.a)); }
    public Color endColor { get => P.ColorValue(this, T, "endColor"); set => Set("endColor", new JsonArray(value.r, value.g, value.b, value.a)); }
    /// <summary>View: faces the camera. Flat: lies across the object's up axis (marks on the ground).</summary>
    public TrailAlignment alignment { get => (TrailAlignment)P.Value(this, T, "alignment", 0); set => Set("alignment", (int)value); }
    public TrailTextureMode textureMode { get => (TrailTextureMode)P.Value(this, T, "textureMode", 0); set => Set("textureMode", (int)value); }
    public float tileLength { get => F("tileLength", 1); set => Set("tileLength", value); }
    public string texture { get => P.Value(this, T, "texture", ""); set => Set("texture", value ?? ""); }
    public string material { get => P.Value(this, T, "material", ""); set => Set("material", value ?? ""); }
    public ParticleBlendMode blendMode { get => (ParticleBlendMode)P.Value(this, T, "blendMode", 0); set => Set("blendMode", (int)value); }
    public float intensity { get => F("intensity", 1); set => Set("intensity", value); }
    /// <summary>Recording new points; off, the trail stops growing and the rest fades out.</summary>
    public bool emitting { get => P.Value(this, T, "emitting", true); set => Set("emitting", value); }
    public int maxPoints { get => P.Value(this, T, "maxPoints", 1000); set => Set("maxPoints", value); }
    public int sortingOrder { get => P.Value(this, T, "sortingOrder", 0); set => Set("sortingOrder", value); }
    /// <summary>Points currently recorded.</summary>
    public int positionCount => P.Value(this, T, "positionCount", 0);
    /// <summary>Removes every point.</summary>
    public void Clear() => P.Set(this, T, new JsonObject { ["command"] = "clear" });
}
