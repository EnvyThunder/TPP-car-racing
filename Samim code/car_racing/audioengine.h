#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <cmath>
#include <algorithm>

class AudioSynthStream : public QIODevice {
    Q_OBJECT
public:
    explicit AudioSynthStream(QObject *parent = nullptr) : QIODevice(parent) {
        open(QIODevice::ReadOnly);
    }

    qint64 readData(char *data, qint64 maxlen) override {
        int16_t *samples = reinterpret_cast<int16_t*>(data);
        qint64 numSamples = maxlen / sizeof(int16_t);
        const float dt = 1.0f / 44100.0f;
        const float twoPi = 6.2831853f;

        // Music sequencing parameters: ~104 BPM chill groove
        const int stepSamples = 6360; // 16th note at 104 BPM
        const int loopSamples = stepSamples * 64; // 4 bars loop (64 steps)

        // Musical note frequencies (Dm - Bb - C - Am chill progression)
        static const float chordRoots[4] = { 73.42f, 58.27f, 65.41f, 55.00f }; // D2, Bb1, C2, A1
        static const float arpTable[4][8] = {
            { 293.66f, 349.23f, 440.00f, 523.25f, 440.00f, 349.23f, 440.00f, 523.25f }, // Dm (D4, F4, A4, C5)
            { 293.66f, 349.23f, 466.16f, 587.33f, 466.16f, 349.23f, 466.16f, 587.33f }, // Bb (D4, F4, Bb4, D5)
            { 329.63f, 392.00f, 523.25f, 659.25f, 523.25f, 392.00f, 523.25f, 659.25f }, // C  (E4, G4, C5, E5)
            { 261.63f, 329.63f, 440.00f, 523.25f, 440.00f, 329.63f, 440.00f, 523.25f }  // Am (C4, E4, A4, C5)
        };

        for (qint64 i = 0; i < numSamples; ++i) {
            if (muted) {
                samples[i] = 0;
                continue;
            }

            // Pseudo-random noise with multi-stage smoothing for soft air/pink noise
            rngState = rngState * 1664525u + 1013904223u;
            float rawNoise = float(int(rngState >> 16) - 32768) / 32768.0f;
            pinkNoise = pinkNoise * 0.85f + rawNoise * 0.15f;
            airNoise  = airNoise  * 0.96f + rawNoise * 0.04f;

            float out = 0.0f;

            // ===============================================================
            // A. MILD CHILL BACKGROUND MUSIC
            // ===============================================================
            int step = (musicSamplePos / stepSamples) % 64;
            int bar  = (step / 16) % 4;
            int stepInBar = step % 16;
            float stepFrac = float(musicSamplePos % stepSamples) / float(stepSamples);
            musicSamplePos = (musicSamplePos + 1) % loopSamples;

            // 1. Warm bassline (pure smooth sine, no harsh harmonics)
            if (stepInBar == 0 || stepInBar == 8) {
                currentBassFreq = chordRoots[bar];
            }
            bassPhase += currentBassFreq * twoPi * dt;
            if (bassPhase > twoPi) bassPhase -= twoPi;
            float bassEnv = std::exp(-(stepInBar % 8 + stepFrac) * 0.45f);
            float bassTone = std::sin(bassPhase);
            out += bassTone * 0.028f * bassEnv;

            // 2. Mellow arpeggio melody
            float targetArpFreq = arpTable[bar][stepInBar % 8];
            arpPhase += targetArpFreq * twoPi * dt;
            if (arpPhase > twoPi) arpPhase -= twoPi;
            float arpEnv = std::exp(-stepFrac * 4.2f);
            float arpTone = std::sin(arpPhase);
            out += arpTone * 0.024f * arpEnv;

            // 3. Subtle ambient chord pad (gentle sustain)
            float padFreq = chordRoots[bar] * 4.0f; // 2 octaves up
            padPhase += padFreq * twoPi * dt;
            if (padPhase > twoPi) padPhase -= twoPi;
            out += std::sin(padPhase) * 0.016f;

            // 4. Soft rhythmic hi-hat pulse on backbeats
            if (stepInBar == 4 || stepInBar == 12) {
                float hatEnv = std::exp(-stepFrac * 14.0f);
                out += airNoise * 0.015f * hatEnv;
            }

            // Master clamp & 16-bit conversion
            out = std::clamp(out, -1.0f, 1.0f);
            samples[i] = static_cast<int16_t>(out * 26000.0f);
        }
        return numSamples * sizeof(int16_t);
    }

    qint64 writeData(const char*, qint64) override { return 0; }
    qint64 bytesAvailable() const override { return 44100 * sizeof(int16_t); }

    void setEngine(float) {}
    void setDrift(bool) {}
    void setNitro(bool) {}
    void triggerCrash(float = 1.0f) {}
    void triggerPickup() {}
    void triggerBeep(bool) {}
    void triggerFanfare() {}
    void setMuted(bool m) { muted = m; }
    bool isMuted() const { return muted; }

private:
    bool  muted = false;
    uint32_t rngState = 1234567;
    float pinkNoise = 0.0f;
    float airNoise = 0.0f;

    // Background music sequencer state
    int   musicSamplePos = 0;
    float bassPhase = 0.0f;
    float arpPhase = 0.0f;
    float padPhase = 0.0f;
    float currentBassFreq = 73.42f;
};

class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr) : QObject(parent) {
        const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
        if (defaultDevice.isNull()) return;

        QAudioFormat format;
        format.setSampleRate(44100);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);

        stream = new AudioSynthStream(this);
        audioSink = new QAudioSink(defaultDevice, format, this);
        audioSink->setBufferSize(44100 * sizeof(int16_t) / 4); // ~250ms buffer
        audioSink->start(stream);
    }

    ~AudioEngine() override {
        if (audioSink) {
            audioSink->stop();
        }
    }

    void update(float speedRatio, bool drifting, bool nitro) {
        if (!stream) return;
        stream->setEngine(speedRatio);
        stream->setDrift(drifting);
        stream->setNitro(nitro);
    }

    void playCrash(float intensity = 1.0f) {
        if (stream) stream->triggerCrash(intensity);
    }

    void playNitroPickup() {
        if (stream) stream->triggerPickup();
    }

    void playCountdownBeep(bool high) {
        if (stream) stream->triggerBeep(high);
    }

    void playFanfare() {
        if (stream) stream->triggerFanfare();
    }

    void toggleMute() {
        if (stream) stream->setMuted(!stream->isMuted());
    }

    bool isMuted() const {
        return stream ? stream->isMuted() : true;
    }

private:
    AudioSynthStream *stream = nullptr;
    QAudioSink *audioSink = nullptr;
};

#endif // AUDIOENGINE_H
