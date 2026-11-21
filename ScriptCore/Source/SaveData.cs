using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Stardust
{
    /// <summary>
    /// Simple persistent storage: values by key (settings, best score, unlocked levels) and whole objects
    /// in named slots (save games). Files live in Application.GetPersistentDataPath()/SaveData.
    /// <code>
    /// SaveData.SetInt("bestScore", 1200);
    /// int best = SaveData.GetInt("bestScore");
    /// SaveData.Save("slot1", new GameState { level = 3, position = player.transform.position });
    /// var state = SaveData.Load&lt;GameState&gt;("slot1");
    /// </code>
    /// Values are written to disk by Save() and automatically when play stops.
    /// </summary>
    public static class SaveData
    {
        static SaveData() { AppDomain.CurrentDomain.ProcessExit += (_, _) => Flush(); }
        static JsonObject s_Values;
        static bool s_Dirty;
        static readonly JsonSerializerOptions s_Options = new() { WriteIndented = true, IncludeFields = true, PropertyNameCaseInsensitive = true };

        /// <summary>The folder with the files.</summary>
        public static string folder
        {
            get
            {
                var root = Application.GetPersistentDataPath();
                if (string.IsNullOrEmpty(root)) root = Path.Combine(System.Environment.GetFolderPath(System.Environment.SpecialFolder.ApplicationData), "Stardust", Application.GetProductName());
                return Path.Combine(root, "SaveData");
            }
        }
        static string ValuesFile => Path.Combine(folder, "values.json");
        static string SlotFile(string slot)
        {
            if (string.IsNullOrWhiteSpace(slot)) throw new ArgumentException("Slot name is empty", nameof(slot));
            foreach (var c in Path.GetInvalidFileNameChars()) slot = slot.Replace(c, '_');
            return Path.Combine(folder, slot + ".json");
        }

        static JsonObject Values
        {
            get
            {
                if (s_Values != null) return s_Values;
                try { s_Values = File.Exists(ValuesFile) ? JsonNode.Parse(File.ReadAllText(ValuesFile)) as JsonObject : null; }
                catch (Exception e) { Debug.LogWarning("SaveData: values unreadable, starting empty (" + e.Message + ")"); }
                return s_Values ??= new JsonObject();
            }
        }
        static void Put(string key, JsonNode value) { if (string.IsNullOrEmpty(key)) throw new ArgumentException("Key is empty", nameof(key)); Values[key] = value; s_Dirty = true; }

        public static void SetInt(string key, int value) => Put(key, value);
        public static void SetFloat(string key, float value) => Put(key, value);
        public static void SetBool(string key, bool value) => Put(key, value);
        public static void SetString(string key, string value) => Put(key, value ?? "");
        public static int GetInt(string key, int fallback = 0) { try { return Values[key] is JsonValue v ? v.GetValue<int>() : fallback; } catch { return fallback; } }
        public static float GetFloat(string key, float fallback = 0) { try { return Values[key] is JsonValue v ? v.GetValue<float>() : fallback; } catch { return fallback; } }
        public static bool GetBool(string key, bool fallback = false) { try { return Values[key] is JsonValue v ? v.GetValue<bool>() : fallback; } catch { return fallback; } }
        public static string GetString(string key, string fallback = "") { try { return Values[key] is JsonValue v ? v.GetValue<string>() : fallback; } catch { return fallback; } }
        /// <summary>Any object (System.Text.Json), stored under a key with the other values.</summary>
        public static void Set<T>(string key, T value) => Put(key, JsonSerializer.SerializeToNode(value, s_Options));
        public static T Get<T>(string key, T fallback = default) { try { var n = Values[key]; return n == null ? fallback : n.Deserialize<T>(s_Options); } catch { return fallback; } }
        public static bool HasKey(string key) => Values.ContainsKey(key);
        public static void DeleteKey(string key) { if (Values.Remove(key)) s_Dirty = true; }
        public static void DeleteAll() { Values.Clear(); s_Dirty = true; }
        public static IEnumerable<string> keys { get { foreach (var pair in Values) yield return pair.Key; } }

        /// <summary>Writes the values to disk now (safe: a crash during the write keeps the old file).</summary>
        public static void Save()
        {
            if (s_Values == null || !s_Dirty) return;
            WriteAtomic(ValuesFile, s_Values.ToJsonString(s_Options));
            s_Dirty = false;
        }

        /// <summary>Saves a whole object (a save game) in its own file.</summary>
        public static void Save<T>(string slot, T data) => WriteAtomic(SlotFile(slot), JsonSerializer.Serialize(data, s_Options));
        /// <summary>Loads a slot saved with Save; fallback when it does not exist or cannot be read.</summary>
        public static T Load<T>(string slot, T fallback = default)
        {
            var file = SlotFile(slot);
            if (!File.Exists(file)) return fallback;
            try { return JsonSerializer.Deserialize<T>(File.ReadAllText(file), s_Options); }
            catch (Exception e) { Debug.LogWarning($"SaveData: slot {slot} unreadable ({e.Message})"); return fallback; }
        }
        public static bool SlotExists(string slot) => File.Exists(SlotFile(slot));
        public static void DeleteSlot(string slot) { var f = SlotFile(slot); if (File.Exists(f)) File.Delete(f); }
        /// <summary>Names of the saved slots, newest first.</summary>
        public static string[] slots
        {
            get
            {
                if (!Directory.Exists(folder)) return Array.Empty<string>();
                var files = new List<FileInfo>();
                foreach (var f in new DirectoryInfo(folder).GetFiles("*.json")) if (f.Name != "values.json") files.Add(f);
                files.Sort((a, b) => b.LastWriteTimeUtc.CompareTo(a.LastWriteTimeUtc));
                return files.ConvertAll(f => Path.GetFileNameWithoutExtension(f.Name)).ToArray();
            }
        }

        static void WriteAtomic(string path, string text)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            var temp = path + ".tmp";
            File.WriteAllText(temp, text);
            if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
        }

        internal static void Flush() { try { Save(); } catch (Exception e) { Debug.LogWarning("SaveData: " + e.Message); } }
        internal static void Reset() { Flush(); s_Values = null; s_Dirty = false; }
    }
}
