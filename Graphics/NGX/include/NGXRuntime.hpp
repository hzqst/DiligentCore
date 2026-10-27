/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once

#include <memory>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>

#ifdef DILIGENT_NGX_EXPORTS
#    define DILIGENT_NGX_API __declspec(dllexport)
#else
#    define DILIGENT_NGX_API __declspec(dllimport)
#endif

namespace Diligent
{

enum class NGXBackend
{
    D3D11,
    D3D12,
    Vulkan
};

// Native handles only: the runtime must not depend on any graphics engine implementation.
struct NGXDevice
{
    NGXBackend Backend;
    void*      Device         = nullptr;
    void*      Instance       = nullptr;
    void*      PhysicalDevice = nullptr;
};

class NGXRuntime;
DILIGENT_NGX_API std::shared_ptr<NGXRuntime> AcquireNGXRuntime(const NGXDevice& Device);
DILIGENT_NGX_API NVSDK_NGX_Parameter* GetNGXCapabilities(NGXRuntime& Runtime);
DILIGENT_NGX_API NVSDK_NGX_Parameter* AllocateNGXParameters(NGXRuntime& Runtime);
DILIGENT_NGX_API void                 DestroyNGXParameters(NGXRuntime& Runtime, NVSDK_NGX_Parameter* Params);
DILIGENT_NGX_API void                 LockNGX();
DILIGENT_NGX_API void                 UnlockNGX();

// Includes helper callback invocations and parameter mutations in the same critical section.
struct NGXLock
{
    NGXLock() { LockNGX(); }
    ~NGXLock() { UnlockNGX(); }
    NGXLock(const NGXLock&) = delete;
    NGXLock& operator=(const NGXLock&) = delete;
};

} // namespace Diligent
