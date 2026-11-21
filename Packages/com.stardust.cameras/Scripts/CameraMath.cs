using System;
using Stardust;

namespace Stardust.Cameras {

/// <summary>Where a camera is and where it looks.</summary>
public struct CameraPose {
    public Vector3 position;
    public Vector3 forward;     // normalized
    public float fieldOfView;   // degrees
    public float roll;          // degrees (shake, dutch angle)
    public CameraPose(Vector3 position, Vector3 forward, float fieldOfView, float roll = 0) {
        this.position = position; this.forward = CameraMath.Normalize(forward); this.fieldOfView = fieldOfView; this.roll = roll;
    }
    public static CameraPose Lerp(CameraPose a, CameraPose b, float t) =>
        new(Vector3.Lerp(a.position, b.position, t), CameraMath.SlerpDirection(a.forward, b.forward, t), a.fieldOfView + (b.fieldOfView - a.fieldOfView) * t, a.roll + (b.roll - a.roll) * t);
}

public enum BlendCurve { EaseInOut, Linear, EaseIn, EaseOut, Cut }

public static class CameraMath {
    public static Vector3 Normalize(Vector3 v) { float m = v.magnitude; return m > 1e-6f ? v / m : Vector3.forward; }

    /// <summary>Damping that does not depend on the frame rate: after `time` seconds about 63% of the way is done.</summary>
    public static float Damp(float time, float dt) => time <= 0 ? 1 : 1 - MathF.Exp(-dt / time);
    public static Vector3 Damp(Vector3 current, Vector3 target, float time, float dt) => Vector3.Lerp(current, target, Damp(time, dt));

    public static Vector3 SlerpDirection(Vector3 a, Vector3 b, float t) {
        a = Normalize(a); b = Normalize(b);
        float dot = Math.Clamp(Vector3.Dot(a, b), -1f, 1f);
        float angle = MathF.Acos(dot);
        if (angle < 1e-4f) return b;
        if (MathF.PI - angle < 1e-3f) { // opposite: turn around the up axis
            var side = Vector3.Cross(Vector3.up, a); if (side.magnitude < 1e-4f) side = Vector3.right;
            return Normalize(Rotate(a, Normalize(side), angle * t));
        }
        float s = MathF.Sin(angle);
        return Normalize(a * (MathF.Sin((1 - t) * angle) / s) + b * (MathF.Sin(t * angle) / s));
    }
    public static Vector3 DampDirection(Vector3 current, Vector3 target, float time, float dt) => SlerpDirection(current, target, Damp(time, dt));

    /// <summary>Rotates v around a unit axis (Rodrigues).</summary>
    public static Vector3 Rotate(Vector3 v, Vector3 axis, float radians) {
        float c = MathF.Cos(radians), s = MathF.Sin(radians);
        return v * c + Vector3.Cross(axis, v) * s + axis * (Vector3.Dot(axis, v) * (1 - c));
    }

    /// <summary>Direction from yaw (degrees around Y, 0 = the engine's forward -Z) and pitch (degrees, positive looks down).</summary>
    public static Vector3 Direction(float yaw, float pitch) {
        float y = yaw * Mathf.Deg2Rad, p = pitch * Mathf.Deg2Rad;
        return new Vector3(-MathF.Sin(y) * MathF.Cos(p), -MathF.Sin(p), -MathF.Cos(y) * MathF.Cos(p));
    }
    public static float Yaw(Vector3 forward) => MathF.Atan2(-forward.x, -forward.z) * Mathf.Rad2Deg;
    public static float Pitch(Vector3 forward) => MathF.Asin(Math.Clamp(-Normalize(forward).y, -1f, 1f)) * Mathf.Rad2Deg;
    public static float DeltaAngle(float from, float to) { float d = (to - from) % 360f; if (d > 180) d -= 360; if (d < -180) d += 360; return d; }

    public static float Evaluate(BlendCurve curve, float t) {
        t = Math.Clamp(t, 0f, 1f);
        return curve switch {
            BlendCurve.Linear => t,
            BlendCurve.EaseIn => t * t,
            BlendCurve.EaseOut => 1 - (1 - t) * (1 - t),
            BlendCurve.Cut => 1,
            _ => t * t * (3 - 2 * t),
        };
    }

    // Smooth noise in -1..1 (sums of sines with unrelated frequencies)
    public static float Noise(float t, float seed) =>
        (MathF.Sin(t * 1.0f + seed) * 0.5f + MathF.Sin(t * 2.31f + seed * 1.7f) * 0.3f + MathF.Sin(t * 4.79f + seed * 2.9f) * 0.2f);
}
}
