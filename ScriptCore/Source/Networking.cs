using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;

namespace Stardust
{
    /// <summary>
    /// HTTP and HTTPS requests. Three ways to use it, all safe for the engine API (results arrive on the main thread):
    /// <code>
    /// // async / await
    /// var text = await Http.GetStringAsync("https://example.com/data.json");
    /// // callback
    /// Http.Get("https://example.com/data.json", r => { if (r.success) Debug.Log(r.text); });
    /// // coroutine
    /// var request = Http.Get("https://example.com/data.json"); yield return request; Debug.Log(request.text);
    /// </code>
    /// </summary>
    public static class Http
    {
        static readonly HttpClient s_Client = CreateClient();
        static HttpClient CreateClient()
        {
            var handler = new SocketsHttpHandler { AutomaticDecompression = System.Net.DecompressionMethods.All, PooledConnectionLifetime = TimeSpan.FromMinutes(5) };
            var client = new HttpClient(handler) { Timeout = Timeout.InfiniteTimeSpan };
            client.DefaultRequestHeaders.UserAgent.ParseAdd("StardustEngine/1.0");
            return client;
        }

        /// <summary>Headers sent with every request (an API key, for example).</summary>
        public static HttpRequestHeaders defaultHeaders => s_Client.DefaultRequestHeaders;
        /// <summary>Seconds before a request gives up (0 = never).</summary>
        public static float timeout = 30;

        /// <summary>A request in progress; yield it in a coroutine or await it.</summary>
        public sealed class Request
        {
            internal readonly TaskCompletionSource<Response> completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
            internal readonly CancellationTokenSource cancel = new();
            public string url { get; internal set; }
            public string method { get; internal set; }
            /// <summary>0..1 while the body downloads (when the server sends its size).</summary>
            public float downloadProgress { get; internal set; }
            public bool isDone => completion.Task.IsCompleted;
            public Response response => isDone ? completion.Task.Result : null;
            public bool success => response?.success ?? false;
            public string text => response?.text;
            public byte[] data => response?.data;
            public string error => response?.error;
            public long statusCode => response?.statusCode ?? 0;
            public void Abort() => cancel.Cancel();
            public Task<Response> task => completion.Task;
            public System.Runtime.CompilerServices.TaskAwaiter<Response> GetAwaiter() => completion.Task.GetAwaiter();
        }

        public sealed class Response
        {
            /// <summary>2xx status and no network error.</summary>
            public bool success { get; internal set; }
            public long statusCode { get; internal set; }
            public string error { get; internal set; }
            public byte[] data { get; internal set; } = Array.Empty<byte>();
            public Dictionary<string, string> headers { get; } = new(StringComparer.OrdinalIgnoreCase);
            public string text => data == null ? null : Encoding.UTF8.GetString(data);
            /// <summary>The body parsed as JSON (null if it is not JSON).</summary>
            public JsonNode json { get { try { return JsonNode.Parse(text ?? ""); } catch { return null; } } }
            /// <summary>The body as T (System.Text.Json, case-insensitive names).</summary>
            public T FromJson<T>() => JsonSerializer.Deserialize<T>(data, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
        }

        /// <summary>Starts a request. body: null, string, byte[], JsonNode or any object (sent as JSON).</summary>
        public static Request Send(string method, string url, object body = null, IDictionary<string, string> headers = null, string contentType = null, Action<Response> onComplete = null)
        {
            var request = new Request { url = url, method = method.ToUpperInvariant() };
            var message = new HttpRequestMessage(new HttpMethod(request.method), url);
            if (headers != null) foreach (var h in headers) if (!message.Headers.TryAddWithoutValidation(h.Key, h.Value)) message.Content?.Headers.TryAddWithoutValidation(h.Key, h.Value);
            switch (body)
            {
                case null: break;
                case byte[] bytes: message.Content = new ByteArrayContent(bytes); message.Content.Headers.ContentType = new MediaTypeHeaderValue(contentType ?? "application/octet-stream"); break;
                case string s: message.Content = new StringContent(s, Encoding.UTF8, contentType ?? "text/plain"); break;
                case JsonNode node: message.Content = new StringContent(node.ToJsonString(), Encoding.UTF8, contentType ?? "application/json"); break;
                case HttpContent content: message.Content = content; break;
                case IDictionary<string, string> form when contentType == "application/x-www-form-urlencoded": message.Content = new FormUrlEncodedContent(form); break;
                default: message.Content = new StringContent(JsonSerializer.Serialize(body), Encoding.UTF8, contentType ?? "application/json"); break;
            }
            if (headers != null && message.Content != null) foreach (var h in headers) if (!message.Headers.Contains(h.Key)) message.Content.Headers.TryAddWithoutValidation(h.Key, h.Value);
            if (timeout > 0) request.cancel.CancelAfter(TimeSpan.FromSeconds(timeout));
            var context = SynchronizationContext.Current;
            _ = Run(request, message);
            if (onComplete != null) request.completion.Task.ContinueWith(t => { void call() { try { onComplete(t.Result); } catch (Exception e) { Debug.LogError("Http callback: " + e); } } if (context != null) context.Post(_ => call(), null); else MainThread.Run(call); }, TaskScheduler.Default);
            return request;
        }

        static async Task Run(Request request, HttpRequestMessage message)
        {
            var response = new Response();
            try
            {
                using var reply = await s_Client.SendAsync(message, HttpCompletionOption.ResponseHeadersRead, request.cancel.Token).ConfigureAwait(false);
                response.statusCode = (long)reply.StatusCode;
                foreach (var h in reply.Headers) response.headers[h.Key] = string.Join(",", h.Value);
                foreach (var h in reply.Content.Headers) response.headers[h.Key] = string.Join(",", h.Value);
                var total = reply.Content.Headers.ContentLength;
                using var stream = await reply.Content.ReadAsStreamAsync(request.cancel.Token).ConfigureAwait(false);
                using var buffer = new MemoryStream();
                var chunk = new byte[81920]; int read; long sum = 0;
                while ((read = await stream.ReadAsync(chunk, request.cancel.Token).ConfigureAwait(false)) > 0)
                {
                    buffer.Write(chunk, 0, read); sum += read;
                    if (total > 0) request.downloadProgress = (float)Math.Min(1.0, sum / (double)total.Value);
                }
                response.data = buffer.ToArray();
                response.success = reply.IsSuccessStatusCode;
                if (!response.success) response.error = $"HTTP {(int)reply.StatusCode} {reply.ReasonPhrase}";
            }
            catch (OperationCanceledException) { response.error = request.cancel.IsCancellationRequested ? "Aborted or timed out" : "Cancelled"; }
            catch (Exception e) { response.error = e.GetBaseException().Message; }
            finally { message.Dispose(); }
            request.downloadProgress = 1;
            request.completion.TrySetResult(response);
        }

        public static Request Get(string url, Action<Response> onComplete = null, IDictionary<string, string> headers = null) => Send("GET", url, null, headers, null, onComplete);
        public static Request Post(string url, object body, Action<Response> onComplete = null, IDictionary<string, string> headers = null, string contentType = null) => Send("POST", url, body, headers, contentType, onComplete);
        public static Request Put(string url, object body, Action<Response> onComplete = null, IDictionary<string, string> headers = null, string contentType = null) => Send("PUT", url, body, headers, contentType, onComplete);
        public static Request Delete(string url, Action<Response> onComplete = null, IDictionary<string, string> headers = null) => Send("DELETE", url, null, headers, null, onComplete);
        /// <summary>Posts form fields (application/x-www-form-urlencoded).</summary>
        public static Request PostForm(string url, IDictionary<string, string> fields, Action<Response> onComplete = null) => Send("POST", url, fields, null, "application/x-www-form-urlencoded", onComplete);

        public static async Task<Response> GetAsync(string url, IDictionary<string, string> headers = null) => await Get(url, null, headers);
        public static async Task<string> GetStringAsync(string url) { var r = await Get(url); if (!r.success) throw new HttpRequestException(r.error); return r.text; }
        public static async Task<byte[]> GetBytesAsync(string url) { var r = await Get(url); if (!r.success) throw new HttpRequestException(r.error); return r.data; }
        /// <summary>GETs JSON and deserializes it.</summary>
        public static async Task<T> GetJsonAsync<T>(string url) { var r = await Get(url); if (!r.success) throw new HttpRequestException(r.error); return r.FromJson<T>(); }
        /// <summary>POSTs body as JSON, returns the response.</summary>
        public static async Task<Response> PostJsonAsync(string url, object body, IDictionary<string, string> headers = null) => await Post(url, body, null, headers, "application/json");
        /// <summary>Downloads to a file (for example under Application.GetPersistentDataPath()).</summary>
        public static async Task<bool> DownloadFileAsync(string url, string path)
        {
            var r = await Get(url);
            if (!r.success) { Debug.LogWarning($"Download {url}: {r.error}"); return false; }
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".");
            await File.WriteAllBytesAsync(path, r.data);
            return true;
        }
        /// <summary>Encodes text for a URL query (spaces, accents, symbols).</summary>
        public static string Escape(string text) => Uri.EscapeDataString(text ?? "");
    }
}
