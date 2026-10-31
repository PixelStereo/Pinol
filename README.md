<div align="center">

<img src="assets/nyxmap.png" alt="NYXMAP" width="160">

# NYXMAP

### Projection mapping, live visuals and show control — in one app.

**Map it. Cue it. Play it live.**<br>
From a single projector to a multi-output installation, design the image and run the show without switching tools.

<p>
  <a href="#download"><img alt="Download" src="https://img.shields.io/badge/Download-latest-f6c453?style=for-the-badge"></a>
  <img alt="macOS · Windows · Linux" src="https://img.shields.io/badge/macOS%20%C2%B7%20Windows%20%C2%B7%20Linux-24272C?style=for-the-badge">
  <img alt="Free and open source" src="https://img.shields.io/badge/Free%20%26%20open%20source-MIT-24272C?style=for-the-badge">
</p>

<p><strong>
<a href="#what-you-can-do">Features</a> &nbsp;·&nbsp;
<a href="#download">Download</a> &nbsp;·&nbsp;
<a href="#control-it-from-anything">Remote control</a> &nbsp;·&nbsp;
<a href="docs/build.md">Build from source</a>
</strong></p>

</div>

<p align="center">
  <img src="docs/assets/readme-mapping.png" alt="NYXMAP mapping a test pattern with corner pins, a plasma warped on a mesh and a floor grid onto a stage" width="100%">
</p>

---

## Why NYXMAP

Most shows end up juggling a media server, a mapping tool, a cue list and a bridge for the lighting desk. **NYXMAP puts all of it in one window**: bring in your media, shape it to the room, build your cues, and drive everything from OSC, MIDI, a Stream Deck or a timecode — live, with the picture on screen the whole time.

- 🎯 **Made for the stage.** Big GO button, fades on every value, a STOP that never surprises you.
- 🧩 **Everything talks to everything.** Any parameter can be animated, recalled in a cue or driven by an external controller.
- 🆓 **Free, open source, cross-platform.** macOS, Windows and Linux, MIT licensed.

## What you can do

### 🗺️ Map — take the picture beyond the rectangle

Fit your content to any surface: a building, a set piece, a floor, a dome.

- **Corner pin** to snap a picture onto a flat surface in seconds.
- **Mesh warp** to bend it over curves and uneven walls, point by point.
- **Position, scale, rotate, crop (ROI) and masks** to shape every layer.
- **Viewports** to split the composition across projectors and screens.

### 🎬 Play — all your media, together

Video (including **HAP** for heavy playback), image sequences, images and SVG, audio, text, clocks and countdowns, network streams (RTSP, SRT, HLS…), live inputs (cameras, screens, **NDI**, **Syphon**, **Spout**, **DeckLink**), web pages and **ISF generative shaders**.

Generate sound in real time with **FM**, **granular synthesis**, **white/pink noise**, **ADSR envelopes** and **live video sonification**, using Faust instruments and reusable `.nyxaudio` presets. → [Audio generators](docs/audio-generators.md) · [Layer audio FX](docs/audio-effects.md) · [Clock, quantized sequences and envelopes](docs/clock-and-envelopes.md) · [Controls and the phone remote](docs/controls-and-remote.md)

Stack them in layers and groups, blend them, add effects and correct the colour. A layer can even reuse another layer's picture, before or after its effects, at no extra cost.

### ⏱️ Cue — build the moment, then recall it

<p align="center">
  <img src="docs/assets/readme-show.png" alt="A sequence of cues with GO, snapshots and timelines in NYXMAP" width="100%">
</p>

- **Snapshots** capture the whole scene; **presets** recall only what you choose.
- **Sequences** chain your cues with waits, fades and auto-follow. One GO, and the show runs — or put the steps on a **timecode**: a soundtrack's playhead, LTC, MTC or ATC plays them on time. With **tracking**, a jump to any step lands where the steps before it would have left the show.
- **Timelines** animate any parameter with Bézier keys, waveforms and colour gradients.
- Each value gets **its own fade and easing**, so transitions look exactly as intended.

### 🎛️ Control — hands on, eyes up

- **Learn** a fader, a knob, a key or an OSC address: move it, it is mapped.
- Shape the response with **filters** (remap, curves, math, smoothing, toggle…) and watch the value at every stage.
- **Action mode**: when a value crosses a threshold, fire a cue, start a timeline or flip a switch.
- **Internal links**: let the audio level, a video's playhead or another parameter drive anything.
- **Pick-up or jump takeover**, so a fader never makes the picture leap.

### 🕰️ Sync — keep the show on time

Time-of-day and transport clocks, **LTC, MTC and ATC** timecode, and routing to MIDI, audio and supported video destinations.

### 📽️ Output — from preview to projection

Fullscreen windows on every display, plus **Syphon, Spout, NDI, OMT and Blackmagic DeckLink / UltraStudio** (depending on your platform).

## Control it from anything

Every button, parameter and cue in NYXMAP has an address.

- **Camera motion and gestures**: reference/relative difference masks, independent pixel analysis, and optional local hand tracking. → [Vision interactions](docs/vision-interactions.md)
- **OSC and OSCQuery**: browse the whole show from any OSC tool. → [OSC API](docs/osc.md)
- **Open Stage Control**: a ready-made touch remote for tablets and phones. → [Setup guide](docs/openstagecontrol-help.md)
- **Bitfocus Companion / Stream Deck**: GO, STOP and cues on physical buttons. → [Companion guide](docs/companion.md)
- **MIDI controllers and the keyboard**, with Learn.

## Download

<!-- release-downloads:start -->
Latest published build: **[v0.5.1](https://github.com/PixelStereo/NyxMap/releases/tag/v0.5.1)**.

| macOS — Apple Silicon | Windows — 64-bit | Ubuntu — 64-bit (x86_64) |
| :---: | :---: | :---: |
| **[Download macOS](https://github.com/PixelStereo/NyxMap/releases/download/v0.5.1/NyxMap-macos-v0.5.1.zip)** | **[Download Windows](https://github.com/PixelStereo/NyxMap/releases/download/v0.5.1/NyxMap-windows-v0.5.1.zip)** | **[Download Ubuntu AppImage](https://github.com/PixelStereo/NyxMap/releases/download/v0.5.1/NyxMap-linux-x86_64-v0.5.1.AppImage)** |

Linux: Ubuntu 24.04 or newer. The **[portable archive](https://github.com/PixelStereo/NyxMap/releases/download/v0.5.1/NyxMap-linux-x86_64-v0.5.1.tar.gz)** runs without FUSE or installation. Both Linux packages include Qt, FFmpeg, NDI and OMT; see the [launch instructions](docs/build.md#get-started).

**[Download Open Stage Control remote](https://github.com/PixelStereo/NyxMap/releases/download/v0.5.1/NyxMap-OpenStageControl.zip)** — dependencies included; [setup guide](docs/openstagecontrol-help.md).
<!-- release-downloads:end -->

Prefer building it yourself? Clone the repository and run `./build.sh` (macOS, Linux, Windows in Git Bash) or `build.cmd` (Windows): it installs what is needed, builds NYXMAP and opens it. See [build and technical details](docs/build.md) and the [known limitations](docs/README.md).

## License

NYXMAP is free software under the [MIT License](LICENSE). Third-party components keep their own licences: see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). The NDI runtime included in the packages is covered by the NDI SDK License Agreement; NDI® is a registered trademark of Vizrt NDI AB.
