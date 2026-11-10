// Video Player: decodes a clip into a texture shown by a Render Texture, the object's material, or its UI Image.
// MPEG-1 (.mpg) plays everywhere through pl_mpeg; MP4, MOV, WMV and AVI use Windows Media Foundation.
#include "UI/VideoPlayer.h"
#include "Assets/AssetManager.h"
#include "Assets/Pak.h"
#include "Audio/AudioEngine.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <algorithm>
#include <fstream>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#define PL_MPEG_IMPLEMENTATION
#include "../../ThirdParty/pl_mpeg.h"
#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#endif

namespace Stardust {
namespace {

// Decoded audio goes to a miniaudio stream. Each block is placed at its own time: early blocks get silence
// in front, late ones lose their old part, and a stream that drifts from the picture is lined up again.
struct AudioOut {
    u64 stream = 0;
    u32 channels = 0, rate = 0;
    double end = -1; // video time where the queued audio ends
    void Reset() { end = -1; if (stream) AudioEngine::ResetStream(stream); }
    void Push(const float* samples, u32 frames, double start, double now) {
        if (!stream || !rate || !frames) return;
        const double queued = (double)AudioEngine::StreamBuffered(stream) / rate;
        // What is heard right now, in video time; with nothing queued it is simply now
        const double heard = queued > 0 && end >= 0 ? end - queued : now;
        if (std::abs(heard - now) > 0.1) { Reset(); end = -1; }
        double at = end >= 0 && AudioEngine::StreamBuffered(stream) > 0 ? end : now;
        if (start > at + 0.005) { // a gap: silence until this block
            std::vector<float> silence((size_t)((start - at) * rate) * channels, 0.f);
            AudioEngine::PushStream(stream, silence.data(), (u32)(silence.size() / channels));
            at = start;
        } else if (start < at - 0.005) { // overlaps what is queued or is already late: skip that part
            const u32 skip = (u32)std::min<double>(frames, (at - start) * rate);
            samples += (size_t)skip * channels; frames -= skip; start += (double)skip / rate;
            if (!frames) return;
        }
        AudioEngine::PushStream(stream, samples, frames);
        end = start + (double)frames / rate;
    }
};

class Decoder {
public:
    virtual ~Decoder() = default;
    int width = 0, height = 0;
    double duration = 0;
    u32 channels = 0, sampleRate = 0; // 0: no audio
    virtual void Seek(double seconds) = 0;
    // Decodes up to the given time; true when rgba holds a new frame (width * height * 4 bytes)
    virtual bool DecodeTo(double seconds, std::vector<u8>& rgba, AudioOut& audio) = 0;
};

// ---------------------------------------------------------------- MPEG-1 (pl_mpeg), from a file or a pak entry
class MpegDecoder final : public Decoder {
public:
    bool Open(const std::filesystem::path& path) {
        std::filesystem::path archive = path;
        if (!Pak::Locate(path, archive, m_Offset, m_Size, &m_Cipher)) { std::error_code ec; m_Size = std::filesystem::file_size(path, ec); if (ec) return false; }
        m_File.open(archive, std::ios::binary); m_File.seekg(m_Offset);
        auto* buffer = plm_buffer_create_with_callbacks(Load, SeekTo, Tell, m_Size, this);
        m_Plm = plm_create_with_buffer(buffer, TRUE);
        if (!m_Plm || !plm_has_headers(m_Plm) || !plm_get_num_video_streams(m_Plm)) return false;
        width = plm_get_width(m_Plm); height = plm_get_height(m_Plm); duration = plm_get_duration(m_Plm);
        plm_set_video_decode_callback(m_Plm, OnFrame, this);
        if (plm_get_num_audio_streams(m_Plm) > 0) {
            channels = 2; sampleRate = (u32)plm_get_samplerate(m_Plm);
            plm_set_audio_enabled(m_Plm, TRUE); plm_set_audio_decode_callback(m_Plm, OnAudio, this);
            plm_set_audio_lead_time(m_Plm, 0.1);
        } else plm_set_audio_enabled(m_Plm, FALSE);
        return true;
    }
    ~MpegDecoder() override { if (m_Plm) plm_destroy(m_Plm); }
    void Seek(double seconds) override { plm_seek(m_Plm, std::clamp(seconds, 0.0, duration), FALSE); }
    bool DecodeTo(double seconds, std::vector<u8>& rgba, AudioOut& audio) override {
        m_Rgba = &rgba; m_Audio = &audio; m_NewFrame = false; m_Now = seconds;
        const double step = seconds - plm_get_time(m_Plm);
        if (step > 0) plm_decode(m_Plm, std::min(step, 0.25));
        return m_NewFrame;
    }
private:
    static void Load(plm_buffer_t* buffer, void* user) {
        auto& s = *(MpegDecoder*)user; u8 data[65536];
        const u64 amount = std::min<u64>(sizeof(data), s.m_Size - s.m_Position);
        if (amount) { s.m_File.read((char*)data, amount); const u64 read = (u64)s.m_File.gcount(); Pak::Decrypt(s.m_Cipher, s.m_Position, data, read); s.m_Position += read; plm_buffer_write(buffer, data, (size_t)read); }
        if (s.m_Position >= s.m_Size) plm_buffer_signal_end(buffer);
    }
    static void SeekTo(plm_buffer_t*, size_t position, void* user) { auto& s = *(MpegDecoder*)user; s.m_Position = std::min<u64>(position, s.m_Size); s.m_File.clear(); s.m_File.seekg(s.m_Offset + s.m_Position); }
    static size_t Tell(plm_buffer_t*, void* user) { return (size_t)((MpegDecoder*)user)->m_Position; }
    static void OnFrame(plm_t*, plm_frame_t* frame, void* user) {
        auto& s = *(MpegDecoder*)user;
        s.m_Rgba->resize((size_t)frame->width * frame->height * 4);
        plm_frame_to_rgba(frame, s.m_Rgba->data(), frame->width * 4);
        for (size_t i = 3; i < s.m_Rgba->size(); i += 4) (*s.m_Rgba)[i] = 255;
        s.m_NewFrame = true;
    }
    static void OnAudio(plm_t*, plm_samples_t* samples, void* user) { auto& s = *(MpegDecoder*)user; if (s.m_Audio) s.m_Audio->Push(samples->interleaved, samples->count, samples->time, s.m_Now); }

    plm_t* m_Plm = nullptr;
    std::ifstream m_File; u64 m_Offset = 0, m_Size = 0, m_Position = 0, m_Cipher = 0;
    std::vector<u8>* m_Rgba = nullptr; AudioOut* m_Audio = nullptr; bool m_NewFrame = false; double m_Now = 0;
};

#ifdef _WIN32
// ---------------------------------------------------------------- Media Foundation: MP4 / MOV / WMV / AVI
template <typename T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// A pak entry as a COM stream: read in pieces and decrypted on the way, so long clips never load whole
class PakStream final : public IStream {
public:
    PakStream(const std::filesystem::path& archive, u64 offset, u64 size, u64 cipher) : m_File(archive, std::ios::binary), m_Offset(offset), m_Size(size), m_Cipher(cipher) {}
    bool Ok() const { return (bool)m_File; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (id == __uuidof(IUnknown) || id == __uuidof(IStream) || id == __uuidof(ISequentialStream)) { *out = static_cast<IStream*>(this); AddRef(); return S_OK; }
        *out = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++m_Refs; }
    ULONG STDMETHODCALLTYPE Release() override { const long n = --m_Refs; if (!n) delete this; return (ULONG)n; }
    HRESULT STDMETHODCALLTYPE Read(void* out, ULONG count, ULONG* read) override {
        const u64 amount = std::min<u64>(count, m_Size - std::min(m_Position, m_Size));
        m_File.clear(); m_File.seekg(m_Offset + m_Position); m_File.read((char*)out, amount);
        const u64 got = (u64)m_File.gcount();
        Pak::Decrypt(m_Cipher, m_Position, out, got);
        m_Position += got;
        if (read) *read = (ULONG)got;
        return got == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* at) override {
        const long long base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? (long long)m_Position : (long long)m_Size;
        const long long target = base + move.QuadPart;
        if (target < 0) return STG_E_INVALIDFUNCTION;
        m_Position = (u64)target;
        if (at) at->QuadPart = m_Position;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override { *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = m_Size; stat->grfMode = STGM_READ; return S_OK; }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
private:
    std::ifstream m_File; u64 m_Offset, m_Size, m_Cipher, m_Position = 0; std::atomic<long> m_Refs{ 1 };
};

class MediaFoundationDecoder final : public Decoder {
public:
    bool Open(const std::filesystem::path& path) {
        static bool started = false;
        if (!started) { CoInitializeEx(nullptr, COINIT_MULTITHREADED); if (FAILED(MFStartup(MF_VERSION))) return false; started = true; }
        IMFAttributes* attributes = nullptr;
        MFCreateAttributes(&attributes, 2);
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); // converts any codec output to RGB32
        HRESULT hr = E_FAIL;
        // Streamed from the disk: a loose file directly, a pak entry through PakStream
        std::filesystem::path archive; u64 offset = 0, size = 0, cipher = 0;
        if (Pak::Locate(path, archive, offset, size, &cipher)) {
            auto* stream = new PakStream(archive, offset, size, cipher);
            m_Stream = stream;
            if (stream->Ok() && SUCCEEDED(MFCreateMFByteStreamOnStream(m_Stream, &m_ByteStream))) hr = MFCreateSourceReaderFromByteStream(m_ByteStream, attributes, &m_Reader);
        } else hr = MFCreateSourceReaderFromURL(path.wstring().c_str(), attributes, &m_Reader);
        Release(attributes);
        if (FAILED(hr)) return false;

        IMFMediaType* video = nullptr; MFCreateMediaType(&video);
        video->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); video->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = m_Reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, video);
        Release(video);
        if (FAILED(hr)) return false;
        IMFMediaType* current = nullptr;
        if (FAILED(m_Reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current))) return false;
        UINT32 w = 0, h = 0; MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
        m_Stride = (INT32)MFGetAttributeUINT32(current, MF_MT_DEFAULT_STRIDE, w * 4);
        Release(current);
        width = (int)w; height = (int)h;
        if (!width || !height) return false;

        IMFMediaType* sound = nullptr; MFCreateMediaType(&sound);
        sound->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); sound->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (SUCCEEDED(m_Reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, sound)) &&
            SUCCEEDED(m_Reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &current))) {
            channels = MFGetAttributeUINT32(current, MF_MT_AUDIO_NUM_CHANNELS, 0);
            sampleRate = MFGetAttributeUINT32(current, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
            Release(current);
        } else m_Reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, FALSE);
        Release(sound);
        if (!channels || !sampleRate) { channels = sampleRate = 0; m_Reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, FALSE); }

        PROPVARIANT length; PropVariantInit(&length);
        if (SUCCEEDED(m_Reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &length))) duration = (double)length.uhVal.QuadPart / 1e7;
        PropVariantClear(&length);
        return true;
    }
    ~MediaFoundationDecoder() override { Release(m_Pending); Release(m_Reader); Release(m_ByteStream); Release(m_Stream); }

    void Seek(double seconds) override {
        PROPVARIANT at; PropVariantInit(&at); at.vt = VT_I8; at.hVal.QuadPart = (LONGLONG)(std::clamp(seconds, 0.0, duration) * 1e7);
        m_Reader->SetCurrentPosition(GUID_NULL, at);
        PropVariantClear(&at);
        Release(m_Pending); m_VideoEnded = m_AudioEnded = false; m_AudioTime = seconds;
    }

    bool DecodeTo(double seconds, std::vector<u8>& rgba, AudioOut& audio) override {
        bool newFrame = false;
        // Video: every frame up to the time, only the last one is converted
        while (!m_VideoEnded) {
            if (!m_Pending) {
                DWORD flags = 0; LONGLONG stamp = 0; IMFSample* sample = nullptr;
                if (FAILED(m_Reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &stamp, &sample)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { Release(sample); m_VideoEnded = true; break; }
                if (!sample) continue;
                m_Pending = sample; m_PendingTime = (double)stamp / 1e7;
            }
            if (m_PendingTime > seconds) break;
            newFrame |= Convert(m_Pending, rgba);
            Release(m_Pending);
        }
        // Audio: a little ahead of the picture so the stream never runs dry
        while (channels && !m_AudioEnded && m_AudioTime < seconds + 0.15) {
            DWORD flags = 0; LONGLONG stamp = 0; IMFSample* sample = nullptr;
            if (FAILED(m_Reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &stamp, &sample)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { Release(sample); m_AudioEnded = true; break; }
            if (!sample) continue;
            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* data = nullptr; DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                    const u32 frames = length / (sizeof(float) * channels);
                    const double start = (double)stamp / 1e7;
                    audio.Push((const float*)data, frames, start, seconds);
                    m_AudioTime = start + (double)frames / sampleRate;
                    buffer->Unlock();
                }
                Release(buffer);
            }
            Release(sample);
        }
        return newFrame;
    }

private:
    bool Convert(IMFSample* sample, std::vector<u8>& rgba) {
        IMFMediaBuffer* buffer = nullptr;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
        BYTE* data = nullptr; DWORD length = 0;
        bool ok = false;
        if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
            const size_t pitch = (size_t)std::abs(m_Stride);
            if (length >= pitch * height) {
                rgba.resize((size_t)width * height * 4);
                for (int y = 0; y < height; ++y) {
                    const BYTE* row = data + (m_Stride < 0 ? (size_t)(height - 1 - y) : (size_t)y) * pitch;
                    u8* out = rgba.data() + (size_t)y * width * 4;
                    const u32* in = (const u32*)row; u32* to = (u32*)out;
                    for (int x = 0; x < width; ++x) { const u32 v = in[x]; to[x] = (v & 0x0000FF00u) | ((v >> 16) & 0xFFu) | ((v & 0xFFu) << 16) | 0xFF000000u; }
                }
                ok = true;
            }
            buffer->Unlock();
        }
        Release(buffer);
        return ok;
    }

    IStream* m_Stream = nullptr; IMFByteStream* m_ByteStream = nullptr; IMFSourceReader* m_Reader = nullptr;
    IMFSample* m_Pending = nullptr; double m_PendingTime = 0, m_AudioTime = 0;
    INT32 m_Stride = 0; bool m_VideoEnded = false, m_AudioEnded = false;
};
#endif

// Runs a decoder on its own thread: the game only picks up the newest finished frame
class ThreadedDecoder final : public Decoder {
public:
    ThreadedDecoder(std::unique_ptr<Decoder> inner, AudioOut* audio) : m_Inner(std::move(inner)), m_Audio(audio) {
        width = m_Inner->width; height = m_Inner->height; duration = m_Inner->duration; channels = m_Inner->channels; sampleRate = m_Inner->sampleRate;
        m_Worker = std::thread([this] { Run(); });
    }
    ~ThreadedDecoder() override { { std::lock_guard lock(m_Mutex); m_Quit = true; } m_Wake.notify_all(); if (m_Worker.joinable()) m_Worker.join(); }
    void Seek(double seconds) override { { std::lock_guard lock(m_Mutex); m_SeekTo = seconds; m_Target = seconds; m_Seek = true; } m_Wake.notify_all(); }
    bool DecodeTo(double seconds, std::vector<u8>& rgba, AudioOut&) override {
        std::lock_guard lock(m_Mutex);
        if (seconds != m_Target) { m_Target = seconds; m_Wake.notify_all(); }
        if (!m_Fresh) return false;
        std::swap(rgba, m_Latest); m_Fresh = false;
        return true;
    }
private:
    void Run() {
#ifdef _WIN32
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
        std::vector<u8> frame;
        double done = -1;
        while (true) {
            double target;
            {
                std::unique_lock lock(m_Mutex);
                m_Wake.wait(lock, [&] { return m_Quit || m_Seek || m_Target > done; });
                if (m_Quit) break;
                if (m_Seek) { m_Seek = false; m_Inner->Seek(m_SeekTo); m_Audio->Reset(); done = m_SeekTo - 0.001; }
                target = m_Target;
            }
            if (m_Inner->DecodeTo(target, frame, *m_Audio)) { std::lock_guard lock(m_Mutex); std::swap(frame, m_Latest); m_Fresh = true; }
            done = target;
        }
#ifdef _WIN32
        CoUninitialize();
#endif
    }
    std::unique_ptr<Decoder> m_Inner; AudioOut* m_Audio;
    std::thread m_Worker; std::mutex m_Mutex; std::condition_variable m_Wake;
    std::vector<u8> m_Latest; bool m_Fresh = false, m_Seek = false, m_Quit = false;
    double m_Target = 0, m_SeekTo = 0;
};

struct State {
    std::string clip, target;
    std::unique_ptr<Decoder> decoder;
    Ref<RHI::Texture> texture;
    std::vector<u8> pixels;
    AudioOut audio;
    double time = 0;
    bool failed = false;
    ~State() { decoder.reset(); if (audio.stream) AudioEngine::CloseStream(audio.stream); if (!target.empty()) AssetManager::SetRuntimeTexture(target, nullptr); }
};
std::unordered_map<Scene*, std::unordered_map<EntityID, Scope<State>>> players;

std::unique_ptr<Decoder> OpenDecoder(const std::string& clip) {
    const auto path = AssetManager::ToAbsolute(clip);
    std::string ext = FileSystem::Utf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (ext == ".mpg" || ext == ".mpeg") { auto d = std::make_unique<MpegDecoder>(); if (d->Open(path)) return d; return nullptr; }
#ifdef _WIN32
    auto d = std::make_unique<MediaFoundationDecoder>();
    if (d->Open(path)) return d;
#endif
    return nullptr;
}
} // namespace

bool VideoPlayerSystem::IsVideoFile(const std::string& path) {
    std::string ext = FileSystem::Utf8(std::filesystem::u8path(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    for (const char* e : SupportedExtensions()) if (ext == e) return true;
    return false;
}
const std::vector<const char*>& VideoPlayerSystem::SupportedExtensions() {
#ifdef _WIN32
    static const std::vector<const char*> list = { ".mp4", ".m4v", ".mov", ".wmv", ".avi", ".mpg", ".mpeg" };
#else
    static const std::vector<const char*> list = { ".mpg", ".mpeg" };
#endif
    return list;
}

VideoPlayerSystem::ClipInfo VideoPlayerSystem::Probe(const std::string& clip) {
    ClipInfo info;
    auto decoder = OpenDecoder(clip);
    if (!decoder) return info;
    info.width = decoder->width; info.height = decoder->height; info.duration = decoder->duration;
    info.channels = decoder->channels; info.sampleRate = decoder->sampleRate;
    std::vector<u8> rgba; AudioOut silent;
    for (int step = 1; step <= 30; ++step)
        if (decoder->DecodeTo(step / 30.0, rgba, silent)) {
            ++info.framesDecoded;
            if (rgba.size() >= 8) { u64 v = 0; for (size_t i = rgba.size() / 2; i < rgba.size() / 2 + 8; ++i) v = v << 8 | rgba[i]; info.firstPixel = v; }
        }
    return info;
}

Ref<RHI::Texture> VideoPlayerSystem::Output(Scene& scene, EntityID e) {
    auto it = players.find(&scene);
    if (it == players.end()) return nullptr;
    auto found = it->second.find(e);
    return found != it->second.end() && found->second ? found->second->texture : nullptr;
}

void VideoPlayerSystem::Remove(Scene& scene, EntityID e) { if (auto it = players.find(&scene); it != players.end()) { it->second.erase(e); if (it->second.empty()) players.erase(it); } }
void VideoPlayerSystem::Clear(Scene& scene) { players.erase(&scene); }

void VideoPlayerSystem::Update(Scene& scene, float dt) {
    auto& reg = scene.Reg();
    reg.Each<VideoPlayerComponent>([&](EntityID e, VideoPlayerComponent& c) {
        auto& state = players[&scene][e];
        if (!reg.IsEnabled<VideoPlayerComponent>(e) || !scene.IsActiveInHierarchy(e) || c.clip.empty()) { if (state && state->audio.stream) AudioEngine::SetStreamPlaying(state->audio.stream, false, 0, 1); return; }
        const std::string target = c.renderMode == 0 && !c.targetTexture.empty() ? c.targetTexture : "runtime:video:" + std::to_string(reg.Get<IDComponent>(e).uuid);
        if (!state || state->clip != c.clip || state->target != target) {
            state = MakeScope<State>();
            state->clip = c.clip; state->target = target;
            state->decoder = OpenDecoder(c.clip);
            if (!state->decoder) {
                state->failed = true; c.playing = false;
#ifdef _WIN32
                SD_WARN("Video Player: cannot play {} (supported: MP4, MOV, WMV, AVI with the codecs Windows has, and MPEG-1 .mpg)", c.clip);
#else
                SD_WARN("Video Player: cannot play {} (on Linux only MPEG-1 .mpg is supported)", c.clip);
#endif
                return;
            }
            c.duration = (float)state->decoder->duration;
            if (state->decoder->channels && c.audio) { state->audio.stream = AudioEngine::OpenStream(state->decoder->channels, state->decoder->sampleRate); state->audio.channels = state->decoder->channels; state->audio.rate = state->decoder->sampleRate; }
            state->decoder = std::make_unique<ThreadedDecoder>(std::move(state->decoder), &state->audio);
            state->time = std::clamp<double>(c.time, 0, c.duration);
            if (state->time > 0) state->decoder->Seek(state->time);
        }
        if (state->failed || !state->decoder) return;
        auto& decoder = *state->decoder;

        // A script or the inspector moved the time: seek there
        if (std::abs(c.time - (float)state->time) > 0.05f) {
            state->time = std::clamp<double>(c.time, 0, c.duration);
            decoder.Seek(state->time);
        }
        if (c.playing) state->time += std::clamp(dt, 0.f, 0.1f) * std::clamp(c.speed, 0.f, 8.f);
        if (c.duration > 0 && state->time >= c.duration) {
            if (c.loop) { state->time = std::fmod(state->time, (double)c.duration); decoder.Seek(0); }
            else { state->time = c.duration; c.playing = false; }
        }
        if (decoder.DecodeTo(state->time, state->pixels, state->audio) && AssetManager::Device()) {
            auto* device = AssetManager::Device();
            const bool wantLit = c.renderMode == 1 && !reg.Has<RectTransformComponent>(e);
            if (!state->texture || (int)state->texture->Desc().width != decoder.width || (int)state->texture->Desc().height != decoder.height || (state->texture->Desc().format == RHI::Format::RGBA8_SRGB) != wantLit) {
                // A lit material decodes sRGB like any texture; UI and Render Textures show the pixels as they are
                const bool lit = c.renderMode == 1 && !reg.Has<RectTransformComponent>(e);
                RHI::TextureDesc d; d.width = decoder.width; d.height = decoder.height; d.format = lit ? RHI::Format::RGBA8_SRGB : RHI::Format::RGBA8_UNORM;
                d.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::CopyDst; d.name = "Video Frame";
                state->texture = device->CreateTexture(d);
                AssetManager::SetRuntimeTexture(state->target, state->texture);
            }
            device->UploadTexture(state->texture.get(), state->pixels.data(), state->pixels.size());
        }
        c.time = (float)state->time;
        if (state->audio.stream) AudioEngine::SetStreamPlaying(state->audio.stream, c.playing && c.audio, std::clamp(c.volume, 0.f, 1.f), std::clamp(c.speed, 0.1f, 8.f));

        // On a UI object the video shows in its Image
        if (reg.Has<RectTransformComponent>(e)) {
            if (!reg.Has<UIImageComponent>(e)) reg.Add<UIImageComponent>(e);
            auto& image = reg.Get<UIImageComponent>(e); image.texture = state->target; image.sprite.clear(); image.preserveAspect = true;
        }
    });
}
} // namespace Stardust
