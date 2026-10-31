# Review of the audio implementation (PR #295)

What was reviewed: `AudioGenerator`, `AudioEffects`, `AudioStream` (mix path), `FaustInterpreter`, the layer
parameters and project state of the audio chain, the sonification tap in `Render.cpp`, the Media Bin and the
inspector. Findings are marked **fixed** (in this branch) or **open** (with what to do).

## Structure and interface

| # | Finding | Status |
| --- | --- | --- |
| 1 | The audio FX chain was a group box under the Source tab, with its own hand-made buttons (Enabled, ↑ ↓, Remove per row) instead of the FX tab's list mechanism. | **fixed** — an **Audio FX** tab; the list / Add / − ▲ ▼ / parameters mechanism is one class, `FxChainPanel`, used by the picture FX tab too (that tab was moved onto it, behaviour unchanged). |
| 2 | No master switch for the chain (the picture FX have *FX Enable*). | **fixed** — `AudioEffectChain::enabled()`, parameter `audio_fx/enable` (OSC, interactions, animations, snapshots). |
| 3 | No way to see the signal: one 5-pixel bar, peak of the mix of the layer only. | **fixed** — `AudioMeter` (lock-free stereo peak with decay) at the source, after each FX, after the fader; `LevelMeter` widget in the Source tab and the Audio FX tab. |
| 4 | Audio generators were filed under the *video* Generators group with an " Audio" suffix; no audio inputs. | **fixed** — Media Bin roots **Video** and **Audio**; Audio holds Sounds, Generators, Inputs. Media (Video + Audio / Video / Audio) and kind (Files / Generators / Inputs / ISF) filters. Imported `.dsp` / `.nyxaudio` files are listed as generators, never as sound files. |
| 5 | No live audio input, while video has cameras, NDI…. | **fixed** — `input:audio:<device>` (`AudioCapture`): a lock-free queue between the device thread and the layer's callback, a sound layer like any other (volume, FX, meters), saved in the project, dragged from Audio > Inputs. Mono first channel, 48 kHz (the device class of the timecode inputs). |
| 6 | FX and instrument controls were addressed `audio_fx/<id>/p0`, `synth/p0`: unusable by hand in OSC, interactions and animations, and fragile when a program's widgets are reordered. | **fixed** — the control's label as its key (`audio_fx/<id>/cutoff`, `synth/level`); a numbered key only when there is no usable label or on a repeat. |

## Real-time safety (checked, no change needed)

* `AudioEffectChain` publishes an immutable list with an epoch counter; the writer waits for a callback that read the
  old list before freeing it. Checked the ordering (the reader's `fetch_add` precedes its `load`; the writer stores
  the pointer then reads the epoch, both sequentially consistent): a reader that the writer did not see started after
  the store and reads the new list.
* No allocation, lock or I/O in `AudioStream::mix`, `AudioEffect::process`, `AudioGenerator::mix` (the blocks are
  stack arrays; the test counts allocations). Non-finite samples are cleaned on both sides of each stage; the output
  of an FX is clamped to ±16.
* `AudioCapture::read` never waits: it drains what is there, drops the oldest samples when late (bounded delay,
  85 ms), and fills with silence otherwise; overruns and underruns are counted.

## Open

| # | Finding | Why it matters / what to do |
| --- | --- | --- |
| A | `NyxSynth.dsp` computes **every** instrument (FM, granular, both noises, the sonification) on every sample and selects with `ba.selectn`: Faust evaluates all branches. | CPU grows with the number of instruments and layers. Split into one DSP per instrument (the generator script already does it for the FX). Needs the Faust 2.79.3 compiler to regenerate the headers (not available in this environment, so not done here). |
| B | The sonification (`Video Sonification`) read the **whole composition** back with `glReadPixels` on the render thread every frame. | **partly fixed** — it now listens to a **viewport** chosen per instrument (Source tab › *Watches*, saved as `sonification_viewport`; the first viewport by default, the composition only when the project has none), one 16×16 read per viewport watched, and the Source tab shows the color it sees. **Open:** the read is still synchronous (a GPU stall on some drivers): use a pixel buffer object read one frame late. |
| C | User `.dsp` files are compiled by libfaust **on the calling thread** (the interface thread when a file is dropped). | A large program freezes the interface while it compiles. Compile on a worker and attach when ready (the chain already supports an unavailable FX that is kept and passed through). |
| D | Live audio inputs show the system's default input first but cannot choose channels (first channel or mix down, as the timecode input). | A multi-channel interface needs a channel choice (a stereo pair). `tcaudio::Device` captures one channel; extend it before exposing more. |
| E | Audio FX are saved as `audio_fx` in the layer JSON, not as a part (`Engine::Part…`) of the snapshot / preset categories. | A snapshot recalls the values of FX that exist, but cannot recreate a chain. Add a `PartAudioEffects` category if snapshots should carry the chain. |
| F | The effect id in the address is a UUID unless the project gives one (`audio_fx/<uuid>/cutoff`). | Stable and saved, but long for OSC. Offer an alias by position (`audio_fx/1/cutoff`) or a user-editable name. |
