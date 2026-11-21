using System;
using System.Collections;
using Stardust;

namespace Stardust.Cameras {

/// <summary>
/// Put it on the object with the real Camera. After every script has updated, it picks the live virtual camera
/// (active, highest Priority; the most recently activated wins a tie), blends to it when it changes and moves
/// the camera there, with the shakes of <see cref="CameraShake"/> on top.
/// </summary>
public class CameraDirector : ScriptBehaviour {
    /// <summary>Seconds to blend from one virtual camera to the next (each virtual camera can override it).</summary>
    public float DefaultBlend = 1.0f;
    public BlendCurve DefaultCurve = BlendCurve.EaseInOut;
    /// <summary>Blends and damping keep going while the game is paused (Time.timeScale 0).</summary>
    public bool IgnoreTimeScale;
    /// <summary>The camera takes the field of view of the virtual cameras.</summary>
    public bool ControlFieldOfView = true;

    /// <summary>The director of the scene (the last one created).</summary>
    public static CameraDirector current { get; private set; }
    /// <summary>The virtual camera shown now.</summary>
    public VirtualCamera live { get; private set; }
    public bool isBlending => m_BlendDuration > 0 && m_BlendTime < m_BlendDuration;
    /// <summary>Raised when the live virtual camera changes: (from, to).</summary>
    public event Action<VirtualCamera, VirtualCamera> onCameraChanged;

    VirtualCamera m_From;
    CameraPose m_FromPose, m_Last;
    bool m_HasLast;
    float m_BlendTime, m_BlendDuration;
    BlendCurve m_BlendCurve;
    readonly System.Diagnostics.Stopwatch m_Clock = System.Diagnostics.Stopwatch.StartNew();
    double m_LastTick;

    void OnCreate() { current = this; StartCoroutine(Run()); }
    void OnDestroy() { if (current == this) current = null; }

    IEnumerator Run() {
        // After all the Updates of the frame, so the targets have already moved
        while (true) { yield return new WaitForEndOfFrame(); Tick(); }
    }

    VirtualCamera Pick() {
        VirtualCamera best = null;
        foreach (var cam in VirtualCamera.s_All)
            if (cam.IsAvailable && (best == null || cam.Priority > best.Priority || cam.Priority == best.Priority && cam.activation > best.activation)) best = cam;
        return best;
    }

    void Tick() {
        double now = m_Clock.Elapsed.TotalSeconds;
        float dt = IgnoreTimeScale ? (float)Math.Min(0.1, now - m_LastTick) : Time.deltaTime;
        m_LastTick = now;
        if (dt <= 0 && !IgnoreTimeScale) dt = 0;
        var next = Pick();
        if (next != live) {
            var previous = live;
            m_From = previous != null && previous.IsAvailable ? previous : null;
            m_FromPose = m_HasLast ? m_Last : new CameraPose(transform.position, transform.forward, CurrentFov());
            float duration = next != null && next.BlendTime >= 0 ? next.BlendTime : DefaultBlend;
            m_BlendCurve = next != null && next.BlendTime >= 0 ? next.BlendCurve : DefaultCurve;
            m_BlendDuration = !m_HasLast || m_BlendCurve == BlendCurve.Cut ? 0 : Math.Max(0, duration);
            m_BlendTime = 0;
            next?.Snap();
            live = next;
            try { onCameraChanged?.Invoke(previous, next); } catch (Exception e) { Debug.LogError("Camera changed: " + e); }
        }
        if (live == null) return;
        var pose = live.UpdatePose(dt);
        if (m_BlendDuration > 0 && m_BlendTime < m_BlendDuration) {
            m_BlendTime += dt;
            // The outgoing camera keeps moving while it fades out, so the blend never jumps
            var from = m_From != null && m_From.IsAvailable ? m_From.UpdatePose(dt) : m_FromPose;
            pose = CameraPose.Lerp(from, pose, CameraMath.Evaluate(m_BlendCurve, m_BlendTime / m_BlendDuration));
        }
        m_Last = pose; m_HasLast = true;
        var shaken = CameraShake.Apply(pose, dt);
        transform.position = shaken.position;
        transform.LookAt(shaken.position + shaken.forward);
        if (Math.Abs(shaken.roll) > 1e-3f) { var e = transform.eulerAngles; transform.eulerAngles = new Vector3(e.x, e.y, e.z + shaken.roll); }
        if (ControlFieldOfView) { var cam = GetComponent<Camera>(); if (cam != null && Math.Abs(cam.fieldOfView - shaken.fieldOfView) > 0.01f) cam.fieldOfView = shaken.fieldOfView; }
    }

    float CurrentFov() { var cam = GetComponent<Camera>(); return cam != null ? cam.fieldOfView : 60; }
}
}
