using System;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using Stardust.Interop;

namespace Stardust
{
    public static class AssetPacks
    {
        public static unsafe bool Mount(string file)
        {
            fixed (byte* p = Native.Utf8(Path.GetFullPath(file))) return Native.Api.MountPak(p) != 0;
        }

        public static unsafe bool Unmount(string file) {
            fixed(byte* p=Native.Utf8(Path.GetFullPath(file)))return Native.Api.UnmountPak(p)!=0;
        }

        // Download to an application-selected writable folder; mount only a complete download.
        // A newer mounted pack overrides earlier packs for subsequent asset loads.
        public static async Task<bool> DownloadAndMountAsync(string url, string destination, CancellationToken cancellation = default)
        {
            var uri = new Uri(url);
            if (uri.Scheme != Uri.UriSchemeHttps) throw new ArgumentException("Asset downloads require HTTPS", nameof(url));
            string full = Path.GetFullPath(destination);
            Directory.CreateDirectory(Path.GetDirectoryName(full));
            string temporary = full + "." + Guid.NewGuid().ToString("N") + ".download";
            try
            {
                using var client = new HttpClient();
                using var response = await client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, cancellation);
                response.EnsureSuccessStatusCode();
                using (var output = File.Create(temporary)) await response.Content.CopyToAsync(output, cancellation);
                File.Move(temporary, full, true);
                return Mount(full);
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
        }
    }
}
