#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QIODevice>
#ifndef __EMSCRIPTEN__
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#else
class AudioSynthStream;
void webAudioStart(AudioSynthStream *s);         // (the browser: Web Audio plays it, audioweb.cpp)
#endif
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include "soundclips.h"

class AudioSynthStream : public QIODevice {
    Q_OBJECT
public:
    explicit AudioSynthStream(QObject *parent = nullptr) : QIODevice(parent) {
        // the recorded sounds (sounds/README.md), only their steady parts. The music on every
        // screen but the race: two bars from 5.075 s and two from 12.658 s (it is 126.6 BPM, bars
        // start on those beats) - without its quiet intro, the stutter at 9.8-11.5 s and the
        // fade-out. The race recording as the track's atmosphere under the engine while you drive:
        // from 3 s to 8 s before its end. Without them the synthesized tracks below play instead.
        raceClip = loadSoundClip(":/sounds/race.mp3", {{3.0f, -8.0f}}, 2.0f);
        musicClip = loadSoundClip(":/sounds/music.mp3", {{5.075f, 8.867f}, {12.658f, 16.450f}}, 0.03f);
        haveClips = !raceClip.empty() && !musicClip.empty();
        if (qEnvironmentVariableIsSet("CAR_RACING_AUDIO_TRACE")) {   // (tests: silent, with the clips' lengths)
            muted = true;
            std::printf("AUDIO: race %.1f s, music %.1f s\n", raceClip.samples.size() / 44100.0, musicClip.samples.size() / 44100.0);
            std::fflush(stdout);
        }
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

            // Which music: the menu's chill groove, the race track, or the results theme. A
            // change fades the old one out and the new one in (0.6 s each way; the recordings
            // 0.25 s each way).
            const int want = wantTrack.load(std::memory_order_relaxed);
            const float fadeTime = haveClips ? 0.25f : 0.6f;
            if (want != track) {
                fade -= dt / fadeTime;
                if (fade <= 0.0f) { fade = 0.0f; track = want; trackPos = 0; }
            } else if (fade < 1.0f) {
                fade = std::min(1.0f, fade + dt / fadeTime);
            }
            if (haveClips) {
                float v;
                if (track == 1) {
                    // driving: your engine over the track's atmosphere, at one steady loudness
                    v = engineSample(dt, pinkNoise) * 0.75f + raceClip.samples[racePos] * 0.5f;
                    if (++racePos >= raceClip.samples.size()) racePos = 0;
                } else {
                    v = musicClip.samples[musicPos];
                    if (++musicPos >= musicClip.samples.size()) musicPos = 0;
                }
                samples[i] = static_cast<int16_t>(std::clamp(v * fade, -1.0f, 1.0f) * 32000.0f);
                continue;
            }
            if (track != 0) {
                const float music = track == 1 ? raceMusic(rawNoise) : resultsMusic(rawNoise);
                samples[i] = static_cast<int16_t>(std::clamp(music * fade, -1.0f, 1.0f) * 26000.0f);
                continue;
            }

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
            out = std::clamp(out * fade, -1.0f, 1.0f);
            samples[i] = static_cast<int16_t>(out * 26000.0f);
        }
        return numSamples * sizeof(int16_t);
    }

    qint64 writeData(const char*, qint64) override { return 0; }
    qint64 bytesAvailable() const override { return 44100 * sizeof(int16_t); }

    void setEngine(float speedRatio) { engineWant.store(std::clamp(speedRatio, 0.0f, 1.0f), std::memory_order_relaxed); }
    void setDrift(bool) {}
    void setNitro(bool) {}
    void triggerCrash(float = 1.0f) {}
    void triggerPickup() {}
    void triggerBeep(bool) {}
    void triggerFanfare() {}
    void setMuted(bool m) { muted = m; }
    bool isMuted() const { return muted; }
    void setTrack(int t) { wantTrack.store(std::clamp(t, 0, 2), std::memory_order_relaxed); }

private:
    // ---- The race track: our own high-energy electro (the arcade-racer style): 140 BPM,
    // F minor (Fm - Db - Ab - Eb), 16 bars that loop -
    //   bars 0-3   kick, pumping bass, off-beat hats
    //   bars 4-11  + wide detuned-saw chord stabs and the lead hook, claps on 2 and 4
    //   bars 12-13 the build: no kick, a snare roll speeding up, a rising sweep
    //   bars 14-15 the drop: everything, the hook an octave up
    // The bass and chords duck on every kick ("sidechain" pumping).
    static float saw(float &phase, float freq)
    {
        phase += freq / 44100.0f;
        if (phase >= 1.0f) phase -= 1.0f;
        return 2.0f * phase - 1.0f;
    }
    float raceMusic(float noise)
    {
        const float dt = 1.0f / 44100.0f, twoPi = 6.2831853f;
        const int stepS = 4725;                                       // a sixteenth at 140 BPM
        static const float ROOT[4] = {43.65f, 34.65f, 51.91f, 38.89f};   // F1 Db1 Ab1 Eb1
        static const float CHORD[4][3] = {{349.23f, 415.30f, 523.25f}, {277.18f, 349.23f, 415.30f},
                                          {311.13f, 415.30f, 523.25f}, {311.13f, 392.00f, 466.16f}};
        static const float HOOK[32] = {   // two bars of lead, 0 = rest (eighth notes)
            698.46f, 0, 830.61f, 698.46f, 0, 622.25f, 698.46f, 0, 523.25f, 0, 622.25f, 0, 698.46f, 830.61f, 0, 0,
            932.33f, 0, 830.61f, 698.46f, 0, 622.25f, 0, 698.46f, 0, 523.25f, 0, 466.16f, 523.25f, 0, 0, 0};
        const int step = (trackPos / stepS) % 256, bar = step / 16, inBar = step % 16, chordBar = bar % 4;
        const float frac = float(trackPos % stepS) / float(stepS);
        const float tStep = frac * float(stepS) * dt;
        ++trackPos;
        const bool build = bar == 12 || bar == 13, drop = bar >= 14, full = bar >= 4;
        float out = 0.0f;
        // the kick: a punchy falling sine, four on the floor (not in the build)
        const float sinceBeat = (float(inBar % 4) + frac) * float(stepS) * dt;   // seconds since the last beat
        if (!build) {
            if (inBar % 4 == 0 && frac < 0.0005f) kickPhase = 0.0f;
            kickPhase += (48.0f + 140.0f * std::exp(-sinceBeat * 35.0f)) * twoPi * dt;
            out += std::sin(kickPhase) * 0.16f * std::exp(-sinceBeat * 7.0f);
        }
        const float duck = build ? 1.0f : 1.0f - 0.75f * std::exp(-sinceBeat * 9.0f);   // the pump
        // pumping bass: a filtered saw on the off-beat eighths, an octave jump at the end of a bar
        {
            const bool on = inBar % 2 == 1 || inBar >= 14;
            const float f = ROOT[chordBar] * (inBar >= 14 ? 4.0f : 2.0f);
            const float raw = saw(rBassPhase, f) + 0.5f * saw(rBass2Phase, f * 1.006f);
            bassLp += (raw - bassLp) * (drop ? 0.10f : 0.06f);
            if (on) out += bassLp * 0.11f * duck * std::exp(-frac * 1.5f);
        }
        // wide chords: three detuned saws per note, stabbed (the full part and the drop)
        if (full && !build) {
            static const int STABS[16] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0};
            if (STABS[inBar]) chordEnv = 1.0f;
            chordEnv *= 0.99985f - (drop ? 0.0f : 0.00005f);
            float ch = 0.0f;
            for (int n = 0; n < 3; ++n)
                for (int d = 0; d < 3; ++d)
                    ch += saw(chordPh[n][d], CHORD[chordBar][n] * (1.0f + 0.007f * float(d - 1)));
            chordLp += (ch - chordLp) * 0.18f;
            out += chordLp * 0.012f * chordEnv * duck;
        }
        // the lead hook (bars 4-11, and an octave up in the drop)
        if ((bar >= 8 && bar <= 11) || drop) {
            const float hk = HOOK[inBar + ((bar % 2) ? 16 : 0)];      // (a sixteenth each; 0: the last note rings on)
            if (step != leadStep) { leadStep = step; if (hk > 0.0f) { leadFreq = hk; leadAge = 0; } }
            if (leadFreq > 0.0f) {
                const float f = leadFreq * (drop ? 2.0f : 1.0f);
                const float sq = (saw(leadPhase, f) > 0.0f ? 1.0f : -1.0f) * 0.6f + saw(lead2Phase, f * 1.004f) * 0.4f;
                leadLp += (sq - leadLp) * 0.25f;
                out += leadLp * 0.030f * std::exp(-float(leadAge++) * dt * 3.0f);
            }
        } else {
            leadFreq = 0.0f;
        }
        // hats: open on the off-beats, closed sixteenths in the full part
        hatHp = noise - hatLp;
        hatLp += (noise - hatLp) * 0.35f;
        if (inBar % 4 == 2) out += hatHp * 0.06f * std::exp(-tStep * 18.0f);
        else if (full) out += hatHp * 0.025f * std::exp(-tStep * 60.0f);
        // claps on 2 and 4
        if (full && !build && (inBar == 4 || inBar == 12)) out += (noise * 0.6f + hatHp * 0.4f) * 0.09f * std::exp(-tStep * 22.0f);
        // the build: a snare roll speeding up (quarters, eighths, sixteenths) and a rising sweep
        if (build) {
            const int pos = (bar - 12) * 16 + inBar;                 // 0..31
            const int every = pos < 12 ? 4 : pos < 24 ? 2 : 1;
            if (inBar % every == 0) out += noise * (0.04f + 0.0025f * float(pos)) * std::exp(-tStep * 25.0f);
            const float rise = float(pos) / 32.0f + frac / 32.0f;
            sweepPhase += (200.0f + 1800.0f * rise * rise) * twoPi * dt;
            out += std::sin(sweepPhase) * 0.015f * rise + hatHp * 0.03f * rise;
        }
        // a crash on the first beat of the drop
        if (bar == 14 && inBar < 4) out += hatHp * 0.08f * std::exp(-(float(inBar) + frac) * float(stepS) * dt * 1.5f);
        return out * 0.9f;
    }

    // ---- The results theme: 92 BPM, C major (C - G - Am - F), a bell melody over a warm pad
    // and a soft kick - the race is done
    float resultsMusic(float noise)
    {
        const float dt = 1.0f / 44100.0f, twoPi = 6.2831853f;
        const int stepS = 7190;                                       // a sixteenth at 92 BPM
        static const float ROOT[4] = {65.41f, 49.00f, 55.00f, 43.65f};   // C2 G1 A1 F1
        static const float MELODY[64] = {
            523.25f, 0, 659.25f, 0, 783.99f, 0, 659.25f, 0, 1046.5f, 0, 0, 0, 783.99f, 0, 0, 0,
            493.88f, 0, 587.33f, 0, 783.99f, 0, 587.33f, 0, 987.77f, 0, 0, 0, 783.99f, 0, 0, 0,
            440.00f, 0, 523.25f, 0, 659.25f, 0, 523.25f, 0, 880.00f, 0, 0, 0, 659.25f, 0, 0, 0,
            349.23f, 0, 440.00f, 0, 523.25f, 0, 698.46f, 0, 783.99f, 0, 698.46f, 0, 659.25f, 0, 0, 0};
        const int step = (trackPos / stepS) % 64, bar = step / 16, inBar = step % 16;
        const float frac = float(trackPos % stepS) / float(stepS);
        ++trackPos;
        float out = 0.0f;
        if (MELODY[step] > 0.0f) { bellFreq = MELODY[step]; bellAge = 0; }
        if (bellFreq > 0.0f) {                                        // a bell: a sine and its octave, ringing out
            bellPhase += bellFreq * twoPi * dt;
            if (bellPhase > twoPi * 2.0f) bellPhase -= twoPi * 2.0f;
            const float env = std::exp(-float(bellAge++) * dt * 2.2f);
            out += (std::sin(bellPhase) + 0.4f * std::sin(2.0f * bellPhase)) * 0.035f * env;
        }
        sPadPhase += ROOT[bar] * 4.0f * twoPi * dt;                   // warm pad: root and fifth
        if (sPadPhase > twoPi) sPadPhase -= twoPi;
        out += (std::sin(sPadPhase) + 0.6f * std::sin(1.5f * sPadPhase)) * 0.014f;
        sBassPhase += ROOT[bar] * twoPi * dt;
        if (sBassPhase > twoPi) sBassPhase -= twoPi;
        out += std::sin(sBassPhase) * 0.035f * std::exp(-(float(inBar % 8) + frac) * 0.35f);
        if (inBar == 0 || inBar == 8) {                               // a soft kick
            const float t = frac * float(stepS) * dt;
            kickPhase += (45.0f + 70.0f * std::exp(-t * 25.0f)) * twoPi * dt;
            out += std::sin(kickPhase) * 0.06f * std::exp(-t * 8.0f);
        } else {
            kickPhase = 0.0f;
        }
        if (inBar % 4 == 2) out += noise * 0.008f * std::exp(-frac * 10.0f);
        return out;
    }

    std::atomic<int> wantTrack{0};
    int   track = 0;                     // what plays now (0 menu, 1 race, 2 results)
    float fade = 1.0f;
    int   trackPos = 0;
    float kickPhase = 0.0f, rBassPhase = 0.0f, rBass2Phase = 0.0f, rArpPhase = 0.0f, rPadPhase = 0.0f;
    float bassLp = 0.0f, chordEnv = 0.0f, chordLp = 0.0f, chordPh[3][3] = {}, leadPhase = 0.0f, lead2Phase = 0.0f, leadLp = 0.0f;
    float leadFreq = 0.0f, hatLp = 0.0f, hatHp = 0.0f, sweepPhase = 0.0f;
    int   leadStep = -1, leadAge = 0;
    float bellPhase = 0.0f, bellFreq = 0.0f, sPadPhase = 0.0f, sBassPhase = 0.0f;
    int   bellAge = 0;

    bool  muted = false;
    uint32_t rngState = 1234567;
    float pinkNoise = 0.0f;
    float airNoise = 0.0f;

    // The recordings (see the constructor)
    SoundClip raceClip, musicClip;
    bool haveClips = false;
    size_t racePos = 0, musicPos = 0;
    std::atomic<float> engineWant{0.0f};
    float engineGain = 0.0f;

    // Your engine, made here from the car's speed: five gears - in each the revs climb, and at a
    // shift they drop - so the pitch follows the race the way the car does. The loudness stays
    // the same (about -20 dB, like the recordings): only the pitch and the tone change.
    float rpm = 0.25f, enginePhase = 0.0f, rumblePhase = 0.0f;
    float engineSample(float dt, float noise)
    {
        engineGain += (engineWant.load(std::memory_order_relaxed) - engineGain) * 0.0001f;   // (~0.25 s)
        const float s = std::clamp(engineGain, 0.0f, 1.0f) * 5.0f;
        const int gear = std::min(4, int(s));
        const float within = s - float(gear);
        const float target = gear == 0 ? 0.25f + 0.75f * within : 0.42f + 0.58f * within;
        rpm += (target - rpm) * 0.0004f;                                                       // (a quick glide, ~55 ms)
        const float f = 38.0f + 130.0f * rpm;                                                  // firing frequency, Hz
        enginePhase += f * dt;
        if (enginePhase > 1.0f) enginePhase -= 1.0f;
        rumblePhase += 0.5f * f * dt;
        if (rumblePhase > 1.0f) rumblePhase -= 1.0f;
        const float twoPi = 6.2831853f;
        float tone = 0.0f;                                                                     // harmonics 1..8, softer upwards
        for (int h = 1; h <= 8; ++h) tone += std::sin(twoPi * enginePhase * float(h)) / float(h) * (h % 2 ? 1.0f : 0.7f);
        tone *= 1.0f + 0.15f * std::sin(twoPi * rumblePhase);                                  // the uneven beat of the cylinders
        return (tone * 0.11f + noise * (0.10f + 0.12f * rpm)) * 0.95f;
    }

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
#ifdef __EMSCRIPTEN__
        stream = new AudioSynthStream(this);
        webAudioStart(stream);
        return;
#else
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
#endif
    }

    ~AudioEngine() override {
#ifndef __EMSCRIPTEN__
        if (audioSink) {
            audioSink->stop();
        }
#endif
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

    // the music for what is on screen: 0 the menu, 1 a race, 2 the results
    void setMusic(int track) {
        if (stream) stream->setTrack(track);
    }

    void toggleMute() {
        if (stream) stream->setMuted(!stream->isMuted());
    }

    bool isMuted() const {
        return stream ? stream->isMuted() : true;
    }

private:
    AudioSynthStream *stream = nullptr;
#ifndef __EMSCRIPTEN__
    QAudioSink *audioSink = nullptr;
#endif
};

#endif // AUDIOENGINE_H
