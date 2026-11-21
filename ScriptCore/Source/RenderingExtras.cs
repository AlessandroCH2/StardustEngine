using System;
using System.Collections.Generic;
using System.Text.Json.Nodes;
using Stardust.Interop;
using P = Stardust.ComponentProperties;

namespace Stardust
{
    internal static class JsonVec
    {
        internal static Vector3 V(JsonNode n) => n is JsonArray a && a.Count == 3 ? new Vector3(a[0].GetValue<float>(), a[1].GetValue<float>(), a[2].GetValue<float>()) : Vector3.zero;
        internal static JsonArray J(Vector3 v) => new(v.x, v.y, v.z);
        internal static JsonArray J(Color c) => new(c.r, c.g, c.b, c.a);
        // Object references: 16 hex digits
        internal static ulong Id(string text) => string.IsNullOrEmpty(text) ? 0 : text.Length == 16 ? Convert.ToUInt64(text, 16) : ulong.Parse(text);
        internal static string Id(ulong id) => id == 0 ? "0" : id.ToString("x16");
    }

    public enum LineAlignment { View, TransformZ }

    /// <summary>A ribbon through a list of points (lasers, ropes, paths, UI lines in the world).</summary>
    public sealed class LineRenderer : Component
    {
        const int T = 42;
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public int positionCount
        {
            get => P.Value(this, T, "positionCount", 0);
            set { var p = GetPositions(); Array.Resize(ref p, Math.Max(0, value)); SetPositions(p); }
        }
        public Vector3[] GetPositions()
        {
            var list = new List<Vector3>();
            if (P.Get(this, T)["positions"] is JsonArray a) foreach (var p in a) list.Add(JsonVec.V(p));
            return list.ToArray();
        }
        public void SetPositions(Vector3[] positions)
        {
            var a = new JsonArray();
            foreach (var p in positions ?? Array.Empty<Vector3>()) a.Add(JsonVec.J(p));
            Set("positions", a);
        }
        public Vector3 GetPosition(int index) => GetPositions()[index];
        public void SetPosition(int index, Vector3 position) { var p = GetPositions(); p[index] = position; SetPositions(p); }
        public bool useWorldSpace { get => P.Value(this, T, "useWorldSpace", false); set => Set("useWorldSpace", value); }
        public bool loop { get => P.Value(this, T, "loop", false); set => Set("loop", value); }
        public float startWidth { get => P.Value(this, T, "startWidth", .1f); set => Set("startWidth", value); }
        public float endWidth { get => P.Value(this, T, "endWidth", .1f); set => Set("endWidth", value); }
        public float widthMultiplier { set => P.Set(this, T, new JsonObject { ["startWidth"] = value, ["endWidth"] = value }); }
        public Color startColor { get => P.ColorValue(this, T, "startColor"); set => Set("startColor", JsonVec.J(value)); }
        public Color endColor { get => P.ColorValue(this, T, "endColor"); set => Set("endColor", JsonVec.J(value)); }
        public LineAlignment alignment { get => (LineAlignment)P.Value(this, T, "alignment", 0); set => Set("alignment", (int)value); }
        public TrailTextureMode textureMode { get => (TrailTextureMode)P.Value(this, T, "textureMode", 0); set => Set("textureMode", (int)value); }
        public float tileLength { get => P.Value(this, T, "tileLength", 1f); set => Set("tileLength", value); }
        public int numCornerVertices { get => P.Value(this, T, "cornerVertices", 0); set => Set("cornerVertices", value); }
        public string texture { get => P.Value(this, T, "texture", ""); set => Set("texture", value ?? ""); }
        public string material { get => P.Value(this, T, "material", ""); set => Set("material", value ?? ""); }
        public ParticleBlendMode blendMode { get => (ParticleBlendMode)P.Value(this, T, "blendMode", 0); set => Set("blendMode", (int)value); }
        public float intensity { get => P.Value(this, T, "intensity", 1f); set => Set("intensity", value); }
        public int sortingOrder { get => P.Value(this, T, "sortingOrder", 0); set => Set("sortingOrder", value); }
    }

    public enum SplineMode { Auto, Bezier, Linear }
    public struct SplineKnot
    {
        public Vector3 position, tangentIn, tangentOut; public float roll;
        public SplineKnot(Vector3 position) { this.position = position; tangentIn = tangentOut = Vector3.zero; roll = 0; }
    }

    /// <summary>
    /// A curve through knots (local space of the object). Evaluate(t) gives a point for t in 0..1;
    /// EvaluateAtDistance walks it at constant speed. A follower object can also be moved along it (Inspector).
    /// </summary>
    public sealed class Spline : Component
    {
        const int T = 43;
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public SplineMode mode { get => (SplineMode)P.Value(this, T, "mode", 0); set => Set("mode", (int)value); }
        public bool closed { get => P.Value(this, T, "closed", false); set => Set("closed", value); }
        public SplineKnot[] knots
        {
            get
            {
                var list = new List<SplineKnot>();
                if (P.Get(this, T)["knots"] is JsonArray a)
                    foreach (var k in a) list.Add(new SplineKnot { position = JsonVec.V(k["position"]), tangentIn = JsonVec.V(k["tangentIn"]), tangentOut = JsonVec.V(k["tangentOut"]), roll = k["roll"]?.GetValue<float>() ?? 0 });
                return list.ToArray();
            }
            set
            {
                var a = new JsonArray();
                foreach (var k in value ?? Array.Empty<SplineKnot>()) a.Add(new JsonObject { ["position"] = JsonVec.J(k.position), ["tangentIn"] = JsonVec.J(k.tangentIn), ["tangentOut"] = JsonVec.J(k.tangentOut), ["roll"] = k.roll });
                Set("knots", a);
            }
        }
        public float followSpeed { get => P.Value(this, T, "followSpeed", 0f); set => Set("followSpeed", value); }
        public Entity follower
        {
            get { var id = JsonVec.Id(P.Value(this, T, "follower", "0")); return id == 0 ? null : new Entity(id); }
            set => Set("follower", JsonVec.Id(value?.ID ?? 0));
        }

        // Curve math (same as the engine's Splines.cpp), on a copy of the knots
        public sealed class Curve
        {
            readonly SplineKnot[] k; readonly SplineMode mode; readonly bool closed; readonly Frame? world;
            internal struct Frame { public Vector3 origin, x, y, z; }
            float[] lengths;
            internal Curve(SplineKnot[] knots, SplineMode mode, bool closed, Frame? world) { k = knots; this.mode = mode; this.closed = closed; this.world = world; }
            int Segments => k.Length < 2 ? 0 : closed ? k.Length : k.Length - 1;
            SplineKnot Knot(int i) { int n = k.Length; return k[closed ? ((i % n) + n) % n : Math.Clamp(i, 0, n - 1)]; }
            void Controls(int s, out Vector3 p0, out Vector3 p1, out Vector3 p2, out Vector3 p3)
            {
                var a = Knot(s); var b = Knot(s + 1); p0 = a.position; p3 = b.position;
                if (mode == SplineMode.Linear) { p1 = p0 + (p3 - p0) / 3f; p2 = p0 + (p3 - p0) * (2f / 3f); return; }
                if (mode == SplineMode.Bezier) { p1 = p0 + a.tangentOut; p2 = p3 + b.tangentIn; return; }
                int n = k.Length;
                var prev = (s > 0 || closed) ? Knot(s - 1).position : p0 * 2f - p3;
                var next = (s + 2 < n || closed) ? Knot(s + 2).position : p3 * 2f - p0;
                p1 = p0 + (p3 - prev) / 6f; p2 = p3 - (next - p0) / 6f;
            }
            void Locate(float t, out int s, out float u)
            {
                t = closed ? t - MathF.Floor(t) : Math.Clamp(t, 0, 1);
                float x = t * Segments; s = Math.Min((int)x, Segments - 1); u = x - s;
            }
            Vector3 ToWorld(Vector3 p) => world.HasValue ? world.Value.origin + ToWorldDir(p) : p;
            Vector3 ToWorldDir(Vector3 d) => world.HasValue ? world.Value.x * d.x + world.Value.y * d.y + world.Value.z * d.z : d;
            public Vector3 Evaluate(float t)
            {
                if (k.Length == 0) return Vector3.zero;
                if (Segments == 0) return ToWorld(k[0].position);
                Locate(t, out int s, out float u); Controls(s, out var p0, out var p1, out var p2, out var p3);
                float v = 1 - u;
                return ToWorld(p0 * (v * v * v) + p1 * (3 * v * v * u) + p2 * (3 * v * u * u) + p3 * (u * u * u));
            }
            public Vector3 EvaluateTangent(float t)
            {
                if (Segments == 0) return Vector3.forward;
                Locate(t, out int s, out float u); Controls(s, out var p0, out var p1, out var p2, out var p3);
                float v = 1 - u;
                var d = (p1 - p0) * (3 * v * v) + (p2 - p1) * (6 * v * u) + (p3 - p2) * (3 * u * u);
                if (d.sqrMagnitude < 1e-10f) d = p3 - p0;
                return ToWorldDir(d).normalized;
            }
            float[] Table()
            {
                if (lengths != null) return lengths;
                int steps = Math.Max(16, Segments * 32);
                lengths = new float[steps + 1];
                var prev = Evaluate(0);
                for (int i = 1; i <= steps; ++i) { var p = Evaluate((float)i / steps); lengths[i] = lengths[i - 1] + (p - prev).magnitude; prev = p; }
                return lengths;
            }
            public float length => Table()[^1];
            /// <summary>t for a distance in metres from the start.</summary>
            public float DistanceToT(float distance)
            {
                var l = Table(); float total = l[^1]; int steps = l.Length - 1;
                if (total <= 1e-6f) return 0;
                distance = closed ? distance - MathF.Floor(distance / total) * total : Math.Clamp(distance, 0, total);
                int i = Array.BinarySearch(l, distance); if (i < 0) i = ~i; i = Math.Clamp(i, 1, steps);
                float span = l[i] - l[i - 1];
                return ((i - 1) + (span > 1e-6f ? (distance - l[i - 1]) / span : 0)) / steps;
            }
            public Vector3 EvaluateAtDistance(float distance) => Evaluate(DistanceToT(distance));
            /// <summary>Closest point on the curve; returns its t.</summary>
            public float Nearest(Vector3 point, out Vector3 closest)
            {
                int steps = Math.Max(32, Segments * 64); float best = float.MaxValue, bestT = 0;
                for (int i = 0; i <= steps; ++i) { float t = (float)i / steps; float d = (Evaluate(t) - point).sqrMagnitude; if (d < best) { best = d; bestT = t; } }
                float step = 1f / steps;
                for (int n = 0; n < 12; ++n) { step *= .5f; foreach (var t0 in new[] { bestT - step, bestT + step }) { float t = closed ? t0 : Math.Clamp(t0, 0, 1); float d = (Evaluate(t) - point).sqrMagnitude; if (d < best) { best = d; bestT = t; } } }
                closest = Evaluate(bestT); return bestT;
            }
        }
        /// <summary>The curve in local space.</summary>
        public Curve GetLocalCurve() => new Curve(knots, mode, closed, null);
        /// <summary>The curve in world space (with the object's transform now).</summary>
        public Curve GetCurve()
        {
            var t = Entity.transform; var scale = t.localScale;
            return new Curve(knots, mode, closed, new Curve.Frame { origin = t.position, x = t.right * scale.x, y = t.up * scale.y, z = -t.forward * scale.z });
        }
        public Vector3 Evaluate(float t) => GetCurve().Evaluate(t);
        public Vector3 EvaluateTangent(float t) => GetCurve().EvaluateTangent(t);
        public Vector3 EvaluateAtDistance(float metres) => GetCurve().EvaluateAtDistance(metres);
        public float GetLength() => GetCurve().length;
    }

    public enum ReflectionProbeMode { OnLoad, Realtime, ViaScripting }

    /// <summary>Captures the surroundings for reflections and local ambient light inside its box.</summary>
    public sealed class ReflectionProbe : Component
    {
        const int T = 44;
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public ReflectionProbeMode mode { get => (ReflectionProbeMode)P.Value(this, T, "mode", 0); set => Set("mode", (int)value); }
        public Vector3 size { get => JsonVec.V(P.Get(this, T)["size"]); set => Set("size", JsonVec.J(value)); }
        public Vector3 center { get => JsonVec.V(P.Get(this, T)["center"]); set => Set("center", JsonVec.J(value)); }
        public float intensity { get => P.Value(this, T, "intensity", 1f); set => Set("intensity", value); }
        public bool boxProjection { get => P.Value(this, T, "boxProjection", true); set => Set("boxProjection", value); }
        public float blendDistance { get => P.Value(this, T, "blendDistance", 1f); set => Set("blendDistance", value); }
        public int resolution { get => P.Value(this, T, "resolution", 128); set => Set("resolution", value); }
        /// <summary>Seconds between Realtime captures (0 = continuously).</summary>
        public float refreshInterval { get => P.Value(this, T, "interval", 0f); set => Set("interval", value); }
        /// <summary>Captures again (it takes six frames).</summary>
        public void RenderProbe() => Set("mode", (int)mode);
    }

    /// <summary>Projects a texture onto the surfaces inside its box, along the object's -Z axis.</summary>
    public sealed class Decal : Component
    {
        const int T = 45;
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public string texture { get => P.Value(this, T, "texture", ""); set => Set("texture", value ?? ""); }
        public string normalMap { get => P.Value(this, T, "normalMap", ""); set => Set("normalMap", value ?? ""); }
        public Color color { get => P.ColorValue(this, T, "color"); set => Set("color", JsonVec.J(value)); }
        /// <summary>Width, height and projection depth in metres.</summary>
        public Vector3 size { get => JsonVec.V(P.Get(this, T)["size"]); set => Set("size", JsonVec.J(value)); }
        public float opacity { get => P.Value(this, T, "opacity", 1f); set => Set("opacity", value); }
        public float angleFade { get => P.Value(this, T, "angleFade", .5f); set => Set("angleFade", value); }
        public float emission { get => P.Value(this, T, "emission", 0f); set => Set("emission", value); }
        public int sortingOrder { get => P.Value(this, T, "sortingOrder", 0); set => Set("sortingOrder", value); }
    }

    /// <summary>The project's Audio Mixer (Project Settings > Audio Mixer). Parameters: volume (dB), pitch,
    /// lowpass, highpass (Hz, 0 = off), reverb, reverbRoom, echo, echoDelay, echoFeedback, mute, solo.</summary>
    public static class AudioMixer
    {
        public static bool SetFloat(string group, string parameter, float value) => Native.Query(new JsonObject { ["kind"] = "mixerSet", ["group"] = group, ["parameter"] = parameter, ["value"] = value })["ok"]?.GetValue<bool>() ?? false;
        public static float GetFloat(string group, string parameter) => Native.Query(new JsonObject { ["kind"] = "mixerGet", ["group"] = group, ["parameter"] = parameter })["value"]?.GetValue<float>() ?? 0;
        public static bool SetVolume(string group, float decibels) => SetFloat(group, "volume", decibels);
        /// <summary>Volume 0..1 converted to decibels (0 = silent).</summary>
        public static bool SetVolumeLinear(string group, float volume) => SetFloat(group, "volume", volume <= 0.0001f ? -80f : 20f * MathF.Log10(volume));
        public static bool SetMute(string group, bool mute) => SetFloat(group, "mute", mute ? 1 : 0);
        /// <summary>Blends every group to a saved snapshot over `seconds`.</summary>
        public static bool TransitionToSnapshot(string snapshot, float seconds = 0) => Native.Query(new JsonObject { ["kind"] = "mixerSnapshot", ["name"] = snapshot, ["seconds"] = seconds })["ok"]?.GetValue<bool>() ?? false;
        public static string[] groups { get { var l = new List<string>(); if (Native.Query(new JsonObject { ["kind"] = "mixerGroups" })["groups"] is JsonArray a) foreach (var g in a) l.Add(g.GetValue<string>()); return l.ToArray(); } }
        public static string[] snapshots { get { var l = new List<string>(); if (Native.Query(new JsonObject { ["kind"] = "mixerGroups" })["snapshots"] is JsonArray a) foreach (var g in a) l.Add(g.GetValue<string>()); return l.ToArray(); } }
    }
}
