using System;
using System.Collections.Generic;
using Stardust;

namespace Stardust.Cameras {

/// <summary>
/// Shakes the camera of the Camera Director: explosions, landings, hits.
/// <code>
/// CameraShake.Shake(0.4f, 0.5f);                      // everywhere
/// CameraShake.ShakeAt(explosion, 20, 1.0f, 0.8f);     // stronger the closer the camera is
/// </code>
/// </summary>
public static class CameraShake {
    sealed class Impulse { public float amplitude, duration, frequency, age, seed; }
    static readonly List<Impulse> s_Active = new();
    static readonly System.Random s_Random = new();

    /// <summary>Scales every shake (a "camera shake" option in the settings).</summary>
    public static float globalScale = 1;

    /// <summary>amplitude: 1 = strong (about 1.5 degrees and 10 cm); fades out over duration seconds.</summary>
    public static void Shake(float amplitude, float duration = 0.5f, float frequency = 18) {
        if (amplitude <= 0 || duration <= 0) return;
        s_Active.Add(new Impulse { amplitude = amplitude, duration = duration, frequency = Math.Max(0.5f, frequency), seed = (float)s_Random.NextDouble() * 100 });
    }
    /// <summary>A shake from a point: full strength at the point, none at radius metres from the camera.</summary>
    public static void ShakeAt(Vector3 source, float radius, float amplitude, float duration = 0.6f, float frequency = 18) {
        var director = CameraDirector.current;
        float distance = director != null ? (director.transform.position - source).magnitude : 0;
        float falloff = radius <= 0 ? 1 : Math.Clamp(1 - distance / radius, 0, 1);
        Shake(amplitude * falloff * falloff, duration, frequency);
    }
    public static void StopAll() => s_Active.Clear();
    public static bool isShaking => s_Active.Count > 0;

    internal static CameraPose Apply(CameraPose pose, float dt) {
        if (s_Active.Count == 0 || globalScale <= 0) return pose;
        float yaw = 0, pitch = 0, roll = 0; Vector3 offset = Vector3.zero;
        for (int i = s_Active.Count - 1; i >= 0; --i) {
            var s = s_Active[i];
            s.age += dt;
            if (s.age >= s.duration) { s_Active.RemoveAt(i); continue; }
            float fade = 1 - s.age / s.duration; fade *= fade;
            float t = s.age * s.frequency, a = s.amplitude * fade * globalScale;
            yaw += CameraMath.Noise(t, s.seed) * 1.5f * a;
            pitch += CameraMath.Noise(t, s.seed + 31) * 1.5f * a;
            roll += CameraMath.Noise(t, s.seed + 57) * 1.0f * a;
            offset += new Vector3(CameraMath.Noise(t, s.seed + 11), CameraMath.Noise(t, s.seed + 19), CameraMath.Noise(t, s.seed + 23)) * 0.1f * a;
        }
        var side = CameraMath.Normalize(Vector3.Cross(pose.forward, Vector3.up));
        var forward = CameraMath.Rotate(CameraMath.Rotate(pose.forward, Vector3.up, yaw * Mathf.Deg2Rad), side, pitch * Mathf.Deg2Rad);
        return new CameraPose(pose.position + offset, forward, pose.fieldOfView, pose.roll + roll);
    }
}
}
