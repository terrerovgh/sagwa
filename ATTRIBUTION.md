# Sagwa Asset Attribution & Licensing

This document records the provenance, licensing, and credit attribution for all art, audio, typography, and machine learning assets utilized in the Sagwa project.

---

## Visual Assets & Character Artwork

- **Sagwa Character Reference**: The visual identity of Sagwa is based on original character reference designs.
- **Pose Atlas (`sagwa/generated/atlas.png`)**: Generated using reference images with transparent alpha channels, composed against pure black (`#000000`) for the CoreS3 LCD screen. Character poses are standardized at 136×136 pixels without cropping ear tips or white fur contours.
- **System & Status Icons**:
  - Source: **Google Noto Emoji** ([Noto Emoji Repository](https://googlefonts.github.io/noto-emoji-files/))
  - License: [Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/)
  - Derivative: Rescaled to 28×28 pixels and composed onto a pure black background.

---

## Animation Pipeline & Video Clips

- **Hub Poses (`sagwa/hubs/`)**: Six key poses cropped from `sagwa/higgsfield-v1/atlas-original.png`:
  - `sleep.png`, `wake.png` (loaf), `listen.png` (sit), `idle.png` (stand), `work.png` (stalk), and `stretch.png`.
  - Rendered on a 1024×1024 canvas with uniform scale ($\times 1.45$) and standardized ground line ($y = 900$).
- **Motion Clips (`sagwa/clips/`)**:
  - 22 video clips generated using MiniMax H3 on Higgsfield (2K resolution, 1:1 aspect ratio, 24 fps).
  - Designed with matching start/end keyframes corresponding to the hub poses, enabling seamless state-machine transitions without cross-dissolves.
- **Packager (`sagwa/tools/pack_clips.py`)**:
  - Crops videos with a consistent bounding box, strips still transition tails, downsamples to 12 fps, eases boundary frames onto exact hub poses, clamps near-black pixels, and packs frames into baseline JPEGs ($240 \times 208$) inside `sagwa_clips.bin`.

---

## Audio & Feline Vocalizations

- **Purring (`sagwa/sounds/purr-original.ogg`)**:
  - Author: **Peter Hull**
  - Source: [Wikimedia Commons: Cat purring](https://commons.wikimedia.org/wiki/File:Cat_purring.ogg)
  - License: [Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)](https://creativecommons.org/licenses/by-sa/4.0/)
  - Derivative (`purr.pcm`): A 1.2-second loop extracted from second 3, converted to 16 kHz 16-bit mono PCM with fade-in/out envelopes. This adaptation is distributed under CC BY-SA 4.0.
- **Meowing (`sagwa/sounds/meow-original.ogg`)**:
  - Author: **Dan Crosby / Dcrosby** (featuring Silas the cat)
  - Source: [Wikimedia Commons: Meow](https://commons.wikimedia.org/wiki/File:Meow.ogg)
  - License: [Creative Commons Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0)](https://creativecommons.org/licenses/by-sa/3.0/)
  - Derivative (`meow.pcm`): A 0.8-second vocalization converted to 16 kHz 16-bit mono PCM with attenuation and soft fades. This adaptation is distributed under CC BY-SA 3.0.

- **Real cat recordings (`sagwa/sounds/cc0/`)**, all from Wikimedia Commons, cut, pitch-shifted, filtered and normalised into `sagwa_sfx_data.h` by `sagwa/tools/make_sfx.py`:
  - `greek-touch-mrrp.flac`: [2015-11-24.νιαούρισμα.Νιάου.noise reduced.flac](https://commons.wikimedia.org/wiki/File:2015-11-24.νιαούρισμα.Νιάου.noise_reduced.flac) by **Tsester**, CC0 1.0. Used for: notification chirps, happy trill, hunting chatter.
  - `black-cat-begging.flac`: [Black cat sound begging the foods.flac](https://commons.wikimedia.org/wiki/File:Black_cat_sound_begging_the_foods.flac) by **매일 누군가가 도둑맞을 때마다**, CC0 1.0. Used for: chat mrrows, notification chirp, happy trill, murmur.
  - `young-female-meow.ogg`: [Maullido de gata hembra joven.ogg](https://commons.wikimedia.org/wiki/File:Maullido_de_gata_hembra_joven.ogg) by **George Miquilena**, CC0 1.0. Used for: startled mew.
  - `female-meow.ogg`: [Maullido de gata hembra.ogg](https://commons.wikimedia.org/wiki/File:Maullido_de_gata_hembra.ogg) by **George Miquilena**, CC0 1.0. Used for: urgent meow, notification chirp.
  - `siamese-meow.wav`: [Meow of a Siamese cat - freemaster2.wav](https://commons.wikimedia.org/wiki/File:Meow_of_a_Siamese_cat_-_freemaster2.wav) by **freemaster2**, CC0 1.0. Used for: urgent meow.
  - `pleading-cat.oga`: [Meow of a pleading cat.oga](https://commons.wikimedia.org/wiki/File:Meow_of_a_pleading_cat.oga) by **Heismark**, Public domain. Used for: urgent meows, yawn.
  - `hiss.wav`: [Cat hissing - Zabuhailo.wav](https://commons.wikimedia.org/wiki/File:Cat_hissing_-_Zabuhailo.wav) by **Zabuhailo**, CC0 1.0. Used for: startled hiss.
  - The rising listen trill and one murmur take are kept from the earlier bank (`sagwa/sounds/legacy/`, derived from `meow-original.ogg`, CC BY-SA 3.0); the purr loop is unchanged (`purr-original.ogg`, CC BY-SA 4.0).

*Note: These feline audio clips and derivatives are licensed under their respective CC licenses; incorporating them into the project does not impose CC BY-SA onto the underlying C/C++ firmware.*

---

## Typography

- **Inter Font Family**:
  - Author: **Rasmus Andersson** ([Inter Repository](https://github.com/rsms/inter))
  - License: [SIL Open Font License 1.1 (OFL-1.1)](https://scripts.sil.org/OFL)
  - Derivative: Embedded bitmap font tables generated at 12px, 16px, and 20px with extended Latin and Spanish diacritics.

---

## Voice Synthesis References

- **Kokoro-82M**:
  - Author: **hexgrad** ([HuggingFace Repository](https://huggingface.co/hexgrad/Kokoro-82M))
  - License: Apache License 2.0
  - Model voice: `ef_dora`. Synthetic female vocal reference generated locally without voice cloning.
- **Chatterbox Multilingual LATAM**:
  - Author: **Resemble AI** ([HuggingFace Repository](https://huggingface.co/ResembleAI/Chatterbox-Multilingual-es-mx-latam))
  - License: MIT License

---

## Firmware & SDK

- **Base Framework**:
  - Derived from the **Muse Gadgets SDK** by Meta Platforms, Inc.
  - License: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
  - Copyright (c) Meta Platforms, Inc. and affiliates.
