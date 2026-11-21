using System;
using System.Collections.Generic;
using Stardust;
using Stardust.InputSystem;

namespace Stardust.Cameras {

/// <summary>How the virtual camera is placed.</summary>
public enum CameraBody {
    /// <summary>Stays where its object is.</summary>
    Fixed,
    /// <summary>Keeps an offset from the Follow target.</summary>
    Follow,
    /// <summary>Third person: turns around the Follow target with the mouse or the right stick, pulled in by walls.</summary>
    Orbit,
    /// <summary>Slides along a Spline (the Rail object) to the point closest to the Follow target.</summary>
    Rail,
}
/// <summary>Where the virtual camera looks.</summary>
public enum CameraAim {
    /// <summary>Its object's forward (Fixed, Follow, Rail) or the orbit direction (Orbit).</summary>
    None,
    /// <summary>At the Look At target.</summary>
    LookAt,
    /// <summary>The same way as the Follow target.</summary>
    FollowRotation,
}
public enum OffsetSpace { World, Target, TargetYaw }

/// <summary>
/// A camera shot: where to be and where to look. It does not render; the Camera Director on the real camera
/// copies the live virtual camera (the one with the highest Priority) and blends when it changes.
/// </summary>
public class VirtualCamera : ScriptBehaviour {
    /// <summary>The live camera is the active one with the highest priority.</summary>
    public int Priority = 10;
    public CameraBody Body = CameraBody.Follow;
    public CameraAim Aim = CameraAim.LookAt;
    public Entity Follow;
    public Entity LookAt;
    // Follow
    /// <summary>Follow: offset from the target. Orbit: ignored. Local +Z is behind (the engine's forward is -Z).</summary>
    public Vector3 FollowOffset = new(0, 3, 6);
    public OffsetSpace OffsetSpace = OffsetSpace.TargetYaw;
    /// <summary>Seconds to catch up with the target (0 = rigid).</summary>
    public float PositionDamping = 0.25f;
    // Aim
    public Vector3 LookAtOffset = new(0, 1.5f, 0);
    public float AimDamping = 0.1f;
    // Orbit
    public Vector3 PivotOffset = new(0, 1.6f, 0);
    public float Distance = 5;
    public float MinDistance = 1.5f, MaxDistance = 12;
    public float MinPitch = -35, MaxPitch = 70;
    public float StartYaw, StartPitch = 15;
    /// <summary>Degrees per pixel of mouse movement.</summary>
    public float MouseSensitivity = 0.15f;
    /// <summary>Degrees per second at full right stick.</summary>
    public float StickSpeed = 180;
    public bool InvertY;
    /// <summary>Only turn while the right mouse button is held.</summary>
    public bool RequireRightMouse;
    /// <summary>Input action that turns the camera (Vector2), e.g. Player/Look. Empty = mouse and right stick.</summary>
    public string LookAction = "";
    /// <summary>The mouse wheel changes the distance.</summary>
    public bool ScrollZoom = true;
    /// <summary>Goes back behind the target after a while without input.</summary>
    public bool Recenter;
    public float RecenterDelay = 2, RecenterSpeed = 90;
    // Collision
    public bool AvoidObstacles = true;
    public float CollisionRadius = 0.3f;
    // Rail
    /// <summary>An object with a Spline component.</summary>
    public Entity Rail;
    // Lens and feel
    public float FieldOfView = 60;
    /// <summary>Handheld sway, degrees.</summary>
    public float NoiseAmplitude;
    public float NoiseFrequency = 0.6f;
    /// <summary>Seconds to blend in when it goes live (-1 = the Camera Director's default).</summary>
    public float BlendTime = -1;
    public BlendCurve BlendCurve = BlendCurve.EaseInOut;
    // Bounds
    public bool UseBounds;
    public Vector3 BoundsMin = new(-100, 0, -100), BoundsMax = new(100, 100, 100);

    // ------------------------------------------------------------------ runtime
    internal static readonly List<VirtualCamera> s_All = new();
    static int s_Activation;
    internal int activation;
    CameraPose m_Pose;
    bool m_HasPose;
    float m_Yaw, m_Pitch, m_IdleTime, m_Collided = -1, m_Time;
    Vector3 m_RailPoint;

    /// <summary>Where the camera is now (valid after the first update while live).</summary>
    public CameraPose pose => m_Pose;
    /// <summary>True while the Camera Director shows this camera.</summary>
    public bool isLive => CameraDirector.current != null && CameraDirector.current.live == this;
    /// <summary>Orbit angles; set them to turn the camera from a script.</summary>
    public float yaw { get => m_Yaw; set => m_Yaw = value; }
    public float pitch { get => m_Pitch; set => m_Pitch = Math.Clamp(value, MinPitch, MaxPitch); }

    void OnCreate() {
        s_All.Add(this); activation = ++s_Activation;
        m_Yaw = StartYaw; m_Pitch = StartPitch;
        if (Body == CameraBody.Orbit && Follow != null && Follow.IsValid && StartYaw == 0) m_Yaw = CameraMath.Yaw(Follow.transform.forward);
    }
    void OnDestroy() => s_All.Remove(this);
    /// <summary>Makes it the most recently activated among cameras of the same priority.</summary>
    public void MoveToTopOfPrioritySubqueue() => activation = ++s_Activation;
    /// <summary>The next update jumps to the target instead of catching up.</summary>
    public void Snap() => m_HasPose = false;

    internal bool IsAvailable => IsValid && isActiveAndEnabled;

    // ------------------------------------------------------------------ update
    internal CameraPose UpdatePose(float dt) {
        m_Time += dt;
        var self = transform;
        bool snap = !m_HasPose;
        Vector3 position = m_HasPose ? m_Pose.position : self.position;
        Vector3 forward = m_HasPose ? m_Pose.forward : self.forward;
        bool hasFollow = Follow != null && Follow.IsValid, hasLook = LookAt != null && LookAt.IsValid;
        float posDamp = snap ? 1 : CameraMath.Damp(PositionDamping, dt), aimDamp = snap ? 1 : CameraMath.Damp(AimDamping, dt);
        switch (Body) {
        case CameraBody.Fixed:
            position = self.position; break;
        case CameraBody.Follow:
            if (hasFollow) position = Vector3.Lerp(position, FollowPoint(), posDamp);
            break;
        case CameraBody.Orbit:
            if (hasFollow) { var orbit = OrbitPose(dt, snap); position = orbit.position; forward = orbit.forward; }
            break;
        case CameraBody.Rail:
            position = Vector3.Lerp(position, RailPoint(position, hasFollow), posDamp);
            break;
        }
        if (UseBounds) position = new Vector3(Math.Clamp(position.x, BoundsMin.x, BoundsMax.x), Math.Clamp(position.y, BoundsMin.y, BoundsMax.y), Math.Clamp(position.z, BoundsMin.z, BoundsMax.z));
        // Aim. Orbit looks along its own direction unless it aims at another object or copies the target's rotation.
        if (Body == CameraBody.Orbit && hasFollow) {
            if (Aim == CameraAim.LookAt && hasLook && LookAt != Follow) forward = CameraMath.SlerpDirection(forward, LookAt.transform.position + LookAtOffset - position, aimDamp);
            else if (Aim == CameraAim.FollowRotation) forward = CameraMath.SlerpDirection(forward, Follow.transform.forward, aimDamp);
        } else {
            Vector3 wanted = Aim == CameraAim.LookAt && hasLook ? LookAt.transform.position + LookAtOffset - position
                           : Aim == CameraAim.FollowRotation && hasFollow ? Follow.transform.forward
                           : self.forward;
            forward = Aim == CameraAim.None ? wanted : CameraMath.SlerpDirection(forward, wanted, aimDamp);
        }
        // Handheld sway
        if (NoiseAmplitude > 0) {
            float t = m_Time * NoiseFrequency * MathF.PI * 2;
            float yawNoise = CameraMath.Noise(t, 1.3f) * NoiseAmplitude, pitchNoise = CameraMath.Noise(t, 7.9f) * NoiseAmplitude * 0.7f;
            var side = CameraMath.Normalize(Vector3.Cross(forward, Vector3.up));
            forward = CameraMath.Rotate(CameraMath.Rotate(forward, Vector3.up, yawNoise * Mathf.Deg2Rad), side, pitchNoise * Mathf.Deg2Rad);
        }
        m_Pose = new CameraPose(position, forward, FieldOfView);
        m_HasPose = true;
        // The object shows where the shot is (Fixed keeps its own placement)
        if (Body != CameraBody.Fixed) { self.position = position; self.LookAt(position + forward); }
        return m_Pose;
    }

    Vector3 FollowPoint() {
        var t = Follow.transform;
        switch (OffsetSpace) {
        case OffsetSpace.World: return t.position + FollowOffset;
        case OffsetSpace.Target: return t.position + t.TransformDirection(FollowOffset);
        default: {
            float yaw = CameraMath.Yaw(t.forward) * Mathf.Deg2Rad;
            var right = new Vector3(MathF.Cos(yaw), 0, -MathF.Sin(yaw));
            var back = new Vector3(MathF.Sin(yaw), 0, MathF.Cos(yaw));
            return t.position + right * FollowOffset.x + Vector3.up * FollowOffset.y + back * FollowOffset.z;
        }
        }
    }

    CameraPose OrbitPose(float dt, bool snap) {
        // Input: an action, else mouse and right stick
        Vector2 look = default; bool input = false;
        if (!string.IsNullOrEmpty(LookAction)) {
            var action = InputSystem.InputSystem.actions.FindAction(LookAction);
            if (action != null) { var v = action.ReadValue<Vector2>(); look = new Vector2(v.x * MouseSensitivity * 10, v.y * MouseSensitivity * 10); }
        } else {
            if (!RequireRightMouse || Input.GetMouseButton(MouseButton.Right)) { var d = Input.mouseDelta; look += new Vector2(d.x, -d.y) * MouseSensitivity; }
            look += new Vector2(Input.GetGamepadAxis(GamepadAxis.RightX), -Input.GetGamepadAxis(GamepadAxis.RightY)) * StickSpeed * dt;
        }
        if (Math.Abs(look.x) + Math.Abs(look.y) > 1e-4f) { input = true; m_IdleTime = 0; } else m_IdleTime += dt;
        m_Yaw -= look.x;
        m_Pitch = Math.Clamp(m_Pitch + (InvertY ? look.y : -look.y), MinPitch, MaxPitch);
        if (ScrollZoom && Math.Abs(Input.mouseScroll) > 0) Distance = Math.Clamp(Distance - Input.mouseScroll * 0.5f, MinDistance, MaxDistance);
        var target = Follow.transform;
        if (Recenter && !input && m_IdleTime > RecenterDelay) {
            float behind = CameraMath.Yaw(target.forward);
            float delta = CameraMath.DeltaAngle(m_Yaw, behind);
            m_Yaw += Math.Clamp(delta, -RecenterSpeed * dt, RecenterSpeed * dt);
        }
        var forward = CameraMath.Direction(m_Yaw, m_Pitch);
        var pivot = target.position + PivotOffset;
        float distance = Math.Clamp(Distance, MinDistance, MaxDistance);
        // Walls between the target and the camera pull it in; it goes back out gently
        float allowed = distance;
        if (AvoidObstacles && Physics.Raycast(pivot, -forward, out var hit, distance + CollisionRadius, Follow)) allowed = Math.Max(0.2f, hit.distance - CollisionRadius);
        if (m_Collided < 0 || snap) m_Collided = allowed;
        m_Collided = allowed < m_Collided ? allowed : m_Collided + (allowed - m_Collided) * CameraMath.Damp(0.35f, dt);
        var position = pivot - forward * m_Collided;
        return new CameraPose(position, forward, FieldOfView);
    }

    Vector3 RailPoint(Vector3 current, bool hasFollow) {
        var spline = Rail != null && Rail.IsValid ? Rail.GetComponent<Spline>() : null;
        if (spline == null) return current;
        var curve = spline.GetCurve();
        var goal = hasFollow ? Follow.transform.position : current;
        curve.Nearest(goal, out var point);
        m_RailPoint = point;
        return point;
    }
}
}
