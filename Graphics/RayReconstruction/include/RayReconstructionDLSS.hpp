/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once
#include "RayReconstructionBase.hpp"
#include "RayReconstructionProvider.hpp"
#include "NGXRuntime.hpp"
#include <nvsdk_ngx_params_dlssd.h>
#include <vector>
namespace Diligent
{
class RayReconstructionDLSSBackend
{
public:
    RayReconstructionDLSSBackend(IRenderDevice* Device, std::shared_ptr<NGXRuntime> Runtime) :
        m_Device{Device}, m_Runtime{std::move(Runtime)} {}
    virtual ~RayReconstructionDLSSBackend()                                 = default;
    virtual bool             ValidateContext(IDeviceContext* Context) const = 0;
    virtual bool             ValidateTexture(ITexture* Texture) const       = 0;
    virtual void             PrepareContext(IDeviceContext*) const {}
    virtual NVSDK_NGX_Result Evaluate(const ExecuteRayReconstructionAttribs& Attribs,
                                      const RayReconstructionDesc&           Desc,
                                      const RayReconstructionDetail::Views&  Resources,
                                      NVSDK_NGX_DLSSD_Create_Params&         Create,
                                      NVSDK_NGX_Parameter*                   Params,
                                      NVSDK_NGX_Handle*&                     Feature,
                                      float*                                 WorldToView,
                                      float*                                 ViewToClip,
                                      bool                                   FirstFrame)                 = 0;
    virtual void             ReleaseFeature(NVSDK_NGX_Handle* Feature) = 0;
    IRenderDevice*           GetDevice() const { return m_Device; }
    NGXRuntime&              GetRuntime() const { return *m_Runtime; }

protected:
    // Keep the native device alive until the runtime has shut down.
    RefCntAutoPtr<IRenderDevice> m_Device;
    std::shared_ptr<NGXRuntime>  m_Runtime;
};

class RayReconstructionDLSS final : public RayReconstructionBase
{
public:
    RayReconstructionDLSS(IReferenceCounters* Counters, std::shared_ptr<RayReconstructionDLSSBackend> Backend, const RayReconstructionDesc& Desc);
    ~RayReconstructionDLSS();
    Bool DILIGENT_CALL_TYPE Execute(const ExecuteRayReconstructionAttribs& Attribs) override;

private:
    std::shared_ptr<RayReconstructionDLSSBackend> m_Backend;
    NVSDK_NGX_Parameter*                          m_Params     = nullptr;
    NVSDK_NGX_Handle*                             m_Feature    = nullptr;
    bool                                          m_FirstFrame = true;
    std::vector<RefCntAutoPtr<IDeviceContext>>    m_Contexts;
};

std::unique_ptr<RayReconstructionProvider> CreateRRDLSSProvider(std::shared_ptr<RayReconstructionDLSSBackend> Backend);
namespace RayReconstructionDetail
{
template <typename EvalType, typename ResourceType>
void FillEvaluation(EvalType& E, const std::array<ResourceType, ResourceCount>& R, const ExecuteRayReconstructionAttribs& A, const RayReconstructionDesc& D, float* WorldToView, float* ViewToClip, bool FirstFrame)
{
    E.pInColor                    = R[Color];
    E.pInDepth                    = R[Depth];
    E.pInMotionVectors            = R[Motion];
    E.pInDiffuseAlbedo            = R[DiffuseAlbedo];
    E.pInSpecularAlbedo           = R[SpecularAlbedo];
    E.pInNormals                  = R[Normals];
    E.pInRoughness                = R[Roughness];
    E.pInOutput                   = R[Output];
    E.pInExposureTexture          = R[Exposure];
    E.pInDiffuseHitDistance       = R[DiffuseHitDistance];
    E.pInSpecularHitDistance      = R[SpecularHitDistance];
    E.pInMotionVectorsReflections = R[SpecularMotion];
    E.InJitterOffsetX             = A.JitterX;
    E.InJitterOffsetY             = A.JitterY;
    E.InMVScaleX                  = A.MotionVectorScaleX;
    E.InMVScaleY                  = A.MotionVectorScaleY;
    E.InReset                     = FirstFrame || A.ResetHistory;
    E.InPreExposure               = A.PreExposure;
    E.InExposureScale             = A.ExposureScale;
    E.InRenderSubrectDimensions   = {D.InputWidth, D.InputHeight};
    E.pInWorldToViewMatrix        = WorldToView;
    E.pInViewToClipMatrix         = ViewToClip;
}
} // namespace RayReconstructionDetail
} // namespace Diligent
