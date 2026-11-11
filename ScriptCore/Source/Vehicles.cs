using System.Text.Json.Nodes;
using P = Stardust.ComponentProperties;
namespace Stardust;

/// <summary>
/// Wheeled vehicle physics (Jolt) on a dynamic Rigidbody: engine, automatic gearbox, differentials and
/// anti-roll bars. Its wheels are the <see cref="WheelCollider"/>s on child objects. Drive it every frame
/// with <see cref="throttle"/> (-1 reverse .. 1), <see cref="steer"/> (-1 left .. 1 right),
/// <see cref="brake"/> and <see cref="handbrake"/> (0..1). Changing an engine setting rebuilds the vehicle.
/// </summary>
public sealed class VehicleController : Component {
    const int T = 33;
    public float throttle { get => P.Value(this, T, "throttle", 0f); set => P.Property(this, T, "throttle", JsonValue.Create(value)); }
    public float steer { get => P.Value(this, T, "steer", 0f); set => P.Property(this, T, "steer", JsonValue.Create(value)); }
    public float brake { get => P.Value(this, T, "brake", 0f); set => P.Property(this, T, "brake", JsonValue.Create(value)); }
    public float handbrake { get => P.Value(this, T, "handbrake", 0f); set => P.Property(this, T, "handbrake", JsonValue.Create(value)); }
    /// <summary>Sets all four driver inputs with one call.</summary>
    public void SetInput(float throttle, float steer, float brake = 0f, float handbrake = 0f) =>
        P.Set(this, T, new JsonObject { ["throttle"] = throttle, ["steer"] = steer, ["brake"] = brake, ["handbrake"] = handbrake });
    /// <summary>Forward speed in m/s (negative when reversing).</summary>
    public float speed => P.Value(this, T, "speed", 0f);
    public float rpm => P.Value(this, T, "rpm", 0f);
    /// <summary>-1 reverse, 0 neutral, 1.. forward gears.</summary>
    public int gear => P.Value(this, T, "gear", 0);
    public float engineTorque { get => P.Value(this, T, "engineTorque", 600f); set => P.Property(this, T, "engineTorque", JsonValue.Create(value)); }
    public float maxRPM { get => P.Value(this, T, "maxRPM", 6500f); set => P.Property(this, T, "maxRPM", JsonValue.Create(value)); }
    public float centerOfMassOffset { get => P.Value(this, T, "centerOfMassOffset", -0.35f); set => P.Property(this, T, "centerOfMassOffset", JsonValue.Create(value)); }
    public bool antiRollBars { get => P.Value(this, T, "antiRollBars", true); set => P.Property(this, T, "antiRollBars", JsonValue.Create(value)); }
}

/// <summary>
/// A wheel of a <see cref="VehicleController"/> (on a child object). In play mode the engine moves this
/// object along the suspension and turns it for steering and rolling, so a wheel mesh (axle along X)
/// can simply be placed on it or below it.
/// </summary>
public sealed class WheelCollider : Component {
    const int T = 34;
    public float radius { get => P.Value(this, T, "radius", .35f); set => P.Property(this, T, "radius", JsonValue.Create(value)); }
    public float width { get => P.Value(this, T, "width", .25f); set => P.Property(this, T, "width", JsonValue.Create(value)); }
    public float suspensionDistance { get => P.Value(this, T, "suspensionDistance", .3f); set => P.Property(this, T, "suspensionDistance", JsonValue.Create(value)); }
    public float springFrequency { get => P.Value(this, T, "springFrequency", 1.6f); set => P.Property(this, T, "springFrequency", JsonValue.Create(value)); }
    public float springDamping { get => P.Value(this, T, "springDamping", .5f); set => P.Property(this, T, "springDamping", JsonValue.Create(value)); }
    public float maxSteerAngle { get => P.Value(this, T, "maxSteerAngle", 0f); set => P.Property(this, T, "maxSteerAngle", JsonValue.Create(value)); }
    public bool motor { get => P.Value(this, T, "motor", false); set => P.Property(this, T, "motor", JsonValue.Create(value)); }
    public bool handbrake { get => P.Value(this, T, "handbrake", false); set => P.Property(this, T, "handbrake", JsonValue.Create(value)); }
    public float forwardFriction { get => P.Value(this, T, "forwardFriction", 1f); set => P.Property(this, T, "forwardFriction", JsonValue.Create(value)); }
    public float sidewaysFriction { get => P.Value(this, T, "sidewaysFriction", 1f); set => P.Property(this, T, "sidewaysFriction", JsonValue.Create(value)); }
    /// <summary>Current steering angle in degrees (positive = left).</summary>
    public float steerAngle => P.Value(this, T, "steerAngle", 0f);
    /// <summary>Current rolling angle in degrees.</summary>
    public float rotationAngle => P.Value(this, T, "rotationAngle", 0f);
    /// <summary>0 = fully extended, 1 = fully compressed.</summary>
    public float compression => P.Value(this, T, "compression", 0f);
    public bool isGrounded => P.Value(this, T, "isGrounded", false);
}
