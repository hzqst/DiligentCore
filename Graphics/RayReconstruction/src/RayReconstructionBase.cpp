/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "RayReconstructionBase.hpp"
#include "GraphicsAccessories.hpp"
#include <cmath>
namespace Diligent
{
using namespace RayReconstructionDetail;
namespace RayReconstructionDetail
{
Views GetViews(const ExecuteRayReconstructionAttribs& A, const RayReconstructionDesc& D)
{
    return {{A.pColorSRV, A.pDepthSRV, A.pMotionVectorsSRV, A.pDiffuseAlbedoSRV,
             A.pSpecularAlbedoSRV, A.pNormalsSRV, D.RoughnessPacked ? nullptr : A.pRoughnessSRV,
             A.pOutputUAV, D.AutoExposure ? nullptr : A.pExposureSRV,
             A.pDiffuseHitDistanceSRV, A.pSpecularHitDistanceSRV, A.pSpecularMotionVectorsSRV}};
}
} // namespace RayReconstructionDetail
bool ValidateRayReconstructionDesc(const RayReconstructionDesc& D)
{
    return D.InputWidth && D.InputHeight &&
        D.InputWidth <= D.OutputWidth && D.InputHeight <= D.OutputHeight &&
        D.Quality < RAY_RECONSTRUCTION_QUALITY_COUNT &&
        !(D.LinearDepth && D.DepthInverted) &&
        (D.Quality != RAY_RECONSTRUCTION_QUALITY_NATIVE || (D.InputWidth == D.OutputWidth && D.InputHeight == D.OutputHeight));
}
RayReconstructionBase::RayReconstructionBase(IReferenceCounters* Counters, IRenderDevice* Device, const RayReconstructionDesc& Desc) :
    TBase{Counters}, m_Device{Device}, m_Desc{Desc}
{}
const RayReconstructionDesc& DILIGENT_CALL_TYPE RayReconstructionBase::GetDesc() const { return m_Desc; }
void DILIGENT_CALL_TYPE                         RayReconstructionBase::GetJitterOffset(Uint32 Index, float& X, float& Y) const
{
    constexpr Uint32 JitterPeriod = 64;
    const auto       Halton       = [](Uint32 I, Uint32 Base) {
        float Result = 0, Fraction = 1;
        for (; I; I /= Base)
        {
            Fraction /= static_cast<float>(Base);
            Result += Fraction * static_cast<float>(I % Base);
        }
        return Result - 0.5f;
    };
    X = Halton(Index % JitterPeriod + 1, 2);
    Y = Halton(Index % JitterPeriod + 1, 3);
}
bool RayReconstructionBase::Validate(const ExecuteRayReconstructionAttribs& A, const Views& R) const
{
    if (!A.pContext || A.pContext->GetDesc().IsDeferred ||
        !(A.pContext->GetDesc().QueueType & COMMAND_QUEUE_TYPE_GRAPHICS) ||
        A.StateTransitionMode > RESOURCE_STATE_TRANSITION_MODE_VERIFY)
        return false;


    for (float Value : {A.JitterX, A.JitterY, A.MotionVectorScaleX, A.MotionVectorScaleY, A.PreExposure, A.ExposureScale})
        if (!std::isfinite(Value)) return false;
    if (A.PreExposure <= 0 || A.ExposureScale <= 0 || A.MotionVectorScaleX == 0 || A.MotionVectorScaleY == 0)
        return false;
    for (float Value : A.WorldToView)
        if (!std::isfinite(Value)) return false;
    for (float Value : A.ViewToClip)
        if (!std::isfinite(Value)) return false;
    for (size_t i = 0; i < R.size(); ++i)
    {
        if (!R[i])
        {
            if (i <= Output && !(i == Roughness && m_Desc.RoughnessPacked)) return false;
            continue;
        }
        auto*       Texture = R[i]->GetTexture();
        const auto& T       = Texture->GetDesc();
        const auto& V       = R[i]->GetDesc();
        if (T.Type != RESOURCE_DIM_TEX_2D || T.SampleCount != 1 || T.MipLevels != 1 ||
            V.MostDetailedMip != 0 || V.FirstArraySlice != 0 || V.NumMipLevels != 1 ||
            V.ViewType != (i == Output ? TEXTURE_VIEW_UNORDERED_ACCESS : TEXTURE_VIEW_SHADER_RESOURCE)) return false;
        const Uint32 W = i == Output ? m_Desc.OutputWidth : i == Exposure ? 1 : m_Desc.InputWidth;
        const Uint32 H = i == Output ? m_Desc.OutputHeight : i == Exposure ? 1 : m_Desc.InputHeight;
        if (T.Width != W || T.Height != H) return false;
        if (i != Output && R[Output] && Texture == R[Output]->GetTexture()) return false;
        const auto& F = GetTextureFormatAttribs(V.Format);
        if (F.IsTypeless || (F.ComponentType != COMPONENT_TYPE_FLOAT && F.ComponentType != COMPONENT_TYPE_UNORM && F.ComponentType != COMPONENT_TYPE_DEPTH)) return false;
        if ((i == Color || i == Output || i == DiffuseAlbedo || i == SpecularAlbedo) && F.NumComponents < 3) return false;
        if ((i == Color || i == Output) && m_Desc.IsHDR && F.ComponentType != COMPONENT_TYPE_FLOAT) return false;
        if (i == Depth && V.Format != TEX_FORMAT_R32_FLOAT && V.Format != TEX_FORMAT_D32_FLOAT) return false;
        if ((i == Exposure || i == DiffuseHitDistance || i == SpecularHitDistance) &&
            V.Format != TEX_FORMAT_R16_FLOAT && V.Format != TEX_FORMAT_R32_FLOAT) return false;
        if (i == Roughness && F.NumComponents != 1) return false;
        if (i == Normals && (F.ComponentType != COMPONENT_TYPE_FLOAT || F.NumComponents < (m_Desc.RoughnessPacked ? 4 : 3))) return false;
        if ((i == Motion || i == SpecularMotion) && (F.ComponentType != COMPONENT_TYPE_FLOAT || F.NumComponents < 2)) return false;
    }
    return true;
}
bool RayReconstructionBase::TransitionResourceStates(const ExecuteRayReconstructionAttribs& A, const Views& R) const
{
    std::array<StateTransitionDesc, ResourceCount> Barriers;
    Uint32                                         Count = 0;
    for (size_t i = 0; i < R.size(); ++i)
    {
        if (!R[i])
            continue;
        const auto State = i == Output ? RESOURCE_STATE_UNORDERED_ACCESS : RESOURCE_STATE_SHADER_RESOURCE;
        if (A.StateTransitionMode == RESOURCE_STATE_TRANSITION_MODE_TRANSITION)
            Barriers[Count++] = StateTransitionDesc{R[i]->GetTexture(), RESOURCE_STATE_UNKNOWN, State, STATE_TRANSITION_FLAG_UPDATE_STATE};
        else if (A.StateTransitionMode == RESOURCE_STATE_TRANSITION_MODE_VERIFY &&
                 (R[i]->GetTexture()->GetState() & State) != State)
        {
            LOG_ERROR_MESSAGE("Ray reconstruction texture is not in the required resource state");
            return False;
        }
    }
    if (Count)
        A.pContext->TransitionResourceStates(Count, Barriers.data());
    return true;
}
} // namespace Diligent
