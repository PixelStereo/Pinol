# Clock, quantized sequences and envelopes

Impacts (a flash, a hit, a pulse on a light or on a sound) are made of three things NyxMap now has: a
trigger, an **Envelope**, and a **clock** that puts the triggers in time.

## Envelope (an interaction filter)

In an interaction's filter chain, **Add filter › Over time › Envelope**. The input is a trigger: a key, a MIDI note,
an OSC value, a timeline event or switch (an Internal source), or an audio `onset` through a *Threshold* filter. A
rise from 0 fires it, at the level it comes in (a note's velocity is its peak); falling back to 0 releases it.

| Setting | Meaning |
| --- | --- |
| Attack | seconds to rise to the peak (a ramp from where the envelope is: a retrigger never jumps) |
| Decay | seconds to fall to the sustain (exponential: the shape of a hit; ends exactly on its target) |
| Sustain | the level held while the trigger stays on (0 to 1 of the peak) |
| Release | seconds to fall to 0 after the trigger ends |

The defaults are an **impact**: 5 ms attack, 0.3 s decay, sustain 0. A sustain of 0 makes a pulse that ends by
itself, whether the key is held or not. Any parameter is a target: the opacity or an FX parameter of a picture, a
sound's volume, a Faust FX control (`audio_fx/<id>/cutoff`), an instrument's gate (`synth/gate`), a pixel-mapping
fixture's intensity. Several interactions can share a trigger, so one hit can flash the picture, open a sound and
light a fixture. Shape the result with a *Curve* or *Remap* after it.

## Hits (an audio trigger)

An Audio interaction's **Measure** can be **Hits (a trigger)**: a pulse at each hit — a kick, a snare — of the chosen
frequency region (the spectrum with its band handles, or the *Regions* menu), at the hit's velocity, 0 otherwise. It is
the input an Envelope wants: *Audio › Hits 30–200 Hz → Envelope → a layer's opacity* is a flash on every kick.

* **Sensitivity** — how faint a hit it hears. The detector learns the usual level of the sound by itself (about a second
  and a half), so a quiet passage and a loud one are both followed; raise it to catch soft hits, lower it to keep the hard ones.
* **Minimum gap** — the shortest time between two hits (80 ms by default; 40 ms at the least). Raise it to ignore the
  rebound of a kick or the ring of a hat.
* The pulse lasts 40 ms: the interactions run once a frame, and a shorter pulse could fall between two.

It works on the program mix and on the live audio input, and per region: a kick in the bass and a hat in the highs are two
interactions on one sound. How it works: the audio thread computes a short spectrum every 5 ms and publishes, for each of 24
bands, how much its level rose (the spectral flux, the classical onset function); each interaction adds the bands of its
region and picks the peaks that stand out of its own recent statistics. No library: about 250 lines (`OnsetAnalysis.h`).
Limits: the timing is within about 25 ms of the hit (the window is 21 ms long); a dense mix in one region (a kick and a
bass line together) is harder than a kick alone — narrow the region.

The envelope is evaluated once per frame (the rate of the interactions), not per audio sample: for a sound's own
envelope, an instrument has its own ADSR (`attack`, `decay`, `sustain`, `release`, `gate`), which runs in the audio
callback.

## Clock

The show's musical clock is in the **Sequences** window (BPM, **Tap**, ▶ / ■, and the beat in the bar), saved with the
project (tempo and bar length, not the running state). From OSC:

| Address | |
| --- | --- |
| `/clock/bpm <f>` | tempo (20 to 400) |
| `/clock/bar <i>` | beats per bar (1 to 16) |
| `/clock/start`, `/clock/stop`, `/clock/resume`, `/clock/tap` | transport and tap tempo |
| `/clock/beat`, `/clock/beat_in_bar`, `/clock/running` | where it is (read) |
| `/clock/sync <beat> <bpm> [playing]` | a tempo and position measured elsewhere: the clock takes them, and follows them while they keep coming |

`/clock/sync` is how an external clock drives the steps: a DAW, a controller, or a small helper program that reads
Ableton Link and sends the beat and tempo. The helper is a separate program (Link is GPL; NyxMap stays MIT).

## Sequences in time

* **Waits in beats** (a checkbox of the sequence): the pre- and post-waits of its steps are counted in beats, at the
  clock's tempo when each step starts.
* **Quantize** (a column of the steps): a step started by a GO waits for the next multiple of that many beats of the
  clock (1: the beat, 4: a 4/4 bar), *then* its pre-wait runs. A stopped clock holds no grid: the step goes at once.
* With *Follow* / *Auto-follow* and *Loop*, a sequence of steps on a bar grid is a step sequencer: each step recalls a
  snapshot or plays a timeline, on the beat.

Limits: the tempo is read when a step starts; a tempo changed during a wait takes effect from the next step.
