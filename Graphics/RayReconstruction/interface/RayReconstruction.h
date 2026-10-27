/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include "../../../Primitives/interface/Object.h"
#include "../../GraphicsEngine/interface/DeviceContext.h"
#include "../../GraphicsEngine/interface/TextureView.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

static DILIGENT_CONSTEXPR INTERFACE_ID IID_RayReconstruction =
    {0x882fd368, 0x04f8, 0x4cbd, {0xa7, 0xe3, 0x30, 0x0e, 0x44, 0x2b, 0x67, 0xd9}};
static DILIGENT_CONSTEXPR INTERFACE_ID RayReconstructionVariant_DLSS =
    {0x71f12ca0, 0x181d, 0x4dd5, {0x8b, 0x97, 0xee, 0x8f, 0x2f, 0xcc, 0x42, 0xe1}};

// clang-format off
DILIGENT_TYPED_ENUM(RAY_RECONSTRUCTION_QUALITY, Uint32)
{
    RAY_RECONSTRUCTION_QUALITY_NATIVE = 0,
    RAY_RECONSTRUCTION_QUALITY_ULTRA_QUALITY,
    RAY_RECONSTRUCTION_QUALITY_QUALITY,
    RAY_RECONSTRUCTION_QUALITY_BALANCED,
    RAY_RECONSTRUCTION_QUALITY_PERFORMANCE,
    RAY_RECONSTRUCTION_QUALITY_ULTRA_PERFORMANCE,
    RAY_RECONSTRUCTION_QUALITY_COUNT
};

DILIGENT_TYPED_ENUM(RAY_RECONSTRUCTION_DLSS_PRESET, Uint32)
{
    RAY_RECONSTRUCTION_DLSS_PRESET_DEFAULT = 0,
    RAY_RECONSTRUCTION_DLSS_PRESET_D = 4,
    RAY_RECONSTRUCTION_DLSS_PRESET_E = 5,
    RAY_RECONSTRUCTION_DLSS_PRESET_F = 6
};

/// Fixed-resolution creation settings. Recreate the object when these change.
struct RayReconstructionDesc
{
    INTERFACE_ID VariantId DEFAULT_INITIALIZER(RayReconstructionVariant_DLSS);
    Uint32 InputWidth  DEFAULT_INITIALIZER(0);
    Uint32 InputHeight DEFAULT_INITIALIZER(0);
    Uint32 OutputWidth  DEFAULT_INITIALIZER(0);
    Uint32 OutputHeight DEFAULT_INITIALIZER(0);
    RAY_RECONSTRUCTION_QUALITY Quality DEFAULT_INITIALIZER(RAY_RECONSTRUCTION_QUALITY_QUALITY);
    RAY_RECONSTRUCTION_DLSS_PRESET DLSSPreset DEFAULT_INITIALIZER(RAY_RECONSTRUCTION_DLSS_PRESET_F);
    Bool IsHDR DEFAULT_INITIALIZER(True);
    Bool AutoExposure DEFAULT_INITIALIZER(True);
    /// True: positive linear view depth. False: hardware depth in [0, 1].
    Bool LinearDepth DEFAULT_INITIALIZER(True);
    /// Applies to hardware depth only.
    Bool DepthInverted DEFAULT_INITIALIZER(False);
    Bool MotionVectorsJittered DEFAULT_INITIALIZER(False);
    /// True: linear roughness is stored in normal.w. False: a separate roughness SRV is required.
    Bool RoughnessPacked DEFAULT_INITIALIZER(False);
};
typedef struct RayReconstructionDesc RayReconstructionDesc;

/// All inputs are full, single-sampled 2D SRVs at input resolution; output is a full UAV.
/// Color is noisy radiance before denoising, tone mapping and conventional SR.
struct ExecuteRayReconstructionAttribs
{
    IDeviceContext* pContext DEFAULT_INITIALIZER(nullptr);
    RESOURCE_STATE_TRANSITION_MODE StateTransitionMode DEFAULT_INITIALIZER(RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ITextureView* pColorSRV DEFAULT_INITIALIZER(nullptr);
    ITextureView* pDepthSRV DEFAULT_INITIALIZER(nullptr);
    /// Previous minus current, including camera motion; scales below convert to render pixels.
    ITextureView* pMotionVectorsSRV DEFAULT_INITIALIZER(nullptr);
    ITextureView* pDiffuseAlbedoSRV DEFAULT_INITIALIZER(nullptr);
    ITextureView* pSpecularAlbedoSRV DEFAULT_INITIALIZER(nullptr);
    /// World-space unit normals in [-1,1], not NRD-encoded normals.
    ITextureView* pNormalsSRV DEFAULT_INITIALIZER(nullptr);
    ITextureView* pRoughnessSRV DEFAULT_INITIALIZER(nullptr);
    ITextureView* pOutputUAV DEFAULT_INITIALIZER(nullptr);
    /// Optional 1x1 exposure. Ignored when AutoExposure is enabled.
    ITextureView* pExposureSRV DEFAULT_INITIALIZER(nullptr);
    /// Optional guides. Distances are in world units; specular motion is in render pixels.
    ITextureView* pDiffuseHitDistanceSRV DEFAULT_INITIALIZER(nullptr);
    ITextureView* pSpecularHitDistanceSRV DEFAULT_INITIALIZER(nullptr);
    ITextureView* pSpecularMotionVectorsSRV DEFAULT_INITIALIZER(nullptr);
    float JitterX DEFAULT_INITIALIZER(0);
    float JitterY DEFAULT_INITIALIZER(0);
    float MotionVectorScaleX DEFAULT_INITIALIZER(1);
    float MotionVectorScaleY DEFAULT_INITIALIZER(1);
    float PreExposure DEFAULT_INITIALIZER(1);
    float ExposureScale DEFAULT_INITIALIZER(1);
    Bool ResetHistory DEFAULT_INITIALIZER(False);
    /// Row-major Diligent row-vector transforms, without projection jitter.
    float WorldToView[16] DEFAULT_INITIALIZER({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
    float ViewToClip[16] DEFAULT_INITIALIZER({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
};
typedef struct ExecuteRayReconstructionAttribs ExecuteRayReconstructionAttribs;

#define DILIGENT_INTERFACE_NAME IRayReconstruction
#include "../../../Primitives/interface/DefineInterfaceHelperMacros.h"
#define IRayReconstructionInclusiveMethods \
    IObjectInclusiveMethods; \
    IRayReconstructionMethods RayReconstruction

DILIGENT_BEGIN_INTERFACE(IRayReconstruction, IObject)
{
    VIRTUAL const RayReconstructionDesc REF METHOD(GetDesc)(THIS) CONST PURE;
    /// Pixel-space Halton jitter; pass the same values to ray generation and Execute.
    VIRTUAL void METHOD(GetJitterOffset)(THIS_ Uint32 Index, float REF JitterX, float REF JitterY) CONST PURE;
    /// Must run outside a render pass on the owning device's immediate graphics context.
    /// Submits the context's pending commands; active queries must be ended before calling.
    /// Returns False on failure; output must then be discarded. First execution resets history.
    /// The caller must submit pending work before destroying the object.
    VIRTUAL Bool METHOD(Execute)(THIS_ const ExecuteRayReconstructionAttribs REF Attribs) PURE;
};
DILIGENT_END_INTERFACE
#include "../../../Primitives/interface/UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IRayReconstruction_GetDesc(This) CALL_IFACE_METHOD(RayReconstruction, GetDesc, This)
#    define IRayReconstruction_GetJitterOffset(This, ...) CALL_IFACE_METHOD(RayReconstruction, GetJitterOffset, This, __VA_ARGS__)
#    define IRayReconstruction_Execute(This, ...) CALL_IFACE_METHOD(RayReconstruction, Execute, This, __VA_ARGS__)
#endif
// clang-format on
DILIGENT_END_NAMESPACE
