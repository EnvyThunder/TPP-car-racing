#ifndef SOUNDCLIPS_H
#define SOUNDCLIPS_H

// The game's recorded sounds (sounds/*.mp3, built in as Qt resources): decoded once at start-up
// with minimp3 into what the synthesizer plays - mono, 44100 Hz - so the desktop and the browser
// play them the same way.

#include <QString>
#include <vector>
#include <utility>
#include <cstdint>

struct SoundClip {
    std::vector<float> samples;          // mono 44100 Hz, about -20 dB loud, steady (see loadSoundClip)
    bool empty() const { return samples.empty(); }
};

// Decode `resource` (":/sounds/race.mp3") and keep only `parts` - {from, to} in seconds (to <= 0:
// that many seconds before the end), played one after another and then round again: the steady
// parts, without the file's own fade-in, fade-out or breaks. Each join (and the loop) crossfades
// over `joinSeconds` at equal power, using a little of the sound just after each part, so the
// clip is exactly as long as the parts - cut on bar lines, music stays on the beat. The loudness
// is evened out (no swells or dips longer than a beat). Empty if the file is missing or not an MP3.
using SoundParts = std::vector<std::pair<float, float>>;
SoundClip loadSoundClip(const QString &resource, const SoundParts &parts, float joinSeconds);

#endif // SOUNDCLIPS_H
