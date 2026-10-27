/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include <vulkan/vulkan.h>
#include "WinHPreface.h"
#include <Windows.h>
#include "WinHPostface.h"
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include "NGXRuntime.hpp"
#include "NGXVulkan.hpp"
#include "DebugUtilities.hpp"
#include <map>
#include <mutex>
#include <utility>

namespace Diligent
{
namespace
{
std::recursive_mutex  Mutex;
constexpr const char* ProjectId = "750fed3a-efba-42ba-801b-22d4cbad9148";
using Key                       = std::pair<NGXBackend, void*>;
} // namespace

class NGXRuntime
{
public:
    NGXDevice            Native;
    NVSDK_NGX_Parameter* Capabilities = nullptr;
    unsigned             Owners       = 0;
};

namespace
{
std::map<Key, std::unique_ptr<NGXRuntime>> Runtimes;

void Shutdown(NGXRuntime& Runtime)
{
    DestroyNGXParameters(Runtime, Runtime.Capabilities);
    switch (Runtime.Native.Backend)
    {
        case NGXBackend::D3D11: NVSDK_NGX_D3D11_Shutdown1(static_cast<ID3D11Device*>(Runtime.Native.Device)); break;
        case NGXBackend::D3D12: NVSDK_NGX_D3D12_Shutdown1(static_cast<ID3D12Device*>(Runtime.Native.Device)); break;
        case NGXBackend::Vulkan: NVSDK_NGX_VULKAN_Shutdown1(static_cast<VkDevice>(Runtime.Native.Device)); break;
    }
}
} // namespace

void LockNGX() { Mutex.lock(); }
void UnlockNGX() { Mutex.unlock(); }

bool GetNGXVulkanExtensions(VkInstance Instance, VkPhysicalDevice PhysicalDevice, std::vector<std::string>& InstanceExtensions, std::vector<std::string>& DeviceExtensions)
{
    NGXLock Lock;
    InstanceExtensions.clear();
    DeviceExtensions.clear();
    NVSDK_NGX_FeatureDiscoveryInfo Info{};
    Info.SDKVersion                   = NVSDK_NGX_Version_API;
    Info.FeatureID                    = NVSDK_NGX_Feature_RayReconstruction;
    Info.Identifier.IdentifierType    = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    Info.Identifier.v.ProjectDesc     = {ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0"};
    Info.ApplicationDataPath          = L".";
    uint32_t               Count      = 0;
    VkExtensionProperties* Properties = nullptr;
    auto                   Result     = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&Info, &Count, &Properties);
    if (NVSDK_NGX_FAILED(Result)) return false;
    for (uint32_t i = 0; i < Count; ++i) InstanceExtensions.emplace_back(Properties[i].extensionName);
    if (!Instance || !PhysicalDevice) return true;
    Count      = 0;
    Properties = nullptr;
    Result     = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(Instance, PhysicalDevice, &Info, &Count, &Properties);
    if (NVSDK_NGX_FAILED(Result)) return false;
    for (uint32_t i = 0; i < Count; ++i) DeviceExtensions.emplace_back(Properties[i].extensionName);
    return true;
}

std::shared_ptr<NGXRuntime> AcquireNGXRuntime(const NGXDevice& Device)
{
    NGXLock Lock;
    if (!Device.Device)
        return {};
    const Key Id{Device.Backend, Device.Device};
    auto      It = Runtimes.find(Id);
    if (It == Runtimes.end())
    {
        auto Runtime            = std::make_unique<NGXRuntime>();
        Runtime->Native         = Device;
        NVSDK_NGX_Result Result = NVSDK_NGX_Result_Fail;
        switch (Device.Backend)
        {
            case NGXBackend::D3D11:
                Result = NVSDK_NGX_D3D11_Init_with_ProjectID(ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0", L".", static_cast<ID3D11Device*>(Device.Device));
                break;
            case NGXBackend::D3D12:
                Result = NVSDK_NGX_D3D12_Init_with_ProjectID(ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0", L".", static_cast<ID3D12Device*>(Device.Device));
                break;
            case NGXBackend::Vulkan:
            {
                const auto Loader = GetModuleHandleW(L"vulkan-1.dll");
                const auto GIPA   = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(Loader, "vkGetInstanceProcAddr"));
                const auto GDPA   = GIPA ? reinterpret_cast<PFN_vkGetDeviceProcAddr>(GIPA(static_cast<VkInstance>(Device.Instance), "vkGetDeviceProcAddr")) : nullptr;
                Result            = NVSDK_NGX_VULKAN_Init_with_ProjectID(ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0", L".", static_cast<VkInstance>(Device.Instance), static_cast<VkPhysicalDevice>(Device.PhysicalDevice), static_cast<VkDevice>(Device.Device),
                                                              GIPA, GDPA);
                break;
            }
        }
        if (NVSDK_NGX_FAILED(Result))
        {
            LOG_WARNING_MESSAGE("NGX initialization failed for backend ", static_cast<unsigned>(Device.Backend), ": ", static_cast<unsigned>(Result));
            return {};
        }
        switch (Device.Backend)
        {
            case NGXBackend::D3D11: Result = NVSDK_NGX_D3D11_GetCapabilityParameters(&Runtime->Capabilities); break;
            case NGXBackend::D3D12: Result = NVSDK_NGX_D3D12_GetCapabilityParameters(&Runtime->Capabilities); break;
            case NGXBackend::Vulkan: Result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&Runtime->Capabilities); break;
        }
        if (NVSDK_NGX_FAILED(Result) || !Runtime->Capabilities)
        {
            LOG_WARNING_MESSAGE("NGX capability query failed: ", static_cast<unsigned>(Result));
            Shutdown(*Runtime);
            return {};
        }
        It = Runtimes.emplace(Id, std::move(Runtime)).first;
    }
    ++It->second->Owners;
    return std::shared_ptr<NGXRuntime>{It->second.get(), [Id](NGXRuntime* Runtime) {
                                           NGXLock Lock;
                                           if (--Runtime->Owners == 0)
                                           {
                                               Shutdown(*Runtime);
                                               Runtimes.erase(Id);
                                           }
                                       }};
}

NVSDK_NGX_Parameter* GetNGXCapabilities(NGXRuntime& Runtime) { return Runtime.Capabilities; }

NVSDK_NGX_Parameter* AllocateNGXParameters(NGXRuntime& Runtime)
{
    NGXLock              Lock;
    NVSDK_NGX_Parameter* Params = nullptr;
    NVSDK_NGX_Result     Result = NVSDK_NGX_Result_Fail;
    switch (Runtime.Native.Backend)
    {
        case NGXBackend::D3D11: Result = NVSDK_NGX_D3D11_AllocateParameters(&Params); break;
        case NGXBackend::D3D12: Result = NVSDK_NGX_D3D12_AllocateParameters(&Params); break;
        case NGXBackend::Vulkan: Result = NVSDK_NGX_VULKAN_AllocateParameters(&Params); break;
    }
    return NVSDK_NGX_SUCCEED(Result) ? Params : nullptr;
}

void DestroyNGXParameters(NGXRuntime& Runtime, NVSDK_NGX_Parameter* Params)
{
    NGXLock Lock;
    if (!Params)
        return;
    switch (Runtime.Native.Backend)
    {
        case NGXBackend::D3D11: NVSDK_NGX_D3D11_DestroyParameters(Params); break;
        case NGXBackend::D3D12: NVSDK_NGX_D3D12_DestroyParameters(Params); break;
        case NGXBackend::Vulkan: NVSDK_NGX_VULKAN_DestroyParameters(Params); break;
    }
}
} // namespace Diligent
