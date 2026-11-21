using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;

namespace Stardust.InputSystem
{
    public enum InputActionType { Button, Value, PassThrough }
    public enum InputActionPhase { Disabled, Waiting, Started, Performed, Canceled }

    /// <summary>What a callback receives: the action, its phase and value.</summary>
    public readonly struct CallbackContext
    {
        public InputAction action { get; }
        public InputActionPhase phase { get; }
        public double time { get; }
        /// <summary>Seconds since the action started (Hold interactions).</summary>
        public double duration { get; }
        public bool started => phase == InputActionPhase.Started;
        public bool performed => phase == InputActionPhase.Performed;
        public bool canceled => phase == InputActionPhase.Canceled;
        internal CallbackContext(InputAction action, InputActionPhase phase, double time, double duration) { this.action = action; this.phase = phase; this.time = time; this.duration = duration; }
        public T ReadValue<T>() => action.ReadValue<T>();
        public bool ReadValueAsButton() => action.IsPressed();
    }

    public sealed class InputBinding
    {
        /// <summary>"&lt;Keyboard&gt;/space", "&lt;Mouse&gt;/leftButton", "&lt;Gamepad&gt;/leftStick"...; for composites, the parts.</summary>
        public string path;
        public string composite;                 // "", "2DVector", "1DAxis", "ButtonWithModifier"
        public Dictionary<string, string> parts = new(StringComparer.OrdinalIgnoreCase);
        public float scale = 1;
        public bool invert;
        public string overridePath;              // set by rebinding (single bindings)
        public Dictionary<string, string> overrideParts = new(StringComparer.OrdinalIgnoreCase);
        public string effectivePath => string.IsNullOrEmpty(overridePath) ? path : overridePath;
        public string Part(string name) => overrideParts.TryGetValue(name, out var o) && !string.IsNullOrEmpty(o) ? o : parts.TryGetValue(name, out var p) ? p : "";
        public bool isComposite => !string.IsNullOrEmpty(composite);
        /// <summary>Readable text, e.g. "Space" or "W/A/S/D".</summary>
        public string ToDisplayString()
        {
            if (!isComposite) return Controls.Display(effectivePath);
            if (composite == "2DVector") return string.Join("/", new[] { "up", "left", "down", "right" }.Select(p => Controls.Display(Part(p))));
            if (composite == "1DAxis") return Controls.Display(Part("negative")) + "/" + Controls.Display(Part("positive"));
            return Controls.Display(Part("modifier")) + "+" + Controls.Display(Part("button"));
        }
    }

    /// <summary>
    /// An action ("Jump", "Move") bound to keys, mouse, gamepad or composites. Read it every frame with
    /// ReadValue / IsPressed / WasPressedThisFrame, or listen to started / performed / canceled.
    /// </summary>
    public sealed class InputAction
    {
        public string name { get; }
        public InputActionType type { get; set; }
        /// <summary>"Axis", "Vector2" or "Button".</summary>
        public string expectedControlType { get; set; } = "Button";
        public string interaction { get; set; } = "";
        public float holdTime = 0.4f, tapTime = 0.2f; public int tapCount = 2;
        public InputActionMap actionMap { get; internal set; }
        public List<InputBinding> bindings { get; } = new();
        public event Action<CallbackContext> started, performed, canceled;
        public InputActionPhase phase { get; private set; } = InputActionPhase.Disabled;
        public bool enabled => phase != InputActionPhase.Disabled;

        Vector2 value; bool pressed, pressedThisFrame, releasedThisFrame, performedThisFrame;
        double startTime; int taps; double lastTap;

        public InputAction(string name, InputActionType type = InputActionType.Button, string binding = null, string interactions = null)
        {
            this.name = name; this.type = type;
            if (type == InputActionType.Value) expectedControlType = "Axis";
            if (!string.IsNullOrEmpty(binding)) AddBinding(binding);
            interaction = interactions ?? "";
        }
        public InputBinding AddBinding(string path) { var b = new InputBinding { path = path }; bindings.Add(b); return b; }
        public InputBinding AddCompositeBinding(string composite, params (string part, string path)[] parts)
        {
            var b = new InputBinding { composite = composite };
            foreach (var (part, path) in parts) b.parts[part] = path;
            bindings.Add(b); return b;
        }

        public void Enable() { if (phase == InputActionPhase.Disabled) { phase = InputActionPhase.Waiting; InputSystem.Register(this); } }
        public void Disable() { if (phase == InputActionPhase.Disabled) return; if (phase == InputActionPhase.Started || phase == InputActionPhase.Performed) Fire(InputActionPhase.Canceled); phase = InputActionPhase.Disabled; value = default; pressed = false; InputSystem.Unregister(this); }

        public T ReadValue<T>()
        {
            object v = typeof(T) == typeof(Vector2) ? value : typeof(T) == typeof(float) ? (object)(expectedControlType == "Vector2" ? value.magnitude : value.x)
                     : typeof(T) == typeof(bool) ? (object)pressed : typeof(T) == typeof(Vector3) ? (object)new Vector3(value.x, value.y, 0) : default(T);
            return (T)v;
        }
        public bool IsPressed() => pressed;
        public bool WasPressedThisFrame() => pressedThisFrame;
        public bool WasReleasedThisFrame() => releasedThisFrame;
        public bool WasPerformedThisFrame() => performedThisFrame;
        public bool triggered => performedThisFrame;

        void Fire(InputActionPhase p)
        {
            phase = p == InputActionPhase.Canceled ? InputActionPhase.Waiting : p;
            var ctx = new CallbackContext(this, p, Time.time, Time.time - startTime);
            try { (p == InputActionPhase.Started ? started : p == InputActionPhase.Performed ? performed : canceled)?.Invoke(ctx); }
            catch (Exception e) { Debug.LogError($"Input action {name}: {e}"); }
        }

        internal void Update()
        {
            pressedThisFrame = releasedThisFrame = performedThisFrame = false;
            if (phase == InputActionPhase.Disabled) return;
            // Strongest of all bindings
            Vector2 v = default; bool down = false;
            foreach (var b in bindings)
            {
                var bv = Controls.Read(b, expectedControlType == "Vector2");
                if (b.invert) bv = -bv;
                bv *= b.scale;
                if (bv.magnitude > v.magnitude) v = bv;
            }
            down = v.magnitude > InputSystem.pressPoint;
            bool was = pressed;
            pressed = down; value = v;
            if (down && !was) pressedThisFrame = true;
            if (!down && was) releasedThisFrame = true;
            double now = Time.time;
            switch (interaction)
            {
                case "Hold":
                    if (pressedThisFrame) { startTime = now; Fire(InputActionPhase.Started); }
                    if (down && phase == InputActionPhase.Started && now - startTime >= holdTime) { performedThisFrame = true; Fire(InputActionPhase.Performed); }
                    if (releasedThisFrame) Fire(InputActionPhase.Canceled);
                    return;
                case "Tap":
                    if (pressedThisFrame) { startTime = now; Fire(InputActionPhase.Started); }
                    if (down && phase == InputActionPhase.Started && now - startTime > tapTime) Fire(InputActionPhase.Canceled);
                    if (releasedThisFrame && phase == InputActionPhase.Started) { performedThisFrame = true; Fire(InputActionPhase.Performed); phase = InputActionPhase.Waiting; }
                    return;
                case "MultiTap":
                    if (pressedThisFrame)
                    {
                        if (taps == 0 || now - lastTap > tapTime * 2) { taps = 0; startTime = now; Fire(InputActionPhase.Started); }
                        lastTap = now; ++taps;
                        if (taps >= tapCount) { taps = 0; performedThisFrame = true; Fire(InputActionPhase.Performed); phase = InputActionPhase.Waiting; }
                    }
                    if (taps > 0 && now - lastTap > tapTime * 2) { taps = 0; Fire(InputActionPhase.Canceled); }
                    return;
            }
            if (type == InputActionType.Button)
            {
                if (pressedThisFrame) { startTime = now; Fire(InputActionPhase.Started); performedThisFrame = true; Fire(InputActionPhase.Performed); }
                if (releasedThisFrame) Fire(InputActionPhase.Canceled);
            }
            else if (type == InputActionType.PassThrough)
            {
                if (!Same(v, lastValue)) { performedThisFrame = true; Fire(InputActionPhase.Performed); }
            }
            else
            {
                // Value: started when it leaves 0, performed every time it changes, canceled back at 0
                bool active = v.magnitude > 1e-4f;
                if (active && phase == InputActionPhase.Waiting) { startTime = now; Fire(InputActionPhase.Started); }
                if (active && !Same(v, lastValue)) { performedThisFrame = true; Fire(InputActionPhase.Performed); }
                if (!active && (phase == InputActionPhase.Started || phase == InputActionPhase.Performed)) Fire(InputActionPhase.Canceled);
            }
            lastValue = v;
        }
        Vector2 lastValue;
        static bool Same(Vector2 a, Vector2 b) => Math.Abs(a.x - b.x) < 1e-5f && Math.Abs(a.y - b.y) < 1e-5f;

        // ------------------------------------------------------------ rebinding
        /// <summary>Changes a binding at run time (for a composite, pass the part: "up", "left"...).</summary>
        public void ApplyBindingOverride(int bindingIndex, string path, string part = null)
        {
            var b = bindings[bindingIndex];
            if (part != null) b.overrideParts[part] = path; else b.overridePath = path;
        }
        public void RemoveAllBindingOverrides() { foreach (var b in bindings) { b.overridePath = null; b.overrideParts.Clear(); } }
        /// <summary>Waits for the player to press a key / button and binds it. Escape cancels.</summary>
        public RebindingOperation PerformInteractiveRebinding(int bindingIndex = 0, string part = null) => new RebindingOperation(this, bindingIndex, part);
        public string GetBindingDisplayString(int bindingIndex = -1) => bindingIndex >= 0 ? bindings[bindingIndex].ToDisplayString() : string.Join(" | ", bindings.Select(b => b.ToDisplayString()));
    }

    public sealed class RebindingOperation
    {
        readonly InputAction action; readonly int index; readonly string part;
        Action<RebindingOperation> onComplete, onCancel;
        string cancelPath = "<Keyboard>/escape";
        bool wasEnabled;
        public bool completed { get; private set; }
        public bool canceled { get; private set; }
        public string selectedPath { get; private set; }
        internal RebindingOperation(InputAction action, int index, string part) { this.action = action; this.index = index; this.part = part; }
        public RebindingOperation OnComplete(Action<RebindingOperation> callback) { onComplete = callback; return this; }
        public RebindingOperation OnCancel(Action<RebindingOperation> callback) { onCancel = callback; return this; }
        public RebindingOperation WithCancelingThrough(string path) { cancelPath = path; return this; }
        public RebindingOperation Start() { wasEnabled = action.enabled; action.Disable(); InputSystem.StartRebind(this); return this; }
        public void Cancel() { if (completed || canceled) return; canceled = true; Finish(); onCancel?.Invoke(this); }
        internal bool Poll()
        {
            var path = Controls.AnyPressed();
            if (path == null) return false;
            if (path == cancelPath) { Cancel(); return true; }
            selectedPath = path; action.ApplyBindingOverride(index, path, part);
            completed = true; Finish(); onComplete?.Invoke(this);
            return true;
        }
        void Finish() { if (wasEnabled) action.Enable(); }
        public void Dispose() { }
    }

    public sealed class InputActionMap
    {
        public string name { get; }
        public List<InputAction> actions { get; } = new();
        public InputActionAsset asset { get; internal set; }
        public InputActionMap(string name) { this.name = name; }
        public bool enabled => actions.Count > 0 && actions.All(a => a.enabled);
        public void Enable() { foreach (var a in actions) a.Enable(); }
        public void Disable() { foreach (var a in actions) a.Disable(); }
        public InputAction FindAction(string actionName) => actions.FirstOrDefault(a => string.Equals(a.name, actionName, StringComparison.OrdinalIgnoreCase));
        public InputAction this[string actionName] => FindAction(actionName) ?? throw new KeyNotFoundException($"No action '{actionName}' in map '{name}'");
        public InputAction AddAction(string actionName, InputActionType type = InputActionType.Button, string binding = null)
        {
            var a = new InputAction(actionName, type, binding) { actionMap = this };
            actions.Add(a); return a;
        }
    }

    /// <summary>Action maps loaded from a JSON file (Project Settings > Input Actions saves Settings/InputActions.json).</summary>
    public sealed class InputActionAsset
    {
        public List<InputActionMap> actionMaps { get; } = new();
        public InputActionMap FindActionMap(string mapName) => actionMaps.FirstOrDefault(m => string.Equals(m.name, mapName, StringComparison.OrdinalIgnoreCase));
        /// <summary>"Map/Action", or just "Action" (first map that has it).</summary>
        public InputAction FindAction(string path)
        {
            int slash = path.IndexOf('/');
            if (slash > 0) return FindActionMap(path[..slash])?.FindAction(path[(slash + 1)..]);
            foreach (var m in actionMaps) if (m.FindAction(path) is { } a) return a;
            return null;
        }
        public InputAction this[string path] => FindAction(path) ?? throw new KeyNotFoundException($"No input action '{path}'");
        public void Enable() { foreach (var m in actionMaps) m.Enable(); }
        public void Disable() { foreach (var m in actionMaps) m.Disable(); }

        public static InputActionAsset FromJson(string json)
        {
            var asset = new InputActionAsset();
            var root = JsonNode.Parse(json);
            if (root?["maps"] is not JsonArray maps) return asset;
            foreach (var m in maps)
            {
                var map = new InputActionMap(m?["name"]?.GetValue<string>() ?? "Map") { asset = asset };
                if (m?["actions"] is JsonArray actions)
                    foreach (var a in actions)
                    {
                        var type = Enum.TryParse<InputActionType>(a?["type"]?.GetValue<string>() ?? "Button", true, out var t) ? t : InputActionType.Button;
                        var action = new InputAction(a?["name"]?.GetValue<string>() ?? "Action", type) { actionMap = map };
                        action.expectedControlType = a?["valueType"]?.GetValue<string>() ?? (type == InputActionType.Button ? "Button" : "Axis");
                        action.interaction = a?["interaction"]?.GetValue<string>() ?? "";
                        action.holdTime = a?["holdTime"]?.GetValue<float>() ?? .4f; action.tapTime = a?["tapTime"]?.GetValue<float>() ?? .2f; action.tapCount = a?["tapCount"]?.GetValue<int>() ?? 2;
                        if (a?["bindings"] is JsonArray bindings)
                            foreach (var b in bindings)
                            {
                                if (b is not JsonObject o) continue;
                                var binding = new InputBinding { path = o["path"]?.GetValue<string>() ?? "", composite = o["composite"]?.GetValue<string>() ?? "", scale = o["scale"]?.GetValue<float>() ?? 1, invert = o["invert"]?.GetValue<bool>() ?? false };
                                foreach (var kv in o) if (kv.Key is not ("path" or "composite" or "scale" or "invert") && kv.Value is JsonValue v && v.TryGetValue<string>(out var s)) binding.parts[kv.Key] = s;
                                action.bindings.Add(binding);
                            }
                        map.actions.Add(action);
                    }
                asset.actionMaps.Add(map);
            }
            return asset;
        }
        public static InputActionAsset Load(string assetPath = "Settings/InputActions.json") => Assets.Exists(assetPath) ? FromJson(Assets.ReadAllText(assetPath)) : new InputActionAsset();

        /// <summary>The rebinds the player made, to save (PlayerPrefs, a file) and load next time.</summary>
        public string SaveBindingOverridesAsJson()
        {
            var list = new JsonArray();
            foreach (var m in actionMaps) foreach (var a in m.actions) for (int i = 0; i < a.bindings.Count; ++i)
            {
                var b = a.bindings[i];
                if (string.IsNullOrEmpty(b.overridePath) && b.overrideParts.Count == 0) continue;
                var parts = new JsonObject(); foreach (var p in b.overrideParts) parts[p.Key] = p.Value;
                list.Add(new JsonObject { ["action"] = m.name + "/" + a.name, ["index"] = i, ["path"] = b.overridePath ?? "", ["parts"] = parts });
            }
            return list.ToJsonString();
        }
        public void LoadBindingOverridesFromJson(string json)
        {
            if (string.IsNullOrWhiteSpace(json) || JsonNode.Parse(json) is not JsonArray list) return;
            foreach (var o in list)
            {
                var a = FindAction(o?["action"]?.GetValue<string>() ?? ""); int i = o?["index"]?.GetValue<int>() ?? -1;
                if (a == null || i < 0 || i >= a.bindings.Count) continue;
                var p = o["path"]?.GetValue<string>(); if (!string.IsNullOrEmpty(p)) a.bindings[i].overridePath = p;
                if (o["parts"] is JsonObject parts) foreach (var kv in parts) a.bindings[i].overrideParts[kv.Key] = kv.Value?.GetValue<string>() ?? "";
            }
        }
        public void RemoveAllBindingOverrides() { foreach (var m in actionMaps) foreach (var a in m.actions) a.RemoveAllBindingOverrides(); }
    }

    /// <summary>The project's input actions (Settings/InputActions.json), enabled when play starts.</summary>
    public static class InputSystem
    {
        static InputActionAsset s_Actions;
        static readonly List<InputAction> s_Enabled = new();
        static RebindingOperation s_Rebind;
        /// <summary>How far an analog control must move to count as pressed.</summary>
        public static float pressPoint = 0.5f;
        public static InputActionAsset actions
        {
            get { if (s_Actions == null) { s_Actions = InputActionAsset.Load(); s_Actions.Enable(); } return s_Actions; }
            set { s_Actions?.Disable(); s_Actions = value; }
        }
        internal static void Register(InputAction a) { if (!s_Enabled.Contains(a)) s_Enabled.Add(a); }
        internal static void Unregister(InputAction a) => s_Enabled.Remove(a);
        internal static void StartRebind(RebindingOperation op) { s_Rebind?.Cancel(); s_Rebind = op; }
        internal static void Update()
        {
            if (s_Rebind != null && s_Rebind.Poll()) s_Rebind = null;
            foreach (var a in s_Enabled.ToArray()) a.Update();
        }
        internal static void Reset() { s_Enabled.Clear(); s_Actions = null; s_Rebind = null; }
    }

    // Reading controls from their paths
    internal static class Controls
    {
        static readonly Dictionary<string, KeyCode> s_Keys = BuildKeys();
        static Dictionary<string, KeyCode> BuildKeys()
        {
            var d = new Dictionary<string, KeyCode>(StringComparer.OrdinalIgnoreCase);
            foreach (KeyCode k in Enum.GetValues(typeof(KeyCode))) d[k.ToString()] = k;
            for (int i = 0; i <= 9; ++i) d[i.ToString()] = KeyCode.Alpha0 + i;
            d["leftCtrl"] = KeyCode.LeftControl; d["rightCtrl"] = KeyCode.RightControl; d["ctrl"] = KeyCode.LeftControl; d["shift"] = KeyCode.LeftShift; d["alt"] = KeyCode.LeftAlt;
            d["leftMeta"] = KeyCode.LeftSuper; d["return"] = KeyCode.Enter; d["esc"] = KeyCode.Escape; d["quote"] = KeyCode.Apostrophe; d["up"] = KeyCode.UpArrow; d["down"] = KeyCode.DownArrow; d["left"] = KeyCode.LeftArrow; d["right"] = KeyCode.RightArrow;
            return d;
        }
        static readonly Dictionary<string, GamepadButton> s_Buttons = new(StringComparer.OrdinalIgnoreCase)
        {
            ["buttonSouth"] = GamepadButton.South, ["buttonEast"] = GamepadButton.East, ["buttonWest"] = GamepadButton.West, ["buttonNorth"] = GamepadButton.North,
            ["a"] = GamepadButton.South, ["b"] = GamepadButton.East, ["x"] = GamepadButton.West, ["y"] = GamepadButton.North,
            ["leftShoulder"] = GamepadButton.LeftBumper, ["rightShoulder"] = GamepadButton.RightBumper, ["select"] = GamepadButton.Back, ["start"] = GamepadButton.Start,
            ["leftStickPress"] = GamepadButton.LeftStick, ["rightStickPress"] = GamepadButton.RightStick,
            ["dpad/up"] = GamepadButton.DpadUp, ["dpad/down"] = GamepadButton.DpadDown, ["dpad/left"] = GamepadButton.DpadLeft, ["dpad/right"] = GamepadButton.DpadRight,
        };

        // One control as a vector (x for one-dimensional controls)
        internal static Vector2 ReadPath(string path)
        {
            if (string.IsNullOrEmpty(path)) return default;
            int close = path.IndexOf('>');
            if (!path.StartsWith("<") || close < 0) return default;
            string device = path[1..close], control = path[(close + 1)..].TrimStart('/');
            switch (device.ToLowerInvariant())
            {
                case "keyboard": return s_Keys.TryGetValue(control, out var key) && Input.GetKey(key) ? new Vector2(1, 0) : default;
                case "mouse":
                    switch (control.ToLowerInvariant())
                    {
                        case "leftbutton": return Input.GetMouseButton(MouseButton.Left) ? new Vector2(1, 0) : default;
                        case "rightbutton": return Input.GetMouseButton(MouseButton.Right) ? new Vector2(1, 0) : default;
                        case "middlebutton": return Input.GetMouseButton(MouseButton.Middle) ? new Vector2(1, 0) : default;
                        case "delta": { var d = Input.mouseDelta; return new Vector2(d.x, -d.y); }
                        case "position": return Input.mousePosition;
                        case "scroll": return new Vector2(0, Input.mouseScroll);
                    }
                    return default;
                case "gamepad":
                    if (s_Buttons.TryGetValue(control, out var button)) return Input.GetGamepadButton(button) ? new Vector2(1, 0) : default;
                    switch (control.ToLowerInvariant())
                    {
                        case "leftstick": return new Vector2(Input.GetGamepadAxis(GamepadAxis.LeftX), -Input.GetGamepadAxis(GamepadAxis.LeftY));
                        case "rightstick": return new Vector2(Input.GetGamepadAxis(GamepadAxis.RightX), -Input.GetGamepadAxis(GamepadAxis.RightY));
                        case "leftstick/x": return new Vector2(Input.GetGamepadAxis(GamepadAxis.LeftX), 0);
                        case "leftstick/y": return new Vector2(-Input.GetGamepadAxis(GamepadAxis.LeftY), 0);
                        case "rightstick/x": return new Vector2(Input.GetGamepadAxis(GamepadAxis.RightX), 0);
                        case "rightstick/y": return new Vector2(-Input.GetGamepadAxis(GamepadAxis.RightY), 0);
                        case "lefttrigger": return new Vector2(Input.GetGamepadAxis(GamepadAxis.LeftTrigger), 0);
                        case "righttrigger": return new Vector2(Input.GetGamepadAxis(GamepadAxis.RightTrigger), 0);
                        case "dpad":
                            return new Vector2((Input.GetGamepadButton(GamepadButton.DpadRight) ? 1 : 0) - (Input.GetGamepadButton(GamepadButton.DpadLeft) ? 1 : 0),
                                               (Input.GetGamepadButton(GamepadButton.DpadUp) ? 1 : 0) - (Input.GetGamepadButton(GamepadButton.DpadDown) ? 1 : 0));
                    }
                    return default;
            }
            return default;
        }
        static float Button(string path) { var v = ReadPath(path); return Math.Max(Math.Abs(v.x), Math.Abs(v.y)); }

        internal static Vector2 Read(InputBinding b, bool vector)
        {
            switch (b.composite)
            {
                case "2DVector":
                {
                    var v = new Vector2(Button(b.Part("right")) - Button(b.Part("left")), Button(b.Part("up")) - Button(b.Part("down")));
                    return v.magnitude > 1 ? v.normalized : v;
                }
                case "1DAxis": return new Vector2(Button(b.Part("positive")) - Button(b.Part("negative")), 0);
                case "ButtonWithModifier": return Button(b.Part("modifier")) > 0.5f ? new Vector2(Button(b.Part("button")), 0) : default;
            }
            var value = ReadPath(b.effectivePath);
            return vector ? value : value;
        }

        // Rebinding: the first key / button / trigger pressed this frame
        internal static string AnyPressed()
        {
            foreach (KeyCode k in Enum.GetValues(typeof(KeyCode))) if (Input.GetKeyDown(k)) return "<Keyboard>/" + KeyName(k);
            if (Input.GetMouseButtonDown(MouseButton.Left)) return "<Mouse>/leftButton";
            if (Input.GetMouseButtonDown(MouseButton.Right)) return "<Mouse>/rightButton";
            if (Input.GetMouseButtonDown(MouseButton.Middle)) return "<Mouse>/middleButton";
            foreach (var kv in s_Buttons) if (kv.Key.Length > 1 && Input.GetGamepadButtonDown(kv.Value)) return "<Gamepad>/" + kv.Key;
            if (Input.GetGamepadAxis(GamepadAxis.LeftTrigger) > .8f) return "<Gamepad>/leftTrigger";
            if (Input.GetGamepadAxis(GamepadAxis.RightTrigger) > .8f) return "<Gamepad>/rightTrigger";
            return null;
        }
        static string KeyName(KeyCode k) => k switch
        {
            >= KeyCode.Alpha0 and <= KeyCode.Alpha9 => ((int)(k - KeyCode.Alpha0)).ToString(),
            KeyCode.LeftControl => "leftCtrl", KeyCode.RightControl => "rightCtrl",
            _ => char.ToLowerInvariant(k.ToString()[0]) + k.ToString()[1..],
        };
        internal static string Display(string path)
        {
            if (string.IsNullOrEmpty(path)) return "-";
            int close = path.IndexOf('>');
            var control = close >= 0 ? path[(close + 1)..].TrimStart('/') : path;
            return control.Length == 1 ? control.ToUpperInvariant() : char.ToUpperInvariant(control[0]) + control[1..];
        }
    }
}
