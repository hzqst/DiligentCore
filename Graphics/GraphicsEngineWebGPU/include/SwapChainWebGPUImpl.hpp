/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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
/// Declaration of Diligent::SwapChainWebGPUImpl class
#include <memory>

#include "SwapChainBase.hpp"
#include "SwapChainWebGPU.h"
#include "EngineWebGPUImplTraits.hpp"
#include "WebGPUObjectWrappers.hpp"

namespace Diligent
{

class WebGPUSwapChainPresentCommand;

/// Swap chain implementation in WebGPU backend.
class SwapChainWebGPUImpl final : public SwapChainBase<ISwapChainWebGPU>
{
public:
    using TSwapChainBase = SwapChainBase;

    SwapChainWebGPUImpl(IReferenceCounters*      pRefCounters,
                        const SwapChainDesc&     SCDesc,
                        RenderDeviceWebGPUImpl*  pDevice,
                        DeviceContextWebGPUImpl* pDeviceContext,
                        const NativeWindow&      Window);

    ~SwapChainWebGPUImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_SwapChainWebGPU, TSwapChainBase)

    /// Implementation of ISwapChain::Present() in Direct3D11 backend.
    void DILIGENT_CALL_TYPE Present(Uint32 SyncInterval) override;

    /// Implementation of ISwapChain::Resize() in Direct3D11 backend.
    void DILIGENT_CALL_TYPE Resize(Uint32 NewWidth, Uint32 NewHeight, SURFACE_TRANSFORM NewPreTransform) override;

    /// Implementation of ISwapChain::SetFullscreenMode() in WebGPU backend.
    void DILIGENT_CALL_TYPE SetFullscreenMode(const DisplayModeAttribs& DisplayMode) override;

    /// Implementation of ISwapChain::SetWindowedMode() in WebGPU backend.
    void DILIGENT_CALL_TYPE SetWindowedMode() override;

    /// Implementation of ISwapChainWebGPU::GetCurrentBackBufferRTV() in WebGPU backend.
    ITextureViewWebGPU* DILIGENT_CALL_TYPE GetCurrentBackBufferRTV() override { return m_pBackBufferRTV; }

    /// Implementation of ISwapChainWebGPU::GetDepthBufferDSV() in WebGPU backend.
    ITextureViewWebGPU* DILIGENT_CALL_TYPE GetDepthBufferDSV() override { return m_pDepthBufferDSV; }

    /// Implementation of ISwapChainWebGPU::GetWebGPUSurface() in WebGPU backend.
    WGPUSurface DILIGENT_CALL_TYPE GetWebGPUSurface() override { return m_wgpuSurface.Get(); }

private:
    void CreateSurface();

    void ConfigureSurface();

    void CreateBuffersAndViews();

    void ReleaseSwapChainResources();

    void RecreateSwapChain();

    NativeWindow                                   m_NativeWindow;
    WebGPUSurfaceWrapper                           m_wgpuSurface;
    RefCntAutoPtr<ITextureViewWebGPU>              m_pBackBufferRTV;
    RefCntAutoPtr<ITextureViewWebGPU>              m_pBackBufferSRV;
    RefCntAutoPtr<ITextureViewWebGPU>              m_pDepthBufferDSV;
    std::unique_ptr<WebGPUSwapChainPresentCommand> m_pCmdPresent;
    bool                                           m_VSyncEnabled = true;
};

} // namespace Diligent
