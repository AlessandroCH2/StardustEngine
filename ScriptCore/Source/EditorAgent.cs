// AI Agent of the editor: a conversation with a model that uses tools on the open scene, the project's files and
// the C# scripts. Providers: Anthropic, OpenAI, Google Gemini and OpenAI-compatible servers (Ollama, LM Studio,
// any custom address). Plain HTTP + JSON, so the engine needs no provider SDK.
// The window (Editor/src/AiAgent.cpp) polls Agent.Handle every frame; HTTP runs on the thread pool and every tool
// runs on the main thread inside a poll, so the tools can touch the scene.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;
using Stardust.Interop;

namespace Stardust.Editor.AI
{
    internal sealed class ToolCall { public string id, name; public JsonObject args; }
    internal sealed class ToolResult { public string id, name, content; public bool error; }
    internal sealed class Turn
    {
        public string role;                 // user, assistant, tools
        public string text;
        public List<ToolCall> calls = new();
        public JsonNode raw;                // the provider's own assistant content, sent back unchanged
        public string provider;             // whose format raw is in; another provider gets text + calls
        public List<ToolResult> results = new();
    }
    internal sealed class Reply { public string text = "", stop = ""; public List<ToolCall> calls = new(); public JsonNode raw; }
    internal sealed class ToolSpec { public string name, description; public JsonObject schema; }

    internal sealed class AgentSettings
    {
        public string provider = "anthropic";
        public string effort = "high";
        public int maxSteps = 30;
        public Dictionary<string, string> models = new(), keys = new(), baseUrls = new();

        public static readonly string[] Providers = { "anthropic", "openai", "gemini", "ollama", "lmstudio", "custom" };
        public static string DefaultModel(string p) => p switch
        {
            "anthropic" => "claude-opus-5-5",
            "openai" => "gpt-5",
            "gemini" => "gemini-2.5-pro",
            "ollama" => "qwen3",
            _ => "",
        };
        public static string DefaultBase(string p) => p switch
        {
            "anthropic" => "https://api.anthropic.com",
            "openai" => "https://api.openai.com/v1",
            "gemini" => "https://generativelanguage.googleapis.com/v1beta",
            "ollama" => "http://localhost:11434/v1",
            "lmstudio" => "http://localhost:1234/v1",
            _ => "http://localhost:8080/v1",
        };
        static string EnvKey(string p) => p switch
        {
            "anthropic" => System.Environment.GetEnvironmentVariable("ANTHROPIC_API_KEY"),
            "openai" => System.Environment.GetEnvironmentVariable("OPENAI_API_KEY"),
            "gemini" => System.Environment.GetEnvironmentVariable("GEMINI_API_KEY") ?? System.Environment.GetEnvironmentVariable("GOOGLE_API_KEY"),
            _ => null,
        };
        public string Model => models.TryGetValue(provider, out var m) && !string.IsNullOrWhiteSpace(m) ? m.Trim() : DefaultModel(provider);
        public string BaseUrl => (baseUrls.TryGetValue(provider, out var b) && !string.IsNullOrWhiteSpace(b) ? b.Trim() : DefaultBase(provider)).TrimEnd('/');
        public string Key => keys.TryGetValue(provider, out var k) && !string.IsNullOrEmpty(k) ? k : EnvKey(provider) ?? "";
        public string KeyStatus
        {
            get
            {
                if (keys.TryGetValue(provider, out var k) && !string.IsNullOrEmpty(k)) return "API key saved (..." + k[Math.Max(0, k.Length - 4)..] + ")";
                if (!string.IsNullOrEmpty(EnvKey(provider))) return "API key from the environment variable";
                return provider is "ollama" or "lmstudio" or "custom" ? "Local server: no key needed unless it asks for one" : "No API key yet: paste one below";
            }
        }

        // Kept in the user's settings, never in the project (keys must not end up in a repository or a build)
        public static string FilePath => System.Environment.GetEnvironmentVariable("STARDUST_AI_SETTINGS") is string p && p.Length > 0 ? p
            : Path.Combine(System.Environment.GetFolderPath(System.Environment.SpecialFolder.ApplicationData), "Stardust", "ai-settings.json");
        public static AgentSettings Load()
        {
            var s = new AgentSettings();
            try
            {
                if (!File.Exists(FilePath)) return s;
                var j = JsonNode.Parse(File.ReadAllText(FilePath)) as JsonObject;
                if (j == null) return s;
                s.provider = j["provider"]?.GetValue<string>() ?? s.provider;
                s.effort = j["effort"]?.GetValue<string>() ?? s.effort;
                s.maxSteps = j["maxSteps"]?.GetValue<int>() ?? s.maxSteps;
                void Read(string name, Dictionary<string, string> into) { if (j[name] is JsonObject o) foreach (var pair in o) if (pair.Value != null) into[pair.Key] = pair.Value.GetValue<string>(); }
                Read("models", s.models); Read("keys", s.keys); Read("baseUrls", s.baseUrls);
            }
            catch (Exception e) { Debug.LogWarning("AI Agent settings: " + e.Message); }
            return s;
        }
        public void Save()
        {
            try
            {
                JsonObject Map(Dictionary<string, string> d) { var o = new JsonObject(); foreach (var pair in d) o[pair.Key] = pair.Value; return o; }
                var j = new JsonObject { ["provider"] = provider, ["effort"] = effort, ["maxSteps"] = maxSteps, ["models"] = Map(models), ["keys"] = Map(keys), ["baseUrls"] = Map(baseUrls) };
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
                File.WriteAllText(FilePath, j.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            }
            catch (Exception e) { Debug.LogWarning("AI Agent settings: " + e.Message); }
        }
    }

    // The wire formats of the providers
    internal static class Providers
    {
        public static bool OpenAICompatible(string p) => p is "openai" or "ollama" or "lmstudio" or "custom";

        public static HttpRequestMessage Build(AgentSettings s, string system, List<ToolSpec> tools, List<Turn> history)
        {
            HttpRequestMessage request;
            JsonObject body;
            if (s.provider == "anthropic")
            {
                var messages = new JsonArray();
                void Add(string role, JsonArray content)
                {
                    if (messages.Count > 0 && messages[^1]["role"]!.GetValue<string>() == role) { var last = messages[^1]["content"]!.AsArray(); foreach (var c in content) last.Add(c!.DeepClone()); }
                    else messages.Add(new JsonObject { ["role"] = role, ["content"] = content });
                }
                foreach (var t in history)
                {
                    if (t.role == "user") Add("user", new JsonArray(new JsonObject { ["type"] = "text", ["text"] = t.text }));
                    else if (t.role == "assistant")
                    {
                        if (t.provider == "anthropic" && t.raw is JsonArray raw && raw.Count > 0) { Add("assistant", (JsonArray)raw.DeepClone()); continue; }
                        var content = new JsonArray();
                        if (!string.IsNullOrWhiteSpace(t.text)) content.Add(new JsonObject { ["type"] = "text", ["text"] = t.text });
                        foreach (var c in t.calls) content.Add(new JsonObject { ["type"] = "tool_use", ["id"] = c.id, ["name"] = c.name, ["input"] = c.args.DeepClone() });
                        if (content.Count == 0) content.Add(new JsonObject { ["type"] = "text", ["text"] = "(no answer)" });
                        Add("assistant", content);
                    }
                    else Add("user", new JsonArray(t.results.Select(r => (JsonNode)new JsonObject { ["type"] = "tool_result", ["tool_use_id"] = r.id, ["content"] = r.content, ["is_error"] = r.error }).ToArray()));
                }
                body = new JsonObject
                {
                    ["model"] = s.Model,
                    ["max_tokens"] = 16000,
                    ["system"] = system,
                    ["messages"] = messages,
                    ["tools"] = new JsonArray(tools.Select(t => (JsonNode)new JsonObject { ["name"] = t.name, ["description"] = t.description, ["input_schema"] = t.schema.DeepClone() }).ToArray()),
                    ["cache_control"] = new JsonObject { ["type"] = "ephemeral" },
                    // A request the model declines is retried on Anthropic's fallback model instead of failing
                    ["fallbacks"] = "default",
                };
                if (!string.IsNullOrEmpty(s.effort)) body["output_config"] = new JsonObject { ["effort"] = s.effort };
                request = new HttpRequestMessage(HttpMethod.Post, s.BaseUrl + "/v1/messages");
                request.Headers.TryAddWithoutValidation("x-api-key", s.Key);
                request.Headers.TryAddWithoutValidation("anthropic-version", "2026-10-21");
                request.Headers.TryAddWithoutValidation("anthropic-beta", "server-side-fallback-2026-07-01");
            }
            else if (s.provider == "gemini")
            {
                var contents = new JsonArray();
                var geminiIds = new HashSet<string>();   // call ids Gemini gave itself: its function responses carry them back
                void Add(string role, IEnumerable<JsonNode> parts)
                {
                    if (contents.Count > 0 && contents[^1]["role"]!.GetValue<string>() == role) { var last = contents[^1]["parts"]!.AsArray(); foreach (var p in parts) last.Add(p); }
                    else contents.Add(new JsonObject { ["role"] = role, ["parts"] = new JsonArray(parts.ToArray()) });
                }
                foreach (var t in history)
                {
                    if (t.role == "user") Add("user", new[] { (JsonNode)new JsonObject { ["text"] = t.text } });
                    else if (t.role == "assistant")
                    {
                        if (t.provider == "gemini" && t.raw is JsonArray raw && raw.Count > 0)
                        {
                            foreach (var c in t.calls) if (!c.id.StartsWith("gemini-")) geminiIds.Add(c.id);
                            Add("model", raw.Select(p => p!.DeepClone()).ToList());
                            continue;
                        }
                        var parts = new List<JsonNode>();
                        if (!string.IsNullOrWhiteSpace(t.text)) parts.Add(new JsonObject { ["text"] = t.text });
                        foreach (var c in t.calls) parts.Add(new JsonObject { ["functionCall"] = new JsonObject { ["name"] = c.name, ["args"] = c.args.DeepClone() } });
                        if (parts.Count == 0) parts.Add(new JsonObject { ["text"] = "(no answer)" });
                        Add("model", parts);
                    }
                    else Add("user", t.results.Select(r =>
                    {
                        var response = new JsonObject { ["name"] = r.name, ["response"] = new JsonObject { [r.error ? "error" : "result"] = r.content } };
                        if (geminiIds.Contains(r.id)) response["id"] = r.id;
                        return (JsonNode)new JsonObject { ["functionResponse"] = response };
                    }).ToList());
                }
                body = new JsonObject
                {
                    ["systemInstruction"] = new JsonObject { ["parts"] = new JsonArray(new JsonObject { ["text"] = system }) },
                    ["contents"] = contents,
                    ["tools"] = new JsonArray(new JsonObject { ["functionDeclarations"] = new JsonArray(tools.Select(t =>
                    {
                        var declaration = new JsonObject { ["name"] = t.name, ["description"] = t.description };
                        if (t.schema["properties"] is JsonObject props && props.Count > 0) declaration["parameters"] = GeminiSchema(t.schema);
                        return (JsonNode)declaration;
                    }).ToArray()) }),
                };
                request = new HttpRequestMessage(HttpMethod.Post, $"{s.BaseUrl}/models/{s.Model}:generateContent");
                request.Headers.TryAddWithoutValidation("x-goog-api-key", s.Key);
            }
            else
            {
                var messages = new JsonArray(new JsonObject { ["role"] = "system", ["content"] = system });
                foreach (var t in history)
                {
                    if (t.role == "user") messages.Add(new JsonObject { ["role"] = "user", ["content"] = t.text });
                    else if (t.role == "assistant")
                    {
                        var m = new JsonObject { ["role"] = "assistant", ["content"] = t.text ?? "" };
                        if (t.calls.Count > 0) m["tool_calls"] = new JsonArray(t.calls.Select(c => (JsonNode)new JsonObject { ["id"] = c.id, ["type"] = "function", ["function"] = new JsonObject { ["name"] = c.name, ["arguments"] = c.args.ToJsonString() } }).ToArray());
                        messages.Add(m);
                    }
                    else foreach (var r in t.results) messages.Add(new JsonObject { ["role"] = "tool", ["tool_call_id"] = r.id, ["content"] = r.content });
                }
                body = new JsonObject
                {
                    ["model"] = s.Model,
                    ["messages"] = messages,
                    ["tools"] = new JsonArray(tools.Select(t => (JsonNode)new JsonObject { ["type"] = "function", ["function"] = new JsonObject { ["name"] = t.name, ["description"] = t.description, ["parameters"] = t.schema.DeepClone() } }).ToArray()),
                };
                request = new HttpRequestMessage(HttpMethod.Post, s.BaseUrl + "/chat/completions");
                if (!string.IsNullOrEmpty(s.Key)) request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + s.Key);
            }
            request.Content = new StringContent(body.ToJsonString(), Encoding.UTF8, "application/json");
            return request;
        }

        // Gemini takes an OpenAPI subset: an object needs properties, so free-form objects travel as JSON text
        static JsonNode GeminiSchema(JsonNode node)
        {
            if (node is not JsonObject o) return node?.DeepClone();
            if (o["type"]?.GetValue<string>() == "object" && o["properties"] is not JsonObject)
                return new JsonObject { ["type"] = "string", ["description"] = (o["description"]?.GetValue<string>() ?? "") + " (a JSON object written as text)" };
            var copy = new JsonObject();
            foreach (var pair in o)
            {
                if (pair.Key is "additionalProperties" or "$schema") continue;
                if (pair.Key == "properties" && pair.Value is JsonObject props) { var p = new JsonObject(); foreach (var prop in props) p[prop.Key] = GeminiSchema(prop.Value); copy["properties"] = p; }
                else copy[pair.Key] = pair.Key == "items" ? GeminiSchema(pair.Value) : pair.Value?.DeepClone();
            }
            return copy;
        }

        public static Reply Parse(string provider, string text)
        {
            var reply = new Reply();
            var j = JsonNode.Parse(text) as JsonObject ?? throw new Exception("the reply is not JSON");
            if (provider == "anthropic")
            {
                var content = j["content"] as JsonArray ?? new JsonArray();
                reply.raw = content.DeepClone();
                reply.stop = j["stop_reason"]?.GetValue<string>() ?? "";
                foreach (var block in content)
                {
                    var type = block?["type"]?.GetValue<string>();
                    if (type == "text") reply.text += block["text"]?.GetValue<string>();
                    else if (type == "tool_use") reply.calls.Add(new ToolCall { id = block["id"]!.GetValue<string>(), name = block["name"]!.GetValue<string>(), args = block["input"] as JsonObject ?? new JsonObject() });
                }
            }
            else if (provider == "gemini")
            {
                var candidate = (j["candidates"] as JsonArray)?.FirstOrDefault();
                if (candidate == null) throw new Exception("no answer" + (j["promptFeedback"] is JsonNode f ? ": " + f.ToJsonString() : ""));
                var parts = candidate["content"]?["parts"] as JsonArray ?? new JsonArray();
                reply.raw = parts.DeepClone();
                reply.stop = candidate["finishReason"]?.GetValue<string>() ?? "";
                int n = 0;
                foreach (var part in parts)
                {
                    if (part?["thought"]?.GetValue<bool>() == true) continue;
                    if (part?["text"] is JsonNode t) reply.text += t.GetValue<string>();
                    if (part?["functionCall"] is JsonObject call)
                        reply.calls.Add(new ToolCall { id = call["id"]?.GetValue<string>() ?? $"gemini-{n++}-{Guid.NewGuid():N}", name = call["name"]!.GetValue<string>(), args = call["args"] as JsonObject ?? new JsonObject() });
                }
            }
            else
            {
                var choice = (j["choices"] as JsonArray)?.FirstOrDefault() ?? throw new Exception("no answer");
                var message = choice["message"] as JsonObject ?? new JsonObject();
                reply.stop = choice["finish_reason"]?.GetValue<string>() ?? "";
                reply.text = message["content"] is JsonValue v && v.TryGetValue<string>(out var s) ? s : "";
                int n = 0;
                if (message["tool_calls"] is JsonArray calls)
                    foreach (var c in calls)
                    {
                        var f = c?["function"];
                        if (f == null) continue;
                        var args = f["arguments"];
                        JsonObject parsed = args is JsonObject o ? (JsonObject)o.DeepClone() : args is JsonValue av && av.TryGetValue<string>(out var str) && str.Trim().Length > 0 ? JsonNode.Parse(str) as JsonObject : null;
                        reply.calls.Add(new ToolCall { id = c["id"]?.GetValue<string>() is string id && id.Length > 0 ? id : $"call_{n++}_{Guid.NewGuid():N}", name = f["name"]!.GetValue<string>(), args = parsed ?? new JsonObject() });
                    }
                reply.raw = message.DeepClone();
            }
            return reply;
        }

        public static HttpRequestMessage Models(AgentSettings s)
        {
            HttpRequestMessage r;
            if (s.provider == "anthropic")
            {
                r = new HttpRequestMessage(HttpMethod.Get, s.BaseUrl + "/v1/models?limit=100");
                r.Headers.TryAddWithoutValidation("x-api-key", s.Key);
                r.Headers.TryAddWithoutValidation("anthropic-version", "2026-10-21");
            }
            else if (s.provider == "gemini")
            {
                r = new HttpRequestMessage(HttpMethod.Get, s.BaseUrl + "/models?pageSize=200");
                r.Headers.TryAddWithoutValidation("x-goog-api-key", s.Key);
            }
            else
            {
                r = new HttpRequestMessage(HttpMethod.Get, s.BaseUrl + "/models");
                if (!string.IsNullOrEmpty(s.Key)) r.Headers.TryAddWithoutValidation("Authorization", "Bearer " + s.Key);
            }
            return r;
        }
        public static List<string> ParseModels(string provider, string text)
        {
            var j = JsonNode.Parse(text) as JsonObject;
            var list = new List<string>();
            if (provider == "gemini")
            {
                foreach (var m in j?["models"] as JsonArray ?? new JsonArray())
                    if ((m?["supportedGenerationMethods"] as JsonArray)?.Any(x => x?.GetValue<string>() == "generateContent") ?? true)
                        list.Add((m!["name"]!.GetValue<string>()).Replace("models/", ""));
            }
            else foreach (var m in j?["data"] as JsonArray ?? new JsonArray()) if (m?["id"] is JsonNode id) list.Add(id.GetValue<string>());
            return list;
        }

        public static string ErrorText(int status, string body)
        {
            try
            {
                var j = JsonNode.Parse(body);
                var message = j?["error"]?["message"]?.GetValue<string>() ?? (j?["error"] is JsonValue v ? v.GetValue<string>() : null) ?? j?["message"]?.GetValue<string>();
                if (message != null) return $"HTTP {status}: {message}";
            }
            catch { }
            return $"HTTP {status}: " + (body.Length > 600 ? body[..600] + "..." : body);
        }
    }

    public static class Agent
    {
        enum Phase { Idle, Requesting, Tools, Approval, Building }
        static readonly HttpClient s_Client = new() { Timeout = TimeSpan.FromMinutes(10) };
        static AgentSettings s_Settings;
        sealed class Chat { public string id = Guid.NewGuid().ToString("N"), title = ""; public DateTime updated = DateTime.Now; public List<Turn> history = new(); public List<JsonObject> view = new(); }
        static List<Chat> s_Chats;
        static Chat s_Chat;
        static List<Turn> s_History => s_Chat.history;
        static List<JsonObject> s_View => s_Chat.view;
        static bool s_Dirty;
        static DateTime s_BusySince = DateTime.Now;
        static long s_Version = 1;
        static Phase s_Phase = Phase.Idle;
        static Task<(int status, string body)> s_Http;
        static CancellationTokenSource s_Cancel;
        static readonly Queue<ToolCall> s_Queue = new();
        static Turn s_ToolTurn;
        static ToolCall s_Waiting;          // waiting for the user's approval, or for the script build
        static JsonObject s_WaitingView;
        static string s_ApprovalTitle, s_ApprovalDetail;
        static bool s_AllowAll;
        static int s_Steps;
        static long s_BuildGeneration; static DateTime s_BuildStart;
        static string s_Status = "";
        static List<string> s_Models = new(); static string s_ModelsError = ""; static Task<(int status, string body)> s_ModelsHttp; static string s_ModelsProvider;
        static List<ToolSpec> s_Tools;

        static AgentSettings Settings => s_Settings ??= AgentSettings.Load();
        static void Changed() { ++s_Version; s_Dirty = true; }

        // Chats are kept per project in Library/AgentChats.json (Library is not in the repository)
        static string ChatsFile
        {
            get
            {
                var root = Native.Query(new JsonObject { ["kind"] = "agentProject" })["root"]?.GetValue<string>();
                return string.IsNullOrEmpty(root) ? null : Path.Combine(root, "Library", "AgentChats.json");
            }
        }
        static string s_ChatsFile;
        static void EnsureChats()
        {
            var file = ChatsFile;
            if (s_Chats != null && file == s_ChatsFile) return;
            if (s_Chats != null && s_Phase != Phase.Idle) return;   // a project switch waits for the current answer
            s_ChatsFile = file;
            s_Chats = new List<Chat>();
            try
            {
                if (file != null && File.Exists(file) && JsonNode.Parse(File.ReadAllText(file)) is JsonArray all)
                    foreach (var c in all.OfType<JsonObject>()) s_Chats.Add(ReadChat(c));
            }
            catch (Exception e) { Debug.LogWarning("AI Agent chats: " + e.Message); }
            s_Chat = s_Chats.FirstOrDefault(c => c.history.Count == 0) ?? new Chat();
            if (!s_Chats.Contains(s_Chat)) s_Chats.Insert(0, s_Chat);
            Changed();
        }
        static void SaveChats()
        {
            if (s_ChatsFile == null) return;
            try
            {
                var all = new JsonArray(s_Chats.Where(c => c.history.Count > 0).OrderByDescending(c => c.updated).Take(50).Select(c => (JsonNode)WriteChat(c)).ToArray());
                Directory.CreateDirectory(Path.GetDirectoryName(s_ChatsFile)!);
                File.WriteAllText(s_ChatsFile, all.ToJsonString());
            }
            catch (Exception e) { Debug.LogWarning("AI Agent chats: " + e.Message); }
        }
        static JsonObject WriteChat(Chat c) => new()
        {
            ["id"] = c.id, ["title"] = c.title, ["updated"] = c.updated.ToString("o"),
            ["view"] = new JsonArray(c.view.Select(v => (JsonNode)v.DeepClone()).ToArray()),
            ["history"] = new JsonArray(c.history.Select(t => (JsonNode)new JsonObject
            {
                ["role"] = t.role, ["text"] = t.text, ["provider"] = t.provider, ["raw"] = t.raw?.DeepClone(),
                ["calls"] = new JsonArray(t.calls.Select(x => (JsonNode)new JsonObject { ["id"] = x.id, ["name"] = x.name, ["args"] = x.args.DeepClone() }).ToArray()),
                ["results"] = new JsonArray(t.results.Select(r => (JsonNode)new JsonObject { ["id"] = r.id, ["name"] = r.name, ["content"] = r.content, ["error"] = r.error }).ToArray()),
            }).ToArray()),
        };
        static Chat ReadChat(JsonObject j)
        {
            string Str(JsonNode n) => n is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;
            var c = new Chat { id = Str(j["id"]) ?? Guid.NewGuid().ToString("N"), title = Str(j["title"]) ?? "" };
            if (DateTime.TryParse(Str(j["updated"]), null, System.Globalization.DateTimeStyles.RoundtripKind, out var when)) c.updated = when;
            foreach (var v in j["view"] as JsonArray ?? new JsonArray()) if (v is JsonObject o) c.view.Add((JsonObject)o.DeepClone());
            foreach (var t in (j["history"] as JsonArray ?? new JsonArray()).OfType<JsonObject>())
                c.history.Add(new Turn
                {
                    role = Str(t["role"]), text = Str(t["text"]), provider = Str(t["provider"]), raw = t["raw"]?.DeepClone(),
                    calls = (t["calls"] as JsonArray ?? new JsonArray()).OfType<JsonObject>().Select(x => new ToolCall { id = Str(x["id"]), name = Str(x["name"]), args = x["args"]?.DeepClone() as JsonObject ?? new JsonObject() }).ToList(),
                    results = (t["results"] as JsonArray ?? new JsonArray()).OfType<JsonObject>().Select(r => new ToolResult { id = Str(r["id"]), name = Str(r["name"]), content = Str(r["content"]) ?? "", error = r["error"]?.GetValue<bool>() ?? false }).ToList(),
                });
            // An answer cut off by closing the editor: its calls get results so the conversation stays valid
            if (c.history.Count > 0 && c.history[^1].role == "assistant" && c.history[^1].calls.Count > 0)
                c.history.Add(new Turn { role = "tools", results = c.history[^1].calls.Select(x => new ToolResult { id = x.id, name = x.name, content = "Not run: the editor was closed.", error = true }).ToList() });
            return c;
        }
        static void NewChat()
        {
            Stop(null);
            s_AllowAll = false;
            if (s_Chat.history.Count == 0 && s_Chat.view.Count == 0) return;
            if (s_Chat.history.Count == 0) s_Chats.Remove(s_Chat);
            s_Chat = new Chat();
            s_Chats.Insert(0, s_Chat);
            Changed();
        }

        public static JsonObject Handle(JsonObject q)
        {
            try
            {
                EnsureChats();
                switch (q["action"]?.GetValue<string>())
                {
                    case "send": Send(q["text"]?.GetValue<string>() ?? ""); break;
                    case "stop": Stop("Stopped."); break;
                    case "clear": NewChat(); break;
                    case "openChat":
                    {
                        var chat = s_Chats.FirstOrDefault(c => c.id == q["id"]?.GetValue<string>());
                        if (chat != null && chat != s_Chat) { Stop(null); if (s_Chat.history.Count == 0) s_Chats.Remove(s_Chat); s_Chat = chat; s_AllowAll = false; Changed(); }
                        break;
                    }
                    case "deleteChat":
                    {
                        var chat = s_Chats.FirstOrDefault(c => c.id == q["id"]?.GetValue<string>());
                        if (chat == null) break;
                        if (chat == s_Chat) { Stop(null); s_Chat = new Chat(); s_Chats.Insert(0, s_Chat); }
                        s_Chats.Remove(chat);
                        Changed();
                        break;
                    }
                    case "approve": Approve(q["allow"]?.GetValue<string>() ?? "no"); break;
                    case "settings": ApplySettings(q); break;
                    case "models": LoadModels(); break;
                }
                Update();
            }
            catch (Exception e) { if (s_Chat != null) AddView("error", "The agent failed: " + e.Message); s_Phase = Phase.Idle; }
            if (s_Dirty && s_Phase == Phase.Idle) { s_Dirty = false; SaveChats(); }
            var reply = new JsonObject { ["version"] = s_Version, ["busy"] = s_Phase != Phase.Idle, ["elapsed"] = s_Phase == Phase.Idle ? 0 : (int)(DateTime.Now - s_BusySince).TotalSeconds };
            long known = q["version"]?.GetValue<long>() ?? -1;
            if (known == s_Version || s_Chat == null) return reply;
            reply["status"] = s_Status;
            reply["chat"] = s_Chat.id;
            reply["title"] = s_Chat.title;
            reply["chats"] = new JsonArray(s_Chats.Where(c => c.history.Count > 0).OrderByDescending(c => c.updated).Select(c => (JsonNode)new JsonObject { ["id"] = c.id, ["title"] = c.title, ["when"] = c.updated.ToString("dd MMM HH:mm") }).ToArray());
            reply["messages"] = new JsonArray(s_View.Select(v => (JsonNode)v.DeepClone()).ToArray());
            if (s_Phase == Phase.Approval) reply["approval"] = new JsonObject { ["action"] = s_ApprovalTitle, ["detail"] = s_ApprovalDetail };
            var s = Settings;
            reply["settings"] = new JsonObject { ["provider"] = s.provider, ["model"] = s.Model, ["baseUrl"] = s.BaseUrl, ["effort"] = s.effort, ["keyStatus"] = s.KeyStatus, ["file"] = AgentSettings.FilePath };
            reply["models"] = new JsonArray(s_Models.Select(m => (JsonNode)m).ToArray());
            reply["modelsLoading"] = s_ModelsHttp != null;
            reply["modelsError"] = s_ModelsError;
            return reply;
        }

        // Test hook: the transcript as text
        internal static string Transcript => string.Join("\n", s_View.Select(v => v["role"] + ": " + (v["text"] ?? v["title"])));
        internal static bool Busy => s_Phase != Phase.Idle;

        static void ApplySettings(JsonObject q)
        {
            var s = Settings;
            if (q["provider"]?.GetValue<string>() is string p && AgentSettings.Providers.Contains(p) && p != s.provider) { s.provider = p; s_Models.Clear(); s_ModelsError = ""; }
            if (q["model"]?.GetValue<string>() is string m) s.models[s.provider] = m.Trim();
            if (q["apiKey"]?.GetValue<string>() is string k) { if (k.Trim().Length == 0) s.keys.Remove(s.provider); else s.keys[s.provider] = k.Trim(); }
            if (q["baseUrl"]?.GetValue<string>() is string b) s.baseUrls[s.provider] = b.Trim();
            if (q["effort"]?.GetValue<string>() is string e) s.effort = e;
            if (q["maxSteps"] is JsonNode n) s.maxSteps = Math.Clamp(n.GetValue<int>(), 1, 200);
            if (q["save"]?.GetValue<bool>() != false) s.Save();
            Changed();
        }

        static void LoadModels()
        {
            if (s_ModelsHttp != null) return;
            s_ModelsProvider = Settings.provider;
            s_ModelsError = "";
            s_ModelsHttp = Request(Providers.Models(Settings), CancellationToken.None);
            Changed();
        }

        static void AddView(string role, string text) { s_View.Add(new JsonObject { ["role"] = role, ["text"] = text }); Changed(); }

        static void Send(string text)
        {
            text = text.Trim();
            if (text.Length == 0 || s_Phase != Phase.Idle) return;
            var s = Settings;
            if (string.IsNullOrEmpty(s.Key) && s.provider is "anthropic" or "openai" or "gemini") { AddView("error", "Add an API key for this provider first: the gear button under the message box."); return; }
            if (string.IsNullOrEmpty(s.Model)) { AddView("error", "Choose a model first: the provider and model button next to Send lists the ones the server has."); return; }
            s_History.Add(new Turn { role = "user", text = text });
            if (s_Chat.title.Length == 0) s_Chat.title = text.Length > 60 ? text[..57].TrimEnd() + "..." : text.Replace('\n', ' ');
            s_Chat.updated = DateTime.Now;
            AddView("user", text);
            s_Steps = 0;
            s_BusySince = DateTime.Now;
            StartRequest();
        }

        static Task<(int, string)> Request(HttpRequestMessage message, CancellationToken token) => Task.Run(async () =>
        {
            using (message)
            using (var response = await s_Client.SendAsync(message, token).ConfigureAwait(false))
                return ((int)response.StatusCode, await response.Content.ReadAsStringAsync(token).ConfigureAwait(false));
        }, token);

        static void StartRequest()
        {
            s_Cancel = new CancellationTokenSource();
            var s = Settings;
            s_Http = Request(Providers.Build(s, SystemPrompt(), Tools, s_History), s_Cancel.Token);
            s_Phase = Phase.Requesting;
            s_Status = $"Waiting for {s.Model}...";
            Changed();
        }

        static void Stop(string note)
        {
            if (s_Phase == Phase.Idle) return;
            s_Cancel?.Cancel();
            if (s_Phase is Phase.Tools or Phase.Approval or Phase.Building) FinishTools("Cancelled by the user.");
            s_Phase = Phase.Idle; s_Http = null; s_Waiting = null;
            if (note != null) AddView("note", note);
            Changed();
        }

        // Every tool call gets a result, also the ones not run, so the conversation stays valid
        static void FinishTools(string reason)
        {
            if (s_ToolTurn == null) return;
            if (s_Waiting != null) { Complete(s_Waiting, s_WaitingView, reason, true); s_Waiting = null; }
            while (s_Queue.Count > 0) { var c = s_Queue.Dequeue(); s_ToolTurn.results.Add(new ToolResult { id = c.id, name = c.name, content = reason, error = true }); }
            s_History.Add(s_ToolTurn);
            s_ToolTurn = null;
        }

        static void Update()
        {
            if (s_ModelsHttp != null && s_ModelsHttp.IsCompleted)
            {
                try
                {
                    var (status, body) = s_ModelsHttp.Result;
                    if (status / 100 == 2) { s_Models = Providers.ParseModels(s_ModelsProvider, body); s_Models.Sort(StringComparer.OrdinalIgnoreCase); if (s_Models.Count == 0) s_ModelsError = "The server listed no models"; }
                    else s_ModelsError = Providers.ErrorText(status, body);
                }
                catch (Exception e) { s_ModelsError = "Cannot reach the server: " + (e.GetBaseException().Message); }
                s_ModelsHttp = null;
                Changed();
            }
            if (s_Phase == Phase.Requesting && s_Http != null && s_Http.IsCompleted)
            {
                var task = s_Http; s_Http = null;
                if (task.IsCanceled || task.IsFaulted)
                {
                    s_Phase = Phase.Idle;
                    if (!task.IsCanceled) AddView("error", "Cannot reach " + Settings.BaseUrl + ": " + task.Exception?.GetBaseException().Message);
                    return;
                }
                var (status, body) = task.Result;
                if (status / 100 != 2) { s_Phase = Phase.Idle; AddView("error", Providers.ErrorText(status, body)); return; }
                Reply reply;
                try { reply = Providers.Parse(Settings.provider, body); }
                catch (Exception e) { s_Phase = Phase.Idle; AddView("error", "Unexpected reply: " + e.Message); return; }
                s_History.Add(new Turn { role = "assistant", text = reply.text, calls = reply.calls, raw = reply.raw, provider = Settings.provider });
                if (reply.text.Trim().Length > 0) AddView("assistant", reply.text.Trim());
                if (reply.stop == "pause_turn") { StartRequest(); return; }
                if (reply.calls.Count == 0)
                {
                    s_Phase = Phase.Idle;
                    if (reply.stop is "max_tokens" or "length" or "MAX_TOKENS") AddView("note", "The answer was cut at the length limit. Say \"continue\" to go on.");
                    else if (reply.stop == "refusal" || reply.stop == "SAFETY") AddView("note", "The model declined this request.");
                    else if (reply.text.Trim().Length == 0) AddView("note", "(no answer)");
                    return;
                }
                s_ToolTurn = new Turn { role = "tools" };
                foreach (var c in reply.calls) s_Queue.Enqueue(c);
                s_Phase = Phase.Tools;
            }
            if (s_Phase == Phase.Building && s_Waiting != null)
            {
                var b = Native.Query(new JsonObject { ["kind"] = "agentBuild" });
                bool building = b["building"]?.GetValue<bool>() ?? false;
                long generation = b["generation"]?.GetValue<long>() ?? 0;
                if (!building && (generation != s_BuildGeneration || (DateTime.Now - s_BuildStart).TotalSeconds > 3))
                {
                    var diagnostics = b["diagnostics"] as JsonArray ?? new JsonArray();
                    Complete(s_Waiting, s_WaitingView, diagnostics.Count == 0 ? "The scripts compiled without errors." : "Build messages:\n" + string.Join("\n", diagnostics.Take(60).Select(d => d?.GetValue<string>())), false);
                    s_Waiting = null;
                    s_Phase = Phase.Tools;
                }
            }
            if (s_Phase == Phase.Tools) RunTools();
        }

        static void RunTools()
        {
            while (s_Queue.Count > 0)
            {
                var call = s_Queue.Dequeue();
                var view = new JsonObject { ["role"] = "tool", ["name"] = call.name, ["title"] = Title(call), ["summary"] = Summary(call.args), ["args"] = Short(call.args.ToJsonString(new JsonSerializerOptions { WriteIndented = true }), 3000) };
                s_View.Add(view); Changed();
                string approval = s_AllowAll ? null : NeedsApproval(call);
                if (approval != null)
                {
                    s_Waiting = call; s_WaitingView = view;
                    s_ApprovalTitle = Title(call); s_ApprovalDetail = approval;
                    s_Phase = Phase.Approval; s_Status = "Waiting for your approval";
                    Changed();
                    return;
                }
                if (call.name == "compile_scripts") { BeginBuild(call, view); return; }
                Run(call, view);
            }
            FinishTools("");
            if (++s_Steps >= Settings.maxSteps) { s_Phase = Phase.Idle; AddView("note", $"Stopped after {s_Steps} steps. Say \"continue\" to let the agent go on."); return; }
            StartRequest();
        }

        static void Approve(string allow)
        {
            if (s_Phase != Phase.Approval || s_Waiting == null) return;
            var call = s_Waiting; var view = s_WaitingView; s_Waiting = null;
            s_Phase = Phase.Tools;
            if (allow == "no") Complete(call, view, "The user denied this action. Do not try it again in another way; ask them what they want instead.", true);
            else
            {
                if (allow == "all") s_AllowAll = true;
                if (call.name == "compile_scripts") { BeginBuild(call, view); return; }
                Run(call, view);
            }
            Changed();
        }

        static void BeginBuild(ToolCall call, JsonObject view)
        {
            var b = Native.Query(new JsonObject { ["kind"] = "agentBuild", ["start"] = true });
            s_BuildGeneration = b["generation"]?.GetValue<long>() ?? 0;
            if (!(b["building"]?.GetValue<bool>() ?? false)) s_BuildGeneration = -1; // finished at once
            s_BuildStart = DateTime.Now;
            s_Waiting = call; s_WaitingView = view;
            s_Phase = Phase.Building; s_Status = "Compiling the scripts...";
            Changed();
        }

        static void Run(ToolCall call, JsonObject view)
        {
            string result; bool error = false;
            try { result = AgentTools.Execute(call.name, call.args); }
            catch (Exception e) { result = e.GetBaseException().Message; error = true; }
            if (result.StartsWith("Error: ")) error = true;
            Complete(call, view, result, error);
        }

        static void Complete(ToolCall call, JsonObject view, string result, bool error)
        {
            result = Short(result, 24000);
            s_ToolTurn?.results.Add(new ToolResult { id = call.id, name = call.name, content = result, error = error });
            if (view != null) { view["result"] = Short(result, 4000); view["error"] = error; }
            s_Status = "Working...";
            Changed();
        }

        static string NeedsApproval(ToolCall call)
        {
            string path = call.args["path"]?.ToString() ?? "";
            switch (call.name)
            {
                case "delete_entity": return "The object and its children are removed from the scene. Ctrl+Z brings them back.";
                case "write_file": return AgentTools.FileExists(path) ? "The file already exists and is replaced. Ctrl+Z does not undo file changes." : null;
                case "edit_file": return "Part of the file is replaced. Ctrl+Z does not undo file changes.";
                case "delete_file": return "The file is deleted for good, not moved to the recycle bin.";
                default: return null;
            }
        }

        // "key: value" of the short arguments, for the line of a tool card
        static string Summary(JsonObject args)
        {
            var parts = new List<string>();
            foreach (var pair in args)
            {
                string value = pair.Value is JsonValue v ? (v.TryGetValue<string>(out var s) ? s : v.ToJsonString()) : pair.Value is JsonArray a ? $"[{a.Count}]" : "{...}";
                value = value.Replace('\n', ' ');
                if (value.Length > 40) value = value[..37] + "...";
                parts.Add($"{pair.Key}: {value}");
            }
            var line = string.Join(", ", parts);
            return line.Length > 110 ? line[..107] + "..." : line;
        }

        static string NameOf(string id)
        {
            if (string.IsNullOrEmpty(id)) return id;
            try { return Native.Query(new JsonObject { ["kind"] = "agentName", ["id"] = id })["name"]?.GetValue<string>() is string n && n.Length > 0 ? n : id; }
            catch { return id; }
        }

        static string Title(ToolCall c)
        {
            string Arg(string n) => c.args[n] is JsonValue v && v.TryGetValue<string>(out var s) ? s : c.args[n]?.ToString() ?? "";
            return c.name switch
            {
                "list_entities" => Arg("filter").Length > 0 ? $"List objects matching \"{Arg("filter")}\"" : "List the scene's objects",
                "get_entity" => "Read " + NameOf(Arg("id")),
                "create_entity" => "Create " + (Arg("name").Length > 0 ? Arg("name") : "an object"),
                "edit_entity" => "Change " + NameOf(Arg("id")),
                "delete_entity" => "Delete " + NameOf(Arg("id")),
                "read_file" => "Read " + Arg("path"),
                "write_file" => "Write " + Arg("path"),
                "edit_file" => "Edit " + Arg("path"),
                "delete_file" => "Delete " + Arg("path"),
                "list_files" => "List files in " + (Arg("folder").Length > 0 ? Arg("folder") : "the project"),
                "find_api" => $"Look up \"{Arg("query")}\" in the scripting API",
                "component_reference" => "Read component formats",
                "get_console" => "Read the console",
                "compile_scripts" => "Compile the scripts",
                "get_selection" => "Read the selection",
                "select_entities" => "Select objects",
                "save_scene" => "Save the scene",
                _ => c.name,
            };
        }

        static string Short(string s, int max) => s == null ? "" : s.Length <= max ? s : s[..max] + $"\n... ({s.Length - max} more characters)";

        static List<ToolSpec> Tools => s_Tools ??= AgentTools.Specs();

        static string SystemPrompt()
        {
            var p = Native.Query(new JsonObject { ["kind"] = "agentProject" });
            return $@"You are the AI agent inside the editor of Stardust Engine, a C++/Vulkan game engine scripted in C#. You help the user build their game: you inspect and change the open scene, write and fix C# scripts, and explain how the engine works. Act through your tools instead of telling the user to do things by hand, then say briefly what you did. Answer in the user's language.

Project: {p["root"]}  (assets: {p["assets"]})
Open scene: {p["sceneName"]} ({(p["playing"]?.GetValue<bool>() == true ? "Play mode: changes are lost when play stops" : "Edit mode")})

Scene
- Objects (entities) are identified by 16-digit hex ids. Find them with list_entities, read one with get_entity.
- An entity is JSON: name, active, tag, layer, transform {{position [x,y,z], rotation quaternion [w,x,y,z], scale [x,y,z], euler [x,y,z] degrees}} and one key per component (meshRenderer, boxCollider, rigidbody, pointLight, camera, scripts...).
- edit_entity applies a JSON merge patch to that JSON: give only the keys to change; null removes a key (a component). To rotate, give transform.euler alone. Read the entity first so you keep the existing format.
- For a component not on the entity yet, read its default JSON with component_reference and patch it in.
- Units are metres; +Y is up and -Z is forward. Primitives: cube, sphere, plane, cylinder (1 metre).
- Every scene change can be undone by the user with Ctrl+Z. Save only when asked (save_scene).

Scripts
- C# files under the assets folder (usually Assets/Scripts). A behaviour: using Stardust; public class Mover : ScriptBehaviour {{ public float Speed = 2; void OnCreate() {{ }} void OnUpdate(float dt) {{ transform.position += transform.forward * Speed * dt; }} }}
- Public fields appear in the inspector. Attach a script with the entity's scripts array: [{{""class"": ""Mover"", ""enabled"": true, ""fields"": {{""Speed"": 2}}}}].
- Check names and signatures with find_api before using an engine API you are unsure of; do not guess the API of other engines.
- After writing scripts call compile_scripts and fix every error it reports before attaching them.

Files: paths are relative to the project folder. Prefer edit_file for small changes to existing files. Changes to files cannot be undone with Ctrl+Z.";
        }
    }

    internal static class AgentTools
    {
        static JsonObject Schema(params (string name, string type, string description, bool required)[] props)
        {
            var properties = new JsonObject();
            var required = new JsonArray();
            foreach (var (name, type, description, req) in props)
            {
                JsonObject prop = type switch
                {
                    "string[]" => new JsonObject { ["type"] = "array", ["items"] = new JsonObject { ["type"] = "string" } },
                    _ => new JsonObject { ["type"] = type },
                };
                prop["description"] = description;
                properties[name] = prop;
                if (req) required.Add(name);
            }
            return new JsonObject { ["type"] = "object", ["properties"] = properties, ["required"] = required };
        }
        static ToolSpec T(string name, string description, JsonObject schema) => new() { name = name, description = description, schema = schema };

        public static List<ToolSpec> Specs() => new()
        {
            T("list_entities", "Lists the objects of the open scene in hierarchy order: id, name, depth, parent id, component keys and script classes. filter matches names, components or script classes.", Schema(("filter", "string", "Optional text to look for", false), ("limit", "integer", "Maximum rows, default 200", false))),
            T("get_entity", "Reads one object as JSON (every component and its values), its children and its world position.", Schema(("id", "string", "Entity id (hex)", true))),
            T("create_entity", "Creates an object. Returns its id and JSON.", Schema(("name", "string", "Name", true), ("primitive", "string", "Optional mesh: cube, sphere, plane or cylinder", false), ("parent", "string", "Optional parent entity id", false), ("components", "object", "Optional JSON merged into the new entity, for example {\"transform\":{\"position\":[0,1,0]},\"boxCollider\":{...}}", false))),
            T("edit_entity", "Changes an object with a JSON merge patch on its JSON (see get_entity): only the given keys change, null removes a key or component. parent moves it under another entity (empty string: to the scene root).", Schema(("id", "string", "Entity id", true), ("patch", "object", "Merge patch, for example {\"name\":\"Door\",\"transform\":{\"position\":[2,0,0]}}", false), ("parent", "string", "Optional new parent id; empty string for the root", false))),
            T("delete_entity", "Deletes an object and its children.", Schema(("id", "string", "Entity id", true))),
            T("component_reference", "Without keys: lists the component keys an entity can have. With keys: the default JSON of those components, to add them with edit_entity.", Schema(("keys", "string[]", "Component keys, for example [\"rigidbody\",\"boxCollider\"]", false))),
            T("find_api", "Searches the C# scripting API (engine types, game scripts and their members) by name or words of the documentation. Returns signatures and docs.", Schema(("query", "string", "A type, member or words, for example \"Rigidbody AddForce\" or \"raycast\"", true))),
            T("list_files", "Lists files of the project (relative paths). folder defaults to the project folder; pattern is a file pattern like *.cs.", Schema(("folder", "string", "Folder relative to the project", false), ("pattern", "string", "File pattern, default *", false))),
            T("read_file", "Reads a text file of the project, with line numbers.", Schema(("path", "string", "Path relative to the project", true), ("offset", "integer", "First line (1-based), optional", false), ("limit", "integer", "Number of lines, default 400", false))),
            T("write_file", "Creates or replaces a text file of the project with content.", Schema(("path", "string", "Path relative to the project", true), ("content", "string", "The whole file", true))),
            T("edit_file", "Replaces old_text with new_text in a file. old_text must appear exactly once.", Schema(("path", "string", "Path relative to the project", true), ("old_text", "string", "Exact text to replace", true), ("new_text", "string", "Replacement", true))),
            T("delete_file", "Deletes a file of the project.", Schema(("path", "string", "Path relative to the project", true))),
            T("get_console", "Reads the last lines of the editor console (script errors, Debug.Log output).", Schema(("count", "integer", "Lines, default 40", false), ("errors_only", "boolean", "Only warnings and errors", false))),
            T("compile_scripts", "Compiles the C# scripts of the project and returns the compiler errors and warnings.", Schema()),
            T("get_selection", "The objects (and the asset) the user has selected in the editor.", Schema()),
            T("select_entities", "Selects objects in the editor so the user sees them.", Schema(("ids", "string[]", "Entity ids", true))),
            T("save_scene", "Saves the open scene to its file.", Schema()),
        };

        static string Dump(JsonNode n) => n?.ToJsonString(new JsonSerializerOptions { WriteIndented = false }) ?? "{}";
        static JsonObject Q(string kind, JsonObject args = null)
        {
            var q = args != null ? (JsonObject)args.DeepClone() : new JsonObject();
            q["kind"] = kind;
            var r = Native.Query(q);
            if (r["error"] != null) throw new Exception(r["error"]!.GetValue<string>());
            return r;
        }
        static string S(JsonObject a, string name) => a[name] is JsonValue v && v.TryGetValue<string>(out var s) ? s : a[name]?.ToString();
        static int I(JsonObject a, string name, int fallback) { try { return a[name] == null ? fallback : a[name] is JsonValue v && v.TryGetValue<string>(out var s) ? int.Parse(s) : a[name]!.GetValue<int>(); } catch { return fallback; } }
        static JsonNode Obj(JsonObject a, string name) => a[name] is JsonValue v && v.TryGetValue<string>(out var s) ? (s.Trim().Length == 0 ? null : JsonNode.Parse(s)) : a[name]?.DeepClone();

        static string Root => Native.Query(new JsonObject { ["kind"] = "agentProject" })["root"]?.GetValue<string>() ?? Directory.GetCurrentDirectory();
        // Inside the project only
        static string Resolve(string path)
        {
            if (string.IsNullOrWhiteSpace(path)) throw new Exception("Error: no path given");
            var root = Path.GetFullPath(Root);
            var full = Path.GetFullPath(Path.IsPathRooted(path) ? path : Path.Combine(root, path));
            var rel = Path.GetRelativePath(root, full);
            if (rel.StartsWith("..") || Path.IsPathRooted(rel)) throw new Exception("Error: only files inside the project folder can be used");
            return full;
        }
        public static bool FileExists(string path) { try { return File.Exists(Resolve(path)); } catch { return false; } }

        public static string Execute(string name, JsonObject a)
        {
            switch (name)
            {
                case "list_entities":
                {
                    var r = Q("agentHierarchy", new JsonObject { ["filter"] = S(a, "filter") ?? "", ["limit"] = I(a, "limit", 200) });
                    var sb = new StringBuilder();
                    sb.AppendLine($"Scene {r["scene"]}: {r["total"]} objects" + (S(a, "filter") is string f && f.Length > 0 ? $" matching \"{f}\"" : ""));
                    foreach (var e in r["entities"]!.AsArray())
                    {
                        sb.Append(new string(' ', Math.Min(20, e!["depth"]!.GetValue<int>()) * 2)).Append(e["id"]).Append("  ").Append(e["name"]);
                        if (e["active"] != null) sb.Append(" (inactive)");
                        if (e["prefab"] != null) sb.Append(" [prefab ").Append(e["prefab"]).Append(']');
                        var comps = e["components"]!.AsArray().Select(c => c!.GetValue<string>()).Where(c => c != "scripts").ToList();
                        if (comps.Count > 0) sb.Append("  {").Append(string.Join(", ", comps)).Append('}');
                        sb.AppendLine();
                    }
                    int shown = r["entities"]!.AsArray().Count, total = r["total"]!.GetValue<int>();
                    if (shown < total) sb.AppendLine($"... {total - shown} more: use filter or a larger limit");
                    return sb.ToString();
                }
                case "get_entity": return Dump(Q("agentEntity", new JsonObject { ["id"] = S(a, "id") }));
                case "create_entity":
                {
                    var q = new JsonObject { ["name"] = S(a, "name") ?? "GameObject", ["primitive"] = S(a, "primitive") ?? "" };
                    if (S(a, "parent") is string p && p.Length > 0) q["parent"] = p;
                    if (Obj(a, "components") is JsonObject c) q["components"] = c;
                    var r = Q("agentCreate", q);
                    return "Created " + r["id"] + ": " + Dump(r["entity"]);
                }
                case "edit_entity":
                {
                    var q = new JsonObject { ["id"] = S(a, "id") };
                    if (Obj(a, "patch") is JsonObject patch) q["patch"] = patch;
                    if (a.ContainsKey("parent") && a["parent"] != null) q["parent"] = S(a, "parent") ?? "";
                    var r = Q("agentEdit", q);
                    return "Changed " + r["id"] + ": " + Dump(r["entity"]);
                }
                case "delete_entity": return "Deleted " + Q("agentDelete", new JsonObject { ["id"] = S(a, "id") })["deleted"];
                case "component_reference":
                {
                    var keys = a["keys"] is JsonArray k ? k : Obj(a, "keys") as JsonArray;
                    var r = Q("agentComponents", new JsonObject { ["keys"] = keys?.DeepClone() ?? new JsonArray() });
                    if (keys == null || keys.Count == 0) return "Component keys: " + string.Join(", ", r["keys"]!.AsArray().Select(x => x!.GetValue<string>())) + ", scripts (array of {class, enabled, fields}).";
                    return Dump(r["defaults"]);
                }
                case "find_api": return FindApi(S(a, "query") ?? "");
                case "list_files":
                {
                    var dir = Resolve(string.IsNullOrWhiteSpace(S(a, "folder")) ? "." : S(a, "folder"));
                    if (!Directory.Exists(dir)) return "Error: no folder " + S(a, "folder");
                    var root = Path.GetFullPath(Root);
                    var files = Directory.EnumerateFiles(dir, string.IsNullOrWhiteSpace(S(a, "pattern")) ? "*" : S(a, "pattern"), SearchOption.AllDirectories)
                        .Select(f => Path.GetRelativePath(root, f).Replace('\\', '/'))
                        .Where(f => !f.StartsWith("Library/") && !f.StartsWith(".") && !f.Contains("/bin/") && !f.Contains("/obj/") && !f.EndsWith(".meta"))
                        .Take(501).ToList();
                    return files.Count == 0 ? "No files." : string.Join("\n", files.Take(500)) + (files.Count > 500 ? "\n... more files: narrow the folder or pattern" : "");
                }
                case "read_file":
                {
                    var path = Resolve(S(a, "path"));
                    if (!File.Exists(path)) return "Error: no file " + S(a, "path");
                    if (new FileInfo(path).Length > 4_000_000) return "Error: the file is too large to read";
                    var lines = File.ReadAllLines(path);
                    int offset = Math.Max(1, I(a, "offset", 1)), limit = Math.Clamp(I(a, "limit", 400), 1, 2000);
                    var sb = new StringBuilder();
                    for (int i = offset - 1; i < lines.Length && i < offset - 1 + limit; ++i) sb.Append(i + 1).Append('\t').AppendLine(lines[i]);
                    if (offset - 1 + limit < lines.Length) sb.AppendLine($"... {lines.Length - (offset - 1 + limit)} more lines (offset {offset + limit})");
                    return sb.Length == 0 ? "(empty file)" : sb.ToString();
                }
                case "write_file":
                {
                    var path = Resolve(S(a, "path"));
                    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                    bool existed = File.Exists(path);
                    File.WriteAllText(path, S(a, "content") ?? "");
                    return (existed ? "Replaced " : "Created ") + S(a, "path");
                }
                case "edit_file":
                {
                    var path = Resolve(S(a, "path"));
                    if (!File.Exists(path)) return "Error: no file " + S(a, "path");
                    var text = File.ReadAllText(path);
                    string oldText = S(a, "old_text") ?? "", newText = S(a, "new_text") ?? "";
                    // The model may write \n where the file has \r\n
                    if (!text.Contains(oldText) && text.Contains("\r\n")) { oldText = oldText.Replace("\r\n", "\n").Replace("\n", "\r\n"); newText = newText.Replace("\r\n", "\n").Replace("\n", "\r\n"); }
                    int at = oldText.Length == 0 ? -1 : text.IndexOf(oldText, StringComparison.Ordinal);
                    if (at < 0) return "Error: old_text was not found in the file; read it again and copy the text exactly";
                    if (text.IndexOf(oldText, at + 1, StringComparison.Ordinal) >= 0) return "Error: old_text appears more than once; include more lines around it";
                    File.WriteAllText(path, text[..at] + newText + text[(at + oldText.Length)..]);
                    return "Edited " + S(a, "path");
                }
                case "delete_file":
                {
                    var path = Resolve(S(a, "path"));
                    if (!File.Exists(path)) return "Error: no file " + S(a, "path");
                    File.Delete(path);
                    return "Deleted " + S(a, "path");
                }
                case "get_console":
                {
                    var r = Q("agentConsole", new JsonObject { ["count"] = I(a, "count", 40), ["errorsOnly"] = a["errors_only"]?.ToString() == "true" });
                    var lines = r["entries"]!.AsArray().Select(e => $"[{e!["level"]}] {e["message"]}").ToList();
                    return lines.Count == 0 ? "The console is empty." : string.Join("\n", lines);
                }
                case "get_selection": return Dump(Q("agentSelection"));
                case "select_entities":
                {
                    var ids = a["ids"] as JsonArray ?? Obj(a, "ids") as JsonArray ?? new JsonArray();
                    return "Selected " + Q("agentSelect", new JsonObject { ["ids"] = ids.DeepClone() })["selected"] + " objects";
                }
                case "save_scene": return "Saved " + Q("agentSave")["saved"];
                default: return "Error: unknown tool " + name;
            }
        }

        static string FindApi(string query)
        {
            var words = query.Split(new[] { ' ', '.', ',', '(', ')' }, StringSplitOptions.RemoveEmptyEntries).Select(w => w.ToLowerInvariant()).ToArray();
            if (words.Length == 0) return "Error: empty query";
            var hits = new List<(int score, string text)>();
            foreach (var t in Bridge.Symbols())
            {
                string type = t!["name"]!.GetValue<string>(), ns = t["ns"]!.GetValue<string>(), typeDoc = t["d"]?.GetValue<string>() ?? "";
                if (ns.StartsWith("System")) continue;
                int typeScore = words.Count(w => type.ToLowerInvariant().Contains(w)) * 3 + words.Count(w => typeDoc.ToLowerInvariant().Contains(w));
                var members = new List<(int, string)>();
                foreach (var group in new[] { "static", "instance" })
                    foreach (var m in t[group]!.AsArray())
                    {
                        string n = m!["n"]!.GetValue<string>(), sig = m["s"]!.GetValue<string>(), doc = m["d"]?.GetValue<string>() ?? "";
                        int score = words.Count(w => n.ToLowerInvariant().Contains(w)) * 3 + words.Count(w => doc.ToLowerInvariant().Contains(w)) + (typeScore > 0 ? typeScore : 0);
                        if (score > typeScore || typeScore > 0 && words.Length == 1) members.Add((score, $"  {(group == "static" ? "static " : "")}{sig}{(doc.Length > 0 ? "  // " + doc : "")}"));
                    }
                if (typeScore == 0 && members.Count == 0) continue;
                var best = members.OrderByDescending(m => m.Item1).Take(typeScore > 0 ? 40 : 8).Select(m => m.Item2);
                hits.Add((Math.Max(typeScore * 2, members.Count > 0 ? members.Max(m => m.Item1) : 0), $"{t["kind"]} {(ns.Length > 0 ? ns + "." : "")}{type}{(t["base"]?.GetValue<string>() is string b && b.Length > 0 && b != "object" ? " : " + b : "")}{(typeDoc.Length > 0 ? "  // " + typeDoc : "")}\n" + string.Join("\n", best)));
            }
            foreach (var pair in Bridge.ScriptTypes)
                if (words.Any(w => pair.Key.ToLowerInvariant().Contains(w)))
                    hits.Add((5, $"script {pair.Key} (inspector fields: {string.Join(", ", pair.Value.Fields.Select(f => f.FieldType.Name + " " + f.Name))})"));
            if (hits.Count == 0) return "Nothing found for \"" + query + "\". Try other words.";
            return string.Join("\n\n", hits.OrderByDescending(h => h.score).Take(6).Select(h => h.text));
        }
    }
}
