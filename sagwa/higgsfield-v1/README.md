# Sagwa · Higgsfield Pose Atlas

This directory contains twelve static character pose illustrations derived from the Sagwa reference character atlas.

## Pose Specifications & Pipeline

- **Original Atlas**: Preserved intact as reference.
- **Pose Extraction**: 136×136 pixel frames with transparent alpha channels, composed against pure black (`#000000`).
- **Segmentation**: Carefully segmented to preserve complete ears, paws, and curved tail profiles while removing neighbor artifacts.
- **Provider & Model**: Generated via Higgsfield using GPT Image 2.5 (Job `1c69adce-e378-4f3b-bea0-11c9a952f08d`).
- **Tracking & Verification**: `manifest.json` tracks sha256 checksums, crop boundaries, and state mapping. `preview-black.png` provides an overview matrix of all 12 key poses.

## Greeting Clip Reference

- `greeting-original.mp4`: 4.042-second clip at 960×960, 24 FPS (H.264 without audio) generated using Seedance 2.5 on Higgsfield.
- Serves as the prototype reference for the subsequent 22-clip transition graph produced in `sagwa/clips/`.
