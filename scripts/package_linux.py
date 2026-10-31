#!/usr/bin/env python3
"""Create the Linux x86_64 AppImage and a FUSE-free portable archive from the same AppDir.

Usage: package_linux.py BUILD_DIR FFMPEG_PREFIX OMT_DIR NDI_DIR OUTPUT_DIR
Run on Ubuntu 24.04 after building NyxMap with build_ffmpeg_linux.py's FFmpeg.
Only glibc and the host's graphics driver stack are intentionally external.
Fails when a bundled library comes from a Debian package whose copyright file is GPL only (see package_licence).
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

from PIL import Image

from build_ffmpeg_lgpl import GPL_ONLY, licence_problem

ROOT = Path(__file__).resolve().parents[1]
HOST_LIB = re.compile(r"^(ld-linux[^/]*|lib(c|m|dl|pthread|rt|resolv|util|anl)\.so.*|"
                      r"lib(GL|EGL|GLX|GLdispatch|OpenGL|GLESv[12]|glapi|gbm|drm[^.]*|"
                      r"vulkan[^.]*|nvidia[^.]*)\.so.*)$")
FFMPEG_LIB = re.compile(r"^lib(avcodec|avdevice|avfilter|avformat|avutil|swresample|swscale)\.so\.")
GST_DIR = Path("/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
# The GStreamer plugins Qt Multimedia's GStreamer backend (Qt 6.4 on Ubuntu 24.04) needs for camera capture into a
# QVideoSink, and nothing else: every other plugin's dependencies would come along too (aasink/cacasink pull GPL-only
# libgpm and libslang, others hundreds of unneeded libraries). NyxMap plays sound with miniaudio and decodes media with
# its own FFmpeg, so no audio or playback plugin. A missing plugin fails packaging.
GST_PLUGINS = {
    # queue, tee, capsfilter, identity: the capture session and video sink pipelines
    "libgstreamer1.0-0": ["coreelements"],
    # videotestsrc: the camera pipeline's placeholder source; videoconvert/videoscale (one plugin since 1.22);
    # videorate; appsrc/appsink; caps detection
    "gstreamer1.0-plugins-base": ["videotestsrc", "videoconvertscale", "videorate", "app", "typefindfunctions"],
    # v4l2src and the V4L2 device provider (camera list); jpegdec for MJPEG cameras; autovideosrc/autovideosink;
    # videoflip (camera mirroring/rotation in newer Qt versions)
    "gstreamer1.0-plugins-good": ["video4linux2", "jpeg", "autodetect", "videofilter"],
}
# Qt plugins left out: libqtiff needs Ubuntu's libtiff, which links libjbig (GPL-2+)
QT_PLUGIN_EXCLUDED = re.compile(r"ffmpeg|virtualkeyboard|^libqtiff\.so$")
# Libraries the package may never contain, whatever their package's copyright file also lists (readline's has an ISC
# stanza for one example file): GPL-only on Debian/Ubuntu
GPL_ONLY_LINUX = re.compile(r"^lib(gpm|slang|readline|history|jbig|lzo2|cdio\w*|fftw3\w*)\.so")
# Short licence names (DEP-5 "License:" fields) under which a library may be shipped with NyxMap
PERMITTED_LICENCE = re.compile(
    r"\b(LGPL|MIT|BSD|0BSD|Apache|ISC|Zlib|MPL|public[ -]?domain|X11|Expat|Unlicense|CC0|BSL|Boost|OpenSSL|SSLeay|"
    r"curl|FTL|OFL|bzip2|libpng|IJG|Unicode|ICU|PSF|HPND|MIT-CMU)", re.I)
GPL_LICENCE = re.compile(r"(?<![\w-])A?GPL", re.I)
# Packages whose copyright file the check cannot read correctly, reviewed by hand: package -> the licence of the
# libraries shipped from it, and why
LICENCE_OVERRIDE = {
    # alsa-lib is LGPL-2.1+ (its COPYING); Ubuntu's copyright file misspells the short name "LPGL-2.1+"
    "libasound2t64": "LGPL-2.1+",
}
# Reviewed, no override needed: Qt WebEngine (libqt6webenginecore6, -bin for QtWebEngineProcess, libqt6webengine6-data)
# is "LGPL-3 or GPL-2" for its library and helper process (upstream src/process is LGPL-3.0-only OR GPL-2.0/3.0, as
# the rest of the module); only its tools and tests are GPL-3 with the Qt exception, and they are not shipped.
# Chromium's components are permissive or LGPL/MPL; the check above reads them as such.


def run(cmd, **kwargs):
    print("+", " ".join(str(x) for x in cmd), flush=True)
    return subprocess.run([str(x) for x in cmd], check=True, **kwargs)


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def elf(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def package_licence(owner):
    """(licence summary, problem or None) for a Debian package, from its /usr/share/doc/<package>/copyright.
    GPL-only means: some License field is GPL and none is LGPL or a permissive licence. Coarse (a package with GPL
    tools and a permissive test file passes), hence GPL_ONLY_LINUX for the known cases."""
    name = owner.split(":")[0]
    if name in LICENCE_OVERRIDE:
        return LICENCE_OVERRIDE[name] + " (reviewed)", None
    text = (Path("/usr/share/doc") / name / "copyright").read_text(errors="replace")
    licences = sorted({m.strip() for m in re.findall(r"^License:[ \t]*(\S.*)$", text, re.M | re.I)})
    if not licences:
        # Not machine-readable (no DEP-5 fields): the licence texts it quotes
        if re.search(r"GNU (Lesser|Library) General Public|Permission is hereby granted|Permission to use, copy, "
                     r"modify|Redistribution and use in source and binary forms|public domain", text, re.I):
            return "free-form, LGPL or permissive", None
        if re.search(r"GNU General Public License", text, re.I):
            return "free-form, GPL", "GPL only"
        return "free-form", "licence not recognised"
    summary = ", ".join(licences).replace("|", "/")
    if any(PERMITTED_LICENCE.search(x) for x in licences):
        return summary, None
    if any(GPL_LICENCE.search(x) for x in licences):
        return summary, "GPL only"
    return summary, "licence not recognised"


def tools(folder):
    folder.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "packaging/linux/tools.json").read_text())
    result = {}
    for name, entry in lock.items():
        path = folder / (name + (".AppImage" if name != "runtime-x86_64" else ""))
        urllib.request.urlretrieve(entry["url"], path)
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            sys.exit(f"{name}: checksum mismatch; review and update packaging/linux/tools.json")
        path.chmod(0o755)
        result[name] = path
    return result


def package(build, ffmpeg, omt, ndi, output):
    output.mkdir(parents=True, exist_ok=True)
    app = output / "NyxMap.AppDir"
    if app.exists():
        shutil.rmtree(app)
    binary = app / "usr/bin"
    library = app / "usr/lib"
    library.mkdir(parents=True)
    copy(build / "NyxMap", binary / "NyxMap")
    shutil.copytree(build / "isf", binary / "isf")
    shutil.copytree(build / "audio", binary / "audio")
    for name in ("LICENSE", "THIRD-PARTY-NOTICES.md"):
        copy(ROOT / name, binary / name)
    shutil.copytree(ROOT / "licenses", binary / "licenses")
    for folder, names in ((omt, ["libomt.so", "libvmx.so", "OMT-LICENSES.txt"]),
                          (ndi, ["libndi.so.6", "Processing.NDI.Lib.Licenses.txt", "NDI-NOTICE.txt"])):
        for name in names:
            copy(folder / name, binary / name)
    # What the package ships first; the build machine's own packages (compiler, Qt headers) after, labelled as such.
    # Ubuntu's FFmpeg (GPL) is only used to make test fixtures and is neither linked nor shipped.
    (binary / "DEPENDENCIES.txt").write_text(
        "FFmpeg in the package: built from source without GPL components (LGPL v2.1 or later):\n"
        + (ffmpeg / "VERSION").read_text()
        + "\n" + (ndi / "NDI-SDK.txt").read_text()
        + "\nQt, GStreamer and the other bundled libraries: Ubuntu packages, versions in THIRD-PARTY-LINUX.md\n"
        + "\nPackaging tools:\n" + (ROOT / "packaging/linux/tools.json").read_text()
        + "\nBuild machine packages (not shipped):\n" + (ROOT / "deps-linux.txt").read_text())

    # Camera capture uses Qt's GStreamer backend. Never ship Ubuntu's GPL FFmpeg via
    # an optional Qt FFmpeg backend. Only the LGPL Qt modules/plugins needed here.
    plugins = Path(subprocess.check_output(["/usr/lib/qt6/bin/qmake", "-query", "QT_INSTALL_PLUGINS"], text=True).strip())
    for category in ("platforms", "imageformats", "multimedia", "xcbglintegrations", "egldeviceintegrations",
                     "wayland-graphics-integration-client", "wayland-decoration-client", "wayland-shell-integration",
                     "platforminputcontexts", "networkinformation", "tls"):
        for source in (plugins / category).glob("*.so"):
            if not QT_PLUGIN_EXCLUDED.search(source.name):
                copy(source, app / "usr/plugins" / category / source.name)
    for required in ("platforms/libqxcb.so", "platforms/libqwayland-egl.so", "multimedia/libgstreamermediaplugin.so"):
        if not (app / "usr/plugins" / required).is_file():
            sys.exit(f"Required Qt plugin missing: {required}")
    (binary / "qt.conf").write_text("[Paths]\nPrefix=..\nPlugins=plugins\n")
    # Only the listed plugins, each from its package: never arbitrary plugins preinstalled on a CI runner (bad/ugly,
    # libav or x264 plugins could bring GPL code or the system FFmpeg along).
    for package_name, names in GST_PLUGINS.items():
        files = set(subprocess.check_output(["dpkg-query", "-L", package_name], text=True).splitlines())
        for name in names:
            source = GST_DIR / f"libgst{name}.so"
            if str(source) not in files:
                sys.exit(f"GStreamer plugin {name} not found in {package_name} ({source})")
            copy(source, library / "gstreamer-1.0" / source.name)
    scanner = Path("/usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner")
    copy(scanner, app / "usr/libexec/gst-plugin-scanner")
    # Child processes (xdg-open: the browser, the file manager) get the environment NyxMap was started with, not the
    # one AppRun made for NyxMap's own libraries
    copy(ROOT / "packaging/linux/xdg-open", app / "usr/libexec/nyxmap-path/xdg-open")
    (app / "usr/libexec/nyxmap-path/xdg-open").chmod(0o755)
    shutil.copytree("/usr/share/alsa", app / "usr/share/alsa")
    for name in ("libasound_module_pcm_pulse.so", "libasound_module_ctl_pulse.so"):
        copy(Path("/usr/lib/x86_64-linux-gnu/alsa-lib") / name, library / "alsa-lib" / name)
    copy(Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"), app / "usr/share/fonts/DejaVuSans.ttf")
    copy(ROOT / "packaging/linux/fonts.conf", app / "usr/share/fontconfig/fonts.conf")

    # Libraries loaded with dlopen() by audio, NDI and Native AOT are invisible to ldd.
    for pattern in ("libasound.so.2", "libpulse.so.0", "libavahi-client.so.3", "libudev.so.1",
                    "libssl.so.3", "libcrypto.so.3", "libicuuc.so.*", "libicui18n.so.*", "libicudata.so.*"):
        sources = sorted(Path("/usr/lib/x86_64-linux-gnu").glob(pattern))
        if not sources:
            sys.exit(f"Required runtime library missing: {pattern}")
        for source in sources:
            # Keep SONAME links relative and store large ICU data only once.
            copy(source.resolve(), library / source.resolve().name)
            if source.is_symlink():
                (library / source.name).symlink_to(source.resolve().name)

    # Qt WebEngine (the Web Page generator), when NyxMap is built with it: Chromium's helper process, its resources
    # and locales, where qt.conf (Prefix=..) and AppRun's QTWEBENGINE* variables point; the NSS modules Chromium's
    # certificate checks load with dlopen()
    if "libQt6WebEngineCore" in subprocess.check_output(["ldd", str(build / "NyxMap")], text=True):
        webengine_process = Path("/usr/lib/qt6/libexec/QtWebEngineProcess")
        webengine_data = Path("/usr/share/qt6")
        copy(webengine_process, app / "usr/libexec/QtWebEngineProcess")
        shutil.copytree(webengine_data / "resources", app / "usr/resources")
        shutil.copytree(webengine_data / "translations/qtwebengine_locales", app / "usr/translations/qtwebengine_locales")
        for required in ("libexec/QtWebEngineProcess", "resources/qtwebengine_resources.pak",
                         "translations/qtwebengine_locales/en-US.pak"):
            if not (app / "usr" / required).is_file():
                sys.exit(f"Qt WebEngine file missing: {required}")
        for name in ("libsoftokn3.so", "libfreeblpriv3.so", "libfreebl3.so", "libnssckbi.so", "libnssdbm3.so"):
            source = Path("/usr/lib/x86_64-linux-gnu") / name
            if source.is_file():
                copy(source, library / name)
    else:
        print("NyxMap is built without Qt WebEngine: no Web Page generator in the package", flush=True)

    tool = tools(output / "tools")
    # Freedesktop icon sizes stop at 512 in linuxdeploy; the source artwork is larger.
    icon = output / "nyxmap.png"
    with Image.open(ROOT / "assets/nyxmap-app.png") as artwork:
        artwork.resize((512, 512), Image.Resampling.LANCZOS).save(icon)
    env = {**os.environ, "APPIMAGE_EXTRACT_AND_RUN": "1", "NO_STRIP": "1",
           "LD_LIBRARY_PATH": f"{ffmpeg}/lib:{library}:{binary}"}
    # linuxdeploy handles desktop metadata and ELF rpaths; the closure below also
    # includes libraries its portable-baseline exclusion list would leave out.
    run([tool["linuxdeploy"], "--appdir", app, "--executable", binary / "NyxMap",
         "--desktop-file", ROOT / "packaging/linux/nyxmap.desktop", "--icon-file", icon,
         "--icon-filename", "nyxmap"], env=env)

    origins = {}
    checked = set()
    while True:
        pending = [p for p in app.rglob("*") if elf(p) and p not in checked]
        if not pending:
            break
        for path in pending:
            checked.add(path)
            result = subprocess.run(["ldd", str(path)], env=env, text=True, capture_output=True)
            if "not found" in result.stdout or result.returncode:
                sys.exit(f"Unresolved dependencies of {path}:\n{result.stdout}\n{result.stderr}")
            for name, source in re.findall(r"^\s*(\S+) => (/.+?)\s+\(0x[0-9a-fA-F]+\)", result.stdout, re.M):
                source = Path(source)
                if source.is_relative_to(app) or HOST_LIB.match(name):
                    continue
                destination = library / name
                if not destination.exists():
                    copy(source, destination)
                    origins[destination] = source

    # Reinstall our launcher after deployment; it also works directly from the tar.
    launcher = app / "AppRun"
    if launcher.exists() or launcher.is_symlink():
        launcher.unlink()
    copy(ROOT / "packaging/linux/AppRun", launcher)
    launcher.chmod(0o755)
    notices = []
    packages = {}
    licences = {}  # package -> (licence summary, problem), for every package an ELF file comes from

    def record_package(owner):
        if owner not in packages:
            packages[owner] = subprocess.check_output(
                ["dpkg-query", "-W", "-f=${binary:Package} ${Version} (source: ${source:Package} ${source:Version})", owner], text=True)
            copyright = Path("/usr/share/doc") / owner.split(":")[0] / "copyright"
            copy(copyright, binary / "licenses/linux" / owner / "copyright")
        return packages[owner]

    for path in sorted(p for p in app.rglob("*") if elf(p)):
        if GPL_ONLY.match(path.name) or GPL_ONLY_LINUX.match(path.name):
            sys.exit(f"GPL-only library in package: {path}")
        if FFMPEG_LIB.match(path.name):
            if problem := licence_problem(path):
                sys.exit(f"{path}: {problem}")
            notices.append(f"| {path.relative_to(app)} | FFmpeg { (ffmpeg / 'VERSION').read_text().splitlines()[0] } "
                           "| LGPL-2.1+ (built without GPL components) |")
            continue
        if path.name in ("NyxMap", "libndi.so.6", "libomt.so", "libvmx.so"):
            continue
        source = origins.get(path)
        queries = [str(source), str(source.resolve()), "*/" + path.name] if source else ["*/" + path.name]
        owners = set()
        for query in queries:
            result = subprocess.run(["dpkg-query", "-S", query], text=True, capture_output=True)
            owners.update(line.split(": ", 1)[0] for line in result.stdout.splitlines() if ": " in line)
            if owners:
                break
        if not owners:
            sys.exit(f"Cannot identify the source package of {path}")
        for owner in sorted(owners):
            licences.setdefault(owner, package_licence(owner))
            notices.append(f"| {path.relative_to(app)} | {record_package(owner)} | {licences[owner][0]} |")
    # Like macOS (macos_notices.py): no library whose package is GPL only
    width = max(len(owner) for owner in licences)
    for owner, (summary, problem) in sorted(licences.items()):
        print(f"{owner:<{width}}  {'FAIL: ' + problem if problem else 'ok'}  {summary}")
    if problems := [f"{owner}: {problem} ({summary})" for owner, (summary, problem) in licences.items() if problem]:
        sys.exit("Libraries that may not be shipped (review, then fix or add to LICENCE_OVERRIDE):\n"
                 + "\n".join(sorted(problems)))
    data_files = [("/usr/share/alsa/*", "ALSA configuration data"),
                  ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVu Sans fallback font")]
    if (app / "usr/resources").is_dir():  # Qt WebEngine's data: Chromium's licences are in its copyright file
        data_files.append(("/usr/share/qt6/resources/qtwebengine_resources.pak", "Qt WebEngine resources and locales"))
    for pattern, label in data_files:
        result = subprocess.check_output(["dpkg-query", "-S", pattern], text=True)
        owners = {line.split(": ", 1)[0] for line in result.splitlines() if ": " in line}
        for owner in sorted(owners):
            notices.append(f"| {label} | {record_package(owner)} | see its copyright file |")
    shutil.copytree("/usr/share/common-licenses", binary / "licenses/linux/common-licenses")
    (binary / "THIRD-PARTY-LINUX.md").write_text(
        "# Libraries in the Linux package\n\nQt, FFmpeg and their dependencies are dynamically linked. "
        "Distribution copyright notices and licence texts are in `licenses/linux`. "
        "FFmpeg source, configuration and checksums are in DEPENDENCIES.txt. "
        "Ubuntu source packages: https://launchpad.net/ubuntu/+source/ (names/versions below).\n\n"
        "Licences are the short names in each package's copyright file (all of its parts, tools included).\n\n"
        "| File | Component / binary and source package versions | Licences |\n|---|---|---|\n"
        + "\n".join(notices) + "\n")
    (app / "README-Linux.txt").write_text(
        "NyxMap — Ubuntu 24.04 or newer, x86_64\n\n"
        "Portable archive: extract the whole folder, then run ./AppRun. No installation or FUSE needed.\n"
        "AppImage: chmod +x NyxMap-linux-x86_64*.AppImage, then run it.\n"
        "If FUSE is unavailable: ./NyxMap-linux-x86_64*.AppImage --appimage-extract-and-run\n"
        "Qt, FFmpeg, audio/camera plugins, NDI and OMT are included. The host provides glibc, a desktop session "
        "and OpenGL 3.3 graphics drivers. Optional Blackmagic hardware needs Desktop Video.\n"
        "OMT discovery needs a running Avahi service; the application reports its absence.\n"
        "Licences and dependency/source versions: usr/bin/THIRD-PARTY-NOTICES.md, THIRD-PARTY-LINUX.md, "
        "DEPENDENCIES.txt and licenses/.\n")
    suffix = "-" + os.environ["GITHUB_REF_NAME"] if os.environ.get("GITHUB_REF_TYPE") == "tag" else ""
    image = output / f"NyxMap-linux-x86_64{suffix}.AppImage"
    run([tool["appimagetool"], "--runtime-file", tool["runtime-x86_64"], app, image], env={**env, "ARCH": "x86_64"})
    with tarfile.open(output / f"NyxMap-linux-x86_64{suffix}.tar.gz", "w:gz") as archive:
        archive.add(app, arcname="NyxMap")


if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    package(*(Path(p).resolve() for p in sys.argv[1:]))
