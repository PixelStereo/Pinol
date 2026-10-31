# Third-party notices

NyxMap's own code is under the MIT licence (see LICENSE). It uses the following third-party components, each under its
own licence; their full texts are in the `licenses` folder (and, for those fetched at packaging time, next to the
library).

| Component | Use in NyxMap | Licence | Licence text | Source |
|---|---|---|---|---|
| OpenCV / OpenCV Zoo MediaPipe Hands | Optional CPU hand detection; adapted model pre/postprocessing and separately downloaded ONNX models | Apache-2.0 | `licenses/opencv-zoo-hands.txt` | https://github.com/opencv/opencv_zoo ; https://opencv.org/ |
| Qt 6 | User interface, OpenGL, multimedia, SVG images | GNU LGPL v3 (dynamically linked) | `licenses/LGPL-3.0.txt`, `licenses/GPL-3.0.txt` | https://www.qt.io/download-open-source (Linux package: Ubuntu's Qt, see below) |
| Qt WebEngine (with Chromium) | Web Page generator (when the package is built with it) | GNU LGPL v3 (dynamically linked; also offered under GPL v2/v3); Chromium BSD 3-clause, its bundled components under their own licences | `licenses/LGPL-3.0.txt`, `licenses/chromium.txt` (Chromium's licence, and where the list of its components' licences is) | https://download.qt.io/official_releases/qt/ (Linux package: Ubuntu's `qt6-webengine`) |
| FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample…) | Video and audio decoding | GNU LGPL v2.1 or later (dynamically linked; built without GPL components). The Windows package's FFmpeg may be built with `--enable-version3`, which makes it LGPL v3 or later; its `DEPENDENCIES.txt` states which | `licenses/LGPL-2.1.txt`; `licenses/LGPL-3.0.txt` for an LGPL v3 build | https://ffmpeg.org/download.html (Windows: https://github.com/BtbN/FFmpeg-Builds) |
| miniaudio | Audio input and output | Public domain (Unlicense) or MIT No Attribution | `licenses/miniaudio.txt` | https://github.com/mackron/miniaudio |
| RtMidi | MIDI | MIT-style (RtMidi licence) | `licenses/rtmidi.txt` | https://github.com/thestk/rtmidi |
| bcdec | BC texture decoding (HAP) | MIT or Unlicense | `licenses/bcdec.txt` | https://github.com/iOrange/bcdec |
| Syphon Framework (macOS) | Syphon input and output | BSD 3-clause | `licenses/syphon.txt` | https://github.com/Syphon/Syphon-Framework |
| Spout2 (Windows) | Spout input and output | BSD 2-clause | `licenses/spout2.txt` | https://github.com/leadedge/Spout2 |
| Blackmagic DeckLink SDK headers | DeckLink output (the driver is installed separately) | Boost Software License 1.0 | `licenses/decklink.txt` | https://www.blackmagicdesign.com/developer |
| Open Media Transport: libomtnet, libomt, libvmx | OMT input and output | MIT | `OMT-LICENSES.txt` (in the package) | https://github.com/openmediatransport |
| TouchEngine SDK (Windows/macOS, opt-in) | TouchDesigner .tox components | Derivative Shared Use License; use requires acceptance of the TouchDesigner licence | `licenses/touchengine.txt` | https://github.com/TouchDesigner/TouchEngine-Windows and https://github.com/TouchDesigner/TouchEngine-macOS |
| NDI® runtime | NDI input and output | NDI SDK License Agreement | `NDI-NOTICE.txt`, `Processing.NDI.Lib.Licenses.txt` (in the package) | https://ndi.video |
| AppImage runtime (Linux) | Portable executable launcher | MIT; linked libfuse under LGPL v2.1, other linked libraries under permissive licences | `licenses/appimage-runtime.txt`, `licenses/LGPL-2.1.txt` | https://github.com/AppImage/type2-runtime |

NDI® is a registered trademark of Vizrt NDI AB. Information about NDI and its tools: https://ndi.video

**NDI runtime in the packages.** The NDI runtime is not covered by NyxMap's MIT licence: it is provided by Vizrt NDI AB
under the NDI SDK License Agreement. You may not modify, disassemble, decompile or separately distribute the NDI
components, nor use them outside NyxMap. Vizrt NDI AB and its licensors provide them "as is", without warranty of any
kind, and disclaim all liability for them to the extent permitted by law.

**LGPL components (Qt, FFmpeg).** They are dynamically linked: you may replace the libraries shipped with NyxMap by
compatible modified versions (in `NyxMap.app/Contents/Frameworks` on macOS, next to `NyxMap.exe` on Windows, in
`usr/lib` on Linux). Their
corresponding source code is available from the links above; the exact versions used by a package are listed in its
`DEPENDENCIES.txt`, and the source of those versions can also be requested from NyxMap's maintainers for three years.

**Linux package.** Qt, FFmpeg and their dependencies are bundled as shared libraries in `usr/lib` of the portable
directory (extract an AppImage with `--appimage-extract` to obtain the same directory). Compatible modified LGPL
libraries can be substituted there, then the application launched with `AppRun`. Its Qt is not the qt.io build but
Ubuntu's Qt packages: their source is on Launchpad (https://launchpad.net/ubuntu/+source/, e.g. `qt6-base`), at the
versions listed in `usr/bin/THIRD-PARTY-LINUX.md`. That file records every bundled binary with its Ubuntu binary and
source package versions and licences; copyright and licence texts are in `usr/bin/licenses/linux`. The build checks
that none of them comes from a package under the GPL only. FFmpeg is built from checksummed source without GPL components; its exact source URL,
version and configuration are recorded in `usr/bin/DEPENDENCIES.txt`. Its FFmpeg is linked with Ubuntu's OpenSSL
(Apache-2.0; TLS for https, rtmps and HLS) and libsrt (MPL-2.0, `libsrt1.5-openssl`; the srt:// protocol), dynamically,
both listed with their copyright files in `THIRD-PARTY-LINUX.md`. Qt SVG (LGPL v3, Ubuntu's `libqt6svg6`) renders SVG images.

**macOS package.** It also contains the libraries Qt and FFmpeg depend on (images, fonts, compression, encryption,
AV1 decoding…), copied from Homebrew when the package is built. Their list, versions, licences and licence texts are
generated with the package: `THIRD-PARTY-MACOS.md` and `licenses/macos/` in `NyxMap.app/Contents/Resources`. The
package's FFmpeg is built from source without GPL components (Homebrew's own includes x264 and x265); the build checks
that every FFmpeg library in it is LGPL and that no GPL-only library is in it.

**Windows package.** Its FFmpeg DLLs come from a pinned, checksummed BtbN/FFmpeg-Builds "lgpl-shared" archive; the
build checks that each one declares LGPL v2.1 or later or LGPL v3 or later and was configured without GPL or non-free
parts, and records that licence in `DEPENDENCIES.txt`. Those builds include OpenSSL (Apache-2.0) and libsrt (MPL-2.0)
inside the FFmpeg DLLs, for TLS and SRT streams; their sources are linked from the BtbN/FFmpeg-Builds repository. Microsoft's `vc_redist.x64.exe` is included unmodified, under
its own licence terms; the build checks its Microsoft signature and records its version and SHA-256.

TouchEngine and TouchDesigner are products of Derivative Inc. The SDK is not covered by NyxMap's MIT licence.
See `licenses/touchengine.txt` for the full Shared Use License. NyxMap is an independent integration; Derivative does
not endorse it. Playback requires a separately installed TouchDesigner or TouchPlayer with a paid Educational,
Commercial or Pro licence. NyxMap neither installs nor activates that licence. Builders must explicitly opt in with
`NYXMAP_ACCEPT_TOUCHENGINE_LICENSE=ON`; see [build instructions](docs/build.md#touchdesigner-components).

**Open Stage Control remote.** The ready-to-load integration includes pure
JavaScript dependencies in `integrations/open-stage-control/nyxmap-dependencies.cjs`:
[ws](https://github.com/websockets/ws) (MIT),
[osc.js](https://github.com/colinbdclark/osc.js) (MIT option) and
[long.js](https://github.com/dcodeIO/long.js) (Apache-2.0).
Their versions and full license texts are preserved beside the bundle in
`nyxmap-dependencies.LICENSE.txt`. Open Stage Control itself remains a separate
GPL-3.0 application, installed by the user; it is not included in the remote ZIP.

## Faust (GRAME)

Built-in audio instruments are generated with Faust 2.79.3 and use its standard
noise, envelope, oscillator and delay/pitch-shifting libraries. See
`third_party/FAUST-NOTICE.md` and the generated DSP metadata for origins.
The four unmodified Faust architecture headers retain LGPL-2.1-or-later notices
and their larger-work exception. Faust's libraries permit distributing generated
code under the enclosing work's terms through their compiled-code exception.
Optional user `.dsp` compilation dynamically loads the installed libfaust
(LGPL-2.1-or-later). It is not required for built-in presets. Redistributors must
supply that library's license materials and honor its LGPL obligations when enabled.
Upstream: https://github.com/grame-cncm/faust

The layer audio FX additionally use Faust's resonant filters, EQ, delay, Freeverb,
compressor, gate, distortion and modulation functions. The upstream STK-style
notice applicable to the relevant library sections is included in
`licenses/faust-stk-4.3.0.md`. Instrument/effect source and generated metadata
retain the upstream provenance; no new audio DSP backend is introduced.
