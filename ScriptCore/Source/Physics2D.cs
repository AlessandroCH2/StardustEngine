using System;
using System.Text.Json.Nodes;
using Stardust.Interop;
using P = Stardust.ComponentProperties;
namespace Stardust;

public enum RigidbodyType2D { Static, Dynamic, Kinematic }
public enum ForceMode2D { Force = 0, Impulse = 1 }

/// <summary>A body that moves on the XY plane and turns only around Z. Needs a 2D collider on the same object.</summary>
public sealed unsafe class Rigidbody2D : Component {
    ulong Id => Entity.ID;
    public Vector2 velocity {
        get { Vector3 v; Native.Api.GetVelocity(Id, &v); return new Vector2(v.x, v.y); }
        set { var v = new Vector3(value.x, value.y, 0); Native.Api.SetVelocity(Id, &v); }
    }
    /// <summary>Degrees per second around Z.</summary>
    public float angularVelocity {
        get { Vector3 v; Native.Api.GetAngularVelocity(Id, &v); return v.z * (180f / MathF.PI); }
        set { var v = new Vector3(0, 0, value * (MathF.PI / 180f)); Native.Api.SetAngularVelocity(Id, &v); }
    }
    public void AddForce(Vector2 force, ForceMode2D mode = ForceMode2D.Force) { var f = new Vector3(force.x, force.y, 0); Native.Api.AddForce(Id, &f, (int)mode); }
    public void AddForceAtPosition(Vector2 force, Vector2 position, ForceMode2D mode = ForceMode2D.Force) { var f = new Vector3(force.x, force.y, 0); var p = new Vector3(position.x, position.y, Entity.transform.position.z); Native.Api.AddForceAtPosition(Id, &f, &p, (int)mode); }
    /// <summary>Torque around Z.</summary>
    public void AddTorque(float torque) { var t = new Vector3(0, 0, torque); Native.Api.AddTorque(Id, &t); }
    public Vector2 position { get { var p = Entity.transform.position; return new Vector2(p.x, p.y); } }
    public void MovePosition(Vector2 position) { var p = Entity.transform.position; Entity.transform.position = new Vector3(position.x, position.y, p.z); }

    public RigidbodyType2D bodyType { get => (RigidbodyType2D)P.Value(this, 28, "bodyType", 1); set => P.Property(this, 28, "bodyType", JsonValue.Create((int)value)); }
    public float mass { get => P.Value(this, 28, "mass", 1f); set => P.Property(this, 28, "mass", JsonValue.Create(value)); }
    public float drag { get => P.Value(this, 28, "linearDamping", 0f); set => P.Property(this, 28, "linearDamping", JsonValue.Create(value)); }
    public float angularDrag { get => P.Value(this, 28, "angularDamping", .05f); set => P.Property(this, 28, "angularDamping", JsonValue.Create(value)); }
    public float gravityScale { get => P.Value(this, 28, "gravityScale", 1f); set => P.Property(this, 28, "gravityScale", JsonValue.Create(value)); }
    public bool freezeRotation { get => P.Value(this, 28, "freezeRotation", false); set => P.Property(this, 28, "freezeRotation", JsonValue.Create(value)); }
}

/// <summary>Base of the 2D colliders: trigger flag and surface material.</summary>
public abstract class Collider2D : Component {
    int Type => Component.TypeIdOf(GetType());
    public bool isTrigger { get => P.Value(this, Type, "isTrigger", false); set => P.Property(this, Type, "isTrigger", JsonValue.Create(value)); }
    public float friction { get => P.Value(this, Type, "friction", .5f); set => P.Property(this, Type, "friction", JsonValue.Create(value)); }
    public float bounciness { get => P.Value(this, Type, "restitution", .1f); set => P.Property(this, Type, "restitution", JsonValue.Create(value)); }
    public Vector2 offset { get => P.VectorValue(this, Type, "offset"); set => P.Property(this, Type, "offset", new JsonArray(value.x, value.y)); }
}
public sealed class BoxCollider2D : Collider2D {
    public Vector2 size { get => P.VectorValue(this, 29, "size"); set => P.Property(this, 29, "size", new JsonArray(value.x, value.y)); }
}
public sealed class CircleCollider2D : Collider2D {
    public float radius { get => P.Value(this, 30, "radius", .5f); set => P.Property(this, 30, "radius", JsonValue.Create(value)); }
}
/// <summary>Vertical capsule; size is the full width and height.</summary>
public sealed class CapsuleCollider2D : Collider2D {
    public Vector2 size { get => P.VectorValue(this, 31, "size"); set => P.Property(this, 31, "size", new JsonArray(value.x, value.y)); }
}

public struct RaycastHit2D {
    public Entity entity;
    public Vector2 point, normal;
    public float distance;
    public static implicit operator bool(RaycastHit2D hit) => hit.entity != null;
}

/// <summary>Queries for the 2D world (the XY plane).</summary>
public static unsafe class Physics2D {
    /// <summary>Casts a ray on the XY plane. <paramref name="ignore"/> skips one object (e.g. the caster's own collider).</summary>
    public static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float distance = 1000f, Entity ignore = null) {
        var o = new Vector3(origin.x, origin.y, 0); var d = new Vector3(direction.x, direction.y, 0);
        RaycastHitNative h;
        if (Native.Api.Raycast(&o, &d, distance, ignore?.ID ?? 0, &h) == 0) return default;
        return new RaycastHit2D { entity = h.Entity != 0 ? new Entity(h.Entity) : null, point = new Vector2(h.Point.x, h.Point.y), normal = new Vector2(h.Normal.x, h.Normal.y), distance = h.Distance };
    }
    /// <summary>Gravity is shared with 3D physics: (0, -9.81).</summary>
    public static Vector2 gravity => new(0, -9.81f);
}
