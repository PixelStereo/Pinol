# Audio engine selection

Scope: real-time generators and video sonification in NyxMap's native C++/Qt
application, on macOS, Windows and Linux. This is a focused review of publicly
accessible upstream material, not an exhaustive audit of every project at the
institutions below. Source and dependency licenses must be checked per component;
a free download or published Max patch is not proof of an embeddable open-source engine.

| Project / sources examined | Role and fit | Decision |
| --- | --- | --- |
| [Faust, GRAME](https://github.com/grame-cncm/faust), [Faust libraries](https://github.com/grame-cncm/faustlibraries) | DSP language compiling to native code; library oscillators, noise, envelopes, delay-based pitch shifting; C++ widget metadata; interpreter API for loading programs | Selected. Built-in programs compile to checked-in portable C++, optional libfaust interpreter exposes user programs. |
| [PiPo, IRCAM ISMM](https://github.com/ircam-ismm/pipo) | BSD-3-clause stream-processing interface for descriptors, gestures and matrices | Useful later for analysis/control, not a replacement for the synthesis backend. |
| [CataRT-MuBu, IRCAM ISMM](https://github.com/ircam-ismm/catart-mubu) | Corpus-based concatenative synthesis and descriptor analysis; README requires Max and MuBu | Useful reference for future corpus grains. Not embedded: a Max patch collection does not provide the standalone Linux/macOS/Windows C++ engine needed here. |
| [soundworks](https://github.com/collective-soundworks/soundworks) | Distributed real-time applications using JavaScript/Web technologies, from the IRCAM ecosystem | Appropriate for networked browser instruments, not this native callback implementation. |
| [XMM, IRCAM ISMM](https://github.com/ircam-ismm/xmm) | Examined repository license (GPLv3); gesture/model-oriented toolkit | Not a synthesis engine. No code imported. |
| [Phonos, IRCAM R&D](https://github.com/Ircam-RnD/phonos) | Physical-system simulation cores in C++ | Interesting specialized physical models; not a general FM/noise/granular program host. No code imported. |
| [FluCoMa core](https://github.com/flucoma/flucoma-core), also linked from [IRCAM ISMM](https://github.com/ircam-ismm/flucoma-core) | C++ audio analysis, corpus processing, host clients and additional dependencies | Candidate for later descriptors/corpus tools. Not required for this generator feature; review canonical release licensing/dependencies before adoption. |
| [ossia score](https://github.com/ossia/score), [libossia](https://github.com/ossia/libossia) | Audiovisual sequencing/hosting (including Faust and ISF); OSC/OSCQuery parameter model | Relevant ecosystem references for the user's GMEA/intermedia direction. NyxMap already has its own sequencer, OSCQuery and parameter model; duplicating those is unnecessary. This is not a claim that these repositories alone represent all GMEA tools. |

The [GMEA GitHub organization](https://github.com/GMEA) was inspected, but did not
provide an engine source inventory sufficient to establish another directly
embeddable candidate. This does not establish that GMEA has no relevant tools;
further project-specific sources should be added when identified.

## Why Faust

Built-ins reuse `os.osc`, `no.noise`, `no.pink_noise`, `en.adsr`, `ef.transpose`
and Faust's runtime/state handling, instead of introducing another hand-written
noise/filter/envelope engine. NyxMap only supplies parameter publication, optional
sample decoding, a small live visual tap and connection to its existing audio mixer.

The pinned Faust submodule supplies original source, architecture interfaces and
license notices. The generated C++ keeps ordinary builds independent of a Faust
compiler. Optional libfaust loads user `.dsp` programs at load time, not in the
callback. Programs discover their own scalar controls. See `audio-generators.md`
for capabilities, limits and packaging considerations.

Faust's architecture headers and relevant library sections carry their own LGPL
notices and explicit larger-work/generated-code exceptions. Those exceptions are
not a blanket license for unrelated IRCAM/GRAME/GMEA repositories. The optional
shared libfaust compiler retains its LGPL obligations.
