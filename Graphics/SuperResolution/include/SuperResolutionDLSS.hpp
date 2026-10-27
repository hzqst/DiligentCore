/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

/// \file
/// Shared DLSS utilities used by per-API DLSS backend implementations.

#include <vector>
#include <algorithm>

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>

#include "SuperResolutionFactory.h"
#include "SuperResolutionProvider.hpp"
#include "SuperResolutionBase.hpp"
#include "NGXRuntime.hpp"

struct NVSDK_NGX_Parameter;

namespace Diligent
{

/// Maps Diligent optimization type to NGX performance/quality preset.
NVSDK_NGX_PerfQuality_Value OptimizationTypeToNGXPerfQuality(SUPER_RESOLUTION_OPTIMIZATION_TYPE Type);

/// Computes the full set of DLSS feature flags from the description and execution attributes.
Int32 ComputeDLSSFeatureFlags(SUPER_RESOLUTION_FLAGS Flags, const ExecuteSuperResolutionAttribs& Attribs);

class DLSSProviderBase : public SuperResolutionProvider
{
public:
    /// Populates DLSS variant info using NGX capability parameters.
    virtual void EnumerateVariants(std::vector<SuperResolutionInfo>& Variants) override final;

    /// Queries DLSS optimal source settings using NGX capability parameters.
    virtual void GetSourceSettings(const SuperResolutionSourceSettingsAttribs& Attribs, SuperResolutionSourceSettings& Settings) override final;

protected:
    RefCntAutoPtr<IRenderDevice> m_pDevice;
    std::shared_ptr<NGXRuntime>  m_Runtime;
    NVSDK_NGX_Parameter* m_pNGXParams = nullptr;
};

template <auto CreateFeature, auto ReleaseFeature>
class SuperResolutionDLSS : public SuperResolutionBase
{
public:
    SuperResolutionDLSS(IReferenceCounters*         pRefCounters,
                        const SuperResolutionDesc&  Desc,
                        const SuperResolutionInfo&  Info,
                        IRenderDevice*              pDevice,
                        std::shared_ptr<NGXRuntime> Runtime) :
        SuperResolutionBase{pRefCounters, Desc, Info},
        m_pDevice{pDevice},
        m_Runtime{std::move(Runtime)},
        m_pNGXParams{AllocateNGXParameters(*m_Runtime)}
    {
        if (!m_pNGXParams)
            LOG_ERROR_AND_THROW("Failed to allocate DLSS feature parameters");
        PopulateHaltonJitterPattern(m_JitterPattern, 64);
    }

    ~SuperResolutionDLSS()
    {
        NGXLock Lock;
        if (m_pDLSSFeature != nullptr)
        {
            for (auto& Context : m_Contexts)
                Context->Flush();
            m_pDevice->IdleGPU();
            ReleaseFeature(m_pDLSSFeature);
        }
        DestroyNGXParameters(*m_Runtime, m_pNGXParams);
    }

protected:
    NVSDK_NGX_Handle* AcquireFeature(const ExecuteSuperResolutionAttribs& Attribs)
    {
        if (std::none_of(m_Contexts.begin(), m_Contexts.end(), [&](const RefCntAutoPtr<IDeviceContext>& Context) { return Context == Attribs.pContext; }))
            m_Contexts.emplace_back(Attribs.pContext);
        const Int32 DLSSCreateFeatureFlags = ComputeDLSSFeatureFlags(m_Desc.Flags, Attribs);
        if (m_pDLSSFeature != nullptr && m_DLSSFeatureFlags == DLSSCreateFeatureFlags)
            return m_pDLSSFeature;

        if (m_pDLSSFeature != nullptr)
        {
            for (auto& Context : m_Contexts)
                Context->Flush();
            m_pDevice->IdleGPU();
            ReleaseFeature(m_pDLSSFeature);
            m_pDLSSFeature = nullptr;
        }
        m_DLSSFeatureFlags = DLSSCreateFeatureFlags;

        NVSDK_NGX_DLSS_Create_Params DLSSCreateParams{};
        DLSSCreateParams.Feature.InWidth        = m_Desc.InputWidth;
        DLSSCreateParams.Feature.InHeight       = m_Desc.InputHeight;
        DLSSCreateParams.Feature.InTargetWidth  = m_Desc.OutputWidth;
        DLSSCreateParams.Feature.InTargetHeight = m_Desc.OutputHeight;
        DLSSCreateParams.InFeatureCreateFlags   = DLSSCreateFeatureFlags;

        NVSDK_NGX_Handle* pFeature = nullptr;
        NVSDK_NGX_Result  Result   = CreateFeature(Attribs.pContext, m_pNGXParams, DLSSCreateParams, &pFeature);
        if (NVSDK_NGX_FAILED(Result))
        {
            LOG_ERROR_MESSAGE("Failed to create DLSS feature. NGX Result: ", static_cast<Uint32>(Result));
            return nullptr;
        }
        m_pDLSSFeature = pFeature;
        return m_pDLSSFeature;
    }

protected:
    RefCntAutoPtr<IRenderDevice> m_pDevice;
    std::shared_ptr<NGXRuntime>  m_Runtime;
    NVSDK_NGX_Parameter* const m_pNGXParams;

private:
    NVSDK_NGX_Handle* m_pDLSSFeature     = nullptr;
    Int32             m_DLSSFeatureFlags = 0;
    std::vector<RefCntAutoPtr<IDeviceContext>> m_Contexts;
};

} // namespace Diligent
