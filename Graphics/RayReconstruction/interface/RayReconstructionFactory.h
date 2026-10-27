/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once
#include "RayReconstruction.h"
#include "../../GraphicsEngine/interface/RenderDevice.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RayReconstructionFactory =
    {0x2f938802, 0x5779, 0x4290, {0x81, 0xdb, 0x95, 0x74, 0xa8, 0x2c, 0x13, 0x65}};

// clang-format off
struct RayReconstructionInfo
{
    INTERFACE_ID VariantId DEFAULT_INITIALIZER({});
    Char Name[64] DEFAULT_INITIALIZER({});
};
typedef struct RayReconstructionInfo RayReconstructionInfo;
struct RayReconstructionSourceSettingsAttribs
{
    INTERFACE_ID VariantId DEFAULT_INITIALIZER(RayReconstructionVariant_DLSS);
    Uint32 OutputWidth DEFAULT_INITIALIZER(0);
    Uint32 OutputHeight DEFAULT_INITIALIZER(0);
    RAY_RECONSTRUCTION_QUALITY Quality DEFAULT_INITIALIZER(RAY_RECONSTRUCTION_QUALITY_QUALITY);
};
typedef struct RayReconstructionSourceSettingsAttribs RayReconstructionSourceSettingsAttribs;
struct RayReconstructionSourceSettings
{
    Uint32 OptimalInputWidth DEFAULT_INITIALIZER(0);
    Uint32 OptimalInputHeight DEFAULT_INITIALIZER(0);
    Uint32 MinInputWidth DEFAULT_INITIALIZER(0);
    Uint32 MinInputHeight DEFAULT_INITIALIZER(0);
    Uint32 MaxInputWidth DEFAULT_INITIALIZER(0);
    Uint32 MaxInputHeight DEFAULT_INITIALIZER(0);
};
typedef struct RayReconstructionSourceSettings RayReconstructionSourceSettings;

#define DILIGENT_INTERFACE_NAME IRayReconstructionFactory
#include "../../../Primitives/interface/DefineInterfaceHelperMacros.h"
#define IRayReconstructionFactoryInclusiveMethods \
    IObjectInclusiveMethods; \
    IRayReconstructionFactoryMethods RayReconstructionFactory
DILIGENT_BEGIN_INTERFACE(IRayReconstructionFactory, IObject)
{
    /// Null Variants queries count; otherwise NumVariants is capacity on entry and written count on return.
    VIRTUAL void METHOD(EnumerateVariants)(THIS_ Uint32 REF NumVariants, RayReconstructionInfo* Variants) CONST PURE;
    /// Returns False and zero settings if unsupported; never silently changes quality mode.
    VIRTUAL Bool METHOD(GetSourceSettings)(THIS_ const RayReconstructionSourceSettingsAttribs REF Attribs,
                                         RayReconstructionSourceSettings REF Settings) CONST PURE;
    /// Returns null on failure. The created object may outlive its factory.
    VIRTUAL void METHOD(CreateRayReconstruction)(THIS_ const RayReconstructionDesc REF Desc,
                                               IRayReconstruction** ppReconstruction) PURE;
};
DILIGENT_END_INTERFACE
#include "../../../Primitives/interface/UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IRayReconstructionFactory_EnumerateVariants(This, ...) CALL_IFACE_METHOD(RayReconstructionFactory, EnumerateVariants, This, __VA_ARGS__)
#    define IRayReconstructionFactory_GetSourceSettings(This, ...) CALL_IFACE_METHOD(RayReconstructionFactory, GetSourceSettings, This, __VA_ARGS__)
#    define IRayReconstructionFactory_CreateRayReconstruction(This, ...) CALL_IFACE_METHOD(RayReconstructionFactory, CreateRayReconstruction, This, __VA_ARGS__)
#endif
// clang-format on
DILIGENT_END_NAMESPACE
