using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json.Nodes;
using P = Stardust.ComponentProperties;

namespace Stardust.UI
{
    public enum ScrollMovement { Unrestricted, Elastic, Clamped }
    public enum ScrollbarVisibility { Hidden, WhenNeeded, Always }
    public enum DropdownDirection { Auto, Down, Up }

    /// <summary>Listeners that receive an int (Dropdown.onValueChanged).</summary>
    public sealed class IntEvent {
        readonly StringEvent source; readonly Dictionary<Action<int>, Action<string>> wrappers = new();
        internal IntEvent(StringEvent source) { this.source = source; }
        internal StringEvent Source => source;
        public void AddListener(Action<int> listener) { if (listener == null || wrappers.ContainsKey(listener)) return; Action<string> w = s => { if (int.TryParse(s, out var v)) listener(v); }; wrappers[listener] = w; source.AddListener(w); }
        public void RemoveListener(Action<int> listener) { if (listener != null && wrappers.Remove(listener, out var w)) source.RemoveListener(w); }
        public void RemoveAllListeners() { foreach (var w in wrappers.Values) source.RemoveListener(w); wrappers.Clear(); }
    }
    /// <summary>Listeners that receive a Vector2 (ScrollView.onValueChanged: the normalized position).</summary>
    public sealed class Vector2Event {
        readonly StringEvent source; readonly Dictionary<Action<Vector2>, Action<string>> wrappers = new();
        internal Vector2Event(StringEvent source) { this.source = source; }
        internal StringEvent Source => source;
        public void AddListener(Action<Vector2> listener) {
            if (listener == null || wrappers.ContainsKey(listener)) return;
            Action<string> w = s => { var parts = s.Split(','); if (parts.Length == 2 && float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var x) && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var y)) listener(new Vector2(x, y)); };
            wrappers[listener] = w; source.AddListener(w);
        }
        public void RemoveListener(Action<Vector2> listener) { if (listener != null && wrappers.Remove(listener, out var w)) source.RemoveListener(w); }
        public void RemoveAllListeners() { foreach (var w in wrappers.Values) source.RemoveListener(w); wrappers.Clear(); }
    }

    static class UIValues {
        internal static Color Color(JsonNode n, Color fallback) => n is JsonArray a && a.Count == 4 ? new Color(a[0].GetValue<float>(), a[1].GetValue<float>(), a[2].GetValue<float>(), a[3].GetValue<float>()) : fallback;
        internal static JsonArray Json(Color c) => new(c.r, c.g, c.b, c.a);
    }

    /// <summary>Scrolls its content (a child, usually "Content") inside its own rectangle, which clips it: wheel, drag with
    /// inertia, elastic or clamped edges, and scroll bars.</summary>
    public sealed class ScrollView : Component {
        const int T = 35;
        JsonObject Data => P.Get(this, T);
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        public bool horizontal { get => P.Value(this, T, "horizontal", false); set => Set("horizontal", value); }
        public bool vertical { get => P.Value(this, T, "vertical", true); set => Set("vertical", value); }
        public ScrollMovement movementType { get => (ScrollMovement)P.Value(this, T, "movementType", 1); set => Set("movementType", (int)value); }
        /// <summary>Seconds to spring back past an edge (Elastic).</summary>
        public float elasticity { get => P.Value(this, T, "elasticity", 0.1f); set => Set("elasticity", value); }
        public bool inertia { get => P.Value(this, T, "inertia", true); set => Set("inertia", value); }
        /// <summary>Share of the speed kept after one second (0..1).</summary>
        public float decelerationRate { get => P.Value(this, T, "decelerationRate", 0.135f); set => Set("decelerationRate", value); }
        public float scrollSensitivity { get => P.Value(this, T, "scrollSensitivity", 40f); set => Set("scrollSensitivity", value); }
        public bool dragToScroll { get => P.Value(this, T, "dragToScroll", true); set => Set("dragToScroll", value); }
        public bool clipContent { get => P.Value(this, T, "clipContent", true); set => Set("clipContent", value); }
        /// <summary>The scrolled object; null = the first child.</summary>
        public Entity content { get { var id = ulong.Parse(P.Value(this, T, "content", "0")); return id == 0 ? null : new Entity(id); } set => Set("content", (value?.ID ?? 0).ToString()); }
        public ScrollbarVisibility scrollbars { get => (ScrollbarVisibility)P.Value(this, T, "scrollbars", 1); set => Set("scrollbars", (int)value); }
        public float scrollbarWidth { get => P.Value(this, T, "scrollbarWidth", 10f); set => Set("scrollbarWidth", value); }
        public Color trackColor { get => UIValues.Color(Data["trackColor"], new Color(0, 0, 0, .18f)); set => Set("trackColor", UIValues.Json(value)); }
        public Color handleColor { get => UIValues.Color(Data["handleColor"], new Color(.55f, .58f, .65f, .9f)); set => Set("handleColor", UIValues.Json(value)); }
        public Color handleHoverColor { get => UIValues.Color(Data["handleHoverColor"], Color.white); set => Set("handleHoverColor", UIValues.Json(value)); }
        public string trackSprite { get => P.Value(this, T, "trackSprite", ""); set => Set("trackSprite", value ?? ""); }
        public string handleSprite { get => P.Value(this, T, "handleSprite", ""); set => Set("handleSprite", value ?? ""); }
        /// <summary>x: 0 left .. 1 right; y: 1 top .. 0 bottom. Setting it moves the content on the next frame.</summary>
        public Vector2 normalizedPosition {
            get { var a = Data["normalized"]?.AsArray(); return a == null ? new Vector2(0, 1) : new Vector2(a[0].GetValue<float>(), a[1].GetValue<float>()); }
            set => Set("normalized", new JsonArray(value.x, value.y));
        }
        public float horizontalNormalizedPosition { get => normalizedPosition.x; set => Set("normalized", new JsonArray(value, -1f)); }
        public float verticalNormalizedPosition { get => normalizedPosition.y; set => Set("normalized", new JsonArray(-1f, value)); }
        public void ScrollToTop() => verticalNormalizedPosition = 1;
        public void ScrollToBottom() => verticalNormalizedPosition = 0;
        public Vector2 velocity {
            get { var a = Data["velocity"]?.AsArray(); return a == null ? new Vector2(0, 0) : new Vector2(a[0].GetValue<float>(), a[1].GetValue<float>()); }
            set => Set("velocity", new JsonArray(value.x, value.y));
        }
        public void StopMovement() => velocity = new Vector2(0, 0);
        /// <summary>The normalized position, whenever it changes.</summary>
        static readonly Dictionary<ulong, Vector2Event> s_Events = new();
        public Vector2Event onValueChanged { get { var source = InputEvents.For(Entity.ID, false); if (!s_Events.TryGetValue(Entity.ID, out var e) || e.Source != source) s_Events[Entity.ID] = e = new(source); return e; } }
    }

    public struct DropdownOption {
        public string text; public string sprite;
        public DropdownOption(string text, string sprite = "") { this.text = text; this.sprite = sprite; }
        public static implicit operator DropdownOption(string text) => new(text);
    }

    /// <summary>Picks one option from a list that opens over the canvas. The object's Image is the button; caption,
    /// arrow, list and items are drawn with the colours and sprites set here.</summary>
    public sealed class Dropdown : Component {
        const int T = 36;
        JsonObject Data => P.Get(this, T);
        void Set(string name, JsonNode value) => P.Property(this, T, name, value);
        Color C(string name, Color fallback) => UIValues.Color(Data[name], fallback);

        /// <summary>Index of the chosen option (-1 none). Setting it notifies onValueChanged.</summary>
        public int value { get => P.Value(this, T, "value", 0); set { int previous = this.value; Set("value", value); if (previous != value) InputEvents.Fire(Entity.ID, value.ToString(), false); } }
        public void SetValueWithoutNotify(int index) => Set("value", index);
        static readonly Dictionary<ulong, IntEvent> s_Events = new();
        public IntEvent onValueChanged { get { var source = InputEvents.For(Entity.ID, false); if (!s_Events.TryGetValue(Entity.ID, out var e) || e.Source != source) s_Events[Entity.ID] = e = new(source); return e; } }
        public string captionText { get { var o = options; int v = value; return v >= 0 && v < o.Length ? o[v].text : ""; } }

        public DropdownOption[] options {
            get => Data["options"]?.AsArray().Select(o => new DropdownOption(o?["text"]?.GetValue<string>() ?? "", o?["sprite"]?.GetValue<string>() ?? "")).ToArray() ?? Array.Empty<DropdownOption>();
            set => Set("options", new JsonArray((value ?? Array.Empty<DropdownOption>()).Select(o => (JsonNode)new JsonObject { ["text"] = o.text ?? "", ["sprite"] = o.sprite ?? "" }).ToArray()));
        }
        public void AddOptions(params string[] texts) => options = options.Concat(texts.Select(t => new DropdownOption(t))).ToArray();
        public void AddOptions(IEnumerable<DropdownOption> list) => options = options.Concat(list).ToArray();
        public void ClearOptions() => options = Array.Empty<DropdownOption>();

        public bool interactable { get => P.Value(this, T, "interactable", true); set => Set("interactable", value); }
        public bool isOpen => P.Value(this, T, "open", false);
        public void Show() => Set("open", true);
        public void Hide() => Set("open", false);

        // Caption and button
        public string font { get => P.Value(this, T, "font", ""); set => Set("font", value ?? ""); }
        public float fontSize { get => P.Value(this, T, "fontSize", 22f); set => Set("fontSize", value); }
        public Color textColor { get => C("textColor", new Color(.12f, .12f, .14f)); set => Set("textColor", UIValues.Json(value)); }
        public string placeholder { get => P.Value(this, T, "placeholder", ""); set => Set("placeholder", value ?? ""); }
        public Color hoverTint { get => C("hoverTint", Color.white); set => Set("hoverTint", UIValues.Json(value)); }
        public Color pressedTint { get => C("pressedTint", Color.white); set => Set("pressedTint", UIValues.Json(value)); }
        public Color disabledTint { get => C("disabledTint", Color.white); set => Set("disabledTint", UIValues.Json(value)); }
        public Color arrowColor { get => C("arrowColor", Color.white); set => Set("arrowColor", UIValues.Json(value)); }
        public string arrowSprite { get => P.Value(this, T, "arrowSprite", ""); set => Set("arrowSprite", value ?? ""); }
        // List
        public DropdownDirection direction { get => (DropdownDirection)P.Value(this, T, "direction", 0); set => Set("direction", (int)value); }
        public float itemHeight { get => P.Value(this, T, "itemHeight", 36f); set => Set("itemHeight", value); }
        public float maxListHeight { get => P.Value(this, T, "maxListHeight", 260f); set => Set("maxListHeight", value); }
        public Color listColor { get => C("listColor", Color.white); set => Set("listColor", UIValues.Json(value)); }
        public string listSprite { get => P.Value(this, T, "listSprite", ""); set => Set("listSprite", value ?? ""); }
        public Color itemColor { get => C("itemColor", new Color(0, 0, 0, 0)); set => Set("itemColor", UIValues.Json(value)); }
        public Color itemHoverColor { get => C("itemHoverColor", Color.white); set => Set("itemHoverColor", UIValues.Json(value)); }
        public Color itemSelectedColor { get => C("itemSelectedColor", Color.white); set => Set("itemSelectedColor", UIValues.Json(value)); }
        public string itemSprite { get => P.Value(this, T, "itemSprite", ""); set => Set("itemSprite", value ?? ""); }
        public Color itemTextColor { get => C("itemTextColor", new Color(.12f, .12f, .14f)); set => Set("itemTextColor", UIValues.Json(value)); }
        public bool showCheckmark { get => P.Value(this, T, "showCheckmark", true); set => Set("showCheckmark", value); }
        public Color checkmarkColor { get => C("checkmarkColor", Color.white); set => Set("checkmarkColor", UIValues.Json(value)); }
    }
}
