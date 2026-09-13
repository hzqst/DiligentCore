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

#include "SuperResolutionProvider.hpp"

#include "SuperResolutionDLSS.hpp"
#include "SuperResolutionBase.hpp"
#include "SuperResolutionVariants.hpp"

#include "../include/VulkanUtilities/VulkanHeaders.h"
#include "../include/VulkanTypeConversions.hpp"

#include <nvsdk_ngx_helpers_vk.h>

#include "RenderDeviceVk.h"
#include "DeviceContextVk.h"
#include "TextureVk.h"
#include "TextureViewVk.h"
#include "EngineMemory.h"

namespace Diligent
{

namespace
{

NVSDK_NGX_Result CreateDLSSFeatureVk(IDeviceContext*               pContext,
                                     NVSDK_NGX_Parameter*          pNGXParams,
                                     NVSDK_NGX_DLSS_Create_Params& DLSSCreateParams,
                                     NVSDK_NGX_Handle**            ppFeature)
{
    VkCommandBuffer vkCmdBuffer = ClassPtrCast<IDeviceContextVk>(pContext)->GetVkCommandBuffer();
    return NGX_VULKAN_CREATE_DLSS_EXT(vkCmdBuffer, 1, 1, ppFeature, pNGXParams, &DLSSCreateParams);
}

class SuperResolutionVk_DLSS final : public SuperResolutionDLSS<CreateDLSSFeatureVk, NVSDK_NGX_VULKAN_ReleaseFeature>
{
public:
    SuperResolutionVk_DLSS(IReferenceCounters*         pRefCounters,
                           const SuperResolutionDesc&  Desc,
                           const SuperResolutionInfo&  Info,
                           IRenderDevice*              pDevice,
                           std::shared_ptr<NGXRuntime> Runtime) :
        SuperResolutionDLSS{pRefCounters, Desc, Info, pDevice, std::move(Runtime)}
    {
    }

    virtual void DILIGENT_CALL_TYPE Execute(const ExecuteSuperResolutionAttribs& Attribs) override final
    {
        NGXLock Lock;
        ValidateExecuteSuperResolutionAttribs(m_Desc, m_Info, Attribs);

        NVSDK_NGX_Handle* pDLSSFeature = AcquireFeature(Attribs);
        if (pDLSSFeature == nullptr)
            return;

        TransitionResourceStates(Attribs);

        auto CreateNGXResourceVK = [](ITextureView* pView, VkImageAspectFlags AspectMask, bool bReadWrite) -> NVSDK_NGX_Resource_VK {
            ITextureVk*        pTexVk  = ClassPtrCast<ITextureVk>(pView->GetTexture());
            ITextureViewVk*    pViewVk = ClassPtrCast<ITextureViewVk>(pView);
            const TextureDesc& TexDesc = pTexVk->GetDesc();

            VkImageSubresourceRange SubresourceRange{};
            SubresourceRange.aspectMask     = AspectMask;
            SubresourceRange.baseMipLevel   = 0;
            SubresourceRange.levelCount     = 1;
            SubresourceRange.baseArrayLayer = 0;
            SubresourceRange.layerCount     = 1;

            return NVSDK_NGX_Create_ImageView_Resource_VK(pViewVk->GetVulkanImageView(), pTexVk->GetVkImage(), SubresourceRange, TexFormatToVkFormat(TexDesc.Format), TexDesc.Width, TexDesc.Height, bReadWrite);
        };

        NVSDK_NGX_Resource_VK ColorResource  = CreateNGXResourceVK(Attribs.pColorTextureSRV, VK_IMAGE_ASPECT_COLOR_BIT, false);
        NVSDK_NGX_Resource_VK OutputResource = CreateNGXResourceVK(Attribs.pOutputTextureView, VK_IMAGE_ASPECT_COLOR_BIT, true);
        const auto            DepthComponent = GetTextureFormatAttribs(Attribs.pDepthTextureSRV->GetTexture()->GetDesc().Format).ComponentType;
        const auto            DepthAspect    = DepthComponent == COMPONENT_TYPE_DEPTH || DepthComponent == COMPONENT_TYPE_DEPTH_STENCIL ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        NVSDK_NGX_Resource_VK DepthResource  = CreateNGXResourceVK(Attribs.pDepthTextureSRV, DepthAspect, false);
        NVSDK_NGX_Resource_VK MotionResource = CreateNGXResourceVK(Attribs.pMotionVectorsSRV, VK_IMAGE_ASPECT_COLOR_BIT, false);

        NVSDK_NGX_Resource_VK ExposureResource = {};
        if (Attribs.pExposureTextureSRV)
            ExposureResource = CreateNGXResourceVK(Attribs.pExposureTextureSRV, VK_IMAGE_ASPECT_COLOR_BIT, false);

        NVSDK_NGX_Resource_VK TransparencyMaskResource = {};
        if (Attribs.pReactiveMaskTextureSRV)
            TransparencyMaskResource = CreateNGXResourceVK(Attribs.pReactiveMaskTextureSRV, VK_IMAGE_ASPECT_COLOR_BIT, false);

        NVSDK_NGX_Resource_VK BiasCurrentColorMaskResource = {};
        if (Attribs.pIgnoreHistoryMaskTextureSRV)
            BiasCurrentColorMaskResource = CreateNGXResourceVK(Attribs.pIgnoreHistoryMaskTextureSRV, VK_IMAGE_ASPECT_COLOR_BIT, false);

        NVSDK_NGX_VK_DLSS_Eval_Params EvalParams{};
        EvalParams.Feature.pInColor                 = &ColorResource;
        EvalParams.Feature.pInOutput                = &OutputResource;
        EvalParams.pInDepth                         = &DepthResource;
        EvalParams.pInMotionVectors                 = &MotionResource;
        EvalParams.pInExposureTexture               = Attribs.pExposureTextureSRV ? &ExposureResource : nullptr;
        EvalParams.pInTransparencyMask              = Attribs.pReactiveMaskTextureSRV ? &TransparencyMaskResource : nullptr;
        EvalParams.pInBiasCurrentColorMask          = Attribs.pIgnoreHistoryMaskTextureSRV ? &BiasCurrentColorMaskResource : nullptr;
        EvalParams.Feature.InSharpness              = Attribs.Sharpness;
        EvalParams.InJitterOffsetX                  = Attribs.JitterX;
        EvalParams.InJitterOffsetY                  = Attribs.JitterY;
        EvalParams.InReset                          = Attribs.ResetHistory ? 1 : 0;
        EvalParams.InMVScaleX                       = Attribs.MotionVectorScaleX;
        EvalParams.InMVScaleY                       = Attribs.MotionVectorScaleY;
        EvalParams.InRenderSubrectDimensions.Width  = m_Desc.InputWidth;
        EvalParams.InRenderSubrectDimensions.Height = m_Desc.InputHeight;
        EvalParams.InPreExposure                    = Attribs.PreExposure;
        EvalParams.InExposureScale                  = Attribs.ExposureScale;

        IDeviceContextVk* pCtxImpl    = ClassPtrCast<IDeviceContextVk>(Attribs.pContext);
        VkCommandBuffer   vkCmdBuffer = pCtxImpl->GetVkCommandBuffer();

        NVSDK_NGX_Result Result = NGX_VULKAN_EVALUATE_DLSS_EXT(vkCmdBuffer, pDLSSFeature, m_pNGXParams, &EvalParams);
        if (NVSDK_NGX_FAILED(Result))
            LOG_ERROR_MESSAGE("DLSS Vulkan evaluation failed. NGX Result: ", static_cast<Uint32>(Result));
    }
};


class DLSSProviderVk final : public DLSSProviderBase
{
public:
    DLSSProviderVk(IRenderDevice* pDevice) :
        m_pDevice{pDevice, IID_RenderDeviceVk}
    {
        DLSSProviderBase::m_pDevice = pDevice;
        if (!m_pDevice)
        {
            if (pDevice == nullptr)
                LOG_ERROR_AND_THROW("Device must not be null");
            else
                LOG_ERROR_AND_THROW("Device must be of type RENDER_DEVICE_TYPE_VULKAN");
        }

        VkInstance       vkInstance   = m_pDevice->GetVkInstance();
        VkPhysicalDevice vkPhysDevice = m_pDevice->GetVkPhysicalDevice();
        VkDevice         vkDevice     = m_pDevice->GetVkDevice();

        m_Runtime    = AcquireNGXRuntime({NGXBackend::Vulkan, vkDevice, vkInstance, vkPhysDevice});
        m_pNGXParams = m_Runtime ? GetNGXCapabilities(*m_Runtime) : nullptr;
    }

    void CreateSuperResolution(const SuperResolutionDesc& Desc, const SuperResolutionInfo& Info, ISuperResolution** ppUpscaler)
    {
        DEV_CHECK_ERR(ppUpscaler != nullptr, "ppUpscaler must not be null");

        SuperResolutionVk_DLSS* pUpscaler = NEW_RC_OBJ(GetRawAllocator(), "SuperResolutionVk_DLSS instance", SuperResolutionVk_DLSS)(Desc, Info, m_pDevice, m_Runtime);
        pUpscaler->QueryInterface(IID_SuperResolution, reinterpret_cast<IObject**>(ppUpscaler));
    }

private:
    RefCntAutoPtr<IRenderDeviceVk> m_pDevice;
};

} // anonymous namespace

std::unique_ptr<SuperResolutionProvider> CreateDLSSProviderVk(IRenderDevice* pDevice)
{
    return pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_VULKAN ?
        std::make_unique<DLSSProviderVk>(pDevice) :
        nullptr;
}

} // namespace Diligent
