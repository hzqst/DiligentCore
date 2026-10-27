/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "RayReconstructionDLSS.hpp"
#include "EngineMemory.h"
#include <nvsdk_ngx_helpers_dlssd_d3d.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
namespace Diligent
{
using namespace RayReconstructionDetail;
namespace
{
NVSDK_NGX_PerfQuality_Value GetQuality(RAY_RECONSTRUCTION_QUALITY Quality)
{
    switch (Quality)
    {
        case RAY_RECONSTRUCTION_QUALITY_NATIVE: return NVSDK_NGX_PerfQuality_Value_DLAA;
        case RAY_RECONSTRUCTION_QUALITY_ULTRA_QUALITY: return NVSDK_NGX_PerfQuality_Value_UltraQuality;
        case RAY_RECONSTRUCTION_QUALITY_QUALITY: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case RAY_RECONSTRUCTION_QUALITY_BALANCED: return NVSDK_NGX_PerfQuality_Value_Balanced;
        case RAY_RECONSTRUCTION_QUALITY_PERFORMANCE: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case RAY_RECONSTRUCTION_QUALITY_ULTRA_PERFORMANCE: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        default: throw std::runtime_error{"Invalid ray reconstruction quality"};
    }
}
class DLSSProvider final : public RayReconstructionProvider
{
public:
    explicit DLSSProvider(std::shared_ptr<RayReconstructionDLSSBackend> Backend) :
        m_Backend{std::move(Backend)}
    {
        NGXLock    Lock;
        int        Available = 0;
        const auto Result    = NVSDK_NGX_Parameter_GetI(GetNGXCapabilities(m_Backend->GetRuntime()), NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &Available);
        m_Available          = NVSDK_NGX_SUCCEED(Result) && Available != 0;
        if (!m_Available)
            LOG_INFO_MESSAGE("NGX Ray Reconstruction is unavailable on this device/runtime");
    }
    void EnumerateVariants(std::vector<RayReconstructionInfo>& Variants) const override
    {
        if (!m_Available) return;
        RayReconstructionInfo Info{};
        Info.VariantId = RayReconstructionVariant_DLSS;
        std::snprintf(Info.Name, sizeof(Info.Name), "%s", "NGX: DLSS Ray Reconstruction");
        Variants.push_back(Info);
    }
    Bool GetSourceSettings(const RayReconstructionSourceSettingsAttribs& A, RayReconstructionSourceSettings& S) const override
    {
        NGXLock Lock;
        S = {};
        if (!m_Available || A.VariantId != RayReconstructionVariant_DLSS || !A.OutputWidth || !A.OutputHeight || A.Quality >= RAY_RECONSTRUCTION_QUALITY_COUNT) return False;
        float Sharpness = 0;
        auto  Result    = NGX_DLSSD_GET_OPTIMAL_SETTINGS(GetNGXCapabilities(m_Backend->GetRuntime()), A.OutputWidth, A.OutputHeight, GetQuality(A.Quality),
                                                     &S.OptimalInputWidth, &S.OptimalInputHeight, &S.MaxInputWidth, &S.MaxInputHeight, &S.MinInputWidth, &S.MinInputHeight, &Sharpness);
        if (NVSDK_NGX_FAILED(Result) || !S.OptimalInputWidth || !S.OptimalInputHeight)
        {
            S = {};
            return False;
        }
        return True;
    }
    void CreateRayReconstruction(const RayReconstructionDesc& Desc, IRayReconstruction** Output) override
    {
        if (!Output) return;
        *Output = nullptr;
        if (!m_Available || (!ValidateRayReconstructionDesc(Desc) || (Desc.DLSSPreset != RAY_RECONSTRUCTION_DLSS_PRESET_DEFAULT && Desc.DLSSPreset != RAY_RECONSTRUCTION_DLSS_PRESET_D && Desc.DLSSPreset != RAY_RECONSTRUCTION_DLSS_PRESET_E && Desc.DLSSPreset != RAY_RECONSTRUCTION_DLSS_PRESET_F))) return;
        try
        {
            auto* RR = NEW_RC_OBJ(GetRawAllocator(), "RayReconstructionDLSS", RayReconstructionDLSS)(m_Backend, Desc);
            RR->QueryInterface(IID_RayReconstruction, reinterpret_cast<IObject**>(Output));
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE(Error.what());
        }
    }

private:
    std::shared_ptr<RayReconstructionDLSSBackend> m_Backend;
    bool                                          m_Available = false;
};
} // namespace
std::unique_ptr<RayReconstructionProvider> CreateRRDLSSProvider(std::shared_ptr<RayReconstructionDLSSBackend> Backend)
{
    return Backend ? std::make_unique<DLSSProvider>(std::move(Backend)) : nullptr;
}
RayReconstructionDLSS::RayReconstructionDLSS(IReferenceCounters* Counters, std::shared_ptr<RayReconstructionDLSSBackend> Backend, const RayReconstructionDesc& Desc) :
    RayReconstructionBase{Counters, Backend->GetDevice(), Desc}, m_Backend{std::move(Backend)}
{
    m_Params = AllocateNGXParameters(m_Backend->GetRuntime());
    if (!m_Params)
        throw std::runtime_error{"Failed to allocate NGX RR parameters"};
}
RayReconstructionDLSS::~RayReconstructionDLSS()
{
    NGXLock Lock;
    if (m_Feature)
    {
        for (auto& Context : m_Contexts)
            Context->Flush();
        m_Device->IdleGPU();
        m_Backend->ReleaseFeature(m_Feature);
    }
    DestroyNGXParameters(m_Backend->GetRuntime(), m_Params);
}
Bool DILIGENT_CALL_TYPE RayReconstructionDLSS::Execute(const ExecuteRayReconstructionAttribs& A)
{
    NGXLock    Lock;
    const auto R = GetViews(A, m_Desc);
    if (!Validate(A, R) || !m_Backend->ValidateContext(A.pContext) ||
        std::any_of(R.begin(), R.end(), [&](ITextureView* View) { return View && !m_Backend->ValidateTexture(View->GetTexture()); }))
    {
        LOG_ERROR_MESSAGE("Invalid ray reconstruction execution attributes");
        return False;
    }
    if (std::none_of(m_Contexts.begin(), m_Contexts.end(), [&](const RefCntAutoPtr<IDeviceContext>& Context) { return Context == A.pContext; }))
        m_Contexts.emplace_back(A.pContext);
    m_Backend->PrepareContext(A.pContext);
    if (!TransitionResourceStates(A, R))
        return False;

    NVSDK_NGX_DLSSD_Create_Params Create{};
    Create.InWidth              = m_Desc.InputWidth;
    Create.InHeight             = m_Desc.InputHeight;
    Create.InTargetWidth        = m_Desc.OutputWidth;
    Create.InTargetHeight       = m_Desc.OutputHeight;
    Create.InPerfQualityValue   = GetQuality(m_Desc.Quality);
    Create.InDenoiseMode        = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
    Create.InRoughnessMode      = m_Desc.RoughnessPacked ? NVSDK_NGX_DLSS_Roughness_Mode_Packed : NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
    Create.InUseHWDepth         = m_Desc.LinearDepth ? NVSDK_NGX_DLSS_Depth_Type_Linear : NVSDK_NGX_DLSS_Depth_Type_HW;
    Create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (m_Desc.IsHDR) Create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if (m_Desc.AutoExposure) Create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if (m_Desc.DepthInverted) Create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (m_Desc.MotionVectorsJittered) Create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    if (!m_Feature)
    {
        for (const char* Key : {NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
                                NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
                                NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
                                NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
                                NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,
                                NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality})
            NVSDK_NGX_Parameter_SetUI(m_Params, Key, m_Desc.DLSSPreset);
    }
    // NGX uses the same row-major, row-vector convention as Diligent.
    float WorldToView[16], ViewToClip[16];
    std::copy(std::begin(A.WorldToView), std::end(A.WorldToView), WorldToView);
    std::copy(std::begin(A.ViewToClip), std::end(A.ViewToClip), ViewToClip);
    const auto Result = m_Backend->Evaluate(A, m_Desc, R, Create, m_Params, m_Feature, WorldToView, ViewToClip, m_FirstFrame);
    // Make NGX writes visible to subsequent consumers, including callers managing states themselves.
    StateTransitionDesc UAVBarrier{R[Output]->GetTexture(), RESOURCE_STATE_UNORDERED_ACCESS,
                                   RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_NONE};
    A.pContext->TransitionResourceStates(1, &UAVBarrier);
    // Vulkan InvalidateState resets the native command-buffer handle, so submit it first.
    A.pContext->Flush();
    A.pContext->InvalidateState();
    if (NVSDK_NGX_FAILED(Result))
    {
        m_FirstFrame = true;
        LOG_ERROR_MESSAGE("NGX Ray Reconstruction create/evaluate failed: ", static_cast<Uint32>(Result));
        return False;
    }
    m_FirstFrame = false;
    return True;
}
} // namespace Diligent
