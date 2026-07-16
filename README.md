# VitaTube

A native YouTube client for the PS Vita. Browse, search and play YouTube videos
using the console's hardware decoder through `SceAvPlayer`. No PSN sign-in, no ads.

The YouTube extraction logic is adapted from the PS3 `yo-player` project
(https://codeberg.org/mohasi/ps3-dev) — the same InnerTube client requests and
JSON parsing, rebuilt on the Vita toolchain.

## Features

- Trending feed (Gaming / Sports / Podcasts) on launch
- Text search (on-screen keyboard)
- Streaming playback up to 720p H.264 + AAC (hardware decoded)
- SponsorBlock skip segments
- Subtitles (one track at a time)
- Chapter markers parsed from the description

## Controls

- D-pad — move selection
- X — play highlighted video
- O — back / exit player
- Start — search
- Triangle (in player) — cycle subtitles
- Left / Right (in player) — seek

## Build

The VPK is built automatically by GitHub Actions on every push. Download the
`vitatube-vpk` artifact from the Actions page.

To build locally you need [vitasdk](https://vitasdk.org):

```sh
vdpm install vita2d vitagl vitashark
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

## Install

Copy the `.vpk` to `ux0:data/` and install it with VitaShell. The app needs the
`libshacccg.suprx` shader compiler for VitaGL (see the VitaGL guide). SceAvPlayer
plays H.264/AAC in MP4; 1080p requires the ReAvPlayer plugin.

## Storage

Settings and the visitor session live under `ux0:data/vitatube/`.

## License

See the original PS3 `yo-player` for the extraction logic provenance.
