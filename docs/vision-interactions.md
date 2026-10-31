# Motion masks and visual interactions

## Measure motion from a camera

1. Add a camera source to a layer. The same tools work on movies, images, and generators.
2. Add **Motion Difference Absolute** or **Motion Difference Relative** from the effects library.
3. Adjust **Threshold** and **Softness** while watching the black/white image. White means change. Use the ordinary FX mask routing when this image should mask another effect.
4. In **Interactions**, add a **Pixels** input. Select the analyzed layer and **Post FX** to measure the resulting mask. Select **Active area** for the fraction of changed pixels, **Mean / amount** for average intensity, or **Energy** for mean squared intensity.
5. Map that input to an effect parameter, then adjust the existing interaction range, curve and smoothing controls.

A hidden layer can still supply an enabled visual interaction. Select a layer and choose **Pre FX** or **Post FX**. Post FX reads the complete effect chain, including a generated motion mask. Pre FX reads that layer before its effects. Composition and raw-source taps are not offered; older raw-source taps migrate to Pre FX. A source layer must be selected before analysis starts.

### Absolute reference

**Motion Difference Absolute** captures the initial image. Press **Capture reference** with an empty scene to detect a person entering or leaving. An optional external reference image can be selected instead. **Background adaptation** slowly learns quiet pixels; leave it at zero for a fixed reference. It does not learn pixels above the change threshold.

**Lighting compensation** offers off, global RGB offset, and offset plus exposure compensation. Leave it off to detect lighting changes. Offset compensation removes common brightness/color shifts; exposure compensation also adjusts contrast when the reference contains sufficient contrast. Statistics use a coarse median, so a small foreground object does not shift the whole background. Large objects covering most of the scene, shadows, clipped highlights and spatially varying illumination can still produce changes. This is image differencing, not semantic person segmentation.

### Relative history

**Motion Difference Relative** compares the current frame with the frame **N** rendered frames ago, or with the mean of the previous **N** frames (1–8). Press **Reset history** after changing a camera or scene. Initialization and reset fill the complete history and suppress the initial flash.

Both effects also offer **Lighten accumulation (hold)** under temporal smoothing. **Lighten hold frames** supports 1–4096 frames, with 0 retaining the result until Capture reference / Reset history. Each changed pixel retains its brightest mask value until that many frames without new motion at that pixel. Binary masks therefore show the union of recently moving regions. Graded masks hold their peak and refresh the hold on each new hit; this is a peak hold rather than an exact sliding maximum of graded values. Use Active area or Energy in a separate Post FX pixel interaction to measure the accumulated footprint. Threshold and blur still apply before accumulation. The comparison history remains independent from this longer accumulation window.

Both effects offer spatial blur and temporal persistence. **Maximum trail** retains recent movement; **Exponential average** smooths it. These controls affect the mask itself. The interaction's filters can separately smooth a derived number. History and adaptation follow rendered frames, so their timing depends on frame rate.

## Independent pixel analysis

The **Pixels** source can measure any image without either motion FX. Channels are red, green, blue, alpha, luminance (Rec.709 weights), and chrominance magnitude (distance from luminance). Select a normalized region using X, Y, width and height; the preview outlines it.

| Measurement | Meaning |
|---|---|
| Mean / amount | Average selected-channel value, 0–1 |
| Energy | Mean squared selected-channel value, 0–1 |
| Active area | Fraction above the threshold, 0–1 |
| Peak | Maximum selected-channel value |
| Centroid X / Y | Intensity-weighted center of pixels above threshold, in normalized image coordinates |
| Spread | Weighted distance from that center |
| Frame change | Average absolute difference from the last analyzed image |
| Velocity X / Y | Centroid displacement per second; may be negative |
| Mean derivative | Change in mean intensity per second; may be negative |

Analysis uses asynchronous GPU readback at 64×64, or the hand-analysis resolution when shared with a hand input. It drops samples when the GPU is busy. These values are suitable for interactive control, not full-resolution measurement. Up to eight distinct source/tap pairs and 128 analyzer configurations are supported. Missing images or data older than half a second yield zero. The first sample and configuration changes reset temporal measurements.

## Hand gestures

Build with OpenCV's **core**, **imgproc**, and **dnn** libraries (see below). Download the two pinned models once:

```sh
python3 scripts/download_hand_models.py --directory /path/to/hand-models
```

In **Interactions → Hands**, choose the camera layer, select this folder, and choose a tracked hand and gesture. The preview displays the detected skeleton and any model error. Available inputs include presence, confidence, wrist/index/thumb coordinates, pinch strength, finger openness, rotation, and visible hand count (0–2). Pinch and openness are normalized heuristics derived from landmarks, not trained gesture classifiers. Hand slots follow the nearest previous wrist and can swap when hands cross or disappear.

Set a minimum confidence, then map the input to the desired parameter. Position is normalized to the image; Y increases downwards. Use an XY interaction's two inputs for index X and Y. Existing interaction smoothing helps reduce landmark jitter. Hand inference runs on a worker thread at a submission rate of at most 15 Hz, with at most two hands and image dimensions up to 320 pixels. Video stays on this machine. Lost/stale detections yield zero after half a second, before any configured interaction smoothing.

Models are optional external files, verified by size/SHA-256 when downloaded and SHA-256 when loaded. They are not Git submodules or bundled weights. `third_party/hand-models/models.json` pins the upstream commit, download URLs and hashes. An unavailable backend, missing file or wrong hash produces an explicit error and no gestures. The model-folder preference is local to the computer; source and gesture settings are saved in the project.

## Build and upstream choices

`NYXMAP_HAND_TRACKING=ON` (default) enables the backend if OpenCV is found. Install OpenCV development packages, e.g. Homebrew `opencv` on macOS, `libopencv-core-dev libopencv-imgproc-dev libopencv-dnn-dev` on Debian/Ubuntu, or OpenCV with DNN via vcpkg on Windows. Set `OpenCV_DIR` / `CMAKE_PREFIX_PATH` when needed. With `-DNYXMAP_HAND_TRACKING=OFF`, motion shaders and pixel interactions remain available. Distributable builds must package the corresponding OpenCV shared libraries; the existing release dependency lists do not install OpenCV automatically.

The implementation uses native OpenCV DNN CPU inference and OpenCV Zoo's MediaPipe palm/21-landmark ONNX models, adapting its pre/postprocessing under Apache-2.0. This reuses an established detector without a browser, Python runtime, or MediaPipe Tasks/Bazel dependency in the application. Sources: [OpenCV Zoo palm detector](https://github.com/opencv/opencv_zoo/tree/48b10fa505aeb51fbd127fc60a5c660040e823b7/models/palm_detection_mediapipe), [landmark estimator](https://github.com/opencv/opencv_zoo/tree/48b10fa505aeb51fbd127fc60a5c660040e823b7/models/handpose_estimation_mediapipe), and [MediaPipe Hands](https://chuoling.github.io/mediapipe/solutions/hands.html). See `licenses/opencv-zoo-hands.txt`.

Automated tests cover synthetic motion masks on OpenGL, pixel statistics and derivatives, async interaction mapping, project persistence, UI edits/undo, and missing/corrupt model handling. Real-model inference tests require the model downloads. Validate detection accuracy, latency, and dependency packaging on each target platform before distributing a hand-enabled build.

To exercise real model inference after installing the weights, run `NYXMAP_HAND_MODELS=/path/to/hand-models ctest --test-dir build -R vision_hand_inference --output-on-failure`. Set `NYXMAP_HAND_IMAGE` to an image with a visible hand to also check positive detection. Without the models, this test is explicitly skipped.
