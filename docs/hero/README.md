# The README's hero animation

M5Stack's own CAD of the CoreS3, rendered in Blender's Cycles with Sagwa's clips playing on its
screen: one slow turn that lingers in front, sweeps past the back and closes seamlessly, because
the turn and the clips share one 17.8 s loop.

## What's from where

- **Shell**: `CoreS3.stl` from [m5stack/M5_Hardware](https://github.com/m5stack/M5_Hardware)
  (`Products/K128_CoreS3/Structures`): the middle frame with its ports, button, speaker grille and
  logo, and the DIN base with its clip. `prepare_model.py` keeps those, leaves out the four
  mounting ears and the exploded copies, and measures the frame's rim for the glass.
- **Front**: the CAD has no glass, so `render.py` builds it to fill the rim (50.9 mm, 3.55 mm
  corners) and places the 40.8 x 30.6 mm screen, camera and copper ring, microphones and sensor
  from measurements of M5Stack's front photo.
- **Details in the CAD's openings**: USB-C, the red Port A, the DC jack, the DC INPUT and OFF/ON
  labels, steel screws, knurled brass inserts and the centre screw on the back.

## Regenerating it

Needs Blender 4.2 and a Python venv with Pillow, numpy, imageio-ffmpeg, trimesh and shapely.

```bash
git clone --depth 1 --filter=blob:none --sparse https://github.com/m5stack/M5_Hardware cad
git -C cad sparse-checkout set Products/K128_CoreS3
python3 docs/hero/prepare_model.py cad/Products/K128_CoreS3/Structures/CoreS3.stl work/model
python3 docs/hero/extract_frames.py sit_meow,sit_to_stand,stand_walk,stand_to_sit work/frames
python3 docs/hero/screens.py work/frames work/screens

R="blender -b -P docs/hero/render.py -- work/model work/screens"
$R work/sheet sheet --res 800        # six angles round the turn, to check
$R work/ref ref --res 800            # the views of M5Stack's product photos, to compare
$R work/anim anim --res 1000 --samples 48 --frames 357    # the turn at 20 fps (~45 s a frame on 4 cores)
$R work/still still --res 2000 --samples 128 --at 0       # the 3/4 front still
$R docs/media stl                     # cores3.stl, without the ears, lying screen up

python3 docs/hero/encode.py work/anim docs/media/sagwa-cores3.webp 20
cp work/still/still.png docs/media/sagwa-cores3.png
```

`anim` skips frames already rendered, so it can be stopped and resumed, or split with
`--from`/`--to`. The shadow is a second, cheap pass (the device unseen but casting onto an
invisible floor) laid under the device, so it works on light and dark pages alike.
