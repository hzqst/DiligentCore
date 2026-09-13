/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "RayReconstructionDLSS.hpp"
#include "VulkanUtilities/VulkanHeaders.h"
#include "RenderDeviceVkImpl.hpp"
#include "DeviceContextVkImpl.hpp"
#include "TextureVkImpl.hpp"
#include "TextureViewVk.h"
#include "VulkanTypeConversions.hpp"
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_helpers_dlssd_vk.h>
#include "NGXVulkan.hpp"
#include "GraphicsAccessories.hpp"
namespace Diligent
{
using namespace RayReconstructionDetail;
namespace
{
class DLSSBackendVk final : public RayReconstructionDLSSBackend
{
public:
    using RayReconstructionDLSSBackend::RayReconstructionDLSSBackend;
    bool ValidateContext(IDeviceContext* Context) const override
    {
        RefCntAutoPtr<IDeviceContextVk> Native{Context, IID_DeviceContextVk};
        if (!Native) return false;
        auto* Impl = ClassPtrCast<DeviceContextVkImpl>(Native.RawPtr());
        return Impl->GetDevice() == m_Device && !Impl->HasActiveRenderPass();
    }
    bool ValidateTexture(ITexture* Texture) const override
    {
        RefCntAutoPtr<ITextureVk> Native{Texture, IID_TextureVk};
        return Native && ClassPtrCast<TextureVkImpl>(Native.RawPtr())->GetDevice() == m_Device;
    }
    void PrepareContext(IDeviceContext* Context) const override
    {
        ClassPtrCast<DeviceContextVkImpl>(Context)->GetCommandBuffer().EndRenderScope();
    }
    void ReleaseFeature(NVSDK_NGX_Handle* Feature) override
    {
        NVSDK_NGX_VULKAN_ReleaseFeature(Feature);
    }
    NVSDK_NGX_Result Evaluate(const ExecuteRayReconstructionAttribs& A, const RayReconstructionDesc& Desc, const Views& R, NVSDK_NGX_DLSSD_Create_Params& Create, NVSDK_NGX_Parameter* Params, NVSDK_NGX_Handle*& Feature, float* WorldToView, float* ViewToClip, bool FirstFrame) override
    {
        NVSDK_NGX_Result Result = NVSDK_NGX_Result_Fail;
        auto*            Device = ClassPtrCast<IRenderDeviceVk>(m_Device.RawPtr());
        auto             Cmd    = ClassPtrCast<IDeviceContextVk>(A.pContext)->GetVkCommandBuffer();
        if (!Feature)
        {
            Result = NGX_VULKAN_CREATE_DLSSD_EXT1(Device->GetVkDevice(), Cmd, 1, 1, &Feature, Params, &Create);
            A.pContext->Flush();
            A.pContext->InvalidateState();
            Cmd = ClassPtrCast<IDeviceContextVk>(A.pContext)->GetVkCommandBuffer();
        }
        if (Feature)
        {
            std::array<NVSDK_NGX_Resource_VK, ResourceCount>  Resources{};
            std::array<NVSDK_NGX_Resource_VK*, ResourceCount> Native{};
            for (size_t i = 0; i < R.size(); ++i)
            {
                if (!R[i]) continue;
                auto*                   Tex       = ClassPtrCast<ITextureVk>(R[i]->GetTexture());
                const auto&             TD        = Tex->GetDesc();
                const auto&             VD        = R[i]->GetDesc();
                const auto              Component = GetTextureFormatAttribs(TD.Format).ComponentType;
                const bool              IsDepth   = Component == COMPONENT_TYPE_DEPTH || Component == COMPONENT_TYPE_DEPTH_STENCIL;
                VkImageSubresourceRange Range{};
                Range.aspectMask = IsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                Range.levelCount = Range.layerCount = 1;
                Resources[i]                        = NVSDK_NGX_Create_ImageView_Resource_VK(ClassPtrCast<ITextureViewVk>(R[i])->GetVulkanImageView(),
                                                                      Tex->GetVkImage(), Range, TexFormatToVkFormat(VD.Format), TD.Width, TD.Height, i == Output);
                Native[i]                           = &Resources[i];
            }
            NVSDK_NGX_VK_DLSSD_Eval_Params E{};
            FillEvaluation(E, Native, A, Desc, WorldToView, ViewToClip, FirstFrame);
            Result = NGX_VULKAN_EVALUATE_DLSSD_EXT(Cmd, Feature, Params, &E);
        }
        return Result;
    }
};
} // namespace
std::unique_ptr<RayReconstructionProvider> CreateRRDLSSProviderVk(IRenderDevice* Device)
{
    if (Device->GetDeviceInfo().Type != RENDER_DEVICE_TYPE_VULKAN) return nullptr;
    NGXLock                  Lock;
    auto*                    D = ClassPtrCast<RenderDeviceVkImpl>(Device);
    std::vector<std::string> InstanceExtensions, DeviceExtensions;
    if (!GetNGXVulkanExtensions(D->GetVkInstance(), D->GetVkPhysicalDevice(), InstanceExtensions, DeviceExtensions)) return nullptr;
    for (const auto& Ext : InstanceExtensions)
        if (!D->GetInstance()->IsExtensionEnabled(Ext.c_str())) return nullptr;
    for (const auto& Ext : DeviceExtensions)
        if (!D->GetLogicalDevice().IsExtensionEnabled(Ext.c_str())) return nullptr;
    auto Runtime = AcquireNGXRuntime({NGXBackend::Vulkan, D->GetVkDevice(), D->GetVkInstance(), D->GetVkPhysicalDevice()});
    if (!Runtime) return nullptr;
    return CreateRRDLSSProvider(std::make_shared<DLSSBackendVk>(Device, std::move(Runtime)));
}
} // namespace Diligent
