# The README's hero animation

A three.js model of the CoreS3 with Sagwa's clips playing on its screen, rendered frame by frame.

```bash
python3 docs/hero/extract_frames.py sit_meow,sit_to_stand,stand_walk,stand_to_sit docs/hero/frames
python3 docs/hero/server.py 8765          # serves this folder, saves POSTed frames to out/
# open http://127.0.0.1:8765/index.html?capture=1 and wait for the title to say "done"
```

`index.html` alone (no `capture`) previews the loop in real time; `?stills=4.5,9` renders a few
stills and `?stl` writes the model as `out/cores3.stl` (`docs/media/cores3.stl`, which GitHub shows
in its 3D viewer). The frames in `out/` (1000x1000, transparent, 20 fps, one full turn) are then
joined into `docs/media/sagwa-cores3.webp` with Pillow; frame 6 is `docs/media/sagwa-cores3.png`. Needs Pillow, numpy and imageio-ffmpeg, like `sagwa/tools/pack_clips.py`.
