using System;
using System.Linq;
using System.Text.Json.Nodes;
using P = Stardust.ComponentProperties;
namespace Stardust;

public enum ParticleSystemShape { Sphere, Hemisphere, Cone, Box, Circle, Edge, Point }
public enum ParticleSystemSimulationSpace { Local, World }
public enum ParticleBlendMode { Alpha, Additive }
public enum ParticleRenderMode { Billboard, Stretch, HorizontalBillboard, VerticalBillboard }

/// <summary>A value that is constant (<c>min == max</c>) or random between two constants.</summary>
public struct MinMax {
    public float min, max;
    public MinMax(float value) { min = max = value; }
    public MinMax(float min, float max) { this.min = min; this.max = max; }
    public static implicit operator MinMax(float value) => new(value);
    internal JsonArray ToJson() => new(min, max);
    internal static MinMax FromJson(JsonNode node, float fallback) => node is JsonArray a && a.Count == 2 ? new(a[0].GetValue<float>(), a[1].GetValue<float>()) : new(fallback);
}
/// <summary>Particles emitted at once, <see cref="time"/> seconds into each cycle of the system.</summary>
public struct ParticleBurst {
    public float time; public int count; public int cycles; public float interval; public float probability;
    /// <param name="cycles">How many times it repeats, every <paramref name="interval"/> seconds (0 = for ever).</param>
    public ParticleBurst(float time, int count, int cycles = 1, float interval = 0.5f, float probability = 1) { this.time = time; this.count = count; this.cycles = cycles; this.interval = interval; this.probability = probability; }
}
public struct GradientColorKey { public float time; public Color color; public GradientColorKey(float time, Color color) { this.time = time; this.color = color; } }
public struct CurveKey { public float time, value; public CurveKey(float time, float value) { this.time = time; this.value = value; } }

/// <summary>Particle emitter. Settings mirror the inspector modules; the emitter's up axis (+Y) is its forward.</summary>
public sealed class ParticleSystem : Component {
    const int T = 32;
    JsonObject Data => P.Get(this, T);
    float F(string name, float fallback) => P.Value(this, T, name, fallback);
    void Set(string name, JsonNode value) => P.Property(this, T, name, value);
    static JsonArray Vec(Vector3 v) => new(v.x, v.y, v.z);
    static Vector3 Vec(JsonNode n) => n is JsonArray a && a.Count == 3 ? new(a[0].GetValue<float>(), a[1].GetValue<float>(), a[2].GetValue<float>()) : Vector3.zero;

    // ---- Playback
    public void Play() => P.Set(this, T, new JsonObject { ["command"] = "play" });
    public void Pause() => P.Set(this, T, new JsonObject { ["command"] = "pause" });
    /// <summary>Stops emitting; the living particles finish their life unless <paramref name="clear"/>.</summary>
    public void Stop(bool clear = false) => P.Set(this, T, new JsonObject { ["command"] = clear ? "stopAndClear" : "stop" });
    public void Clear() => P.Set(this, T, new JsonObject { ["command"] = "clear" });
    /// <summary>Starts again from the beginning, without the current particles.</summary>
    public void Restart() => P.Set(this, T, new JsonObject { ["command"] = "restart" });
    /// <summary>Emits <paramref name="count"/> particles now, even when the system is stopped.</summary>
    public void Emit(int count) => P.Set(this, T, new JsonObject { ["command"] = "emit", ["count"] = count });
    public bool isPlaying => P.Value(this, T, "isPlaying", false);
    public bool isPaused => P.Value(this, T, "isPaused", false);
    public bool isEmitting => P.Value(this, T, "isEmitting", false);
    public bool isStopped => !isPlaying && !isPaused;
    public int particleCount => P.Value(this, T, "particleCount", 0);
    /// <summary>Seconds into the current cycle.</summary>
    public float time => F("time", 0);

    // ---- Main
    public float duration { get => F("duration", 5); set => Set("duration", value); }
    public bool loop { get => P.Value(this, T, "looping", true); set => Set("looping", value); }
    public bool prewarm { get => P.Value(this, T, "prewarm", false); set => Set("prewarm", value); }
    public bool playOnAwake { get => P.Value(this, T, "playOnAwake", true); set => Set("playOnAwake", value); }
    public MinMax startDelay { get => MinMax.FromJson(Data["startDelay"], 0); set => Set("startDelay", value.ToJson()); }
    public MinMax startLifetime { get => MinMax.FromJson(Data["startLifetime"], 5); set => Set("startLifetime", value.ToJson()); }
    public MinMax startSpeed { get => MinMax.FromJson(Data["startSpeed"], 5); set => Set("startSpeed", value.ToJson()); }
    public MinMax startSize { get => MinMax.FromJson(Data["startSize"], 1); set => Set("startSize", value.ToJson()); }
    /// <summary>Degrees.</summary>
    public MinMax startRotation { get => MinMax.FromJson(Data["startRotation"], 0); set => Set("startRotation", value.ToJson()); }
    /// <summary>Setting it gives every particle this colour; use <see cref="SetStartColors"/> for a random colour between two.</summary>
    public Color startColor { get => P.ColorValue(this, T, "startColor"); set => SetStartColors(value, value); }
    public void SetStartColors(Color a, Color b) => P.Set(this, T, new JsonObject { ["startColor"] = new JsonArray(a.r, a.g, a.b, a.a), ["startColor2"] = new JsonArray(b.r, b.g, b.b, b.a) });
    public float gravityModifier { get => F("gravityModifier", 0); set => Set("gravityModifier", value); }
    public float simulationSpeed { get => F("simulationSpeed", 1); set => Set("simulationSpeed", value); }
    public ParticleSystemSimulationSpace simulationSpace { get => (ParticleSystemSimulationSpace)P.Value(this, T, "simulationSpace", 0); set => Set("simulationSpace", (int)value); }
    public int maxParticles { get => P.Value(this, T, "maxParticles", 1000); set => Set("maxParticles", value); }
    /// <summary>0 = a different effect every time; any other value replays the same one.</summary>
    public uint randomSeed { get => P.Value(this, T, "seed", 0u); set => Set("seed", value); }

    // ---- Emission
    public bool emissionEnabled { get => P.Value(this, T, "emission", true); set => Set("emission", value); }
    public float rateOverTime { get => F("rateOverTime", 10); set => Set("rateOverTime", value); }
    public float rateOverDistance { get => F("rateOverDistance", 0); set => Set("rateOverDistance", value); }
    public ParticleBurst[] bursts {
        get => Data["bursts"]?.AsArray().Select(b => new ParticleBurst(b["time"].GetValue<float>(), b["count"].GetValue<int>(), b["cycles"].GetValue<int>(), b["interval"].GetValue<float>(), b["probability"].GetValue<float>())).ToArray() ?? Array.Empty<ParticleBurst>();
        set => Set("bursts", new JsonArray((value ?? Array.Empty<ParticleBurst>()).Select(b => (JsonNode)new JsonObject { ["time"] = b.time, ["count"] = b.count, ["cycles"] = b.cycles, ["interval"] = b.interval, ["probability"] = b.probability }).ToArray()));
    }

    // ---- Shape
    public bool shapeEnabled { get => P.Value(this, T, "shapeEnabled", true); set => Set("shapeEnabled", value); }
    public ParticleSystemShape shape { get => (ParticleSystemShape)P.Value(this, T, "shape", 2); set => Set("shape", (int)value); }
    public float radius { get => F("radius", 1); set => Set("radius", value); }
    /// <summary>0 emits from the surface, 1 from the whole volume.</summary>
    public float radiusThickness { get => F("radiusThickness", 1); set => Set("radiusThickness", value); }
    /// <summary>Cone opening, degrees.</summary>
    public float angle { get => F("angle", 25); set => Set("angle", value); }
    public float arc { get => F("arc", 360); set => Set("arc", value); }
    public Vector3 boxSize { get => Vec(Data["boxSize"]); set => Set("boxSize", Vec(value)); }
    public Vector3 shapePosition { get => Vec(Data["shapeOffset"]); set => Set("shapeOffset", Vec(value)); }
    public float randomizeDirection { get => F("randomizeDirection", 0); set => Set("randomizeDirection", value); }

    // ---- Over lifetime
    public bool velocityOverLifetimeEnabled { get => P.Value(this, T, "velocityOverLifetime", false); set => Set("velocityOverLifetime", value); }
    public Vector3 velocityOverLifetime { get => Vec(Data["velocity"]); set => Set("velocity", Vec(value)); }
    public bool forceOverLifetimeEnabled { get => P.Value(this, T, "forceOverLifetime", false); set => Set("forceOverLifetime", value); }
    public Vector3 forceOverLifetime { get => Vec(Data["force"]); set => Set("force", Vec(value)); }
    public bool limitVelocityEnabled { get => P.Value(this, T, "limitVelocity", false); set => Set("limitVelocity", value); }
    public float speedLimit { get => F("speedLimit", 10); set => Set("speedLimit", value); }
    public float drag { get => F("drag", 0); set => Set("drag", value); }
    public bool colorOverLifetimeEnabled { get => P.Value(this, T, "colorOverLifetime", false); set => Set("colorOverLifetime", value); }
    /// <summary>Colour (with alpha) multiplied over each particle's life; time 0..1. Up to 8 keys.</summary>
    public GradientColorKey[] colorOverLifetime {
        get => Data["colorKeys"]?.AsArray().Select(k => { var a = k.AsArray(); return new GradientColorKey(a[0].GetValue<float>(), new Color(a[1].GetValue<float>(), a[2].GetValue<float>(), a[3].GetValue<float>(), a[4].GetValue<float>())); }).ToArray() ?? Array.Empty<GradientColorKey>();
        set => P.Set(this, T, new JsonObject { ["colorOverLifetime"] = true, ["colorKeys"] = new JsonArray((value ?? Array.Empty<GradientColorKey>()).Take(8).Select(k => (JsonNode)new JsonArray(k.time, k.color.r, k.color.g, k.color.b, k.color.a)).ToArray()) });
    }
    public bool sizeOverLifetimeEnabled { get => P.Value(this, T, "sizeOverLifetime", false); set => Set("sizeOverLifetime", value); }
    /// <summary>Size multiplier over each particle's life; time 0..1.</summary>
    public CurveKey[] sizeOverLifetime {
        get => Data["sizeKeys"]?.AsArray().Select(k => new CurveKey(k[0].GetValue<float>(), k[1].GetValue<float>())).ToArray() ?? Array.Empty<CurveKey>();
        set => P.Set(this, T, new JsonObject { ["sizeOverLifetime"] = true, ["sizeKeys"] = new JsonArray((value ?? Array.Empty<CurveKey>()).Take(8).Select(k => (JsonNode)new JsonArray(k.time, k.value)).ToArray()) });
    }
    public bool rotationOverLifetimeEnabled { get => P.Value(this, T, "rotationOverLifetime", false); set => Set("rotationOverLifetime", value); }
    /// <summary>Degrees per second.</summary>
    public MinMax angularVelocity { get => MinMax.FromJson(Data["angularVelocity"], 45); set => Set("angularVelocity", value.ToJson()); }
    public bool noiseEnabled { get => P.Value(this, T, "noise", false); set => Set("noise", value); }
    public float noiseStrength { get => F("noiseStrength", 1); set => Set("noiseStrength", value); }
    public float noiseFrequency { get => F("noiseFrequency", 0.5f); set => Set("noiseFrequency", value); }
    public float noiseScrollSpeed { get => F("noiseScrollSpeed", 0.5f); set => Set("noiseScrollSpeed", value); }
    /// <summary>Particles bounce on a horizontal plane at <see cref="collisionPlaneHeight"/> (world).</summary>
    public bool collisionEnabled { get => P.Value(this, T, "collision", false); set => Set("collision", value); }
    public float collisionPlaneHeight { get => F("collisionPlane", 0); set => Set("collisionPlane", value); }
    public float bounce { get => F("bounce", 0.5f); set => Set("bounce", value); }
    public float dampen { get => F("dampen", 0); set => Set("dampen", value); }
    public float lifetimeLoss { get => F("lifetimeLoss", 0); set => Set("lifetimeLoss", value); }

    // ---- Texture Sheet Animation
    public bool textureSheetEnabled { get => P.Value(this, T, "textureSheet", false); set => Set("textureSheet", value); }
    public int tilesX { get => P.Value(this, T, "tilesX", 2); set => Set("tilesX", value); }
    public int tilesY { get => P.Value(this, T, "tilesY", 2); set => Set("tilesY", value); }
    /// <summary>0 = the frames play over each particle's life (<see cref="sheetCycles"/> times).</summary>
    public float sheetFps { get => F("sheetFps", 0); set => Set("sheetFps", value); }
    public float sheetCycles { get => F("sheetCycles", 1); set => Set("sheetCycles", value); }
    public bool randomStartFrame { get => P.Value(this, T, "randomStartFrame", false); set => Set("randomStartFrame", value); }

    // ---- Renderer
    /// <summary>Texture asset path; empty = soft round particle.</summary>
    public string texture { get => P.Value(this, T, "texture", ""); set => Set("texture", value ?? ""); }
    public string material { get => P.Value(this, T, "material", ""); set => Set("material", value ?? ""); }
    public ParticleBlendMode blendMode { get => (ParticleBlendMode)P.Value(this, T, "blendMode", 0); set => Set("blendMode", (int)value); }
    public ParticleRenderMode renderMode { get => (ParticleRenderMode)P.Value(this, T, "renderMode", 0); set => Set("renderMode", (int)value); }
    public float lengthScale { get => F("lengthScale", 2); set => Set("lengthScale", value); }
    public float speedScale { get => F("speedScale", 0); set => Set("speedScale", value); }
    /// <summary>Colour multiplier; above 1 the particles glow with bloom.</summary>
    public float intensity { get => F("intensity", 1); set => Set("intensity", value); }
    public bool sortByDistance { get => P.Value(this, T, "sortByDistance", false); set => Set("sortByDistance", value); }
    public int sortingOrder { get => P.Value(this, T, "sortingOrder", 0); set => Set("sortingOrder", value); }
}
