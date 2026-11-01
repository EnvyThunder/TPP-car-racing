# The game's sounds

| Sound | Plays | Made from |
|---|---|---|
| music | everywhere but the race: menus, lobby, story scene, countdown, pause, results | `music.mp3`, "Car Music" by Alex Morgan (Pixabay, no. 545487) |
| your engine | from GO until you cross the finish line | made by the game from your speed (`audioengine.h`, `engineSample`) |
| track atmosphere | under the engine while you drive | `race.mp3`, "NASCAR race car interior perspective" (Pixabay, no. 349361) |

Both recordings are free to use under the Pixabay Content License.

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
