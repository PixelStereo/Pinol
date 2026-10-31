cmake_minimum_required(VERSION 3.21)
include(cmake/Version.cmake)
project(NyxMap VERSION ${NYXMAP_VERSION_NUMERIC} LANGUAGES CXX)
configure_file(cmake/Version.h.in generated/NyxMapVersion.h @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(MSVC)
  add_compile_options(/utf-8 /W3)   # UTF-8 sources (UI strings contain non-ASCII characters)
  add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()
# Warnings on NyxMap's own code (the libraries fetched below keep theirs)
set(NYXMAP_WARNINGS $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wno-unused-parameter>)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)

# ---------------------------------------------------------------------------
# FFmpeg: pkg-config (macOS Homebrew, Linux) or FFMPEG_ROOT (Windows, "shared" build)
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg folder containing include/ and lib/ (Windows)")
set(FFMPEG_LIBS avformat avcodec swscale swresample avutil)

if(FFMPEG_ROOT)
  add_library(ffmpeg INTERFACE)
  target_include_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
  foreach(lib ${FFMPEG_LIBS})
    find_library(FF_${lib} NAMES ${lib} lib${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(ffmpeg INTERFACE ${FF_${lib}})
  endforeach()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
  add_library(ffmpeg INTERFACE)
  target_link_libraries(ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

# ---------------------------------------------------------------------------
# Engine: no dependency on widgets (reusable with another UI)
# ---------------------------------------------------------------------------
add_library(nyxmap_engine STATIC
  src/engine/Gl.cpp
  src/engine/Isf.cpp
  src/engine/LineGenerator.cpp
  src/engine/ImageFile.cpp
  src/engine/Psd.cpp
  src/engine/IsfLibrary.cpp
  src/engine/Mapping.cpp
  src/engine/VideoDecoder.cpp
  src/engine/VideoDecoderHap.cpp
  src/engine/VideoDecoderDelivery.cpp
  src/engine/VideoDecoderLayout.cpp
  src/engine/VideoDecoderDemux.cpp
  src/engine/VideoDecoderSeek.cpp
  src/engine/VideoDecoderMetrics.cpp
  src/engine/ProcessMemory.cpp
  src/engine/VideoTexture.cpp
  src/engine/Hap.cpp
  src/engine/Snappy.cpp
  src/engine/WorkerPool.cpp
  src/engine/Engine.cpp
  src/engine/Layers.cpp
  src/engine/Render.cpp
  src/engine/Project.cpp
  src/engine/ProjectCollect.cpp
  src/engine/Media.cpp
  src/engine/Publish.cpp
  src/engine/DeckLink.cpp
  src/engine/Midi.cpp
  src/engine/LiveInput.cpp
  src/engine/Tox.cpp
  src/engine/ToxTouchEngine.cpp
  src/engine/WebPage.cpp
  src/engine/Permissions.cpp
  src/engine/FaustInterpreter.cpp
  src/engine/AudioEffects.cpp
  src/engine/AudioEffectEngine.cpp
  src/engine/AudioGenerator.cpp
  src/engine/BeatClock.cpp
  src/engine/Controls.cpp
  src/engine/RemoteServer.cpp
  src/engine/AudioCapture.cpp
  src/engine/AudioCaptureDevice.cpp
  src/engine/AudioStream.cpp
  src/engine/AudioOutput.cpp
  src/engine/AudioInput.cpp
  src/engine/MiniaudioImpl.cpp
  src/engine/Osc.cpp
  src/engine/OscShowControl.cpp
  src/engine/EventLog.cpp
  src/engine/FrameTiming.cpp
  src/engine/OscCodec.cpp
  src/engine/SequenceFeedback.cpp
  src/engine/Zeroconf.cpp
  src/engine/Snapshots.cpp
  src/engine/Sequencer.cpp
  src/engine/Publishing.cpp
  src/engine/Fixtures.cpp
  src/engine/Dmx.cpp
  src/engine/PixelSampler.cpp
  src/engine/PixelMapping.cpp
  src/engine/Zip.cpp
  src/engine/Gdtf.cpp
  src/engine/TestCard.cpp
  src/engine/Masks.cpp
  src/engine/Timecode.cpp
  src/engine/TimecodeClock.cpp
  src/engine/TimecodeIo.cpp
  src/engine/TimecodeOutputs.cpp
  src/engine/ClockGenerator.cpp
  src/engine/Interactions.cpp
  src/engine/PixelAnalysis.cpp
  src/engine/HandTracking.cpp
  src/engine/VisionRuntime.cpp
  src/engine/VisionEngine.cpp
  src/engine/InteractionsEngine.cpp
  src/engine/Presets.cpp
  src/engine/Animations.cpp
  src/engine/ShowParams.cpp
  src/engine/Parameter.cpp
  src/engine/LayerParams.cpp
)
target_include_directories(nyxmap_engine PUBLIC src/engine)
target_include_directories(nyxmap_engine PRIVATE "${CMAKE_SOURCE_DIR}/third_party/faust/architecture")
# Built-in Faust instruments use portable generated C++; the optional interpreter
# enables arbitrary user .dsp files without any compilation in the audio callback.
option(NYXMAP_FAUST_COMPILER "Compile user Faust .dsp generators with libfaust" ON)
if(NYXMAP_FAUST_COMPILER)
  find_path(FAUST_COMPILER_INCLUDE_DIR faust/dsp/interpreter-dsp.h)
  find_library(FAUST_COMPILER_LIBRARY NAMES faust libfaust)
  find_path(FAUST_LIBRARY_PATH stdfaust.lib PATH_SUFFIXES share/faust faust)
  if(FAUST_COMPILER_INCLUDE_DIR AND FAUST_COMPILER_LIBRARY AND FAUST_LIBRARY_PATH)
    target_include_directories(nyxmap_engine PRIVATE "${FAUST_COMPILER_INCLUDE_DIR}")
    target_compile_definitions(nyxmap_engine PUBLIC NYXMAP_FAUST_COMPILER)
    target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_FAUST_LIBRARY_PATH="${FAUST_LIBRARY_PATH}" NYXMAP_FAUST_LIBRARY_FILE="${FAUST_COMPILER_LIBRARY}")
  else()
    message(STATUS "libfaust compiler not found: built-in audio instruments available; user .dsp loading disabled")
  endif()
endif()

option(NYXMAP_HAND_TRACKING "OpenCV DNN / MediaPipe hand landmark tracking" ON)
if(NYXMAP_HAND_TRACKING)
  find_package(OpenCV QUIET COMPONENTS core imgproc dnn)
  if(OpenCV_FOUND)
    target_include_directories(nyxmap_engine PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(nyxmap_engine PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(nyxmap_engine PUBLIC NYXMAP_OPENCV_HANDS)
  else()
    # Component development packages on Debian need not install the umbrella config.
    find_path(NYXMAP_OPENCV_INCLUDE opencv2/core.hpp PATH_SUFFIXES opencv4)
    find_library(NYXMAP_OPENCV_CORE NAMES opencv_core)
    find_library(NYXMAP_OPENCV_IMGPROC NAMES opencv_imgproc)
    find_library(NYXMAP_OPENCV_DNN NAMES opencv_dnn)
    if(NYXMAP_OPENCV_INCLUDE AND NYXMAP_OPENCV_CORE AND NYXMAP_OPENCV_IMGPROC AND NYXMAP_OPENCV_DNN)
      target_include_directories(nyxmap_engine PRIVATE "${NYXMAP_OPENCV_INCLUDE}")
      target_link_libraries(nyxmap_engine PRIVATE "${NYXMAP_OPENCV_CORE}" "${NYXMAP_OPENCV_IMGPROC}" "${NYXMAP_OPENCV_DNN}")
      target_compile_definitions(nyxmap_engine PUBLIC NYXMAP_OPENCV_HANDS)
    else()
      message(STATUS "OpenCV DNN not found: pixel analysis and motion masks available; hand tracking disabled")
    endif()
  endif()
endif()

target_link_libraries(nyxmap_engine PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Network ffmpeg)
target_compile_definitions(nyxmap_engine PUBLIC GL_SILENCE_DEPRECATION)
if(WIN32)
  target_link_libraries(nyxmap_engine PRIVATE psapi) # process memory (ProcessMemory.cpp)
endif()
# Cameras (live inputs): Qt Multimedia when it is there
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(Qt6Multimedia_FOUND)
  target_link_libraries(nyxmap_engine PRIVATE Qt6::Multimedia)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_CAMERA)
else()
  message(STATUS "Qt Multimedia not found: no camera inputs")
endif()
# SVG drawings as images: Qt SVG when it is there
find_package(Qt6 QUIET COMPONENTS Svg)
if(Qt6Svg_FOUND)
  target_link_libraries(nyxmap_engine PRIVATE Qt6::Svg)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_SVG)
else()
  message(STATUS "Qt SVG not found: no SVG images")
endif()

# ---------------------------------------------------------------------------
# Third-party sources: git submodules in third_party/ (git submodule update --init), built here
# ---------------------------------------------------------------------------
function(nyxmap_submodule name file)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/${name}/${file}")
    message(FATAL_ERROR "third_party/${name} is empty: run  git submodule update --init")
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Audio output: miniaudio (single header, public domain / MIT-0), compiled in src/engine/MiniaudioImpl.cpp
# ---------------------------------------------------------------------------
nyxmap_submodule(faust architecture/faust/dsp/dsp.h)
nyxmap_submodule(miniaudio miniaudio.h)
target_include_directories(nyxmap_engine PRIVATE "${CMAKE_SOURCE_DIR}/third_party/miniaudio")
target_compile_definitions(nyxmap_engine PRIVATE MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_ENGINE
  MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH)
if(MSVC)
  set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(src/engine/MiniaudioImpl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(APPLE)
  # Link the Core Audio frameworks directly rather than loading them at runtime
  target_compile_definitions(nyxmap_engine PRIVATE MA_NO_RUNTIME_LINKING)
  target_link_libraries(nyxmap_engine PRIVATE "-framework CoreFoundation" "-framework CoreAudio"
    "-framework AudioToolbox" "-framework OpenGL")
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(nyxmap_engine PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ---------------------------------------------------------------------------
# MIDI (the timecode's MTC): RtMidi (MIT), a submodule. CoreMIDI, Windows MM, ALSA (Linux: libasound2-dev; without it,
# no MIDI port)
# ---------------------------------------------------------------------------
nyxmap_submodule(rtmidi RtMidi.cpp)
set(rtmidi_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/rtmidi")
set(NYXMAP_MIDI_API "")
if(APPLE)
  set(NYXMAP_MIDI_API __MACOSX_CORE__)
  set(NYXMAP_MIDI_LIBS "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
elseif(WIN32)
  set(NYXMAP_MIDI_API __WINDOWS_MM__)
  set(NYXMAP_MIDI_LIBS winmm)
else()
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    set(NYXMAP_MIDI_API __LINUX_ALSA__)
    set(NYXMAP_MIDI_LIBS ALSA::ALSA Threads::Threads)
  else()
    message(STATUS "ALSA not found: no MIDI ports (sudo apt install libasound2-dev)")
  endif()
endif()
if(NYXMAP_MIDI_API)
  add_library(rtmidi_static STATIC "${rtmidi_SOURCE_DIR}/RtMidi.cpp")
  target_include_directories(rtmidi_static PUBLIC "${rtmidi_SOURCE_DIR}")
  target_compile_definitions(rtmidi_static PUBLIC ${NYXMAP_MIDI_API})
  target_link_libraries(rtmidi_static PUBLIC ${NYXMAP_MIDI_LIBS})
  if(MSVC)
    target_compile_options(rtmidi_static PRIVATE /W0)
  else()
    target_compile_options(rtmidi_static PRIVATE -w)
  endif()
  target_link_libraries(nyxmap_engine PRIVATE rtmidi_static)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_MIDI)
endif()

# ---------------------------------------------------------------------------
# HAP: texture blocks decoded on the CPU when the GPU cannot sample them (BC7 and BC6 on macOS).
# bcdec: single header, MIT or Unlicense, compiled in src/engine/Hap.cpp.
# ---------------------------------------------------------------------------
nyxmap_submodule(bcdec bcdec.h)
target_include_directories(nyxmap_engine PRIVATE "${CMAKE_SOURCE_DIR}/third_party/bcdec")

# ---------------------------------------------------------------------------
# Output publishing
#  - NDI and OMT: loaded at runtime (no build-time dependency)
#  - Syphon (macOS) and Spout (Windows): sources downloaded and built with NyxMap
# ---------------------------------------------------------------------------
option(NYXMAP_SYPHON "Syphon publishing (macOS)" ON)
option(NYXMAP_SPOUT "Spout publishing and inputs (Windows)" ON)

# macOS: the output windows shown through Metal (OpenGL into a window only works on the main thread there)
if(APPLE)
  enable_language(OBJCXX)
  target_sources(nyxmap_engine PRIVATE src/engine/PresentMetal.mm src/engine/Permissions.mm src/engine/VideoSurfaceMac.cpp)
  set_source_files_properties(src/engine/PresentMetal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(nyxmap_engine PRIVATE GL_SILENCE_DEPRECATION)
  target_link_libraries(nyxmap_engine PRIVATE "-framework Metal" "-framework QuartzCore" "-framework IOSurface"
    "-framework AppKit" "-framework Network" "-framework CoreVideo" "-framework CoreGraphics")
else()
  target_sources(nyxmap_engine PRIVATE src/engine/VideoSurfaceNone.cpp)
endif()

if(APPLE AND NYXMAP_SYPHON)
  enable_language(C OBJC OBJCXX) # Syphon: C and Objective-C sources
  nyxmap_submodule(syphon SyphonServer.h)
  set(syphon_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/syphon")
  # The sources import <Syphon/...>: expose the headers under this prefix.
  file(GLOB SYPHON_HEADERS "${syphon_SOURCE_DIR}/*.h")
  file(COPY ${SYPHON_HEADERS} DESTINATION "${CMAKE_BINARY_DIR}/syphon-include/Syphon")
  file(GLOB SYPHON_SOURCES "${syphon_SOURCE_DIR}/*.m" "${syphon_SOURCE_DIR}/*.c")
  # OpenGL version only: the Metal server would require compiling .metal shaders
  list(FILTER SYPHON_SOURCES EXCLUDE REGEX "Metal")
  add_library(syphon STATIC ${SYPHON_SOURCES})
  target_include_directories(syphon PUBLIC "${CMAKE_BINARY_DIR}/syphon-include" PRIVATE "${syphon_SOURCE_DIR}")
  target_compile_definitions(syphon PRIVATE SYPHON_CORE_SHARE GL_SILENCE_DEPRECATION)
  target_compile_options(syphon PRIVATE -w "SHELL:-include ${syphon_SOURCE_DIR}/Syphon_Prefix.pch"
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
  target_link_libraries(syphon PUBLIC "-framework Cocoa" "-framework OpenGL" "-framework IOSurface" "-framework Foundation")
  target_sources(nyxmap_engine PRIVATE src/engine/PublishSyphon.mm src/engine/LiveInputSyphon.mm)
  set_source_files_properties(src/engine/PublishSyphon.mm src/engine/LiveInputSyphon.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(nyxmap_engine PRIVATE syphon)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_SYPHON)
endif()

# DeckLink (Blackmagic SDI / HDMI cards): the SDK's headers are in third_party/decklink (Boost license), the driver
# (Desktop Video) is loaded at run time. Windows: its interfaces come from the SDK's IDL, compiled by MIDL.
option(NYXMAP_DECKLINK "DeckLink outputs (Blackmagic Desktop Video)" ON)
if(NYXMAP_DECKLINK)
  set(DECKLINK_SDK "${CMAKE_SOURCE_DIR}/third_party/decklink")
  if(APPLE)
    target_include_directories(nyxmap_engine PRIVATE "${DECKLINK_SDK}/mac")
    target_sources(nyxmap_engine PRIVATE "${DECKLINK_SDK}/mac/DeckLinkAPIDispatch.cpp")
    set_source_files_properties("${DECKLINK_SDK}/mac/DeckLinkAPIDispatch.cpp" PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(nyxmap_engine PRIVATE "-framework CoreFoundation")
    target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_DECKLINK)
  elseif(WIN32)
    find_program(MIDL_EXE midl)
    if(MIDL_EXE)
      set(DECKLINK_GEN "${CMAKE_BINARY_DIR}/decklink")
      file(MAKE_DIRECTORY "${DECKLINK_GEN}")
      file(GLOB DECKLINK_IDL "${DECKLINK_SDK}/win/*.idl")
      add_custom_command(
        OUTPUT "${DECKLINK_GEN}/DeckLinkAPI_h.h" "${DECKLINK_GEN}/DeckLinkAPI_i.c"
        COMMAND "${MIDL_EXE}" /nologo /env x64 /I "${DECKLINK_SDK}/win" /out "${DECKLINK_GEN}"
                /h DeckLinkAPI_h.h /iid DeckLinkAPI_i.c /tlb DeckLinkAPI.tlb /proxy DeckLinkAPI_p.c /dlldata dlldata.c
                "${DECKLINK_SDK}/win/DeckLinkAPI.idl"
        DEPENDS ${DECKLINK_IDL}
        COMMENT "DeckLink SDK interfaces (MIDL)")
      add_custom_target(decklink_idl DEPENDS "${DECKLINK_GEN}/DeckLinkAPI_h.h" "${DECKLINK_GEN}/DeckLinkAPI_i.c")
      add_dependencies(nyxmap_engine decklink_idl)
      target_include_directories(nyxmap_engine PRIVATE "${DECKLINK_GEN}")
      target_link_libraries(nyxmap_engine PRIVATE ole32 oleaut32)
      target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_DECKLINK)
    else()
      message(WARNING "MIDL not found (run from a Visual Studio developer prompt): built without DeckLink outputs")
    endif()
  else()
    target_include_directories(nyxmap_engine PRIVATE "${DECKLINK_SDK}/linux")
    target_sources(nyxmap_engine PRIVATE "${DECKLINK_SDK}/linux/DeckLinkAPIDispatch.cpp")
    set_source_files_properties("${DECKLINK_SDK}/linux/DeckLinkAPIDispatch.cpp" PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(nyxmap_engine PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_DECKLINK)
  endif()
endif()

if(WIN32 AND NYXMAP_SPOUT)
  nyxmap_submodule(spout2 SPOUTSDK/SpoutGL/Spout.cpp)
  set(SPOUT_DIR "${CMAKE_SOURCE_DIR}/third_party/spout2/SPOUTSDK/SpoutGL")
  add_library(spout_gl STATIC
    ${SPOUT_DIR}/Spout.cpp ${SPOUT_DIR}/SpoutCopy.cpp ${SPOUT_DIR}/SpoutDirectX.cpp
    ${SPOUT_DIR}/SpoutFrameCount.cpp ${SPOUT_DIR}/SpoutGL.cpp ${SPOUT_DIR}/SpoutGLextensions.cpp
    ${SPOUT_DIR}/SpoutReceiver.cpp ${SPOUT_DIR}/SpoutSender.cpp ${SPOUT_DIR}/SpoutSenderNames.cpp
    ${SPOUT_DIR}/SpoutSharedMemory.cpp ${SPOUT_DIR}/SpoutUtils.cpp)
  target_include_directories(spout_gl PUBLIC ${SPOUT_DIR})
  target_compile_definitions(spout_gl PUBLIC SPOUT_BUILD_STATIC)
  if(MSVC)
    target_compile_options(spout_gl PRIVATE /W0)
  endif()
  target_link_libraries(spout_gl PUBLIC opengl32 d3d9 d3d11 dxgi version winmm shlwapi)
  target_sources(nyxmap_engine PRIVATE src/engine/PublishSpout.cpp src/engine/LiveInputSpout.cpp)
  target_link_libraries(nyxmap_engine PRIVATE spout_gl)
  target_compile_definitions(nyxmap_engine PRIVATE NYXMAP_HAS_SPOUT)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
include(cmake/TouchEngine.cmake)

set(APP_SOURCES
  assets/branding.qrc
  src/main.cpp
  src/ui/MainWindow.cpp
  src/ui/LogPanel.cpp
  src/ui/LogModel.cpp
  src/ui/ControlsPanel.cpp
  src/ui/FxChainPanel.cpp
  src/ui/LayerInspector.cpp
  src/ui/ParamPanel.cpp
  src/ui/PresetEditor.cpp
  src/ui/MappingView.cpp src/ui/MappingViewLines.cpp
  src/ui/PreviewWorkspace.cpp
  src/ui/FixtureOverlay.cpp
  src/ui/FixturePanel.cpp
  src/ui/FixtureInspector.cpp
  src/ui/DmxOutputsPanel.cpp
  src/ui/MaskPanel.cpp
  src/ui/OutputWindow.cpp
  src/ui/GlDraw.cpp
  src/ui/Commands.cpp
  src/ui/LayerTable.cpp
  src/ui/MediaBin.cpp
  src/ui/CompositionPanel.cpp
  src/ui/ViewportOutput.cpp
  src/ui/SettingsPanel.cpp
  src/ui/SnapshotPanel.cpp
  src/ui/SequencePanel.cpp
  src/ui/TimelineActivity.cpp
  src/ui/AnimEditor.cpp
  src/ui/ParamAnimPanel.cpp
  src/ui/TimelinePanel.cpp
  src/ui/Widgets.cpp
  src/ui/ColorPicker.cpp
  src/ui/ColorPicker.h
  src/ui/InteractionPanel.cpp
  src/ui/AudioSpectrum.cpp
  src/ui/WebPageQt.cpp
)

if(APPLE)
  enable_language(OBJCXX)
  list(APPEND APP_SOURCES src/ui/MacPresentation.mm)
  set_source_files_properties(assets/NyxMap.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  add_executable(NyxMap MACOSX_BUNDLE ${APP_SOURCES} assets/NyxMap.icns)
  set_target_properties(NyxMap PROPERTIES MACOSX_BUNDLE_ICON_FILE NyxMap.icns)
  target_link_libraries(NyxMap PRIVATE "-framework AppKit")
  # The camera permission plugin (static, from Qt): without it Qt refuses the camera before macOS is asked
  find_package(Qt6QDarwinCameraPermissionPlugin QUIET)
  if(TARGET Qt6::QDarwinCameraPermissionPlugin)
    target_link_libraries(NyxMap PRIVATE Qt6::QDarwinCameraPermissionPlugin Qt6::QDarwinCameraPermissionPlugin_init)
    # Qt's permission handler is an Objective-C class in that static library: -ObjC keeps it in the link
    target_link_options(NyxMap PRIVATE "-ObjC")
  else()
    message(WARNING "Qt camera permission plugin not found: the camera will not be asked for on macOS")
  endif()
  # The microphone permission plugin, the same way
  find_package(Qt6QDarwinMicrophonePermissionPlugin QUIET)
  if(TARGET Qt6::QDarwinMicrophonePermissionPlugin)
    target_link_libraries(NyxMap PRIVATE Qt6::QDarwinMicrophonePermissionPlugin Qt6::QDarwinMicrophonePermissionPlugin_init)
  else()
    message(WARNING "Qt microphone permission plugin not found: the microphone will not be asked for on macOS")
  endif()
  set_target_properties(NyxMap PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "NyxMap"
    MACOSX_BUNDLE_GUI_IDENTIFIER "fr.nyxmap.app"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_LONG_VERSION_STRING "${NYXMAP_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in") # with the camera's usage text
  set(ISF_DEST "$<TARGET_FILE_DIR:NyxMap>/../Resources/isf")
  set(AUDIO_DEST "$<TARGET_FILE_DIR:NyxMap>/../Resources/audio")
elseif(WIN32)
  add_executable(NyxMap WIN32 ${APP_SOURCES} assets/NyxMap.rc)
  target_include_directories(NyxMap PRIVATE "${CMAKE_SOURCE_DIR}/assets")
  set(ISF_DEST "$<TARGET_FILE_DIR:NyxMap>/isf")
  set(AUDIO_DEST "$<TARGET_FILE_DIR:NyxMap>/audio")
else()
  add_executable(NyxMap ${APP_SOURCES})
  set(ISF_DEST "$<TARGET_FILE_DIR:NyxMap>/isf")
  set(AUDIO_DEST "$<TARGET_FILE_DIR:NyxMap>/audio")
endif()

# The source revision, for endurance-session comparison: regenerated at every build (cmake/BuildSha.cmake), not
# frozen at configure time
set(NYXMAP_BUILD_SHA_HEADER "${CMAKE_BINARY_DIR}/generated/nyxmap_build_sha.h")
add_custom_target(nyxmap_build_sha
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${NYXMAP_BUILD_SHA_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/BuildSha.cmake
  BYPRODUCTS ${NYXMAP_BUILD_SHA_HEADER}
  VERBATIM)
add_dependencies(NyxMap nyxmap_build_sha)
target_include_directories(NyxMap PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_include_directories(NyxMap PRIVATE src src/ui)
target_link_libraries(NyxMap PRIVATE nyxmap_engine Qt6::Widgets Qt6::OpenGLWidgets)

# Web pages (the Web Page generator, src/ui/WebPageQt.cpp): Qt WebEngine when it is there, in the application only
# (nyxmap_engine has no browser: the application gives it one at startup). Without it the generator is not offered.
option(NYXMAP_WEB "Web Page generator (Qt WebEngine, when found)" ON)
set(NYXMAP_WEB_LIBS)
if(NYXMAP_WEB)
  # Homebrew: the qt formula and qtwebengine can be at different versions, and Qt's prefix then lacks WebEngine's
  # headers (its packages point there all the same); they are taken from qtwebengine's own prefix then
  get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
  get_filename_component(_qt_web "${_qt_prefix}/../qtwebengine" ABSOLUTE)
  if(NOT EXISTS "${_qt_prefix}/include/QtWebEngineWidgets" AND EXISTS "${_qt_web}/include/QtWebEngineWidgets")
    foreach(_m WebEngineWidgets WebEngineCore WebEngineCoreTools)
      set(Qt6${_m}_DIR "${_qt_web}/lib/cmake/Qt6${_m}" CACHE PATH "" FORCE)
    endforeach()
  endif()
  find_package(Qt6 QUIET COMPONENTS WebEngineWidgets Quick QuickWidgets)
endif()
if(NYXMAP_WEB AND Qt6WebEngineWidgets_FOUND AND Qt6QuickWidgets_FOUND)
  set(NYXMAP_WEB_LIBS Qt6::WebEngineWidgets Qt6::Quick Qt6::QuickWidgets)
  target_link_libraries(NyxMap PRIVATE ${NYXMAP_WEB_LIBS})
  target_compile_definitions(NyxMap PRIVATE NYXMAP_HAS_WEB)
  message(STATUS "Qt WebEngine found: Web Page generator")
else()
  message(STATUS "Qt WebEngine not found: no Web Page generator")
endif()

# Copy the bundled ISF shaders next to the executable (or into the macOS bundle).
# The folder is emptied first so renamed or deleted shaders do not linger in the build.
if(APPLE)
  # Remove legacy copies from incremental bundles: codesign treats .dsp files
  # under Contents/MacOS as nested code rather than data.
  add_custom_command(TARGET NyxMap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:NyxMap>/audio")
endif()
add_custom_command(TARGET NyxMap POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${ISF_DEST}" "${AUDIO_DEST}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/isf" "${ISF_DEST}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/audio" "${AUDIO_DEST}")

# Windows: the FFmpeg DLLs must be next to NyxMap.exe
if(WIN32 AND FFMPEG_ROOT)
  file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
  if(FFMPEG_DLLS)
    add_custom_command(TARGET NyxMap POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} "$<TARGET_FILE_DIR:NyxMap>")
  endif()
endif()

# NDI and OMT next to the program, as in the packages: NyxMap looks there first (Publish.cpp). Built once into
# build/runtimes: OMT from source (scripts/build_omt.py: git, the .NET SDK 8 or later, clang++), the NDI runtime from the
# NDI SDK (scripts/fetch_ndi.py, which accepts the NDI SDK licence agreement on your behalf: only with
# NYXMAP_ACCEPT_NDI_SDK_LICENSE). Off on CI, whose packaging steps do it themselves.
if(DEFINED ENV{CI})
  set(_runtimes_default OFF)
  set(_ndi_licence_default ON)
else()
  set(_runtimes_default ON)
  set(_ndi_licence_default OFF)
endif()
option(NYXMAP_BUNDLE_RUNTIMES "Put the NDI runtime and OMT next to the program (downloads; OMT needs git, clang++ and the .NET SDK)"
       ${_runtimes_default})
option(NYXMAP_ACCEPT_NDI_SDK_LICENSE
       "Accept the NDI SDK License Agreement (https://ndi.video/sdk/), so that the build downloads the NDI runtime"
       ${_ndi_licence_default})
if(NYXMAP_BUNDLE_RUNTIMES)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(RUNTIMES "${CMAKE_BINARY_DIR}/runtimes")
  if(APPLE)
    set(RUNTIME_DEST "$<TARGET_FILE_DIR:NyxMap>/../Frameworks")
    set(NDI_FILE libndi.dylib)
    set(OMT_FILES libomt.dylib libvmx.dylib)
  elseif(WIN32)
    set(RUNTIME_DEST "$<TARGET_FILE_DIR:NyxMap>")
    set(NDI_FILE Processing.NDI.Lib.x64.dll)
    set(OMT_FILES libomt.dll libvmx.dll)
  else()
    set(RUNTIME_DEST "$<TARGET_FILE_DIR:NyxMap>")
    set(NDI_FILE libndi.so.6)
    set(OMT_FILES libomt.so libvmx.so)
  endif()
  set(RUNTIME_FILES)
  if(NYXMAP_ACCEPT_NDI_SDK_LICENSE)
    add_custom_command(OUTPUT "${RUNTIMES}/ndi/${NDI_FILE}"
      COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/scripts/fetch_ndi.py" "${RUNTIMES}/ndi"
      COMMENT "NDI runtime from the NDI SDK" VERBATIM)
    list(APPEND RUNTIME_FILES "${RUNTIMES}/ndi/${NDI_FILE}")
  else()
    message(NOTICE "The NDI runtime is not downloaded: fetching it from the NDI SDK means accepting the NDI SDK License "
                   "Agreement (https://ndi.video/sdk/). If you accept it, configure again with "
                   "-DNYXMAP_ACCEPT_NDI_SDK_LICENSE=ON, or set an NDI library folder in NyxMap.")
  endif()
  # What scripts/build_omt.py runs: git, the .NET SDK and clang++ (Xcode's on macOS)
  find_program(DOTNET dotnet)
  find_program(NYXMAP_OMT_CLANGXX clang++)
  find_program(NYXMAP_OMT_GIT git)
  set(_omt_missing)
  foreach(_tool DOTNET NYXMAP_OMT_CLANGXX NYXMAP_OMT_GIT)
    if(NOT ${_tool})
      list(APPEND _omt_missing ${_tool})
    endif()
  endforeach()
  if(NOT _omt_missing)
    list(TRANSFORM OMT_FILES PREPEND "${RUNTIMES}/omt/" OUTPUT_VARIABLE OMT_PATHS)
    add_custom_command(OUTPUT ${OMT_PATHS}
      COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/scripts/build_omt.py" "${RUNTIMES}/omt"
      DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_omt.py"
      COMMENT "OMT (libomt, libvmx) from source" VERBATIM)
    list(APPEND RUNTIME_FILES ${OMT_PATHS})
  else()
    list(TRANSFORM _omt_missing REPLACE "^DOTNET$" "the .NET SDK, version 8 or later (https://dotnet.microsoft.com/download)")
    list(TRANSFORM _omt_missing REPLACE "^NYXMAP_OMT_CLANGXX$" "clang++")
    list(TRANSFORM _omt_missing REPLACE "^NYXMAP_OMT_GIT$" "git")
    list(JOIN _omt_missing ", " _omt_missing)
    message(WARNING "OMT is not built next to NyxMap: it needs ${_omt_missing}. "
                    "Install what is missing and configure again, or set an OMT library folder in NyxMap.")
  endif()
  if(RUNTIME_FILES)
    add_custom_target(nyxmap_runtimes DEPENDS ${RUNTIME_FILES})
    add_dependencies(NyxMap nyxmap_runtimes)
    add_custom_command(TARGET NyxMap POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNTIME_DEST}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNTIME_FILES} "${RUNTIME_DEST}" VERBATIM)
  endif()
endif()

target_compile_options(nyxmap_engine PRIVATE ${NYXMAP_WARNINGS})
target_compile_options(NyxMap PRIVATE ${NYXMAP_WARNINGS})

install(TARGETS NyxMap BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Fuzzing (Clang, libFuzzer + ASan / UBSan): the HAP and Snappy readers against damaged files (test/fuzz_hap.cpp)
# ---------------------------------------------------------------------------
option(NYXMAP_FUZZ "Build the fuzz targets (requires Clang)" OFF)
if(NYXMAP_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "NYXMAP_FUZZ needs Clang (libFuzzer): -DCMAKE_CXX_COMPILER=clang++")
  endif()
  add_executable(fuzz_hap EXCLUDE_FROM_ALL test/fuzz_hap.cpp src/engine/Hap.cpp src/engine/Snappy.cpp
    src/engine/WorkerPool.cpp)
  target_include_directories(fuzz_hap PRIVATE src/engine "${CMAKE_SOURCE_DIR}/third_party/bcdec")
  target_link_libraries(fuzz_hap PRIVATE Qt6::Core)
  target_compile_options(fuzz_hap PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)
  target_link_options(fuzz_hap PRIVATE -fsanitize=fuzzer,address,undefined)
  add_executable(fuzz_osc EXCLUDE_FROM_ALL test/fuzz_osc.cpp src/engine/OscCodec.cpp)
  target_include_directories(fuzz_osc PRIVATE src/engine)
  target_link_libraries(fuzz_osc PRIVATE Qt6::Core)
  target_compile_options(fuzz_osc PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)
  target_link_options(fuzz_osc PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# ---------------------------------------------------------------------------
# Engine tests (headless): cmake --build build --target nyxmap_tests
# ---------------------------------------------------------------------------
option(NYXMAP_TESTS "Build the engine tests" ON)
if(NYXMAP_TESTS)
  add_executable(nyxmap_tests EXCLUDE_FROM_ALL test/engine_test.cpp test/operator_state.cpp test/video_test.cpp test/cue_capture.cpp test/cue_lifecycle.cpp test/gpu_targets.cpp test/image_precision.cpp test/show_hold.cpp test/output_color.cpp test/golden_render.cpp test/timeline_repeat.cpp test/timeline_events.cpp test/media_playhead.cpp test/show_actions.cpp test/interaction_actions.cpp test/interaction_filters.cpp test/color_animation.cpp test/xy_controls.cpp test/show_control.cpp test/preview_sources.cpp test/recall_delay.cpp test/recall_integrity.cpp test/emergency_stop.cpp test/psd_layers.cpp test/live_inputs.cpp test/clock_generators.cpp test/file_sources.cpp test/tox.cpp src/ui/Commands.cpp src/ui/LogModel.cpp)
  target_include_directories(nyxmap_tests PRIVATE src/ui)
  target_link_libraries(nyxmap_tests PRIVATE nyxmap_engine)
  target_compile_options(nyxmap_tests PRIVATE ${NYXMAP_WARNINGS})
  target_compile_definitions(nyxmap_tests PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
  # CTest: each targeted area in a process of its own (a new engine).
  # Test media first: sh test/make_media.sh
  enable_testing()
  add_executable(nyxmap_audio_resources_test test/audio_resources.cpp)
  target_include_directories(nyxmap_audio_resources_test PRIVATE src/engine)
  target_link_libraries(nyxmap_audio_resources_test PRIVATE Qt6::Core)
  add_test(NAME audio_resources COMMAND nyxmap_audio_resources_test)
  add_executable(nyxmap_audio_generators_test test/audio_generators.cpp)
  target_link_libraries(nyxmap_audio_generators_test PRIVATE nyxmap_engine)
  add_test(NAME audio_generators COMMAND nyxmap_audio_generators_test)
  # Test the dependency-free generated backend even on hosts with libfaust installed.
  add_executable(nyxmap_audio_generators_portable_test test/audio_generators.cpp src/engine/AudioGenerator.cpp src/engine/FaustInterpreter.cpp src/engine/AudioEffects.cpp src/engine/AudioStream.cpp src/engine/AudioCapture.cpp test/portable_audio_capture.cpp)
  target_include_directories(nyxmap_audio_generators_portable_test PRIVATE src/engine third_party/faust/architecture)
  target_link_libraries(nyxmap_audio_generators_portable_test PRIVATE Qt6::Core ffmpeg)
  add_test(NAME audio_generators_portable COMMAND nyxmap_audio_generators_portable_test)
  add_executable(nyxmap_audio_generator_engine_test test/audio_generator_engine.cpp)
  target_link_libraries(nyxmap_audio_generator_engine_test PRIVATE nyxmap_engine)
  add_test(NAME audio_generator_engine COMMAND nyxmap_audio_generator_engine_test)
  add_executable(nyxmap_remote_server_test test/remote_server.cpp)
  target_link_libraries(nyxmap_remote_server_test PRIVATE nyxmap_engine)
  add_test(NAME remote_server COMMAND nyxmap_remote_server_test)
  set_tests_properties(remote_server PROPERTIES TIMEOUT 120)
  add_executable(nyxmap_show_controls_test test/show_controls.cpp)
  target_link_libraries(nyxmap_show_controls_test PRIVATE nyxmap_engine)
  add_test(NAME show_controls COMMAND nyxmap_show_controls_test)
  set_tests_properties(show_controls PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_beat_clock_test test/beat_clock.cpp)
  target_link_libraries(nyxmap_beat_clock_test PRIVATE nyxmap_engine)
  add_test(NAME beat_clock COMMAND nyxmap_beat_clock_test)
  set_tests_properties(beat_clock PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_audio_hits_test test/audio_hits.cpp)
  target_include_directories(nyxmap_audio_hits_test PRIVATE src/engine)
  add_test(NAME audio_hits COMMAND nyxmap_audio_hits_test)
  set_tests_properties(audio_hits PROPERTIES TIMEOUT 120)
  add_executable(nyxmap_audio_levels_test test/audio_levels.cpp)
  target_link_libraries(nyxmap_audio_levels_test PRIVATE nyxmap_engine)
  add_test(NAME audio_levels COMMAND nyxmap_audio_levels_test)
  set_tests_properties(audio_levels PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_audio_effects_test test/audio_effects.cpp)
  target_link_libraries(nyxmap_audio_effects_test PRIVATE nyxmap_engine)
  add_test(NAME audio_effects COMMAND nyxmap_audio_effects_test)
  set_tests_properties(audio_effects PROPERTIES TIMEOUT 60)
  set_tests_properties(audio_generators audio_generators_portable audio_generator_engine PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_vision_analysis_test test/vision_analysis.cpp)
  target_link_libraries(nyxmap_vision_analysis_test PRIVATE nyxmap_engine)
  add_test(NAME vision_analysis COMMAND nyxmap_vision_analysis_test)
  add_executable(nyxmap_vision_engine_test test/vision_engine.cpp)
  target_link_libraries(nyxmap_vision_engine_test PRIVATE nyxmap_engine)
  add_test(NAME vision_engine COMMAND nyxmap_vision_engine_test)
  set_tests_properties(vision_analysis vision_engine PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_hand_inference_test test/hand_inference.cpp)
  target_link_libraries(nyxmap_hand_inference_test PRIVATE nyxmap_engine)
  add_test(NAME vision_hand_inference COMMAND nyxmap_hand_inference_test)
  set_tests_properties(vision_hand_inference PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
  add_executable(nyxmap_isf_library_test test/isf_library.cpp)
  target_link_libraries(nyxmap_isf_library_test PRIVATE nyxmap_engine)
  add_test(NAME isf_library COMMAND nyxmap_isf_library_test)
  add_executable(nyxmap_color_timeline_ui_test test/color_timeline_ui.cpp src/ui/AnimEditor.cpp src/ui/TimelinePanel.cpp
                 src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_color_timeline_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_color_timeline_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME color_timeline_ui COMMAND nyxmap_color_timeline_ui_test)
  set_tests_properties(color_timeline_ui PROPERTIES TIMEOUT 30)
  add_executable(nyxmap_timeline_events_ui_test test/timeline_events_ui.cpp src/ui/AnimEditor.cpp src/ui/TimelinePanel.cpp
                 src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_timeline_events_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_timeline_events_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME timeline_events_ui COMMAND nyxmap_timeline_events_ui_test)
  set_tests_properties(timeline_events_ui PROPERTIES TIMEOUT 30)
  add_executable(nyxmap_output_color_state_test test/output_color_state.cpp src/ui/Commands.cpp)
  target_include_directories(nyxmap_output_color_state_test PRIVATE src/ui)
  target_link_libraries(nyxmap_output_color_state_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME output_color_state COMMAND nyxmap_output_color_state_test)
  add_executable(nyxmap_image_import_test test/image_import.cpp)
  target_include_directories(nyxmap_image_import_test PRIVATE src/engine)
  target_link_libraries(nyxmap_image_import_test PRIVATE Qt6::Gui)
  add_test(NAME image_import COMMAND nyxmap_image_import_test)
  add_executable(nyxmap_benchmark_rates_test test/benchmark_rates.cpp)
  add_test(NAME benchmark_rates COMMAND nyxmap_benchmark_rates_test)
  add_executable(nyxmap_pixel_mapping_test test/pixel_mapping.cpp)
  target_link_libraries(nyxmap_pixel_mapping_test PRIVATE nyxmap_engine)
  target_compile_options(nyxmap_pixel_mapping_test PRIVATE ${NYXMAP_WARNINGS})
  add_test(NAME pixel_mapping COMMAND nyxmap_pixel_mapping_test)
  set_tests_properties(pixel_mapping PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_line_generator_test test/line_generator.cpp)
  target_link_libraries(nyxmap_line_generator_test PRIVATE nyxmap_engine)
  target_compile_options(nyxmap_line_generator_test PRIVATE ${NYXMAP_WARNINGS})
  target_compile_definitions(nyxmap_line_generator_test PRIVATE NYXMAP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME line_generator COMMAND nyxmap_line_generator_test)
  set_tests_properties(line_generator PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_midi_virtual_test test/midi_virtual.cpp)
  target_link_libraries(nyxmap_midi_virtual_test PRIVATE nyxmap_engine)
  add_test(NAME midi_virtual COMMAND nyxmap_midi_virtual_test)
  set_tests_properties(midi_virtual PROPERTIES TIMEOUT 30)
  add_executable(nyxmap_show_clock_test test/show_clock.cpp)
  target_link_libraries(nyxmap_show_clock_test PRIVATE nyxmap_engine)
  add_test(NAME show_clock COMMAND nyxmap_show_clock_test)
  set_tests_properties(show_clock PROPERTIES TIMEOUT 15)
  add_executable(nyxmap_audio_snapshot_test test/audio_snapshot.cpp)
  target_link_libraries(nyxmap_audio_snapshot_test PRIVATE nyxmap_engine)
  add_test(NAME audio_snapshot COMMAND nyxmap_audio_snapshot_test)
  add_executable(nyxmap_decoder_alpha_test test/decoder_alpha.cpp)
  target_link_libraries(nyxmap_decoder_alpha_test PRIVATE nyxmap_engine)
  target_compile_definitions(nyxmap_decoder_alpha_test PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
  add_test(NAME decoder_alpha COMMAND nyxmap_decoder_alpha_test)
  set_tests_properties(decoder_alpha PROPERTIES TIMEOUT 15)
  # GNU-compatible Linux linkers inject failures at our FFmpeg call sites only.
  # No test hooks or extra branches are compiled into the production decoder.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(nyxmap_decoder_allocations_test test/decoder_allocations.cpp)
    target_link_libraries(nyxmap_decoder_allocations_test PRIVATE nyxmap_engine)
    target_compile_options(nyxmap_decoder_allocations_test PRIVATE ${NYXMAP_WARNINGS})
    target_compile_definitions(nyxmap_decoder_allocations_test PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
    target_link_options(nyxmap_decoder_allocations_test PRIVATE
      -Wl,--wrap=avcodec_alloc_context3 -Wl,--wrap=avcodec_parameters_to_context
      -Wl,--wrap=av_frame_alloc -Wl,--wrap=av_packet_alloc
      -Wl,--wrap=av_seek_frame -Wl,--wrap=avcodec_flush_buffers)
    add_test(NAME decoder_allocations COMMAND nyxmap_decoder_allocations_test)
    set_tests_properties(decoder_allocations PROPERTIES TIMEOUT 20)
  endif()
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    # The Demo example: its project built (without its soundtrack) and rendered, every layer and effect in order
    add_test(NAME demo_fixture COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/examples/demo/build_show.py --out ${CMAKE_BINARY_DIR}/demo-fixture)
    set_tests_properties(demo_fixture PROPERTIES FIXTURES_SETUP demo)
    add_test(NAME demo_render COMMAND $<TARGET_FILE:NyxMap> --render ${CMAKE_BINARY_DIR}/demo-fixture/preview.png --frames 3 --strict ${CMAKE_BINARY_DIR}/demo-fixture/Demo.nyxmap)
    set_tests_properties(demo_render PROPERTIES FIXTURES_REQUIRED demo TIMEOUT 120)
    add_test(NAME endurance_analysis COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/test -p test_endurance_analysis.py)
    add_test(NAME vision_model_download COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/hand_model_download.py)
    add_test(NAME ci_scope COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/.github/scripts -p test_ci_scope.py)
  endif()
  add_executable(nyxmap_mask_geometry_test test/mask_geometry.cpp src/engine/Masks.cpp)
  target_include_directories(nyxmap_mask_geometry_test PRIVATE src/engine)
  target_link_libraries(nyxmap_mask_geometry_test PRIVATE Qt6::Gui)
  add_test(NAME mask_geometry COMMAND nyxmap_mask_geometry_test)
  find_package(Threads REQUIRED)
  add_executable(nyxmap_realtime_timing_test test/realtime_timing.cpp)
  target_include_directories(nyxmap_realtime_timing_test PRIVATE src/engine)
  target_link_libraries(nyxmap_realtime_timing_test PRIVATE Threads::Threads)
  add_test(NAME realtime_timing COMMAND nyxmap_realtime_timing_test)
  set_tests_properties(realtime_timing PROPERTIES TIMEOUT 30)

  add_executable(nyxmap_audio_analysis_test test/audio_analysis.cpp)
  target_include_directories(nyxmap_audio_analysis_test PRIVATE src/engine)
  add_test(NAME audio_analysis COMMAND nyxmap_audio_analysis_test)
  find_package(Threads REQUIRED)
  add_executable(nyxmap_publishing_concurrency_test test/publishing_concurrency.cpp)
  target_link_libraries(nyxmap_publishing_concurrency_test PRIVATE nyxmap_engine)
  add_test(NAME publishing_concurrency COMMAND nyxmap_publishing_concurrency_test)
  set_tests_properties(publishing_concurrency PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_audio_analysis_concurrency_test test/audio_analysis_concurrency.cpp)
  target_include_directories(nyxmap_audio_analysis_concurrency_test PRIVATE src/engine)
  target_link_libraries(nyxmap_audio_analysis_concurrency_test PRIVATE Threads::Threads)
  add_test(NAME audio_analysis_concurrency COMMAND nyxmap_audio_analysis_concurrency_test)
  set_tests_properties(audio_analysis_concurrency PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_vision_interaction_ui_test test/vision_interaction_ui.cpp src/ui/InteractionPanel.cpp src/ui/AudioSpectrum.cpp src/ui/PresetEditor.cpp src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp src/ui/AnimEditor.cpp src/ui/TimelinePanel.cpp)
  target_include_directories(nyxmap_vision_interaction_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_vision_interaction_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME vision_interaction_ui COMMAND nyxmap_vision_interaction_ui_test)
  set_tests_properties(vision_interaction_ui PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_audio_interaction_ui_test test/audio_interaction_ui.cpp src/ui/InteractionPanel.cpp src/ui/AudioSpectrum.cpp src/ui/PresetEditor.cpp src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp src/ui/AnimEditor.cpp src/ui/TimelinePanel.cpp)
  target_include_directories(nyxmap_audio_interaction_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_audio_interaction_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME audio_interaction_ui COMMAND nyxmap_audio_interaction_ui_test)
  add_executable(nyxmap_interaction_actions_ui_test test/interaction_actions_ui.cpp src/ui/InteractionPanel.cpp src/ui/AudioSpectrum.cpp src/ui/PresetEditor.cpp src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp src/ui/AnimEditor.cpp src/ui/TimelinePanel.cpp)
  target_include_directories(nyxmap_interaction_actions_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_interaction_actions_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME interaction_actions_ui COMMAND nyxmap_interaction_actions_ui_test)
  add_executable(nyxmap_interaction_filters_ui_test test/interaction_filters_ui.cpp src/ui/InteractionPanel.cpp src/ui/AudioSpectrum.cpp src/ui/PresetEditor.cpp src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp src/ui/AnimEditor.cpp src/ui/TimelinePanel.cpp)
  target_include_directories(nyxmap_interaction_filters_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_interaction_filters_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME interaction_filters_ui COMMAND nyxmap_interaction_filters_ui_test)
  add_executable(nyxmap_snapshot_update_ui_test test/snapshot_update_ui.cpp src/ui/PresetEditor.cpp src/ui/Commands.cpp)
  target_include_directories(nyxmap_snapshot_update_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_snapshot_update_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME snapshot_update_ui COMMAND nyxmap_snapshot_update_ui_test)
  add_executable(nyxmap_preset_snapshot_integrity_test test/preset_snapshot_integrity.cpp src/ui/PresetEditor.cpp src/ui/Commands.cpp)
  target_include_directories(nyxmap_preset_snapshot_integrity_test PRIVATE src/ui)
  target_link_libraries(nyxmap_preset_snapshot_integrity_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME preset_snapshot_integrity COMMAND nyxmap_preset_snapshot_integrity_test)
  add_executable(nyxmap_timeline_activity_test test/timeline_activity.cpp src/ui/TimelineActivity.cpp src/ui/SequencePanel.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_timeline_activity_test PRIVATE src/ui)
  target_link_libraries(nyxmap_timeline_activity_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME timeline_activity COMMAND nyxmap_timeline_activity_test)
  set_tests_properties(timeline_activity PROPERTIES TIMEOUT 30)
  # The Web Page generator: settings, parameters, persistence; a real page too when Qt WebEngine is there (it needs a
  # display: xvfb-run on Linux; Chromium's sandbox is turned off in the test only)
  add_executable(nyxmap_web_page_test test/web_page.cpp src/ui/WebPageQt.cpp)
  target_include_directories(nyxmap_web_page_test PRIVATE src/ui)
  target_link_libraries(nyxmap_web_page_test PRIVATE nyxmap_engine Qt6::Widgets ${NYXMAP_WEB_LIBS})
  target_compile_options(nyxmap_web_page_test PRIVATE ${NYXMAP_WARNINGS})
  if(NYXMAP_WEB_LIBS)
    target_compile_definitions(nyxmap_web_page_test PRIVATE NYXMAP_HAS_WEB)
  endif()
  add_test(NAME web_page COMMAND nyxmap_web_page_test)
  set_tests_properties(web_page PROPERTIES TIMEOUT 120)
  add_executable(nyxmap_cue_dispatch_test test/cue_dispatch.cpp)
  target_link_libraries(nyxmap_cue_dispatch_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME cue_dispatch COMMAND nyxmap_cue_dispatch_test)
  set_tests_properties(cue_dispatch PROPERTIES TIMEOUT 60)
  foreach(area show_control xy_controls color_animation output_color show_hold recall_integrity image_precision operator_state preview_sources emergency_stop cue_capture cue_lifecycle gpu_targets golden timeline_repeat timeline_events media_playhead show_actions interaction_actions interaction_filters recall_delay isf_safe video project playback undo structure compositing osc snapshots masks rendering thread publishing
          media sound layersource timecode interactions psd live_inputs clocks file_sources tox)
    add_test(NAME ${area} COMMAND nyxmap_tests --area ${area})
  endforeach()
  # Areas depending on real time (sound clocks, threads, waits, publishers): label "timing".
  # CI gates on the first attempt; run_timing_tests.py reruns failures for diagnosis only.
  set_tests_properties(sound playback thread publishing timecode snapshots media file_sources PROPERTIES LABELS timing)
  add_executable(nyxmap_sequence_ui_test test/sequence_ui.cpp src/ui/SequencePanel.cpp src/ui/TimelineActivity.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_sequence_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_sequence_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME sequence_ui COMMAND nyxmap_sequence_ui_test)
  add_executable(nyxmap_number_widgets_test test/number_widgets.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_number_widgets_test PRIVATE src/ui)
  target_link_libraries(nyxmap_number_widgets_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME number_widgets COMMAND nyxmap_number_widgets_test)
  add_executable(nyxmap_mask_ui_test test/mask_ui.cpp src/ui/MappingView.cpp src/ui/MappingViewLines.cpp src/ui/FixtureOverlay.cpp src/ui/GlDraw.cpp
                 src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp src/ui/MaskPanel.cpp)
  target_include_directories(nyxmap_mask_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_mask_ui_test PRIVATE nyxmap_engine Qt6::Widgets Qt6::OpenGLWidgets)
  add_test(NAME mask_ui COMMAND nyxmap_mask_ui_test)
  add_executable(nyxmap_line_ui_test test/line_ui.cpp src/ui/MappingView.cpp src/ui/MappingViewLines.cpp src/ui/FixtureOverlay.cpp
                 src/ui/GlDraw.cpp src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_line_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_line_ui_test PRIVATE nyxmap_engine Qt6::Widgets Qt6::OpenGLWidgets)
  add_test(NAME line_ui COMMAND nyxmap_line_ui_test)
  set(WORKSPACE_TEST_SOURCES ${APP_SOURCES})
  list(REMOVE_ITEM WORKSPACE_TEST_SOURCES src/main.cpp)
  add_executable(nyxmap_workspace_ui_test test/workspace_ui.cpp test/xy_parameter_ui.cpp test/color_parameter_ui.cpp test/clock_preset_ui.cpp test/layer_source_ui.cpp test/audio_fx_ui.cpp test/controls_ui.cpp ${WORKSPACE_TEST_SOURCES})
  target_include_directories(nyxmap_workspace_ui_test PRIVATE src src/ui "${CMAKE_BINARY_DIR}/generated")
  # Without Qt WebEngine (WebPageQt.cpp compiled empty): the Media Bin then does not offer the Web Page generator
  target_link_libraries(nyxmap_workspace_ui_test PRIVATE nyxmap_engine Qt6::Widgets Qt6::OpenGLWidgets)
  target_compile_definitions(nyxmap_workspace_ui_test PRIVATE TEST_DIR="${CMAKE_SOURCE_DIR}/test")
  if(APPLE)
    target_link_libraries(nyxmap_workspace_ui_test PRIVATE "-framework AppKit")
  endif()
  add_test(NAME workspace_ui COMMAND nyxmap_workspace_ui_test)
  add_test(NAME clock_preset_ui COMMAND nyxmap_workspace_ui_test --clock-presets)
  add_test(NAME layer_source_ui COMMAND nyxmap_workspace_ui_test --layer-source)
  add_test(NAME controls_ui COMMAND nyxmap_workspace_ui_test --controls)
  set_tests_properties(controls_ui PROPERTIES TIMEOUT 60)
  add_test(NAME audio_fx_ui COMMAND nyxmap_workspace_ui_test --audio-fx)
  set_tests_properties(audio_fx_ui PROPERTIES TIMEOUT 60)
  add_test(NAME system_isf_ui COMMAND nyxmap_workspace_ui_test --system-isf)
  set_tests_properties(clock_preset_ui PROPERTIES TIMEOUT 60)
  add_test(NAME xy_parameter_ui COMMAND nyxmap_workspace_ui_test --xy)
  set_tests_properties(xy_parameter_ui PROPERTIES TIMEOUT 60)
  add_test(NAME color_parameter_ui COMMAND nyxmap_workspace_ui_test --color-animation)
  set_tests_properties(color_parameter_ui PROPERTIES TIMEOUT 60)
  add_test(NAME selection_ui COMMAND nyxmap_workspace_ui_test --selection) # not in the Valgrind job: see the test
  add_executable(nyxmap_preview_ui_test test/preview_ui.cpp src/ui/PreviewWorkspace.cpp src/ui/MappingView.cpp src/ui/MappingViewLines.cpp src/ui/FixtureOverlay.cpp
                 src/ui/GlDraw.cpp src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_preview_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_preview_ui_test PRIVATE nyxmap_engine Qt6::Widgets Qt6::OpenGLWidgets)
  add_test(NAME preview_ui COMMAND nyxmap_preview_ui_test)
  add_executable(nyxmap_fixture_ui_test test/fixture_ui.cpp src/ui/FixturePanel.cpp src/ui/FixtureInspector.cpp
                 src/ui/DmxOutputsPanel.cpp src/ui/FixtureOverlay.cpp
                 src/ui/MappingView.cpp src/ui/MappingViewLines.cpp src/ui/PreviewWorkspace.cpp src/ui/GlDraw.cpp src/ui/Commands.cpp
                 src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_fixture_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_fixture_ui_test PRIVATE nyxmap_engine Qt6::Widgets Qt6::OpenGLWidgets)
  add_test(NAME fixture_ui COMMAND nyxmap_fixture_ui_test)
  set_tests_properties(fixture_ui PROPERTIES TIMEOUT 60)
  add_executable(nyxmap_recall_delay_ui_test test/recall_delay_ui.cpp src/ui/SnapshotPanel.cpp src/ui/PresetEditor.cpp
                 src/ui/SequencePanel.cpp src/ui/TimelineActivity.cpp src/ui/Commands.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_recall_delay_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_recall_delay_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME recall_delay_ui COMMAND nyxmap_recall_delay_ui_test)
  add_executable(nyxmap_color_picker_ui_test test/color_picker_ui.cpp src/ui/Widgets.cpp src/ui/ColorPicker.cpp)
  target_include_directories(nyxmap_color_picker_ui_test PRIVATE src/ui)
  target_link_libraries(nyxmap_color_picker_ui_test PRIVATE nyxmap_engine Qt6::Widgets)
  add_test(NAME color_picker_ui COMMAND nyxmap_color_picker_ui_test)
  set_tests_properties(color_picker_ui PROPERTIES TIMEOUT 30)
  if(NOT WIN32) # the application's exit codes in --render mode (Windows: its Qt libraries are deployed separately)
    add_test(NAME render_cli COMMAND ${CMAKE_COMMAND} -DNYXMAP=$<TARGET_FILE:NyxMap> -DOUT=${CMAKE_BINARY_DIR}/render_cli
             -P "${CMAKE_SOURCE_DIR}/test/render_cli.cmake")
  endif()
  # Several videos played together: cmake --build build --target nyxmap_bench
  add_executable(nyxmap_bench EXCLUDE_FROM_ALL test/bench.cpp)
  target_link_libraries(nyxmap_bench PRIVATE nyxmap_engine)
endif()

# TouchEngine is a redistributable SDK shim, not a TouchDesigner installation.
if(NYXMAP_TOUCHENGINE_FRAMEWORK)
  set_property(TARGET NyxMap APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
  add_custom_command(TARGET NyxMap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:NyxMap>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND}
      "-DSOURCE=${NYXMAP_TOUCHENGINE_FRAMEWORK}"
      "-DDESTINATION=$<TARGET_BUNDLE_DIR:NyxMap>/Contents/Frameworks/TouchEngine.framework"
      -P "${NYXMAP_TOUCHENGINE_STAGE_SCRIPT}"
    VERBATIM)
elseif(NYXMAP_TOUCHENGINE_DLL)
  add_custom_command(TARGET NyxMap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NYXMAP_TOUCHENGINE_DLL}" "$<TARGET_FILE_DIR:NyxMap>/TouchEngine.dll")
endif()
