/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once
#include "RayReconstructionFactory.h"
#include <memory>
#include <vector>
namespace Diligent
{
class RayReconstructionProvider
{
public:
    virtual ~RayReconstructionProvider()                                                                                                   = default;
    virtual void EnumerateVariants(std::vector<RayReconstructionInfo>& Variants) const                                                     = 0;
    virtual Bool GetSourceSettings(const RayReconstructionSourceSettingsAttribs& Attribs, RayReconstructionSourceSettings& Settings) const = 0;
    virtual void CreateRayReconstruction(const RayReconstructionDesc& Desc, IRayReconstruction** Output)                                   = 0;
};
std::unique_ptr<RayReconstructionProvider> CreateRRDLSSProviderD3D12(IRenderDevice* Device);
std::unique_ptr<RayReconstructionProvider> CreateRRDLSSProviderVk(IRenderDevice* Device);
std::unique_ptr<RayReconstructionProvider> CreateRRFSRProvider(IRenderDevice* Device);
} // namespace Diligent
