# The game's sounds

| Sound | Plays | Made from |
|---|---|---|
| music, at half level | everywhere but the race: menus, lobby, story scene, countdown, pause, results | `music.mp3`, "Pole Position", written for this game (`tools/compose_music.py`): original hybrid orchestral, D minor, 112 BPM, 32 bars that loop |
| race music | from GO until you cross the finish line | `race_music.mp3`, "Grid to Glory", written for this game (`tools/compose_race_music.py`): original hybrid orchestral, D minor, 118 BPM, 92 bars (3 min 7 s), an octave-jumping bass and a gritty engine-like synth; its energy follows a broad map (quiet start, middle-weight part, long full part, drop, heavy dark part, rise); intro once, then it loops from bar 5 |
| your engine | under the race music, about 20 dB quieter | made by the game every frame from your speed and keys (`audioengine.h`, `engineSample`): six gears, a steady note at top speed |

**Your own menu music (desktop only).** A file named `private_menu_music.mp3` beside the program replaces the menu and
results music, from 0:15 to 1:20, on that computer. It is not built in and git ignores it: a track you have no licence
to share stays off the website and GitHub.

**Driving follows speed, at once.** Every frame the speed sets volume (0.14 to 0.57 of the recording) and playback rate
(0.85 to 1.15), within about 15 ms; from 85% of top speed they stay put. The desktop sound device reads no further ahead
than 80 ms (it used to lag up to about 0.4 s). The recording
is evened out over 0.12 s, so only speed changes it. Music plays at 0.3. Each race starts at a random place in the recording. The notes below on a generated engine are older history.

The race recording is free to use under the Pixabay Content License. An earlier menu track was "Car Music" by Alex Morgan (Pixabay, no. 545487); the notes below about its parts are history.

**Why the sound is shaped like this.** The files as they are rise and fall:

- `music.mp3` (18.7 s) has a quiet intro (1-4 s), a stutter effect (9.8-11.5 s, the sound chopped
  on and off four times a second) and a fade-out. Looped, it dipped and came back every 18 s;
- `race.mp3` swells every 6-8 s (cars passing the microphone, -24 to -16 dB) and fades at both ends;
- the race sound used to follow your speed in volume too.

So the game now plays only the steady parts, evens out what is left, and makes the engine itself:

- **music:** 126.6 BPM. Two bars from 5.075 s and two from 12.658 s, joined and looped on bar lines
  (30 ms crossfades that don't shorten anything), so it stays on the beat: a 7.58 s loop. The
  level over each 0.75 s is pulled towards the clip's average;
- **engine:** five gears. In each the revs climb with your speed, and at a shift they drop, so the
  pitch follows the race. The loudness never changes;
- **atmosphere:** `race.mp3` from 3 s to 8 s before its end, levelled the same way, under the engine.

Measured offline with the game's own code (loudness in half-second steps, 10-90 % range):

| | Before | Now |
|---|---|---|
| music | 8.3 dB, dips to -35 dB | 3.4 dB (the drum beat), no dips |
| race | 8.6 dB, 12 jumps > 4 dB | 0.9 dB, no jumps |

How they play (`soundclips.cpp`, `audioengine.h`):

- built into the game as Qt resources (`sounds.qrc`), decoded at start-up with
  [minimp3](https://github.com/lieff/minimp3) (CC0, `third_party/minimp3/`), mono 44100 Hz, about -20 dB;
- the desktop (Qt Multimedia) and the browser (Web Audio) play the same samples;
- a switch between music and race is a 0.5 s crossfade;
- if a file is missing, the old synthesized music plays instead.

**To change a sound:** replace the MP3, then set its steady parts in `AudioSynthStream`'s constructor.

`CAR_RACING_AUDIO_TRACE=1` mutes the game and prints each switch (for tests).

**Inside the car (while racing).** A deep rumble from the engine's low notes and a rush of air, both set by your speed every
frame (about 14 dB under the music at speed), and the race music a little softer at the top, as through the car's speakers
(`cabinSample`, `SPEAKER_CUT` in `audioengine.h`).
