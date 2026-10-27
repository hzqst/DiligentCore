/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once
#include "ObjectBase.hpp"
#include "RayReconstruction.h"
#include "RenderDevice.h"
#include "RefCntAutoPtr.hpp"
#include <array>

namespace Diligent
{
namespace RayReconstructionDetail
{
enum ResourceSlot : size_t
{
    Color,
    Depth,
    Motion,
    DiffuseAlbedo,
    SpecularAlbedo,
    Normals,
    Roughness,
    Output,
    Exposure,
    DiffuseHitDistance,
    SpecularHitDistance,
    SpecularMotion,
    ResourceCount
};
using Views = std::array<ITextureView*, ResourceCount>;
Views GetViews(const ExecuteRayReconstructionAttribs& Attribs, const RayReconstructionDesc& Desc);
} // namespace RayReconstructionDetail
bool ValidateRayReconstructionDesc(const RayReconstructionDesc& Desc);

class RayReconstructionBase : public ObjectBase<IRayReconstruction>
{
public:
    using TBase = ObjectBase<IRayReconstruction>;
    RayReconstructionBase(IReferenceCounters* Counters, IRenderDevice* Device, const RayReconstructionDesc& Desc);
    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RayReconstruction, TBase)
    const RayReconstructionDesc& DILIGENT_CALL_TYPE GetDesc() const override final;
    void DILIGENT_CALL_TYPE                         GetJitterOffset(Uint32 Index, float& X, float& Y) const override final;

protected:
    bool                         Validate(const ExecuteRayReconstructionAttribs& Attribs, const RayReconstructionDetail::Views& Resources) const;
    bool                         TransitionResourceStates(const ExecuteRayReconstructionAttribs& Attribs, const RayReconstructionDetail::Views& Resources) const;
    RefCntAutoPtr<IRenderDevice> m_Device;
    const RayReconstructionDesc  m_Desc;
};
} // namespace Diligent
