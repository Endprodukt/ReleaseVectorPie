"""loop_music.py — cut a track into a seamless loop, as WAV or Ogg Vorbis (by the output's name).

    python loop_music.py <in> <out.ogg|out.wav> <loop start s> [<loop end s> [<crossfade ms>]]
    python loop_music.py --intro <in> <out> <loop start s> [<loop end s> [<crossfade ms>]]
    python loop_music.py --seam <in> <out> <start> <end> <crossfade ms>   (a preview of the jump)

The file is cut to [start, end) and its last `crossfade` ms are blended (equal power) into the
audio just before `start`: the end runs on into the beginning (vp_music_play loops a whole file).
--intro keeps the track from 0 instead, with the same blended end: the game streams it from 0
once and then jumps from the end back to `start` (main.c's music_feed). The game's files:
  python loop_music.py --intro starship.wav starship.ogg 16.4980 71.5156 150
  python loop_music.py --intro space.mp3 attract.ogg 33.8832 68.8003 150
  python loop_music.py space.mp3 entry.ogg 12.0650 29.6019 150
--seam writes the loop's last 8 s followed by its first 8 s, to hear the jump.

Points used (found with the scratchpad's tempo.py / loopfind.py / beatgrid.py):
  dubChip.wav   (the original's game music) 120 bpm, bar 2.0004 s from 0.020 s: 2.0204 .. 82.0364, 20 ms
  opening.wav   (the original's title music until 0.5.6) 0.9451 .. end, 20 ms
  Retro-Futurist Space.mp3 (attract and initials since 0.5.6) 110 bpm, bar 2.18182 s from 1.1559 s:
                attract: from 0, then bar 16 33.8832 .. bar 32 68.8003 (the end 8 ms after the grid);
                initials: bar 6 12.0650 .. bar 14 29.6019 (82 ms after the grid: the song's early
                bars sit off a strict 110 bpm grid), both 150 ms (scratchpad scripts/loopcands.py)
  Starship 8-Bit.wav (the game music since 0.4.0) 148.3 bpm, bar 1.6183 s from 0.315 s:
                bar 2 3.5516 .. bar 46 74.7587 (44 bars), one beat (405 ms): its beat slipped 26 ms
                at the jump. Since 0.5.2: bar 10 16.4980 .. bar 44 71.5156 (34 bars, 55 s; the end
                4.6 ms before the grid, by onset alignment; the bars before both 0.89 alike), 150 ms
                (scratchpad scripts/barloop.py)
Needs numpy and soundfile (libsndfile with Vorbis).
"""
import sys

import numpy as np
import soundfile as sf

seam = sys.argv[1] == "--seam"
intro = sys.argv[1] == "--intro"
args = sys.argv[2:] if seam or intro else sys.argv[1:]
src, dst, start_s = args[0], args[1], float(args[2])
x, rate = sf.read(src, dtype="float32", always_2d=True)
n = len(x)
end_s = float(args[3]) if len(args) > 3 else n / rate
fade_ms = float(args[4]) if len(args) > 4 else 20.0

start, end = int(round(start_s * rate)), min(n, int(round(end_s * rate)))
fade = int(fade_ms / 1000 * rate)
loop = x[start:end].copy()
t = np.linspace(0, 1, fade, endpoint=False)[:, None]
out_gain, in_gain = np.cos(t * np.pi / 2), np.sin(t * np.pi / 2)   # equal power
loop[-fade:] = x[end - fade:end] * out_gain + x[start - fade:start] * in_gain
if intro:
    loop = np.concatenate([x[:start], loop])

if seam:
    k = 8 * rate
    loop = np.concatenate([loop[-k:], loop[:k]])
fmt = "OGG" if dst.lower().endswith(".ogg") else "WAV"
loop = np.clip(loop, -1, 1)
# in blocks: libsndfile's Vorbis encoder crashes on one big write
with sf.SoundFile(dst, "w", rate, loop.shape[1], format=fmt, subtype="VORBIS" if fmt == "OGG" else "PCM_16") as f:
    for i in range(0, len(loop), 4096):
        f.write(loop[i:i + 4096])
print(f"{dst}: {'0 .. ' if intro else ''}{start / rate:.4f} .. {end / rate:.4f} s, {len(loop) / rate:.3f} s, {fmt}")
