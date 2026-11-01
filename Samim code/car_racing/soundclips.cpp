// The recorded sounds (see soundclips.h).
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3/minimp3.h"
#include "soundclips.h"

#include <QFile>
#include <algorithm>
#include <cmath>

SoundClip loadSoundClip(const QString &resource, const SoundParts &parts, float joinSeconds)
{
    SoundClip clip;
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly)) return clip;
    const QByteArray mp3 = f.readAll();

    // 1. Decode, frame by frame, to mono at the file's own rate
    mp3dec_t dec;
    mp3dec_init(&dec);
    std::vector<float> mono;
    int rate = 0;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    const uint8_t *p = reinterpret_cast<const uint8_t *>(mp3.constData());
    int left = int(mp3.size());
    while (left > 0) {
        mp3dec_frame_info_t info;
        const int n = mp3dec_decode_frame(&dec, p, left, pcm, &info);
        if (info.frame_bytes == 0) break;                // (nothing more it can read)
        p += info.frame_bytes;
        left -= info.frame_bytes;
        if (n == 0) continue;                            // (a tag or a skipped frame)
        rate = info.hz;
        for (int i = 0; i < n; ++i) {
            float v = 0.0f;
            for (int c = 0; c < info.channels; ++c) v += float(pcm[i * info.channels + c]);
            mono.push_back(v / (32768.0f * float(info.channels)));
        }
    }
    if (mono.size() < 4096 || rate <= 0) return clip;

    // 2. To the synthesizer's 44100 Hz, keeping only the parts. Each part brings `join` more of
    // the sound after its end, which is crossfaded over the start of the next part (the last one
    // over the first: the loop), so the joins never shorten the clip.
    const double total = double(mono.size() - 1) / double(rate);
    auto sample = [&](double sec) {                      // (the file at any moment, interpolated)
        const double at = std::clamp(sec, 0.0, total) * double(rate);
        const size_t k = std::min(size_t(at), mono.size() - 2);
        const float t = float(at - double(k));
        return mono[k] * (1.0f - t) + mono[k + 1] * t;
    };
    const size_t join = size_t(std::max(0.0f, joinSeconds) * 44100.0f);
    std::vector<float> &out = clip.samples;
    std::vector<float> carry;                            // the previous part's extra, to crossfade in
    for (const auto &part : parts) {
        const double t0 = std::clamp(double(part.first), 0.0, total);
        const double t1 = part.second > 0.0f ? std::min(double(part.second), total) : total + double(part.second);
        if (t1 - t0 < 0.5) return SoundClip();
        const size_t n = size_t((t1 - t0) * 44100.0);
        const size_t begin = out.size();
        for (size_t i = 0; i < n; ++i) out.push_back(sample(t0 + double(i) / 44100.0));
        for (size_t i = 0; i < carry.size() && i < n; ++i) {
            const float a = float(i) / float(carry.size()) * 1.5707963f;
            out[begin + i] = out[begin + i] * std::sin(a) + carry[i] * std::cos(a);
        }
        carry.clear();
        for (size_t i = 0; i < join; ++i) carry.push_back(sample(t1 + double(i) / 44100.0));
    }
    if (out.size() < 4096) return SoundClip();
    for (size_t i = 0; i < carry.size() && i < out.size(); ++i) {    // (the loop: the last part's extra over the start)
        const float a = float(i) / float(carry.size()) * 1.5707963f;
        out[i] = out[i] * std::sin(a) + carry[i] * std::cos(a);
    }
    const size_t count = out.size();

    // 3. Even loudness: the level over the last/next 0.75 s (every 50 ms) is pulled towards the
    // clip's own average, by at most 3x up or down, the gain gliding smoothly between points.
    // Swells (cars passing the microphone) and breaks in the music go; the beat stays.
    {
        const size_t hop = 2205, win = 33075;            // 50 ms, 0.75 s each side
        const size_t pts = count / hop + 2;
        std::vector<double> prefix(count + 1, 0.0);
        for (size_t i = 0; i < count; ++i) prefix[i + 1] = prefix[i] + double(out[i]) * out[i];
        const double avg = std::sqrt(prefix[count] / double(count));
        std::vector<float> gain(pts);
        for (size_t j = 0; j < pts; ++j) {
            const size_t c = std::min(j * hop, count - 1);
            const size_t a = c > win ? c - win : 0, b = std::min(count, c + win);
            const double rms = std::sqrt((prefix[b] - prefix[a]) / double(std::max<size_t>(1, b - a)));
            gain[j] = float(std::clamp(avg / std::max(rms, 1e-6), 1.0 / 3.0, 3.0));
        }
        for (size_t i = 0; i < count; ++i) {
            const size_t j = i / hop;
            const float t = float(i % hop) / float(hop);
            out[i] *= gain[j] * (1.0f - t) + gain[std::min(j + 1, pts - 1)] * t;
        }
    }

    // 4. The same loudness for every clip (RMS 0.1, about -20 dB); the rare peak above 0.9 is
    // rounded off rather than turning the whole clip down
    double sum = 0.0;
    for (float v : out) sum += double(v) * v;
    const float rms = float(std::sqrt(sum / double(out.size())));
    const float gain = rms > 1e-5f ? 0.1f / rms : 1.0f;
    for (float &v : out) {
        v *= gain;
        if (std::fabs(v) > 0.9f) v = std::copysign(0.9f + 0.1f * std::tanh((std::fabs(v) - 0.9f) / 0.1f), v);
    }
    return clip;
}
