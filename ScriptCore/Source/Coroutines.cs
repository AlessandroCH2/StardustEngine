using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Stardust.Interop;

namespace Stardust
{
    /// <summary>Base of the objects a coroutine can yield to wait.</summary>
    public abstract class YieldInstruction { }
    /// <summary>Waits this many seconds of game time (scaled by Time.timeScale).</summary>
    public sealed class WaitForSeconds : YieldInstruction { internal readonly float seconds; public WaitForSeconds(float seconds) { this.seconds = seconds; } }
    /// <summary>Waits this many seconds of real time (not affected by Time.timeScale).</summary>
    public sealed class WaitForSecondsRealtime : YieldInstruction { internal readonly float seconds; public WaitForSecondsRealtime(float seconds) { this.seconds = seconds; } }
    /// <summary>Resumes after the next fixed (physics) step.</summary>
    public sealed class WaitForFixedUpdate : YieldInstruction { }
    /// <summary>Resumes at the end of this frame, after every Update.</summary>
    public sealed class WaitForEndOfFrame : YieldInstruction { }
    /// <summary>Waits until the condition is true.</summary>
    public sealed class WaitUntil : YieldInstruction { internal readonly Func<bool> condition; public WaitUntil(Func<bool> condition) { this.condition = condition ?? throw new ArgumentNullException(nameof(condition)); } }
    /// <summary>Waits while the condition is true.</summary>
    public sealed class WaitWhile : YieldInstruction { internal readonly Func<bool> condition; public WaitWhile(Func<bool> condition) { this.condition = condition ?? throw new ArgumentNullException(nameof(condition)); } }
    /// <summary>A custom wait: the coroutine waits while keepWaiting is true.</summary>
    public abstract class CustomYieldInstruction : YieldInstruction { public abstract bool keepWaiting { get; } }

    /// <summary>A running coroutine (StartCoroutine). Yield it from another coroutine to wait for it.</summary>
    public sealed class Coroutine : YieldInstruction
    {
        internal readonly object owner;
        internal readonly Stack<IEnumerator> stack = new();
        internal object waitingFor;
        internal float waitUntil, waitRealtime;
        internal bool waitFixed, waitEndOfFrame, done;
        internal Exception error;
        internal Coroutine(object owner, IEnumerator routine) { this.owner = owner; stack.Push(routine); }
        /// <summary>The coroutine has finished (or was stopped).</summary>
        public bool isDone => done;
    }

    internal enum CoroutinePhase { Update, FixedUpdate }

    // Runs the coroutines and the Invoke timers of every script, after Update (and after FixedUpdate for WaitForFixedUpdate)
    internal static class Coroutines
    {
        static readonly List<Coroutine> s_Running = new();
        static readonly List<Coroutine> s_Added = new();
        internal sealed class Timer { public object owner; public string method; public Action action; public float at, repeat; public bool realtime; public bool cancelled; }
        static readonly List<Timer> s_Timers = new();
        static readonly System.Diagnostics.Stopwatch s_Clock = System.Diagnostics.Stopwatch.StartNew();
        internal static float Realtime => (float)s_Clock.Elapsed.TotalSeconds;

        internal static Coroutine Start(object owner, IEnumerator routine)
        {
            if (routine == null) throw new ArgumentNullException(nameof(routine));
            var c = new Coroutine(owner, routine);
            // Runs at once until its first yield, like a normal call
            Step(c, CoroutinePhase.Update, true);
            if (!c.done) s_Added.Add(c);
            return c;
        }
        internal static void StartFromResult(object owner, object result) { if (result is IEnumerator routine) Start(owner, routine); }
        internal static void Stop(Coroutine c) { if (c != null) c.done = true; }
        internal static void StopAll(object owner)
        {
            foreach (var c in s_Running) if (c.owner == owner) c.done = true;
            foreach (var c in s_Added) if (c.owner == owner) c.done = true;
        }
        internal static void StopNamed(object owner, string name)
        {
            foreach (var list in new[] { s_Running, s_Added })
                foreach (var c in list) if (c.owner == owner && c.stack.Count > 0 && RoutineName(c) == name) c.done = true;
        }
        static string RoutineName(Coroutine c)
        {
            var bottom = c.stack.ToArray()[^1];
            var n = bottom.GetType().Name; // compiler names it "<Method>d__N"
            int a = n.IndexOf('<'), b = n.IndexOf('>');
            return a >= 0 && b > a ? n.Substring(a + 1, b - a - 1) : n;
        }

        internal static void AddTimer(Timer t) => s_Timers.Add(t);
        internal static void CancelTimers(object owner, string method)
        {
            foreach (var t in s_Timers) if (t.owner == owner && (method == null || t.method == method)) t.cancelled = true;
        }
        internal static bool HasTimer(object owner, string method)
        {
            foreach (var t in s_Timers) if (!t.cancelled && t.owner == owner && (method == null || t.method == method)) return true;
            return false;
        }

        // The owner script still runs: destroyed or deactivated objects stop their coroutines
        static bool Alive(object owner)
        {
            if (owner is ScriptBehaviour s) return s.IsValid && s.activeInHierarchy;
            if (owner is Entity e) return e.IsValid;
            return true;
        }

        internal static void Tick(CoroutinePhase phase)
        {
            if (phase == CoroutinePhase.Update) RunTimers();
            s_Running.AddRange(s_Added); s_Added.Clear();
            for (int i = 0; i < s_Running.Count; ++i)
            {
                var c = s_Running[i];
                if (c.done) continue;
                if (!Alive(c.owner)) { c.done = true; continue; }
                Step(c, phase, false);
            }
            s_Running.RemoveAll(c => c.done);
        }

        static bool Waiting(Coroutine c, CoroutinePhase phase)
        {
            if (c.waitFixed) { if (phase != CoroutinePhase.FixedUpdate) return true; c.waitFixed = false; return false; }
            if (phase == CoroutinePhase.FixedUpdate) return true; // everything else resumes after Update
            if (c.waitEndOfFrame) { c.waitEndOfFrame = false; return false; }
            if (c.waitUntil > 0 && Time.time < c.waitUntil) return true;
            if (c.waitRealtime > 0 && Realtime < c.waitRealtime) return true;
            c.waitUntil = c.waitRealtime = 0;
            switch (c.waitingFor)
            {
                case null: return false;
                case WaitUntil u: if (!u.condition()) return true; break;
                case WaitWhile w: if (w.condition()) return true; break;
                case CustomYieldInstruction custom: if (custom.keepWaiting) return true; break;
                case Coroutine other: if (!other.done) return true; break;
                case Task task: if (!task.IsCompleted) return true; break;
                case Http.Request request: if (!request.isDone) return true; break;
                case SceneLoadOperation op: if (!op.isDone) return true; break;
            }
            c.waitingFor = null;
            return false;
        }

        static void Step(Coroutine c, CoroutinePhase phase, bool first)
        {
            if (!first && Waiting(c, phase)) return;
            while (c.stack.Count > 0)
            {
                var routine = c.stack.Peek();
                bool more;
                try { more = routine.MoveNext(); }
                catch (Exception e) { c.error = e; c.done = true; Native.Log(3, $"Coroutine {RoutineName(c)}: {e}"); return; }
                if (!more) { c.stack.Pop(); continue; }
                switch (routine.Current)
                {
                    case null: return; // next frame
                    case WaitForSeconds s: c.waitUntil = Time.time + Math.Max(0, s.seconds); return;
                    case WaitForSecondsRealtime r: c.waitRealtime = Realtime + Math.Max(0, r.seconds); return;
                    case WaitForFixedUpdate: c.waitFixed = true; return;
                    case WaitForEndOfFrame: c.waitEndOfFrame = true; return;
                    case IEnumerator nested: c.stack.Push(nested); continue; // runs inside this coroutine
                    case Coroutine other when other == c: return;
                    default: c.waitingFor = routine.Current; if (Waiting(c, CoroutinePhase.Update)) return; continue;
                }
            }
            c.done = true;
        }

        static void RunTimers()
        {
            if (s_Timers.Count == 0) return;
            var due = new List<Timer>();
            foreach (var t in s_Timers) if (!t.cancelled && (t.realtime ? Realtime : Time.time) >= t.at) due.Add(t);
            foreach (var t in due)
            {
                if (t.cancelled) continue;
                if (!Alive(t.owner)) { t.cancelled = true; continue; }
                try
                {
                    if (t.action != null) t.action();
                    else
                    {
                        var m = t.owner.GetType().GetMethod(t.method, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null, Type.EmptyTypes, null);
                        if (m == null) { Native.Log(2, $"Invoke: {t.owner.GetType().Name} has no method {t.method}()"); t.cancelled = true; continue; }
                        StartFromResult(t.owner, m.Invoke(t.owner, null));
                    }
                }
                catch (TargetInvocationException e) { Native.Log(3, $"Invoke {t.method}: {e.InnerException}"); }
                catch (Exception e) { Native.Log(3, $"Invoke {t.method}: {e}"); }
                if (t.repeat > 0) t.at += t.repeat; else t.cancelled = true;
            }
            s_Timers.RemoveAll(t => t.cancelled);
        }

        internal static void Clear() { s_Running.Clear(); s_Added.Clear(); s_Timers.Clear(); MainThread.Clear(); }
    }

    public abstract partial class ScriptBehaviour
    {
        /// <summary>Runs a coroutine: a method returning IEnumerator that can yield return null (next frame),
        /// new WaitForSeconds(t), WaitUntil(...), another coroutine, a Task or an Http request.</summary>
        public Coroutine StartCoroutine(IEnumerator routine) => Coroutines.Start(this, routine);
        /// <summary>Starts the coroutine method with this name (no parameters).</summary>
        public Coroutine StartCoroutine(string methodName)
        {
            var m = GetType().GetMethod(methodName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null, Type.EmptyTypes, null);
            if (m == null || !typeof(IEnumerator).IsAssignableFrom(m.ReturnType)) throw new ArgumentException($"{GetType().Name} has no coroutine {methodName}()");
            return Coroutines.Start(this, (IEnumerator)m.Invoke(this, null));
        }
        public void StopCoroutine(Coroutine routine) => Coroutines.Stop(routine);
        public void StopCoroutine(string methodName) => Coroutines.StopNamed(this, methodName);
        public void StopAllCoroutines() => Coroutines.StopAll(this);

        /// <summary>Calls the method with this name after `time` seconds.</summary>
        public void Invoke(string methodName, float time) => Coroutines.AddTimer(new Coroutines.Timer { owner = this, method = methodName, at = Time.time + Math.Max(0, time) });
        /// <summary>Calls the method after `time` seconds, then every `repeatRate` seconds.</summary>
        public void InvokeRepeating(string methodName, float time, float repeatRate)
        {
            if (repeatRate <= 0) throw new ArgumentOutOfRangeException(nameof(repeatRate), "repeatRate must be greater than 0");
            Coroutines.AddTimer(new Coroutines.Timer { owner = this, method = methodName, at = Time.time + Math.Max(0, time), repeat = repeatRate });
        }
        /// <summary>Runs an action after `time` seconds (scaled game time).</summary>
        public void Invoke(Action action, float time) => Coroutines.AddTimer(new Coroutines.Timer { owner = this, method = action?.Method.Name, action = action ?? throw new ArgumentNullException(nameof(action)), at = Time.time + Math.Max(0, time) });
        public void CancelInvoke() => Coroutines.CancelTimers(this, null);
        public void CancelInvoke(string methodName) => Coroutines.CancelTimers(this, methodName);
        public bool IsInvoking() => Coroutines.HasTimer(this, null);
        public bool IsInvoking(string methodName) => Coroutines.HasTimer(this, methodName);
    }

    /// <summary>Runs code on the game's main thread (where the engine API may be used). Async/await in scripts
    /// already comes back to the main thread; use this from your own threads.</summary>
    public static class MainThread
    {
        static readonly Queue<(SendOrPostCallback, object)> s_Queue = new();
        static readonly object s_Lock = new();
        static int s_MainThreadId = -1;
        sealed class Context : SynchronizationContext
        {
            public override void Post(SendOrPostCallback d, object state) { lock (s_Lock) s_Queue.Enqueue((d, state)); }
            public override void Send(SendOrPostCallback d, object state) { if (IsMainThread) d(state); else { using var done = new ManualResetEventSlim(); Post(s => { try { d(s); } finally { done.Set(); } }, state); done.Wait(); } }
            public override SynchronizationContext CreateCopy() => this;
        }
        static readonly Context s_Context = new();
        public static bool IsMainThread => System.Environment.CurrentManagedThreadId == s_MainThreadId;
        /// <summary>Queues the action for the next frame on the main thread.</summary>
        public static void Run(Action action) { if (action != null) s_Context.Post(_ => action(), null); }
        // On the main thread: async code in scripts continues there after an await
        internal static void Install()
        {
            if (s_MainThreadId < 0) s_MainThreadId = System.Environment.CurrentManagedThreadId;
            if (SynchronizationContext.Current != s_Context) SynchronizationContext.SetSynchronizationContext(s_Context);
        }
        internal static void Pump()
        {
            Install();
            for (int guard = 0; guard < 10000; ++guard)
            {
                (SendOrPostCallback, object) item;
                lock (s_Lock) { if (s_Queue.Count == 0) return; item = s_Queue.Dequeue(); }
                try { item.Item1(item.Item2); }
                catch (Exception e) { Native.Log(3, "Main thread callback: " + e); }
            }
        }
        internal static void Clear() { lock (s_Lock) s_Queue.Clear(); }
    }
}
