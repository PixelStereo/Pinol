#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Generated headers and the vendored architecture files use this audited version.
faust --version | head -1 | grep -q '2.79.3' || { echo 'Faust 2.79.3 is required' >&2; exit 1; }
# Prefer the libraries pinned with the source, so generation is reproducible.
if [ -z "${FAUST_LIB_PATH:-}" ] && [ -f third_party/faust/libraries/stdfaust.lib ]; then
  FAUST_LIB_PATH=third_party/faust/libraries
fi
# FAUST_LIB_PATH can override this for non-system installations.
if [ -n "${FAUST_LIB_PATH:-}" ]; then
  faust -I "$FAUST_LIB_PATH" -lang cpp -cn NyxFaustSynth audio/faust/NyxSynth.dsp -o src/engine/generated/NyxFaustSynth.h
else
  faust -lang cpp -cn NyxFaustSynth audio/faust/NyxSynth.dsp -o src/engine/generated/NyxFaustSynth.h
fi
# Normalize compiler-emitted trailing whitespace for reproducible clean diffs.
python3 - <<'PY'
from pathlib import Path
p = Path('src/engine/generated/NyxFaustSynth.h')
p.write_text('\n'.join(line.rstrip() for line in p.read_text().splitlines()) + '\n')
PY
for program in audio/fx/*.dsp; do
  effect=$(basename "$program" .dsp)
  if [ -n "${FAUST_LIB_PATH:-}" ]; then
    faust -I "$FAUST_LIB_PATH" -lang cpp -cn "NyxFx$effect" "$program" -o "src/engine/generated/NyxFx$effect.h"
  else
    faust -lang cpp -cn "NyxFx$effect" "$program" -o "src/engine/generated/NyxFx$effect.h"
  fi
done
python3 - <<'PY'
from pathlib import Path
for p in Path('src/engine/generated').glob('NyxFx*.h'):
    p.write_text('\n'.join(line.rstrip() for line in p.read_text().splitlines()) + '\n')
PY
