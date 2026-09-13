/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "RayReconstructionDLSS.hpp"
#include <nvsdk_ngx_helpers_dlssd_d3d.h>
#include "WinHPreface.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <atlbase.h>
#include "WinHPostface.h"
#include "RenderDeviceD3D12.h"
#include "DeviceContextD3D12Impl.hpp"
#include "TextureD3D12Impl.hpp"
#include "GraphicsAccessories.hpp"
namespace Diligent
{
using namespace RayReconstructionDetail;
namespace
{
class DLSSBackendD3D12 final : public RayReconstructionDLSSBackend
{
public:
    using RayReconstructionDLSSBackend::RayReconstructionDLSSBackend;
    bool ValidateContext(IDeviceContext* Context) const override
    {
        RefCntAutoPtr<IDeviceContextD3D12> Native{Context, IID_DeviceContextD3D12};
        if (!Native) return false;
        auto* Impl = ClassPtrCast<DeviceContextD3D12Impl>(Native.RawPtr());
        return Impl->GetDevice() == m_Device && !Impl->HasActiveRenderPass();
    }
    bool ValidateTexture(ITexture* Texture) const override
    {
        RefCntAutoPtr<ITextureD3D12> Native{Texture, IID_TextureD3D12};
        return Native && ClassPtrCast<TextureD3D12Impl>(Native.RawPtr())->GetDevice() == m_Device;
    }

    void ReleaseFeature(NVSDK_NGX_Handle* Feature) override
    {
        NVSDK_NGX_D3D12_ReleaseFeature(Feature);
    }
    NVSDK_NGX_Result Evaluate(const ExecuteRayReconstructionAttribs& A, const RayReconstructionDesc& Desc, const Views& R, NVSDK_NGX_DLSSD_Create_Params& Create, NVSDK_NGX_Parameter* Params, NVSDK_NGX_Handle*& Feature, float* WorldToView, float* ViewToClip, bool FirstFrame) override
    {
        NVSDK_NGX_Result Result = NVSDK_NGX_Result_Fail;
        auto*            Cmd    = ClassPtrCast<IDeviceContextD3D12>(A.pContext)->GetD3D12CommandList();
        if (!Feature)
            Result = NGX_D3D12_CREATE_DLSSD_EXT(Cmd, 1, 1, &Feature, Params, &Create);
        if (Feature)
        {
            std::array<ID3D12Resource*, ResourceCount> Native{};
            for (size_t i = 0; i < R.size(); ++i)
                if (R[i]) Native[i] = ClassPtrCast<ITextureD3D12>(R[i]->GetTexture())->GetD3D12Texture();
            NVSDK_NGX_D3D12_DLSSD_Eval_Params E{};
            FillEvaluation(E, Native, A, Desc, WorldToView, ViewToClip, FirstFrame);
            Result = NGX_D3D12_EVALUATE_DLSSD_EXT(Cmd, Feature, Params, &E);
        }
        return Result;
    }
};
} // namespace
std::unique_ptr<RayReconstructionProvider> CreateRRDLSSProviderD3D12(IRenderDevice* Device)
{
    if (Device->GetDeviceInfo().Type != RENDER_DEVICE_TYPE_D3D12) return nullptr;
    NGXLock Lock;
    auto    Runtime = AcquireNGXRuntime({NGXBackend::D3D12, ClassPtrCast<IRenderDeviceD3D12>(Device)->GetD3D12Device()});
    if (!Runtime) return nullptr;
    return CreateRRDLSSProvider(std::make_shared<DLSSBackendD3D12>(Device, std::move(Runtime)));
}
} // namespace Diligent
