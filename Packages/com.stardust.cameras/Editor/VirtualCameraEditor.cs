using System;
using Stardust;
using Stardust.Editor;

namespace Stardust.Cameras.Editor {

[CustomEditor(typeof(VirtualCamera))]
public class VirtualCameraEditor : Stardust.Editor.Editor {
    VirtualCamera cam => (VirtualCamera)target;

    public override void OnInspectorGUI() {
        var c = cam;
        EditorGUILayout.HelpBox(c.Body switch {
            CameraBody.Fixed => "Fixed: the shot stays where this object is; Aim decides where it looks.",
            CameraBody.Follow => "Follow: keeps Follow Offset from the Follow target (Target Yaw: behind it, whatever its pitch), catching up in Position Damping seconds.",
            CameraBody.Orbit => "Orbit: third person around the Follow target. Mouse or right stick turn it, the wheel zooms, walls pull it in.",
            _ => "Rail: slides along the Spline of the Rail object to the point closest to the Follow target.",
        });
        DrawDefaultInspector();
        if (Application.IsPlaying) {
            EditorGUILayout.Space();
            EditorGUILayout.LabelField(c.isLive ? "LIVE: the Camera Director shows this camera" : "Standby (another camera has a higher priority, or this one is off)");
        } else if (c.Follow == null && c.Body != CameraBody.Fixed) EditorGUILayout.HelpBox("Set Follow: the object the camera follows.");
    }

    public override void OnSceneGUI() {
        var c = cam;
        var self = c.transform;
        bool hasFollow = c.Follow != null && c.Follow.IsValid;
        Handles.color = new Color(0.3f, 0.8f, 1f, 1f);
        // Where it looks
        var p = self.position;
        Handles.DrawLine(p, p + self.forward * 2f, 2f);
        Handles.Label(p, $"Virtual Camera ({c.Priority})");
        if (!hasFollow) return;
        var target = c.Follow.transform.position;
        switch (c.Body) {
        case CameraBody.Orbit: {
            var pivot = target + c.PivotOffset;
            Handles.color = new Color(1f, 0.8f, 0.3f, 1f);
            Circle(pivot, c.Distance);
            Handles.DrawLine(pivot, pivot + CameraMath.Direction(c.StartYaw, c.StartPitch) * -c.Distance);
            break;
        }
        case CameraBody.Follow:
            Handles.color = new Color(1f, 0.8f, 0.3f, 1f);
            Handles.DrawLine(target, p);
            break;
        }
        if (c.Aim == CameraAim.LookAt && c.LookAt != null && c.LookAt.IsValid) {
            Handles.color = new Color(1f, 0.4f, 0.4f, 1f);
            Handles.DrawLine(p, c.LookAt.transform.position + c.LookAtOffset);
        }
    }

    static void Circle(Vector3 center, float radius) {
        var previous = center + new Vector3(radius, 0, 0);
        for (int i = 1; i <= 32; ++i) {
            float a = i / 32f * MathF.PI * 2;
            var point = center + new Vector3(MathF.Cos(a) * radius, 0, MathF.Sin(a) * radius);
            Handles.DrawLine(previous, point);
            previous = point;
        }
    }
}
}
