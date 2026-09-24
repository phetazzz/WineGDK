# Vector Direct2D Effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. No subagents unless the user explicitly chooses delegation.

**Goal:** Render and verify every builtin Direct2D effect CLSID embedded in the supplied Vector DLL, preserving the working command-list/layer path, and produce an x86_64/i386 GDK-Proton package.

**Architecture:** A builtin GPU evaluator separates graph validation and bounds/request propagation from execution. An evaluation owns intermediate images and memoizes repeated inputs; the device context owns reusable D3D11 resources. Logical bounds, texture allocation bounds, DPI, and alpha representation are separate, and WIC, color management, histogram, and planar input receive dedicated implementations.

**Tech Stack:** Wine C/COM, D3D11/HLSL, Direct2D/DWrite/WIC, existing Wine color-management APIs, Wine tests, DXVK, WineD3D, pinned umu SDK container, Xvfb.

**Spec:** `/home/peti/Projects/BedrockProjects/GDK-Proton-d2d-build/vector-effects-design.md` (user approved).

## Global Constraints

- Actual pixel processing, not CreateEffect-only or pass-through stubs.
- Properties, defaults, validation, input counts, alpha handling and DPI.
- Preserve destination target, clips, layer state, text state and D3D11 state.
- Unsupported/invalid conditions return an explicit HRESULT through Wine's existing error path; no silent identity effects or fake-success rendering.
- Build x86_64/i386 d2d1 DLLs and produce a separately named GDK-Proton package.
- No replacement of an installed/running Proton package without a separate request.
- Do not label intermediate group-A builds as all-44 coverage.
- Do not modify DXVK, application binaries, remote repositories, or CI configuration.
- Run GUI tests only in Xvfb. Do not reuse the Minecraft prefix for tests.
- No new external runtime dependency without a concrete need and user approval.
- Do not automatically commit the existing uncommitted alpha/layer changes or the effect draft. Commit only when requested; preserve their provenance in snapshots/diffs.

## Review Focus

1. Negative/fractional origins and non-square DPI must not shift a crop, blur halo, mask, or final DrawImage. Tasks 3–6 test these numerically.
2. Infinite output or transparent-black-changing effects must evaluate the requested finite region without huge allocations or accidental clipping to an input. Tasks 3, 7–9, and 13 test this.
3. Cyclic graphs, wide shared DAGs, changed properties, and source==target aliases must not hang, leak, use stale images, or sample a bound RTV. Tasks 3–4 and 16 test this.
4. Straight-alpha color math and HDR values must not acquire dark fringes or unintended clamps. Tasks 5–8, 12, and 15 test this.
5. A failed shader compilation, input acquisition, allocation, or nested draw must restore both COM/render state and the host D3D11 pipeline. Tasks 2–4 and 16 test this.

---

## Workspace and known evidence

Repository: `/home/peti/Projects/BedrockProjects/WineGDK`.
Build/artifact root: `/home/peti/Projects/BedrockProjects/GDK-Proton-d2d-build`.
Binary: `/home/peti/Projects/BedrockProjects/vector(12).dll`.
SHA-256: `aa1e7c6cea00ebf24873f5fdc536fca772d4cbce8c58e21295bc60089d843fa2`.

`eb79918efb7` contains the original command-list work. The uncommitted alpha/layer changes are the basis of user-confirmed working `layers-r3`. The unfinished effect draft has not been integrated or built. In particular, it declares undefined bounds/rasterization entry points, conflates logical bounds with allocation rounding, uses depth-only traversal limits, and does not implement all sampling modes. Do not assume the draft is a verified foundation.

Existing chain test `test_effect_images()` failed on r3 (five failures). The new implementation must make it pass without weakening its assertions. Existing focused tests cover command lists, IGNORE-alpha targets, and layer replay. The r3 full suite reported 17,741 assertions, 243 todo, zero failures, one REF-device skip. Record fresh baselines because the working tree now also includes unfinished tests.

## File responsibilities

| File | Responsibility |
|---|---|
| `dlls/d2d1/effect.c` | Existing COM identity, input ownership, property callbacks; registration dispatcher. |
| `dlls/d2d1/effect_builtin.c` (new) | Missing builtin schemas/validation and per-effect descriptors. Move only new/shared code here; no unrelated rewrite. |
| `dlls/d2d1/effect_render.c` (draft) | Evaluation, requested regions, GPU passes, effect-specific execution. Split to a shader source header rather than a giant mixed C/HLSL function. |
| `dlls/d2d1/effect_shaders.h` (new) | Shared HLSL and effect-family shader sources, compiled using the existing D3DCompile conventions. |
| `dlls/d2d1/effect_private.h` (new) | Private effect descriptors, logical image results and evaluation interfaces. |
| `dlls/d2d1/command_list.c` | Bounds traversal and effect-image capture semantics for recorded lists. |
| `dlls/d2d1/device.c` | DrawImage/GetImageLocalBounds/GetImageWorldBounds integration, isolated command-list rasterization, renderer teardown. |
| `dlls/d2d1/d2d1_private.h` | Minimal cross-file hooks and context-owned renderer pointer. |
| `dlls/d2d1/color_context.c` (new) | Color-context COM implementation needed by ColorManagement. |
| `dlls/d2d1/Makefile.in` | Add sources and only necessary existing Wine imports. |
| `dlls/d2d1/tests/d2d1.c` | Existing end-to-end chain/layer tests; preserve their working setup helpers. |
| `dlls/d2d1/tests/effects.c` (new) | Table-driven registration/property tests, focused math/pixel/bounds tests, independent fixtures. |
| `dlls/d2d1/tests/Makefile.in` | Register `effects.c`; WIC imports only when used. |
| Artifact root `vector-effects-coverage.md` | Per-GUID status and evidence, kept separate from success claims. |

## Execution commands

Use an isolated **build directory**, not a clean checkout that loses the uncommitted working layer changes. Preserve the current source tree; task-specific changes must be inspected alongside the saved baseline.

Pinned SDK:

```sh
docker run --rm --user 1000:1000 -e CCACHE_DISABLE=1 -e HOME=/tmp \
  -v /home/peti/Projects/BedrockProjects/WineGDK:/src:ro \
  -v /home/peti/Projects/BedrockProjects/GDK-Proton-d2d-build:/build \
  -w /build \
  ghcr.io/open-wine-components/umu-sdk@sha256:0bb33aacc80194bed34eea08a64f85b3dcf5f3887d81494782a58d5689a0c399 \
  bash -c 'make -C win64 -j8 dlls/d2d1/all > win64/build-effects.log 2>&1 && make -C win32 -j8 dlls/d2d1/all > win32/build-effects.log 2>&1'
```

`win64` was configured with `/src/configure --enable-win64 --disable-tests`; `win32` with `/src/configure --disable-tests`. New source lists trigger Makefile regeneration.

Host test build:

```sh
make -C /tmp/opencode/winegdk-d2d-build -j8 dlls/d2d1/all dlls/d2d1/tests/all
python3 /tmp/opencode/winegdk-d2d-build/stage-d2d.py
```

The stage script changes the builtin marker **only in a disposable system-Wine test-prefix copy**. Archive DLLs retain original builtin markers.

WineD3D focused test (after adding `effects.c`):

```sh
env -u WAYLAND_DISPLAY LIBGL_ALWAYS_SOFTWARE=1 \
  WINEPREFIX=/tmp/opencode/winegdk-d2d-prefix WINEDLLOVERRIDES='d2d1=n' \
  WINETEST_NO_MT_D3D=1 WINEDEBUG=-all xvfb-run -a wine \
  /tmp/opencode/winegdk-d2d-build/dlls/d2d1/tests/x86_64-windows/d2d1_test.exe effects
```

Full suite: run the same executable with `d2d1` and then `effects`; record both summaries. The existing `/tmp/opencode/d2d1-package-test.c` wrapper is useful during bring-up, but the checked-in `effects.c` entry point is the deliverable, not an untracked-only test.

DXVK: use a separately assembled candidate package at `$CANDIDATE`, then:

```sh
env -u WAYLAND_DISPLAY WINEPREFIX=/tmp/opencode/gdk-proton-d2d-test-prefix \
  WINEDLLOVERRIDES='d3d11,dxgi=n' D2D1_TEST_D3D11_ONLY=1 \
  WINETEST_NO_MT_D3D=1 WINEDEBUG=+d2d xvfb-run -a \
  "$CANDIDATE/files/bin/wine64" \
  /tmp/opencode/winegdk-d2d-build/dlls/d2d1/tests/x86_64-windows/d2d1_test.exe effects
```

Implement the `D2D1_TEST_D3D11_ONLY` check in the new test entry point. The test prefix already contains the Minecraft DXVK `d3d11.dll`/`dxgi.dll`; native D3D10 creation is not available through those DLLs. Log DXVK version and loaded d2d1 path so a system builtin cannot silently satisfy the run.

---

### Task 1: Freeze the binary inventory and compatibility matrix

**Files:** Artifact root `vector-effects-coverage.md`, `vector-effects-inventory.json`, `inventory-vector-effects.py`.
**Consumes:** DLL bytes, checked-in Wine headers and official SDK effect GUID definitions.
**Produces:** An auditable finite set of effect CLSIDs with address and reference evidence.

- [ ] Save tracked and untracked draft changes without resetting the working tree. Record `git status --short`, HEAD, binary hash, and the existing build/test environment.
- [ ] Scan `d2d1effects.h`, `d2d1effects_1.h`, `d2d1effects_2.h` and later official effect definitions, comparing GUIDs in little-endian PE layout:

```python
raw = (data1.to_bytes(4, 'little') + data2.to_bytes(2, 'little')
       + data3.to_bytes(2, 'little') + bytes(data4))
offset = dll_bytes.find(raw)
if offset >= 0:
    va = pe.OPTIONAL_HEADER.ImageBase + pe.get_rva_from_offset(offset)
```

- [ ] Deduplicate by GUID, distinguish effect CLSIDs from interface IIDs, and retain exact source-header revisions. Expand the matrix if this cross-check finds additional effects.
- [ ] For every CLSID record registration status, property/default/input schema, finite/unbounded bounds, alpha rules, reference source, pixel test, build status and actual runtime status. Initial status is `pending`, never `supported` from GUID presence.
- [ ] Cross-reference the eight observed callers using `/tmp/opencode/vector-effects-disassembly.txt`, including SetValue indices and value origins. Verify a generic CreateEffect helper cannot select a separate GUID table dynamically before claiming the eight are exhaustive *usage*.
- [ ] Review the inventory against the approved 44-entry list. Any additions get a corresponding task and test row before coding them.

### Task 2: Build the reference test harness and missing property infrastructure

**Files:** `tests/effects.c`, test Makefile, `effect_builtin.c`, `effect_private.h`, `effect.c`.
**Produces:** Tests consume real `ID2D1Effect` objects and compare GPU output with independent numeric fixtures.

- [ ] Add a fixture owning D3D11 device/context, D2D device/context, FP32 or BGRA target, staging readback texture, factory and COM initialization. Unwind all successfully created objects on setup failure. Do not depend on helper internals in the existing giant test file.
- [ ] Define these test-local interfaces:

```c
struct effect_test_context;
static BOOL init_effect_context(struct effect_test_context *ctx);
static void cleanup_effect_context(struct effect_test_context *ctx);
static ID2D1Bitmap1 *create_float_bitmap(struct effect_test_context *ctx,
        UINT width, UINT height, const D2D1_VECTOR_4F *pixels);
static HRESULT draw_effect(struct effect_test_context *ctx, ID2D1Effect *effect,
        const D2D1_POINT_2F *offset, const D2D1_RECT_F *source_rect);
static D2D1_VECTOR_4F read_float_pixel(struct effect_test_context *ctx, UINT x, UINT y);
static void check_vector(D2D1_VECTOR_4F actual, D2D1_VECTOR_4F expected, float tolerance);
```

- [ ] Add an identity-fixture test using a raw bitmap: opaque red, half-premultiplied red `{.5,0,0,.5}`, transparent black and HDR `{2,1,.5,1}`. Assert exact float values for copy, and `{.5,.5,0,1}` for half-red over opaque green. This validates the fixture before effects.
- [ ] Build and run the harness; a rendering setup skip is not a pass. Print adapter/backend details.
- [ ] Add per-effect registration/schema cases with `CreateEffect`, `GetInputCount`, `GetValueSize`, typed default values, wrong property size/type, null inputs and input-count ranges. Add each effect's case in its owning task, not meaningless count-only tests.
- [ ] Implement owned-array/IUnknown/color-context property storage with AddRef/Release, deep-copy arrays, explicit property types, range validation and changed-property tracking. Existing generic macros do not implement every required ownership/validation rule.
- [ ] Check resource release when replacing a property twice or failing a property update; the old value must remain valid and referenced. Do not blindly enable registrations before their render semantics are ready.

### Task 3: Graph validation, logical bounds, requested regions and evaluation ownership

**Files:** `effect_private.h`, `effect_render.c`, `effect_builtin.c`, `d2d1_private.h`, `tests/effects.c`.
**Interfaces:** Replace draft bitmap-plus-rounded-bounds API with:

```c
struct d2d_effect_image
{
    struct d2d_bitmap *bitmap;
    D2D1_RECT_F logical_bounds;
    D2D1_RECT_F texture_bounds;
    D2D1_ALPHA_MODE alpha_mode;
};
struct d2d_effect_evaluation;
HRESULT d2d_effect_get_bounds(struct d2d_device_context *context,
        ID2D1Image *image, D2D1_RECT_F *bounds);
HRESULT d2d_effect_evaluate(struct d2d_device_context *context,
        ID2D1Image *image, const D2D1_RECT_F *requested,
        struct d2d_effect_image *result);
void d2d_effect_image_cleanup(struct d2d_effect_image *image);
```

- [ ] Test a diamond DAG with a shared input; mutate a leaf property between draws and verify new pixels. Test direct/two-node cycles and release cycles explicitly in test teardown. Tests must not leave COM reference cycles alive.
- [ ] Introduce a tri-color DFS keyed by image identity (`visiting` detects a cycle, `visited` permits shared nodes), an explicit work limit and an allocation budget. Count nodes/work, not merely recursive depth. Validate device/factory compatibility and missing inputs before touching the output target.
- [ ] Keep memoization evaluation-local and keyed by node plus requested region/DPI. Reuse shared outputs only when the requested region is covered. Clean every intermediate on all exits.
- [ ] Separate exact logical bounds from texel-rounded allocation bounds:

```c
/* Input: fractional DIP bounds, output: pixel-aligned texture bounds only. */
texture.left = floorf(logical.left * dpi_x / 96.0f) * 96.0f / dpi_x;
texture.right = ceilf(logical.right * dpi_x / 96.0f) * 96.0f / dpi_x;
```

- [ ] Test logical crop `{-2.25f, 1.5f, 10.25f, 8.5f}` at DPI `(144,192)`; GetImageLocalBounds must retain fractions even though the texture uses integral texels.
- [ ] Define per-effect `bounds` and `map_input_request` callbacks. Propagate transform requests through inverse transforms; expand convolution requests by support radius; intersect crop requests; request both composite inputs. Represent empty/unbounded results explicitly, never through a giant finite texture.
- [ ] Test a finite crop over Flood/Border without overflow, and an invalid enormous finite bitmap request with an error before allocation. Validate NaN/Inf and matrix singularity according to native/documented behavior; use a native probe to resolve uncertain HRESULTs rather than inventing them.

### Task 4: Wire DrawImage, command-list inputs and GPU state isolation

**Files:** `device.c`, `command_list.c`, `effect_render.c`, private headers, both test files.
**Consumes:** Task 3 result/evaluation interfaces.
**Produces:** Effect images work as ordinary DrawImage inputs and inside command lists.

- [ ] Implement a command-list bounds traversal over stored operations using recorded transforms, geometry bounds/widened bounds, bitmap/source rectangles, glyph geometry and clip/layer intersections. Share the command iterator rather than interpreting buffer layout in another file. Clear without a finite clip yields an unbounded image.
- [ ] Expose `d2d_command_list_get_bounds(context, list, bounds, depth)` only as an internal bridge into the evaluator; ensure the evaluator's cycle/work tracking includes command-list image edges.
- [ ] Implement isolated command-list rasterization using the existing sink. Apply translation from logical origin to allocation origin once, clear under PREMULTIPLIED alpha, and restore target/clip/text/DPI state after any stream error.
- [ ] Add effect-image dispatch to DrawImage after command-list recording handling. Preserve null offset versus explicit offset and image_rect origin semantics. Composite the evaluated image with the destination transform and source clip exactly once.
- [ ] Implement effect GetImageLocalBounds and world bounds using the same bounds evaluation; world bounds applies the destination world transform, not texture rounding.
- [ ] Test the existing shadow chain over a command-list source; test a stroked negative-origin geometry, glyph, clip and rounded layer. Test recording an effect then modifying it: probe native capture timing and match native snapshot/reference semantics rather than assuming live replay.
- [ ] Sample a source bitmap that aliases the destination through a snapshot or reject with the native error; never bind the same subresource as SRV and RTV. Add the corresponding realistic alias test.
- [ ] Validate host D3D11 state by binding sentinel shaders/resources/viewport/blend/depth state before DrawImage, querying them afterwards, then rendering a sentinel pixel. Explicitly clear unintended geometry/hull/domain shaders and blend/depth state in effect passes. Restore on shader compilation and allocation failure too.
- [ ] Wire `d2d_effect_renderer_destroy()` into device-context destruction, and use one cleanup label for each acquired resource set. Run existing command-list/layer tests before moving on.

### Task 5: GPU kernels, sampling and the observed transform/composite chain

**Files:** `effect_shaders.h`, `effect_render.c`, `effect_builtin.c`, `tests/effects.c`, `tests/d2d1.c`.
**Effects:** Crop, Scale, 2DAffineTransform, Composite.

- [ ] Test crop retains image-space origin, Scale honors center point, affine transform supports rotation/shear/negative determinant, and null DrawImage offset retains negative bounds. Use a 2x2 four-color input and exact integer-coordinate transforms first.
- [ ] Build shared fullscreen GPU pass resources per context: shaders, constant buffers, sampler descriptors and explicit raster/blend state. Propagate HRESULTs from every resource creation/update. Do not return a bitmap with missing SRV/RTV as success.
- [ ] Implement nearest, linear, cubic, multisample-linear, anisotropic and high-quality cubic modes using their documented kernels/footprints; do not silently map non-linear modes to bilinear. Verify cubic overshoot and sharpness using native/reference fixtures.
- [ ] Cover transparent border, hard border, negative sampling coordinates and IGNORE input alpha outside the image. For border-clamped samples alpha follows the sampled edge, not an unrelated uv-inside test.
- [ ] Implement all Composite modes with correct ordered inputs and logical bounds. In particular SOURCE_COPY is not union bounds, and MASK_INVERT must not be omitted. Numeric premultiplied reference cases:

```c
/* s=(.5,0,0,.5), d=(0,.25,0,.25) */
/* SOURCE_OVER = s + d*(1-s.a) = (.5,.125,0,.625) */
/* SOURCE_IN   = s*d.a         = (.125,0,0,.125) */
/* XOR         = s*(1-d.a)+d*(1-s.a) = (.375,.125,0,.5) */
```

- [ ] Run representative every-mode pixel/bounds tests and chain tests under both backends. Check input order with three distinct colored inputs, not only two opaque images.

### Task 6: Blur and shadow kernels

**Files:** same rendering/descriptor/test files.
**Effects:** GaussianBlur, DirectionalBlur, Shadow.

- [ ] Test a single opaque impulse on transparent black, a uniform image under hard border, sigma zero, fractional sigma, non-square DPI, a directional kernel at 0/45/90 degrees, and colored half-alpha shadows.
- [ ] Implement Gaussian weights normalized independently from out-of-bounds samples (soft-border energy must disappear outside the input, not be renormalized at every edge):

```c
double weight = exp(-0.5 * distance * distance / (sigma * sigma));
/* Normalize the complete kernel once; sample transparent black outside soft bounds. */
```

- [ ] Execute separable horizontal/vertical passes. Expand soft bounds by `3*sigma` DIPs per side, retain hard bounds and implement the documented border sampling. Track DPI separately in each axis.
- [ ] Implement DirectionalBlur angle and support along the direction, and Shadow alpha-only blur followed by straight color converted to premultiplied output. Verify sigma=0 still performs Shadow colorization.
- [ ] Implement SPEED/BALANCED/QUALITY semantics without unbounded dynamic shader loops. Use documented downsampling/kernel optimization with reference tolerances; report perceptual/numeric differences found against native outputs.
- [ ] Make both preexisting `test_effect_images()` chains pass; add halo/background and GetImageLocalBounds assertions. Do not package group A as all-effect completion.

### Task 7: Color matrix and pointwise color/alpha processing

**Effects:** ColorMatrix, Brightness, Grayscale, HueRotation, Saturation, LuminanceToAlpha, Premultiply, UnPremultiply.
**Files:** descriptors, shaders, renderer, tests.

- [ ] Add default/typed/range tests for each effect and four-pixel color/alpha/HDR fixtures from Task 2.
- [ ] Implement explicit alpha-domain conversion; zero-alpha division must be defined and must not generate NaNs. ColorMatrix rows follow Direct2D's 5x4 layout including the constant row; clamp only where requested.
- [ ] Verify a matrix that swaps red/blue, a matrix with an alpha bias and a matrix producing values above 1. Constant alpha bias changes transparent black and can affect output outside finite input bounds; route through requested-region logic.
- [ ] Implement luminance coefficients and hue/saturation transforms from the fetched Direct2D specifications, not generic sRGB guesses. Test Saturation=0 equals the documented luminance mapping and HueRotation=360 returns the original within tolerance.
- [ ] Test brightness black/white-point mapping with midtone anchors and degenerate point validation. Test premultiply then unpremultiply on nonzero alpha, and transparent/HDR inputs separately.
- [ ] Run each effect individually and a multi-effect chain to expose double-premultiplication and missing color-domain conversions.

### Task 8: Transfer functions and highlights/shadows

**Effects:** LinearTransfer, GammaTransfer, DiscreteTransfer, TableTransfer, HighlightsShadows.
**Files:** descriptors, renderer/shader files, tests.

- [ ] Add owned-array property tests for unequal channel table lengths, disabled channels, zero/one-element tables and rejected malformed byte sizes. Confirm native empty-table behavior before accepting it.
- [ ] Implement linear `slope*x+intercept`, gamma `amplitude*pow(x, exponent)+offset`, interpolated table lookup and discrete interval selection in the documented alpha/color domain. Apply each channel's disable flag and clamp option independently.
- [ ] Test exact table boundaries and endpoints with values `[0,.25,.5,.75,1]`; use asymmetric tables so wrong indexing cannot pass. Test replacements after one draw for evaluation invalidation.
- [ ] Implement HighlightsShadows with documented input gamma, mask blur radius, highlights/shadows/clarity controls and a luminance mask pass. Establish reference fixtures on a dark/midtone/bright ramp and compare against native Windows for the nonlinear curve; do not substitute a hand-tuned brightness filter.
- [ ] Test zero controls as identity, individual positive/negative controls, alpha preservation and border behavior around a high-contrast mask edge.

### Task 9: Repetition, metadata, atlas and DPI compensation

**Effects:** Atlas, Border, DpiCompensation, OpacityMetadata, Tile.
**Files:** descriptors, evaluator, sampler shaders, tests.

- [ ] Add 2x2 asymmetric checker tests at negative coordinates for wrap/mirror/clamp; implement independent horizontal/vertical edge modes. Finite crop over Border/Tile must request finite input regions without allocating unbounded output.
- [ ] Test Atlas input rectangle/padding with nonzero origin; distinguish atlas sample padding from logical content bounds and avoid bleeding neighboring atlas texels.
- [ ] Implement DpiCompensation from input DPI, destination DPI, interpolation and border properties. Test input 192x96 DPI drawn into 96x144 DPI with rectangular—not square—features.
- [ ] Implement OpacityMetadata's documented opaque-rectangle metadata without changing pixels. Pass-through pixels are correct for this metadata effect only; metadata must remain observable to internal bounds/optimization code and never falsely mark transparent pixels opaque.
- [ ] Validate Tile's rectangle and infinite bounds via a finite downstream Crop, including partial border tiles.

### Task 10: Arithmetic, blend and displacement

**Effects:** ArithmeticComposite, Blend, DisplacementMap.
**Files:** descriptors, GPU shaders, evaluator, tests.

- [ ] Implement arithmetic `k1*s*d + k2*s + k3*d + k4`, proper input order, alpha treatment and optional clamp. Test `k4 != 0` on transparent/empty regions and HDR values.
- [ ] Implement each D2D1_BLEND_MODE, including nonseparable hue/saturation/color/luminosity and dissolve behavior. Use W3C/Direct2D definitions and native comparison for ambiguous random/dissolve details; do not treat Blend as Porter-Duff Composite.
- [ ] Table-drive asymmetric source/destination color tests per mode; include zero/one values for division/dodge/burn, transparent and half-alpha inputs, and nonseparable color cases with different channel orderings.
- [ ] Implement DisplacementMap's X/Y channel selectors, scale in the documented coordinate space and border handling. Test neutral map `.5`, positive/negative displacement, independently selected R/A channels and mismatched input sizes.
- [ ] Check source and displacement map bounds/request propagation independently so sampling is not incorrectly cropped to their intersection.

### Task 11: Convolution and morphology

**Effects:** ConvolveMatrix, Morphology.
**Files:** descriptors, GPU shaders, renderer, tests.

- [ ] Implement validated kernel arrays with exact width*height size, divisor/bias, kernel offset, kernel-unit length, alpha preservation, interpolation, clamp and border properties.
- [ ] Test a nonsymmetric 3x3 kernel with one nonzero coefficient off-center. This distinguishes correlation/convolution orientation and kernel-offset mistakes. Test divisor zero according to native behavior, not arbitrary division-by-zero handling.
- [ ] Test blur-like kernel, edge detector with signed/HDR results, preserve-alpha on semi-transparent input, and non-unit kernel spacing. Derive support/request expansion from kernel coordinates.
- [ ] Implement erosion/dilation as component-wise neighborhood min/max in the documented alpha domain. Test rectangular 3x5 kernels, one-pixel identity and shape/bounds changes on an impulse and a hollow rectangle.
- [ ] Bound shader work using native-supported sizes or documented multipass decomposition. Reject invalid values without leaking intermediate textures.

### Task 12: 3D transforms

**Effects:** 3DTransform, 3DPerspectiveTransform.
**Files:** descriptors, evaluator, transform shaders, tests.

- [ ] Add property/default tests for matrix, interpolation, border, depth, rotation origin/order, perspective origin and local/global offset.
- [ ] Implement homogeneous-coordinate mapping and clipping consistently with Direct2D. A 3D transform is not a 2D affine matrix with discarded terms. Handle perspective-correct sampling and bounds crossing the eye plane.
- [ ] Test identity, pure translation, z-axis rotation matching 2D affine, a foreshortened checkerboard and a native-reference perspective projection. Include points with near-zero w, behind-camera geometry and a cropped finite request.
- [ ] Verify 3DPerspectiveTransform's composed matrix against documented transform order and native reference points. Test both nonzero local and global offsets so reversed order fails.

### Task 13: Flood and turbulence generators

**Effects:** Flood, Turbulence.
**Files:** descriptors, evaluator, shaders, tests.

- [ ] Test Flood at half alpha over green, unbounded local bounds, finite Crop consumption and color-property mutation between frames. Render only the requested region.
- [ ] Implement deterministic turbulence/fractal-sum using the specified seeded noise algorithm, octave accumulation, offset/size, base frequency and stitchable behavior. Use the SVG/Direct2D algorithm and native fixtures; a generic hash-noise shader is not equivalent.
- [ ] Test same seed gives same image, distinct seeds differ, zero octaves/default behavior, frequency changes, negative offset and matching opposite borders for a stitchable tile.
- [ ] Preserve generator alpha semantics and expose finite/infinite bounds according to each effect's specification.

### Task 14: Lighting family

**Effects:** DistantDiffuse, DistantSpecular, PointDiffuse, PointSpecular, SpotDiffuse, SpotSpecular.
**Files:** descriptors, shaders, evaluator, tests.

- [ ] Build a shared height/normal shader from input alpha using Direct2D's documented edge/corner gradient coefficients and kernel unit lengths. Read the required neighborhood through the request mapper.
- [ ] Diffuse uses the appropriate light vector and clamped normal dot product; specular uses the documented halfway/view vector and exponent. Point/spot positions and surface scale must use consistent units. Spot adds focus, cone angle and points-at normalization.
- [ ] Test a flat opaque height field under a perpendicular distant light (known uniform diffuse output), a tilted ramp, a single bump and each image edge/corner. Test a moved point light changes highlight position in the expected direction.
- [ ] Test spotlight cone inclusion/exclusion, points-at equal to light position, focus/exponent limits, colored lights, kernel spacing and zero constants. Confirm specular output alpha and premultiplication using native references.
- [ ] Register/verify each of the six CLSIDs individually despite sharing GPU code. Run chained color/transform tests on lighting results.

### Task 15: Specialized input, color and result effects

**Effects:** BitmapSource, ColorManagement, Histogram, YCbCr.
**Files:** `color_context.c`, `effect_builtin.c`, `effect.c`, renderer, private headers, Makefiles, `tests/effects.c`.

- [ ] BitmapSource: own the IWICBitmapSource property correctly; use WIC conversion/transform APIs for supported pixel formats, orientation, scale, interpolation and alpha. Test a synthetic WIC 2x3 bitmap with distinct corners, replacement source, disposal and source mutation/caching semantics.
- [ ] Color contexts: implement factory/resource identity, source/profile lifetime, GetColorSpace/GetProfileSize/GetProfile and profile validation. Integrate existing Wine color-management APIs through a color transform/LUT pathway; do not add a new external CMM dependency by default.
- [ ] ColorManagement: implement source/destination contexts, rendering intents and alpha handling. Test sRGB-to-linear anchors: 0 -> 0, 1 -> 1 and .5 -> approximately .214041; test linear-to-sRGB inverse, custom ICC profile transforms and invalid profiles. Preserve HDR behavior when supported by the selected format.
- [ ] Histogram: implement channel selection, bin count, output values and result availability after evaluation. Use a four-pixel input with exact bin populations and compare normalized counts/sum. A visual pass-through is not sufficient; GetValue must return computed results and reevaluation must invalidate old results.
- [ ] YCbCr: implement Y/Cb/Cr input planes, chroma subsampling/alignment and the documented transform matrix/interpolation properties. Test neutral chroma gray, black/white anchors, distinct chroma values, odd dimensions, differing plane dimensions and required/missing alpha-plane behavior according to the actual schema.
- [ ] Include IUnknown-property refcount checks and teardown under failed conversion/profile/plane validation. Record native verification gaps rather than declaring broad support from a default-case pass.

### Task 16: Cross-effect integration, build, audit and package

**Files:** all effect/layer tests, source manifests, coverage report, package provenance.

- [ ] For every inventory row require evidence for registration, non-default properties, bounds and numeric output. Cross-check current SDK scan again; no row may remain silently omitted.
- [ ] Run both Vector chains, DirectionalBlur and ColorMatrix call patterns, then a composed stress graph with a shared command-list leaf, rounded layers, negative offsets and 192x144 DPI. Repeat property mutation and draw at least twice to expose stale caches.
- [ ] Run state-sentinel/alias/cycle/failure-cleanup tests from Tasks 3–4, including failures in a nested layer and in each specialized property type. Run full `d2d1` and `effects` suites under WineD3D, and all D3D11-capable focused tests under the package's DXVK path in Xvfb.
- [ ] Build x86_64 and i386 with the pinned SDK command above. Run i386 tests when a compatible test runtime is available; otherwise explicitly distinguish build verification from runtime verification. Do not claim unrun architecture tests pass.
- [ ] Review `git diff --check`, actual HRESULT propagation, state/resource lifetime and coverage table against the spec. Rebuild/retest only if review changes code or reveals an unverified concern.
- [ ] Assemble `GDK-Proton10-32-d2d1-effects-r4` from the known base plus both verified DLLs; set unique version/compatibilitytool names and include source HEAD, full diff (including new files), SDK digest, DLL hashes, tests, limitations and inventory hash in provenance.
- [ ] Create `.tar.gz` and `.sha256`; inspect archive DLL bytes against built outputs and verify original builtin markers. Keep installed layers-r3 untouched unless asked to replace it.
- [ ] Final report must list any unsupported modes/semantics and native-comparison gaps. If any of the target effects remains unimplemented, report the overall objective as incomplete; do not relabel partial group coverage as full future compatibility.

## Self-review mapping

- 44 current GUIDs: A=8 (Tasks 5–7), B=12 (Tasks 7–8), C=12 (Tasks 9–12), D=6 (Task 14), E=6 (Tasks 13,15). ColorMatrix is in A and Task 7, counted once.
- API/property ownership: Tasks 2 and every effect task; missing registration is an implementation item, not a reason to shrink scope.
- Bounds/DPI/empty/infinite/alpha: Tasks 3–15 with explicit fixtures.
- COM/D3D11/layer isolation and failure paths: Tasks 2–4, 16.
- SDK scan and source references: Task 1; unexpected GUIDs require expanding this plan.
- Build/test/package/diff provenance: Task 16. Deployment and remote changes are excluded.

## Review and execution choice

This is a substantial renderer implementation, not a small extension of eight
switch cases. Review the plan before continuing product code. Recommended:
native execution in this session, because graph/bounds/image ownership and
shader interfaces are tightly coupled. User-authorized delegation is possible,
but is not assumed by this plan. Group checkpoints report verified progress;
they do not reduce the approved all-GUID objective.
