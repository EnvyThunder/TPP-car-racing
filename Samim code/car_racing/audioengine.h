#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QIODevice>
#include <QCoreApplication>
#include <QFile>
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
        // The music on every screen but the race is our own "Pole Position" (sounds/README.md),
        // written to loop. While you drive: our race track "Grid to Glory" - its intro once from GO,
        // then round from bar 5 - with a very faint engine under it that follows your speed
        // (engineSample). Without the music file the synthesized tracks below play.
        musicClip = loadSoundClip(":/sounds/music.mp3", {{0.06f, 68.55f}}, 0.02f);   // (past the silence MP3 files start and end with)
#ifndef __EMSCRIPTEN__
        // Your own menu music, on your own computer only: an MP3 beside the program, played from
        // 0:15 to 1:20. It is never built in, so it is not part of the website or the repository.
        const QString own = QCoreApplication::applicationDirPath() + "/private_menu_music.mp3";
        if (QFile::exists(own)) {
            SoundClip clip = loadSoundClip(own, {{15.0f, 80.0f}}, 1.0f, 0.0f, true);   // (as loud as the record itself)
            if (!clip.empty()) { musicClip = std::move(clip); musicLevel = 1.0f; }
        }
#endif
        raceTrack = loadSoundClip(":/sounds/race_music.mp3", {{0.0f, 187.1f}}, 0.0f, 0.0f);   // (its own build-ups kept)
#ifndef __EMSCRIPTEN__
        // ... and while racing too, the whole of it from the start, round and round, at the menu's
        // evened-out loudness (a finished record is far louder than our own track)
        if (QFile::exists(own)) {
            SoundClip clip = loadSoundClip(own, {{0.06f, 100000.0f}}, 1.0f, 0.0f, true);
            if (!clip.empty()) { raceTrack = std::move(clip); raceLoopAt = 0.0f; raceLevel = 1.0f; raceDirect = true; }
        }
#endif
        if (qEnvironmentVariableIsSet("CAR_RACING_AUDIO_ENGINE_ONLY")) raceTrack = SoundClip();   // (tests: the engine alone)
        cabinOnly = qEnvironmentVariableIsSet("CAR_RACING_AUDIO_CABIN_ONLY");                    // (tests: the cabin sounds alone)
        haveClips = !musicClip.empty();
        if (qEnvironmentVariableIsSet("CAR_RACING_AUDIO_TRACE")) {   // (tests: silent, with the clips' lengths)
            muted = true;
            auto rms = [](const SoundClip &c) { double t = 0; for (float v : c.samples) t += double(v) * v; return c.empty() ? 0.0 : std::sqrt(t / double(c.samples.size())); };
            std::printf("AUDIO: music %.1f s, race music %.1f s; played at RMS %.3f (menu), %.3f (race)\n", musicClip.samples.size() / 44100.0, raceTrack.samples.size() / 44100.0,
                        rms(musicClip) * musicLevel, rms(raceTrack) * raceLevel);
            std::fflush(stdout);
        }
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);   // (no read-ahead: the sound keeps up with the car)
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
                if (fade <= 0.0f) {
                    fade = 0.0f;
                    track = want;
                    trackPos = 0;
                    if (track == 1) {                    // a new race: the track from its start, the engine from where the car is
                        rpm = rpmFor(engineWant.load(std::memory_order_relaxed), false);
                        load = 0.35f;
                        racePos = 0;
                    }
                }
            } else if (fade < 1.0f) {
                fade = std::min(1.0f, fade + dt / fadeTime);
            }
            if (haveClips) {
                float v;
                if (track == 1) {
                    v = engineSample(dt, rawNoise) * ENGINE_MIX;
                    const float cabin = cabinSample(rawNoise);
                    if (cabinOnly) { samples[i] = static_cast<int16_t>(std::clamp(cabin * fade, -1.0f, 1.0f) * 32000.0f); continue; }
                    if (!raceTrack.empty()) {
                        speakers += (raceTrack.samples[racePos] - speakers) * (raceDirect ? 1.0f : SPEAKER_CUT);   // (the music through the car's speakers; your own record as it is)
                        v += speakers * raceLevel + cabin;
                        if (++racePos >= raceTrack.samples.size())
                            racePos = std::min(raceTrack.samples.size() - 1, size_t(raceLoopAt * 44100.0f));
                    } else {
                        v /= ENGINE_MIX;                 // (no race track: the engine on its own)
                    }
                } else {
                    v = musicClip.samples[musicPos] * musicLevel;
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

    // Every frame: the speed (a share of top speed; nitro goes above 1) and what the driver does
    void setEngine(float speedRatio) { engineWant.store(std::clamp(speedRatio, 0.0f, 1.6f), std::memory_order_relaxed); }
    void setDrive(bool throttle, bool brake, bool air, bool nitro)
    {
        driveBits.store((throttle ? 1 : 0) | (brake ? 2 : 0) | (air ? 4 : 0) | (nitro ? 8 : 0), std::memory_order_relaxed);
    }
    // The engine's revs (0..1) at a speed: six gears. In each gear the revs climb with the speed and
    // drop at the change up; above 82% of top speed the car is in 6th and the note stays put (nitro
    // still pushes it up). In the air the wheels spin free: a little higher.
    static constexpr float GEAR_AT[7] = {0.0f, 0.12f, 0.27f, 0.44f, 0.62f, 0.82f, 1.6f};
    static float rpmFor(float speed, bool air)
    {
        int g = 0;
        while (g < 5 && speed >= GEAR_AT[g + 1]) ++g;
        const float within = std::clamp((speed - GEAR_AT[g]) / (GEAR_AT[g + 1] - GEAR_AT[g]), 0.0f, 1.0f);
        const float lo = g == 0 ? 0.18f : 0.52f;
        return std::min(1.0f, lo + (0.97f - lo) * within + (air ? 0.18f : 0.0f));
    }
    static float engineHz(float rpm) { return 28.0f + 190.0f * rpm; }   // the firing note
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

    // The music recording (see the constructor)
    SoundClip musicClip;
    bool haveClips = false;
    size_t musicPos = 0;
    static constexpr float MUSIC_LEVEL = 0.3f;
    float musicLevel = MUSIC_LEVEL;                    // (your own record beside the program: 1, as loud as made)
    bool raceDirect = false;                           // (your own record in races: not through the car's speakers)
    // While racing: the race track at twice the menu music's level, the engine about 24 dB under it -
    // there if you listen for it, never in the way
    SoundClip raceTrack;
    size_t racePos = 0;
    static constexpr float RACE_LEVEL = 0.6f, RACE_LOOP_AT = 10.170f, ENGINE_MIX = 0.12f;
    float raceLoopAt = RACE_LOOP_AT, raceLevel = RACE_LEVEL;   // (your own music beside the program: from the start, quieter)   // (twice the menu music: louder while racing)

    // ---- Inside the car: a deep rumble through the body and the rush of air outside, both from
    // your speed every frame, and the music a little softer at the top, as through the car's own
    // speakers. The rumble is the engine's low notes only: felt more than heard.
    static constexpr float SPEAKER_CUT = 0.45f;        // (one-pole: about 4.5 kHz)
    static constexpr float RUMBLE_MIX = 0.030f, WIND_MIX = 0.020f;
    bool cabinOnly = false;
    float speakers = 0.0f, rumbleLp = 0.0f, rumbleLp2 = 0.0f, windLo = 0.0f, windHi = 0.0f;
    float cabinSample(float noise)
    {
        const float speed = std::min(1.0f, engineWant.load(std::memory_order_relaxed));
        const float twoPi = 6.2831853f;
        const float body = std::sin(twoPi * halfPhase) + 0.6f * std::sin(twoPi * enginePhase);   // (the engine's own phases: in step with it)
        rumbleLp += (body - rumbleLp) * 0.018f;                              // (two gentle lows: about 130 Hz)
        rumbleLp2 += (rumbleLp - rumbleLp2) * 0.018f;
        const float rumble = rumbleLp2 * RUMBLE_MIX * (0.25f + 0.75f * speed) * (0.7f + 0.3f * load);
        windLo += (noise - windLo) * 0.02f;                                  // (air: between about 150 Hz and 1.5 kHz)
        windHi += (noise - windHi) * 0.2f;
        const float wind = (windHi - windLo) * WIND_MIX * speed * speed;
        return rumble + wind;
    }

    // ---- The engine: the revs follow the speed within about 15 ms (no slow fades), the load
    // (growl and loudness) follows the keys within about 60 ms. A soft tone of harmonics, an
    // uneven half-beat, intake noise, a little saturation under load, a whine with nitro.
    static constexpr float ENGINE_LEVEL = 0.075f;      // low: about -25 dB at top speed, softer at idle
    std::atomic<float> engineWant{0.0f};
    std::atomic<int> driveBits{0};
    float rpm = 0.18f, load = 0.35f, enginePhase = 0.0f, halfPhase = 0.0f, whinePhase = 0.0f, intake = 0.0f;
    float engineSample(float dt, float noise)
    {
        const int bits = driveBits.load(std::memory_order_relaxed);
        const bool throttle = bits & 1, brake = bits & 2, air = bits & 4, nitro = bits & 8;
        const float target = rpmFor(engineWant.load(std::memory_order_relaxed), air);
        rpm += (target - rpm) * 0.0015f;                                     // (about 15 ms)
        const float loadWant = air ? 0.5f : throttle ? 1.0f : brake ? 0.15f : 0.35f;
        load += (loadWant - load) * 0.00038f;                                // (about 60 ms)
        const float f = engineHz(rpm);
        const float twoPi = 6.2831853f;
        enginePhase += f * dt;
        if (enginePhase >= 1.0f) enginePhase -= 1.0f;
        halfPhase += 0.5f * f * dt;
        if (halfPhase >= 1.0f) halfPhase -= 1.0f;
        const float tilt = 1.7f - 0.6f * load;                               // (more load, brighter)
        float tone = 0.0f;
        for (int h = 1; h <= 10; ++h)
            tone += std::sin(twoPi * enginePhase * float(h)) * std::pow(float(h), -tilt) * (h % 2 ? 1.0f : 0.75f);
        tone *= 1.0f + 0.22f * std::sin(twoPi * halfPhase);                  // the cylinders' uneven beat
        tone += 0.25f * std::sin(twoPi * halfPhase);
        const float cut = std::min(0.5f, (300.0f + 2600.0f * rpm) / 44100.0f * twoPi);
        intake += (noise - intake) * cut;                                    // breathing in, brighter at high revs
        float x = tone * 0.55f + intake * (0.15f + 0.35f * load);
        const float drive = 1.0f + 1.5f * load;
        x = std::tanh(x * drive) / std::tanh(drive);
        if (nitro) {
            whinePhase += 9.0f * f * dt;
            if (whinePhase >= 1.0f) whinePhase -= 1.0f;
            x += 0.10f * std::sin(twoPi * whinePhase) + 0.06f * noise;
        }
        return x * ENGINE_LEVEL * (0.45f + 0.35f * load + 0.20f * rpm);
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
        audioSink->setBufferSize(44100 * sizeof(int16_t) / 12); // ~80 ms: the engine keeps up with the car
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
        stream->setDrive(false, false, false, false);
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

    // while driving, every frame: the speed and what the driver does
    void drive(float speedRatio, bool throttle, bool brake, bool air, bool nitro) {
        if (!stream) return;
        stream->setEngine(speedRatio);
        stream->setDrive(throttle, brake, air, nitro);
    }

    // the music for what is on screen: 0 the menu, 1 a race, 2 the results
    void setMusic(int track) {
        if (stream) stream->setTrack(track);
    }

    void toggleMute() {
        if (stream) stream->setMuted(!stream->isMuted());
    }

    void setMuted(bool m) {
        if (stream) stream->setMuted(m);
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
