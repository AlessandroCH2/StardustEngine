using System;
using System.Collections.Generic;
using System.Text.Json.Nodes;
using Stardust.Interop;
using P = Stardust.ComponentProperties;

namespace Stardust
{
    [Flags] public enum CollisionFlags { None = 0, Sides = 1, Above = 2, Below = 4 }

    /// <summary>
    /// A capsule moved by Move(): it slides along walls, climbs steps up to stepOffset and slopes up to
    /// slopeLimit, and pushes dynamic bodies. Gravity is up to the script (or use SimpleMove).
    /// </summary>
    public sealed class CharacterController : Component
    {
        const int T = 38;
        static Vector3 V(JsonNode n) => n is JsonArray a && a.Count == 3 ? new Vector3(a[0].GetValue<float>(), a[1].GetValue<float>(), a[2].GetValue<float>()) : Vector3.zero;
        static JsonArray J(Vector3 v) => new(v.x, v.y, v.z);
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);

        /// <summary>Moves by `motion` metres now. Returns what it touched.</summary>
        public CollisionFlags Move(Vector3 motion)
        {
            Set("move", J(motion));
            return (CollisionFlags)P.Value(this, T, "collisionFlags", 0);
        }
        static readonly Dictionary<ulong, float> s_Fall = new();
        /// <summary>Moves at `speed` metres per second (horizontal) with gravity; returns true when grounded.</summary>
        public bool SimpleMove(Vector3 speed)
        {
            s_Fall.TryGetValue(Entity.ID, out float fall);
            fall = isGrounded ? -1f : fall + Physics.gravity.y * Time.deltaTime;
            var flags = Move(new Vector3(speed.x, fall, speed.z) * Time.deltaTime);
            if ((flags & CollisionFlags.Below) != 0) fall = -1f;
            if ((flags & CollisionFlags.Above) != 0 && fall > 0) fall = 0;
            s_Fall[Entity.ID] = fall;
            return (flags & CollisionFlags.Below) != 0;
        }
        public bool isGrounded => P.Value(this, T, "isGrounded", false);
        public CollisionFlags collisionFlags => (CollisionFlags)P.Value(this, T, "collisionFlags", 0);
        /// <summary>Speed of the last Move, metres per second.</summary>
        public Vector3 velocity => V(P.Get(this, T)["velocity"]);
        public Vector3 groundNormal => V(P.Get(this, T)["groundNormal"]);
        public float height { get => P.Value(this, T, "height", 2f); set => Set("height", value); }
        public float radius { get => P.Value(this, T, "radius", .5f); set => Set("radius", value); }
        public Vector3 center { get => V(P.Get(this, T)["center"]); set => Set("center", J(value)); }
        public float slopeLimit { get => P.Value(this, T, "slopeLimit", 45f); set => Set("slopeLimit", value); }
        public float stepOffset { get => P.Value(this, T, "stepOffset", .3f); set => Set("stepOffset", value); }
        public float skinWidth { get => P.Value(this, T, "skinWidth", .02f); set => Set("skinWidth", value); }
        public float minMoveDistance { get => P.Value(this, T, "minMoveDistance", 0f); set => Set("minMoveDistance", value); }
        /// <summary>Newtons it pushes dynamic bodies with.</summary>
        public float pushForce { get => P.Value(this, T, "pushForce", 100f); set => Set("pushForce", value); }
    }

    public enum JointType { Fixed, Hinge, Spring, Slider, Ball }

    /// <summary>
    /// Connects this object's Rigidbody to another (or to the world when connectedBody is null):
    /// Fixed, Hinge (doors, wheels), Spring, Slider (drawers, pistons), Ball (ragdoll limbs, with cone and twist limits).
    /// A script on the object receives OnJointBreak(Entity) when it breaks.
    /// </summary>
    public sealed class Joint : Component
    {
        const int T = 39;
        static Vector3 V(JsonNode n) => n is JsonArray a && a.Count == 3 ? new Vector3(a[0].GetValue<float>(), a[1].GetValue<float>(), a[2].GetValue<float>()) : Vector3.zero;
        static JsonArray J(Vector3 v) => new(v.x, v.y, v.z);
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public JointType type { get => (JointType)P.Value(this, T, "type", 1); set => Set("type", (int)value); }
        public Entity connectedBody
        {
            get { var id = JsonVec.Id(P.Value(this, T, "connectedBody", "0")); return id == 0 ? null : new Entity(id); }
            set => Set("connectedBody", JsonVec.Id(value?.ID ?? 0));
        }
        /// <summary>Local space of this object.</summary>
        public Vector3 anchor { get => V(P.Get(this, T)["anchor"]); set => Set("anchor", J(value)); }
        public Vector3 axis { get => V(P.Get(this, T)["axis"]); set => Set("axis", J(value)); }
        public bool enableCollision { get => P.Value(this, T, "enableCollision", false); set => Set("enableCollision", value); }
        public bool useLimits { get => P.Value(this, T, "useLimits", false); set => Set("useLimits", value); }
        /// <summary>Degrees (Hinge), metres (Slider), min/max distance (Spring).</summary>
        public float minLimit { get => P.Value(this, T, "minLimit", -45f); set => Set("minLimit", value); }
        public float maxLimit { get => P.Value(this, T, "maxLimit", 45f); set => Set("maxLimit", value); }
        public float swingLimit { get => P.Value(this, T, "swingLimit", 45f); set => Set("swingLimit", value); }
        public float twistMin { get => P.Value(this, T, "twistMin", -30f); set => Set("twistMin", value); }
        public float twistMax { get => P.Value(this, T, "twistMax", 30f); set => Set("twistMax", value); }
        public float springFrequency { get => P.Value(this, T, "springFrequency", 2f); set => Set("springFrequency", value); }
        public float springDamping { get => P.Value(this, T, "springDamping", .5f); set => Set("springDamping", value); }
        public bool useMotor { get => P.Value(this, T, "useMotor", false); set => Set("useMotor", value); }
        /// <summary>Degrees per second (Hinge) or metres per second (Slider).</summary>
        public float motorTargetVelocity { get => P.Value(this, T, "motorTargetVelocity", 90f); set => Set("motorTargetVelocity", value); }
        public float motorForce { get => P.Value(this, T, "motorForce", 500f); set => Set("motorForce", value); }
        public float friction { get => P.Value(this, T, "friction", 0f); set => Set("friction", value); }
        /// <summary>Newtons; 0 = unbreakable.</summary>
        public float breakForce { get => P.Value(this, T, "breakForce", 0f); set => Set("breakForce", value); }
        public float breakTorque { get => P.Value(this, T, "breakTorque", 0f); set => Set("breakTorque", value); }
        public bool broken => P.Value(this, T, "broken", false);
        /// <summary>Reconnects a broken joint.</summary>
        public void Repair() => Set("broken", false);
        /// <summary>Hinge angle in degrees, Slider position in metres.</summary>
        public float currentAngle => P.Value(this, T, "currentAngle", 0f);
        public Vector3 currentForce => V(P.Get(this, T)["currentForce"]);
        public Vector3 currentTorque => V(P.Get(this, T)["currentTorque"]);
    }

    public static unsafe partial class Physics
    {
        /// <summary>Gravity of the running scene (metres per second squared).</summary>
        public static Vector3 gravity
        {
            get { var a = Native.Query(new JsonObject { ["kind"] = "physicsGravity" })["gravity"] as JsonArray; return a == null ? new Vector3(0, -9.81f, 0) : new Vector3(a[0].GetValue<float>(), a[1].GetValue<float>(), a[2].GetValue<float>()); }
            set => Native.Query(new JsonObject { ["kind"] = "physicsGravity", ["set"] = new JsonArray(value.x, value.y, value.z) });
        }
    }
}
