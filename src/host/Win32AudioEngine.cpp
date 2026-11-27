#include "aero/host/Win32AudioEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <objbase.h>
#endif

namespace aero {

#if defined(_WIN32)
namespace detail::win32_audio {

template <typename T>
struct ComPtr final {
    T* ptr = nullptr;
    ComPtr() noexcept = default;
    ~ComPtr() {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
};

struct DecodedClip final {
    std::uint32_t sampleRate = 0u;
    std::uint16_t channels = 0u;
    std::vector<std::int16_t> samples;
};

[[nodiscard]] std::uint16_t ReadLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8u);
}

[[nodiscard]] std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) |
           (static_cast<std::uint32_t>(p[3]) << 24u);
}

[[nodiscard]] bool DecodeWavFileToPcm16(
    std::string_view filePath,
    std::uint32_t& outSampleRate,
    std::uint16_t& outChannels,
    std::vector<std::int16_t>& outSamples) {
    outSampleRate = 0u;
    outChannels = 0u;
    outSamples.clear();

    const std::filesystem::path p{std::string(filePath)};
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }
    const std::streamoff szOff = in.tellg();
    if (szOff < 44 || szOff > 64 * 1024 * 1024) {
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(szOff));
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), szOff)) {
        return false;
    }
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    std::uint16_t audioFormat = 0u;
    std::uint16_t channels = 0u;
    std::uint32_t sampleRate = 0u;
    std::uint16_t bitsPerSample = 0u;
    std::size_t dataOffset = 0u;
    std::size_t dataSize = 0u;
    bool hasFmt = false;
    bool hasData = false;

    std::size_t pos = 12u;
    while (pos + 8u <= bytes.size()) {
        const std::uint8_t* chId = bytes.data() + pos;
        const std::uint32_t chSize = ReadLe32(bytes.data() + pos + 4u);
        pos += 8u;
        if (pos + chSize > bytes.size()) {
            break;
        }
        if (std::memcmp(chId, "fmt ", 4) == 0 && chSize >= 16u) {
            const std::uint8_t* f = bytes.data() + pos;
            audioFormat = ReadLe16(f + 0u);
            channels = ReadLe16(f + 2u);
            sampleRate = ReadLe32(f + 4u);
            bitsPerSample = ReadLe16(f + 14u);
            if (audioFormat == 0xFFFEu && chSize >= 40u) {
                audioFormat = ReadLe16(f + 24u);
            }
            hasFmt = true;
        } else if (std::memcmp(chId, "data", 4) == 0) {
            dataOffset = pos;
            dataSize = chSize;
            hasData = true;
        }
        pos += chSize + (chSize & 1u);
    }

    if (!hasFmt || !hasData || channels == 0u || sampleRate == 0u || dataSize == 0u) {
        return false;
    }

    const std::uint8_t* src = bytes.data() + dataOffset;
    if (audioFormat == 1u && bitsPerSample == 16u) {
        const std::size_t n = dataSize / 2u;
        outSamples.resize(n);
        for (std::size_t i = 0u; i < n; ++i) {
            outSamples[i] = static_cast<std::int16_t>(ReadLe16(src + i * 2u));
        }
    } else if (audioFormat == 1u && bitsPerSample == 8u) {
        outSamples.resize(dataSize);
        for (std::size_t i = 0u; i < dataSize; ++i) {
            outSamples[i] = static_cast<std::int16_t>((static_cast<int>(src[i]) - 128) << 8);
        }
    } else if (audioFormat == 1u && bitsPerSample == 24u) {
        const std::size_t n = dataSize / 3u;
        outSamples.resize(n);
        for (std::size_t i = 0u; i < n; ++i) {
            const std::int32_t v =
                (static_cast<std::int32_t>(src[i * 3u + 0u]) << 8) |
                (static_cast<std::int32_t>(src[i * 3u + 1u]) << 16) |
                (static_cast<std::int32_t>(static_cast<std::int8_t>(src[i * 3u + 2u])) << 24);
            outSamples[i] = static_cast<std::int16_t>(v >> 16);
        }
    } else if (audioFormat == 3u && bitsPerSample == 32u) {
        const std::size_t n = dataSize / 4u;
        outSamples.resize(n);
        for (std::size_t i = 0u; i < n; ++i) {
            float f = 0.0f;
            std::memcpy(&f, src + i * 4u, 4u);
            const float c = std::clamp(f, -1.0f, 1.0f);
            outSamples[i] = static_cast<std::int16_t>(std::lround(c * 32767.0f));
        }
    } else {
        return false;
    }

    outSampleRate = sampleRate;
    outChannels = channels;
    return !outSamples.empty();
}

[[nodiscard]] bool DecodeWithMediaFoundationPcm16(
    std::string_view filePath,
    std::uint32_t& outSampleRate,
    std::uint16_t& outChannels,
    std::vector<std::int16_t>& outSamples) {
    outSampleRate = 0u;
    outChannels = 0u;
    outSamples.clear();
    if (filePath.empty()) {
        return false;
    }

    int wideLen = MultiByteToWideChar(
        CP_UTF8, 0, filePath.data(), static_cast<int>(filePath.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    if (wideLen <= 0) {
        cp = CP_ACP;
        wideLen = MultiByteToWideChar(
            cp, 0, filePath.data(), static_cast<int>(filePath.size()), nullptr, 0);
    }
    if (wideLen <= 0) {
        return false;
    }
    std::wstring wpath(static_cast<std::size_t>(wideLen), L'\0');
    (void)MultiByteToWideChar(
        cp, 0, filePath.data(), static_cast<int>(filePath.size()), wpath.data(), wideLen);

    struct ComScope final {
        bool uninit = false;
        HRESULT hr = S_OK;
        ComScope() noexcept {
            hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            uninit = (hr == S_OK || hr == S_FALSE);
        }
        ~ComScope() {
            if (uninit) {
                CoUninitialize();
            }
        }
        [[nodiscard]] bool Ok() const noexcept {
            return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        }
    } comScope;
    if (!comScope.Ok()) {
        return false;
    }

    struct MfScope final {
        bool started = false;
        MfScope() noexcept {
            started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        }
        ~MfScope() {
            if (started) {
                (void)MFShutdown();
            }
        }
    } mfScope;
    if (!mfScope.started) {
        return false;
    }

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader.ptr)) || !reader.ptr) {
        return false;
    }
    (void)reader.ptr->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader.ptr->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) {
        return false;
    }

    ComPtr<IMFMediaType> partialType;
    if (FAILED(MFCreateMediaType(&partialType.ptr)) || !partialType.ptr) {
        return false;
    }
    if (FAILED(partialType.ptr->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(partialType.ptr->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(partialType.ptr->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16u)) ||
        FAILED(reader.ptr->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, partialType.ptr))) {
        return false;
    }

    ComPtr<IMFMediaType> actualType;
    if (FAILED(reader.ptr->GetCurrentMediaType(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actualType.ptr)) ||
        !actualType.ptr) {
        return false;
    }

    UINT32 sr = 0u;
    UINT32 ch = 0u;
    if (FAILED(actualType.ptr->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr)) ||
        FAILED(actualType.ptr->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch)) ||
        sr == 0u || ch == 0u) {
        return false;
    }

    for (;;) {
        DWORD streamIdx = 0u;
        DWORD flags = 0u;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader.ptr->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0u,
            &streamIdx,
            &flags,
            &timestamp,
            &sample.ptr);
        if (FAILED(hr)) {
            outSamples.clear();
            return false;
        }
        if (sample.ptr) {
            ComPtr<IMFMediaBuffer> buf;
            if (SUCCEEDED(sample.ptr->ConvertToContiguousBuffer(&buf.ptr)) && buf.ptr) {
                BYTE* data = nullptr;
                DWORD cb = 0u;
                if (SUCCEEDED(buf.ptr->Lock(&data, nullptr, &cb)) && data && cb >= 2u) {
                    const std::size_t sampleCount = cb / 2u;
                    const std::size_t oldSize = outSamples.size();
                    outSamples.resize(oldSize + sampleCount);
                    std::memcpy(outSamples.data() + oldSize, data, sampleCount * 2u);
                    (void)buf.ptr->Unlock();
                }
            }
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
            break;
        }
        if (!sample.ptr && (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) == 0u) {
            break;
        }
    }

    if (outSamples.empty()) {
        return false;
    }
    outSampleRate = sr;
    outChannels = static_cast<std::uint16_t>(ch);
    return true;
}

} // namespace detail::win32_audio

struct Win32AudioEngine::Impl final {
    struct PlayRequest final {
        std::string path;
        float volume = 1.0f;
    };
    struct Voice final {
        HWAVEOUT hwo = nullptr;
        WAVEHDR hdr{};
        bool prepared = false;
        std::vector<std::int16_t> scaledPcm;
    };

    static constexpr std::size_t kMaxVoices = 8u;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<PlayRequest> queue;
    std::unordered_map<std::string, std::shared_ptr<const detail::win32_audio::DecodedClip>> cache;
    std::vector<std::unique_ptr<Voice>> voices;
    bool stopping = false;
    std::thread worker;

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        for (auto& v : voices) {
            DestroyVoice(std::move(v));
        }
        voices.clear();
    }

    void EnsureStartedLocked() {
        if (!worker.joinable() && !stopping) {
            worker = std::thread([this]() { WorkerLoop(); });
        }
    }

    void Enqueue(std::string_view path, float volume) {
        if (path.empty()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) {
                return;
            }
            EnsureStartedLocked();
            queue.push_back(PlayRequest{std::string(path), std::clamp(volume, 0.0f, 1.0f)});
        }
        cv.notify_one();
    }

private:
    static void DestroyVoice(std::unique_ptr<Voice> v) noexcept {
        if (!v) {
            return;
        }
        if (v->hwo) {
            (void)waveOutReset(v->hwo);
            if (v->prepared) {
                (void)waveOutUnprepareHeader(v->hwo, &v->hdr, sizeof(WAVEHDR));
                v->prepared = false;
            }
            (void)waveOutClose(v->hwo);
            v->hwo = nullptr;
        }
    }

    void ReapCompletedVoices() noexcept {
        for (std::size_t i = 0u; i < voices.size(); ) {
            Voice* v = voices[i].get();
            if (!v || !v->hwo || (v->hdr.dwFlags & WHDR_DONE) != 0u) {
                DestroyVoice(std::move(voices[i]));
                voices.erase(voices.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    [[nodiscard]] std::shared_ptr<const detail::win32_audio::DecodedClip> LoadClip(const std::string& path) {
        if (const auto it = cache.find(path); it != cache.end()) {
            return it->second;
        }
        auto clip = std::make_shared<detail::win32_audio::DecodedClip>();
        if (!Win32AudioEngine::DecodeSoundFileToPcm16(
                path, clip->sampleRate, clip->channels, clip->samples)) {
            cache.emplace(path, nullptr);
            return nullptr;
        }
        cache.emplace(path, clip);
        return clip;
    }

    void PlayDecodedClip(
        const detail::win32_audio::DecodedClip& clip,
        float volume) {
        if (clip.samples.empty() || clip.sampleRate == 0u || clip.channels == 0u) {
            return;
        }
        ReapCompletedVoices();
        while (voices.size() >= kMaxVoices) {
            DestroyVoice(std::move(voices.front()));
            voices.erase(voices.begin());
        }

        auto voice = std::make_unique<Voice>();
        voice->scaledPcm.resize(clip.samples.size());
        const float gain = std::clamp(volume, 0.0f, 1.0f);
        if (gain >= 0.999f) {
            std::memcpy(voice->scaledPcm.data(), clip.samples.data(), clip.samples.size() * sizeof(std::int16_t));
        } else {
            for (std::size_t i = 0u; i < clip.samples.size(); ++i) {
                const float s = static_cast<float>(clip.samples[i]) * gain;
                voice->scaledPcm[i] = static_cast<std::int16_t>(
                    std::clamp(static_cast<long>(std::lround(s)), -32768L, 32767L));
            }
        }

        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = clip.channels;
        wfx.nSamplesPerSec = clip.sampleRate;
        wfx.wBitsPerSample = 16u;
        wfx.nBlockAlign = static_cast<WORD>(wfx.nChannels * 2u);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0u;

        if (waveOutOpen(&voice->hwo, WAVE_MAPPER, &wfx, 0u, 0u, CALLBACK_NULL) != MMSYSERR_NOERROR ||
            !voice->hwo) {
            return;
        }
        voice->hdr.lpData = reinterpret_cast<LPSTR>(voice->scaledPcm.data());
        voice->hdr.dwBufferLength = static_cast<DWORD>(voice->scaledPcm.size() * sizeof(std::int16_t));
        voice->hdr.dwFlags = 0u;
        if (waveOutPrepareHeader(voice->hwo, &voice->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            DestroyVoice(std::move(voice));
            return;
        }
        voice->prepared = true;
        if (waveOutWrite(voice->hwo, &voice->hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            DestroyVoice(std::move(voice));
            return;
        }
        voices.push_back(std::move(voice));
    }

    void WorkerLoop() {
        for (;;) {
            std::deque<PlayRequest> batch;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (queue.empty() && !stopping) {
                    if (voices.empty()) {
                        cv.wait(lock, [this]() { return stopping || !queue.empty(); });
                    } else {
                        (void)cv.wait_for(
                            lock,
                            std::chrono::milliseconds(150),
                            [this]() { return stopping || !queue.empty(); });
                    }
                }
                if (stopping) {
                    break;
                }
                batch.swap(queue);
            }
            ReapCompletedVoices();
            while (!batch.empty()) {
                const PlayRequest req = std::move(batch.front());
                batch.pop_front();
                const auto clip = LoadClip(req.path);
                if (clip) {
                    PlayDecodedClip(*clip, req.volume);
                }
            }
        }
    }
};
#else
struct Win32AudioEngine::Impl final {};
#endif

Win32AudioEngine::Win32AudioEngine()
    : impl_(std::make_unique<Impl>()) {}

Win32AudioEngine::~Win32AudioEngine() = default;

void Win32AudioEngine::Enqueue(std::string_view filePath, float volume) noexcept {
#if defined(_WIN32)
    if (impl_) {
        impl_->Enqueue(filePath, volume);
    }
#else
    (void)filePath;
    (void)volume;
#endif
}

bool Win32AudioEngine::DecodeSoundFileToPcm16(
    std::string_view filePath,
    std::uint32_t& outSampleRate,
    std::uint16_t& outChannels,
    std::vector<std::int16_t>& outSamples) {
#if defined(_WIN32)
    if (detail::win32_audio::DecodeWavFileToPcm16(filePath, outSampleRate, outChannels, outSamples)) {
        return true;
    }
    return detail::win32_audio::DecodeWithMediaFoundationPcm16(
        filePath, outSampleRate, outChannels, outSamples);
#else
    (void)filePath;
    outSampleRate = 0u;
    outChannels = 0u;
    outSamples.clear();
    return false;
#endif
}

} // namespace aero
