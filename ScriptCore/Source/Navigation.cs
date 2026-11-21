using System;
using System.Collections.Generic;
using System.Text.Json.Nodes;
using Stardust.Interop;
using P = Stardust.ComponentProperties;

namespace Stardust.AI
{
    public enum NavMeshPathStatus { PathComplete, PathPartial, PathInvalid }

    public sealed class NavMeshPath
    {
        public Vector3[] corners { get; internal set; } = Array.Empty<Vector3>();
        public NavMeshPathStatus status { get; internal set; } = NavMeshPathStatus.PathInvalid;
        public void ClearCorners() { corners = Array.Empty<Vector3>(); status = NavMeshPathStatus.PathInvalid; }
        /// <summary>Length in metres along the corners.</summary>
        public float length { get { float d = 0; for (int i = 1; i < corners.Length; ++i) d += (corners[i] - corners[i - 1]).magnitude; return d; } }
    }

    public struct NavMeshHit { public Vector3 position; public float distance; public bool hit; }

    internal static class NavJson
    {
        internal static Vector3 V(JsonNode n) => n is JsonArray a && a.Count == 3 ? new Vector3(a[0].GetValue<float>(), a[1].GetValue<float>(), a[2].GetValue<float>()) : Vector3.zero;
        internal static JsonArray J(Vector3 v) => new(v.x, v.y, v.z);
    }

    /// <summary>Queries on the scene's baked navigation mesh (Nav Mesh Surface).</summary>
    public static class NavMesh
    {
        public static bool isBaked => Native.Query(new JsonObject { ["kind"] = "navInfo" })["baked"]?.GetValue<bool>() ?? false;
        /// <summary>Finds the shortest walkable path; false when there is none.</summary>
        public static bool CalculatePath(Vector3 from, Vector3 to, NavMeshPath path)
        {
            var r = Native.Query(new JsonObject { ["kind"] = "navPath", ["from"] = NavJson.J(from), ["to"] = NavJson.J(to) });
            var list = new List<Vector3>();
            if (r["corners"] is JsonArray corners) foreach (var c in corners) list.Add(NavJson.V(c));
            if (path != null) { path.corners = list.ToArray(); path.status = (NavMeshPathStatus)(r["status"]?.GetValue<int>() ?? 2); }
            return r["found"]?.GetValue<bool>() ?? false;
        }
        /// <summary>The closest point on the nav mesh within maxDistance.</summary>
        public static bool SamplePosition(Vector3 position, out NavMeshHit hit, float maxDistance)
        {
            var r = Native.Query(new JsonObject { ["kind"] = "navSample", ["position"] = NavJson.J(position), ["maxDistance"] = maxDistance });
            hit = new NavMeshHit { position = NavJson.V(r["position"]), hit = r["found"]?.GetValue<bool>() ?? false };
            hit.distance = (hit.position - position).magnitude;
            return hit.hit;
        }
        /// <summary>Walks a straight line on the nav mesh; true when something blocks it (hit = where).</summary>
        public static bool Raycast(Vector3 from, Vector3 to, out NavMeshHit hit)
        {
            var r = Native.Query(new JsonObject { ["kind"] = "navRaycast", ["from"] = NavJson.J(from), ["to"] = NavJson.J(to) });
            bool blocked = r["blocked"]?.GetValue<bool>() ?? true;
            hit = new NavMeshHit { position = NavJson.V(r["position"]), hit = blocked };
            hit.distance = (hit.position - from).magnitude;
            return blocked;
        }
    }

    /// <summary>Bakes the walkable surface of the scene. BuildNavMesh() rebuilds it while the game runs (procedural levels).</summary>
    public sealed class NavMeshSurface : Component
    {
        const int T = 40;
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public float agentRadius { get => P.Value(this, T, "agentRadius", .5f); set => Set("agentRadius", value); }
        public float agentHeight { get => P.Value(this, T, "agentHeight", 2f); set => Set("agentHeight", value); }
        public float maxSlope { get => P.Value(this, T, "maxSlope", 45f); set => Set("maxSlope", value); }
        public float stepHeight { get => P.Value(this, T, "stepHeight", .4f); set => Set("stepHeight", value); }
        public void BuildNavMesh() => P.Set(this, T, new JsonObject { ["command"] = "bake" });
    }

    /// <summary>Moves its object along the nav mesh to a destination, avoiding the other agents.</summary>
    public sealed class NavMeshAgent : Component
    {
        const int T = 41;
        JsonObject Data => P.Get(this, T);
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public bool SetDestination(Vector3 target) { Set("setDestination", NavJson.J(target)); return true; }
        public Vector3 destination { get => NavJson.V(Data["destination"]); set => SetDestination(value); }
        public bool isStopped { get => P.Value(this, T, "isStopped", false); set => Set("isStopped", value); }
        public void ResetPath() => Set("resetPath", true);
        /// <summary>Moves the agent to a point at once.</summary>
        public bool Warp(Vector3 position) { Set("warp", NavJson.J(position)); return true; }
        /// <summary>Steers by velocity instead of a path (until the next SetDestination).</summary>
        public void Move(Vector3 velocity) => Set("velocity", NavJson.J(velocity));
        public bool hasPath => P.Value(this, T, "hasPath", false);
        public bool pathPending => P.Value(this, T, "pathPending", false);
        public bool isOnNavMesh => P.Value(this, T, "isOnNavMesh", false);
        public NavMeshPathStatus pathStatus => (NavMeshPathStatus)P.Value(this, T, "pathStatus", 0);
        public float remainingDistance => P.Value(this, T, "remainingDistance", 0f);
        public Vector3 velocity => NavJson.V(Data["velocity"]);
        public Vector3 nextPosition => NavJson.V(Data["nextPosition"]);
        /// <summary>The next corners of the current path.</summary>
        public Vector3[] steeringCorners { get { var list = new List<Vector3>(); if (Data["corners"] is JsonArray a) foreach (var c in a) list.Add(NavJson.V(c)); return list.ToArray(); } }
        public float speed { get => P.Value(this, T, "speed", 3.5f); set => Set("speed", value); }
        public float angularSpeed { get => P.Value(this, T, "angularSpeed", 360f); set => Set("angularSpeed", value); }
        public float acceleration { get => P.Value(this, T, "acceleration", 8f); set => Set("acceleration", value); }
        public float stoppingDistance { get => P.Value(this, T, "stoppingDistance", 0f); set => Set("stoppingDistance", value); }
        public bool autoBraking { get => P.Value(this, T, "autoBraking", true); set => Set("autoBraking", value); }
        public float radius { get => P.Value(this, T, "radius", .5f); set => Set("radius", value); }
        public float height { get => P.Value(this, T, "height", 2f); set => Set("height", value); }
        public float baseOffset { get => P.Value(this, T, "baseOffset", 0f); set => Set("baseOffset", value); }
        public int avoidancePriority { get => P.Value(this, T, "avoidancePriority", 50); set => Set("avoidancePriority", value); }
        public bool updatePosition { get => P.Value(this, T, "updatePosition", true); set => Set("updatePosition", value); }
        public bool updateRotation { get => P.Value(this, T, "updateRotation", true); set => Set("updateRotation", value); }
    }
}
