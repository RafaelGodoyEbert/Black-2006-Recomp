# Ambient occlusion regressions

These tests use the XboxRecomp source from the `third_party/xboxrecomp` and `third_party/dsp56300`
submodules (`git submodule update --init --recursive`). They require Windows, Visual Studio 2022's C/C++
tools and Python. Retail BLACK files are not needed. The launcher's settings model has its own unit tests
in `launcher/tests`.

```powershell
cmake -S tests/ambient_occlusion -B build-ambient-occlusion -G "Visual Studio 17 2022" -A x64
cmake --build build-ambient-occlusion --config Release
ctest --test-dir build-ambient-occlusion -C Release --output-on-failure
cmake --build build-ambient-occlusion --config Debug
ctest --test-dir build-ambient-occlusion -C Debug --output-on-failure
```

The workflow first compiles the production renderer, video and input libraries, then runs:

- The actual video settings and menu row services: all AO methods and qualities, INI round trips,
  validation, legacy SSAO conversion, per-field environment locks, persistence, cycling and Reset.
- Native page parsing, input and state against a fake game host, including both English AO rows.
- Crosshair defaults and marker timing, plus the extracted production hit-ownership lookup: the latest
  player wins even for equal timestamps, ring/cursor wrap, expiry and eviction. Per-player frame checks
  verify that both hit and kill markers appear only for the returned owner.
- The actual embedded pixel shaders on D3D11 WARP: all 16 enabled method/quality combinations,
  unoccluded flat/sky depth, an occluded corner, finite float masks, R8 masks, bilateral blur,
  alpha-preserving scene darkening, distinct kernels, quality differences and camera sensitivity.
  All 16 method/quality combinations also preserve unoccluded tilted planes at four depths,
  including viewport boundaries. Existing DOF, FXAA, sharpening and SSAA shader entry points also compile. The actual 1x1
  autofocus and CoC passes additionally verify that minority foreground silhouettes do not
  pull focus from the background, deliberate foreground subjects acquire focus, invalid depth
  and history stay finite, and sky/temporal smoothing retain their intended behavior.
  The CoC and final DoF RGB passes also blur backgrounds at twice the focus distance across
  four perspective-depth ranges, including distant depths compressed above 0.998. All three
  presets preserve foreground, focus tolerance and alpha. FXAA must smooth a stair-step edge,
  sharpening presets must visibly differ on detail, SSAA must resolve alternating texels to
  their mean, and the colour filters must preserve uniform fields at viewport boundaries.
- The authored camera hook and guard regressions in `camera/`.
- Focus timing across 30/60 Hz and interrupted scenes, alongside AO draw-order checks.
- The extracted production autofocus coordinator across missing, invalid or incompatible
  depth, disabled DoF and unavailable resources: skipped observations retain their original
  timestamp so long interruptions reacquire focus, while short gaps still smooth normally.
  Failed resource setup leaves the scene eligible for retry. Successful DLSS upscaling must
  route sharpening through output-sized scratch; fallback uses the scene-sized copy. Disabled
  sharpening and output-scratch allocation failure are covered. This mocks the DLSS boundary,
  so it does not establish NVIDIA runtime availability or visual DLSS correctness.
- The extracted production AO draw trigger/coordinator against synthetic surfaces: an early
  blended draw with absent or incompatible depth does not consume the frame's AO pass; later
  valid depth retries exactly once. Invalid resources/camera and completed-pass deduplication
  are checked without running the game.

WARP uses synthetic depth and scene textures; it does not execute the complete guest game or
prove HUD timing in gameplay. Camera/frame queue behavior and final scene appearance still need
real-game verification. Native font/image rendering and the optional historical default-menu
migration checks are skipped without their privately owned input files. No retail assets or
generated guest source are included in this test directory.

For a private live-game check, `RECOMP_KGPU_POST_TRACE=1` logs the renderer's setting generations
and focus observation timing. `RECOMP_KGPU_DOF_SCENE_DUMP_PREFIX=<absolute prefix>` captures one
actual DoF pass at or after `RECOMP_KGPU_DOF_SCENE_DUMP_FRAME` (default 120): `_before.bmp`,
`_after.bmp`, `_coc.bmp`, and `_depth.bin`. The RGB pair is from the same frame before the HUD;
the trace reports the GPU focus scalar and CoC distribution. These are optional synchronous
readbacks, unsuitable for frame-time benchmarks. No readback is added when capture is unset.
`RECOMP_KGPU_SHARP_SCENE_DUMP_PREFIX` and `RECOMP_KGPU_SHARP_SCENE_DUMP_FRAME` similarly capture
one same-frame `_before.bmp`/`_after.bmp` pair around the actual sharpening pass, including its
output-sized path after successful DLSS. The trace records whether upscaling ran and the size.

`RECOMP_KGPU_CAPTURE_PREFIX=<absolute prefix>` enables a finite sequence of finished-frame BMPs,
starting at `RECOMP_KGPU_CAPTURE_FRAME` (default 120), spaced by
`RECOMP_KGPU_CAPTURE_INTERVAL` frames (default 60), for `RECOMP_KGPU_CAPTURE_COUNT` captures
(default 1, capped at 120). This observes moving scenes without pausing in photo mode. It captures
the game render, including its HUD, before presenter overlays. Counts/spacing are diagnostic frame
numbers, so compare recorded wall-clock timing too when testing different simulation rates.

The depth file stores little-endian uint32 width/height, float32 focus, then width*height
float32 depth samples. Replay a private capture through the production CoC shader with:

```powershell
ao-warp-test.exe <kelvin_gpu_post.inc> --dof-depth <prefix_depth.bin>
```

To compare the previous formula on identical geometry/focus, supply its saved renderer source
and append `--legacy` (uses its original scale 140 and raw-depth zone 0.002). Keep captures local;
they contain the user's retail scene. Shader replay still does not reproduce guest draw ordering.
