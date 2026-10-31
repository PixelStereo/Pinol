# Faust source and licenses

`third_party/faust` is a Git submodule pinned to Faust **2.79.3**, commit
`384f648988c05640a42637cb89e7191e9ad7d6eb`.
Upstream: https://github.com/grame-cncm/faust/tree/2.79.3

NyxMap includes unmodified `architecture/faust/export.h`, `dsp/dsp.h`, `gui/UI.h`
and `gui/meta.h` from that checkout. They retain LGPL-2.1-or-later notices and
explicit architecture exceptions for larger works. They are not copied into or
modified by NyxMap. Full license material remains in the submodule.

The instrument program is `audio/faust/NyxSynth.dsp`; its generated C++ is
`src/engine/generated/NyxFaustSynth.h`. Regenerate using
`scripts/generate_audio_dsp.sh`, Faust 2.79.3, and the submodule's pinned
`libraries` submodule. An installed compiler is required only for regeneration;
the default NyxMap build does not build the Faust compiler or its other targets.

Faust standard-library sections include their own notices and the compiled-code
LGPL exception. Generated metadata records contributing libraries. The optional
user DSP compiler loads installed **libfaust** (LGPL-2.1-or-later); distributions
using it must include the shared library and applicable license materials.
