# Ray Reconstruction

`IRayReconstruction` reconstructs noisy ray-traced radiance and performs temporal
upscaling in one operation. The first implementation uses NVIDIA NGX DLSSD directly
on Windows x64 (D3D12 and Vulkan), without Streamline. It is independent of
`ISuperResolution`; do not run conventional DLSS SR on its output.

## Implementation structure

The module follows the SuperResolution base/provider/factory structure:

- `RayReconstructionBase` owns the description, reference-counted interface,
  jitter calculation, common input validation, and resource state transitions.
  It has no NGX dependency.
- `RayReconstructionProvider` is the internal interface for variant enumeration,
  source settings queries, and object creation. `RayReconstructionFactory.cpp`
  registers providers and dispatches by variant ID without SDK-specific logic.
- `RayReconstructionDLSS` owns per-object NGX parameters and feature lifetime,
  maps common DLSSD parameters, and delegates native operations to its backend.
  `DLSSProviderD3D12.cpp` and `DLSSProviderVk.cpp` acquire the shared runtime and
  implement native validation, resource conversion, creation, evaluation, and release.
  Providers and execution objects share backend ownership, so destroying the factory
  does not invalidate an execution object.
- `RayReconstructionFSR.cpp` reserves a provider creation entry point that returns
  null. It advertises no variants and adds no FSR SDK dependency.

To add another implementation, derive an execution object from the base, implement
the provider interface, and register its creation function with the factory.
Keep SDK types and backend-specific checks within that implementation.

## Initialization

Link `Diligent-RayReconstruction-static` or `Diligent-RayReconstruction-shared`.
Deploy `DiligentNGX_64d.dll`/`DiligentNGX_64r.dll` using
`copy_diligent_ngx_runtime(target)` and `nvngx_dlssd.dll` using
`copy_dlssd_dlls(target)`. Shared RR additionally needs the RayReconstruction DLL.
The SDK is obtained from the pinned hzqst/DLSS-Headers commit; `DLSS_BUILD_VARIANT`
selects `rel` (default) or `dev` independently of the application build configuration.
When linking installed static archives directly, also link the installed
`DiligentNGX_64d.lib`/`DiligentNGX_64r.lib` import library.

For Vulkan, set `EngineVkCreateInfo::EnableRayReconstruction = True` **before**
creating the device. This requests NGX's required instance/device extensions and
fails explicitly if they cannot be enabled. Attached external devices must already
enable and report their extension lists to the Vulkan device wrapper.

```cpp
#include "RayReconstructionFactoryLoader.h"

RefCntAutoPtr<IRayReconstructionFactory> Factory;
LoadAndCreateRayReconstructionFactory(Device, &Factory);
if (!Factory)
    return;
Uint32 Count = 0;
Factory->EnumerateVariants(Count, nullptr);
if (Count == 0)
    return; // Retain the application's existing rendering path.

RayReconstructionSourceSettingsAttribs Query;
Query.OutputWidth = Width;
Query.OutputHeight = Height;
Query.Quality = RAY_RECONSTRUCTION_QUALITY_QUALITY;
RayReconstructionSourceSettings Settings;
if (!Factory->GetSourceSettings(Query, Settings))
    return;

RayReconstructionDesc Desc;
Desc.InputWidth = Settings.OptimalInputWidth;
Desc.InputHeight = Settings.OptimalInputHeight;
Desc.OutputWidth = Width;
Desc.OutputHeight = Height;
Desc.Quality = Query.Quality;
RefCntAutoPtr<IRayReconstruction> RR;
Factory->CreateRayReconstruction(Desc, &RR);
```

The default DLSS preset is F (SDK 310.9.1). Default, D and E can be selected
explicitly. Unsupported quality queries fail without silently changing quality.
The object owns its device/runtime references and may outlive the factory.

## Input contract

- Color: noisy, pre-tone-mapped radiance. Supply diffuse/specular albedo separately.
- Normals: world-space unit XYZ in a floating-point texture, **not NRD encoded**.
  Linear roughness is in a separate texture, or normal.w when `RoughnessPacked` is set.
- Depth: positive linear view depth by default. For hardware [0,1] depth, set
  `LinearDepth = False`; `DepthInverted` indicates reverse Z.
- Motion: previous minus current, including camera motion. Scale converts the XY
  values to render-resolution pixels. Declare jittered motion at object creation.
- Jitter: render-pixel units. Use the same offset for camera ray generation and RR.
- Matrices: unjittered row-major Diligent row-vector WorldToView and ViewToClip.
- Optional guides: diffuse/specular hit distances in world units and specular motion
  in render pixels. A null optional input clears it for that frame.
- Exposure: optional 1x1 texture when automatic exposure is disabled. Scalar
  pre-exposure/exposure scale default to one and must be positive.

All textures are full 2D, single-sample, single-mip views. Input textures match input
resolution; the output UAV matches output resolution. Output must not alias an input.
Depth uses R32_FLOAT (or D32_FLOAT hardware depth); exposure and hit distances use
R16_FLOAT/R32_FLOAT. Motion and normals require floating-point channels.
The module does not decode NRD buffers or generate missing guides.

```cpp
ExecuteRayReconstructionAttribs A;
A.pContext = Context;
A.pColorSRV = NoisyRadiance;
A.pDepthSRV = LinearDepth;
A.pMotionVectorsSRV = Motion;
A.pDiffuseAlbedoSRV = DiffuseAlbedo;
A.pSpecularAlbedoSRV = SpecularAlbedo;
A.pNormalsSRV = WorldNormals;
A.pRoughnessSRV = LinearRoughness;
A.pOutputUAV = ReconstructedHDR;
RR->GetJitterOffset(FrameIndex, A.JitterX, A.JitterY);
// Use these offsets for this frame's camera rays; fill unjittered matrices in A.
A.ResetHistory = CameraCut;
if (!RR->Execute(A))
    return; // Discard output on failure.
```

Execute outside a render pass on the owning device's immediate graphics context.
Execute submits pending context commands; end active queries before calling it.
The default transition mode transitions all resources. `VERIFY` validates tracked
states; `NONE` leaves transitions to the caller. NGX invalidates cached pipeline
state, so rebind the subsequent pass. Output remains in unordered-access state;
RR inserts a UAV dependency, and the caller transitions it for later consumers.

The first frame resets history automatically. Recreate the object when resolution,
quality, preset or creation flags change. Submit pending GPU commands before object
destruction; destruction waits for submitted feature work before releasing NGX.

## Validation and future RTXPT integration

`RayReconstructionTest.*` exercises factory lifetime, C interfaces, reconstruction,
readback, native/upscaled resolution, packed/unpacked roughness and reset/recreation.
Set `DILIGENT_TEST_RAY_RECONSTRUCTION=1` when running API tests with `--mode=vk`.
Unavailable devices skip hardware tests; skips are not GPU validation evidence.

Future RTXPT integration must prepare raw radiance and DLSSD-compatible guides,
share pixel-space jitter with camera rays, and replace its denoising/upscaling branch
with RR before bloom/tone mapping. This module does not enable or change RTXPT's
existing rendering path.

### Preset F Vulkan validation limitation

On an RTX 5060 with driver 610.74 and Vulkan SDK 1.4.350.0 validation, SDK 310.9.1
Preset F reports `VUID-vkCmdDraw-None-09600` for two SDK-owned
`nv.ngx.dlssd.resource` images on first evaluation (UNDEFINED versus GENERAL layout).
This reproduces with both dev and rel DLLs, with automatic exposure disabled, and
when feature creation is submitted separately. Identical inputs with explicit Preset E pass GPU readback and
validation. The module does not suppress these messages or silently switch presets.
Use `Desc.DLSSPreset = RAY_RECONSTRUCTION_DLSS_PRESET_E` to select the validated
Vulkan path in this environment. Preset F remains the requested default.

For diagnostic API test runs, `DILIGENT_TEST_RR_PRESET=5` selects E and `=6` selects F.
