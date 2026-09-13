/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "RayReconstructionFactoryLoader.h"
#include "RayReconstructionProvider.hpp"
#include "ObjectBase.hpp"
#include "EngineMemory.h"
#include <algorithm>
#include <stdexcept>
namespace Diligent
{
namespace
{
class RayReconstructionFactory final : public ObjectBase<IRayReconstructionFactory>
{
public:
    using TBase = ObjectBase<IRayReconstructionFactory>;
    RayReconstructionFactory(IReferenceCounters* Counters, IRenderDevice* Device) :
        TBase{Counters}
    {
#if DILIGENT_RR_D3D12
        AddProvider(Device, CreateRRDLSSProviderD3D12, "DLSS D3D12");
#endif
#if DILIGENT_RR_VULKAN
        AddProvider(Device, CreateRRDLSSProviderVk, "DLSS Vulkan");
#endif
        AddProvider(Device, CreateRRFSRProvider, "FSR");
    }
    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RayReconstructionFactory, TBase)
    void DILIGENT_CALL_TYPE EnumerateVariants(Uint32& Count, RayReconstructionInfo* Variants) const override
    {
        if (!Variants)
        {
            Count = m_TotalVariants;
            return;
        }
        const Uint32 Capacity = Count;
        Count                 = 0;
        for (const auto& Entry : m_Providers)
            for (const auto& Info : Entry.Variants)
            {
                if (Count == Capacity) return;
                Variants[Count++] = Info;
            }
    }
    Bool DILIGENT_CALL_TYPE GetSourceSettings(const RayReconstructionSourceSettingsAttribs& A, RayReconstructionSourceSettings& S) const override
    {
        S              = {};
        auto* Provider = FindProvider(A.VariantId);
        return Provider ? Provider->GetSourceSettings(A, S) : False;
    }
    void DILIGENT_CALL_TYPE CreateRayReconstruction(const RayReconstructionDesc& Desc, IRayReconstruction** Output) override
    {
        if (!Output) return;
        *Output        = nullptr;
        auto* Provider = FindProvider(Desc.VariantId);
        if (!Provider) return;
        try
        {
            Provider->CreateRayReconstruction(Desc, Output);
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE(Error.what());
        }
    }

private:
    struct ProviderInfo
    {
        std::unique_ptr<RayReconstructionProvider> Provider;
        std::vector<RayReconstructionInfo>         Variants;
    };
    void AddProvider(IRenderDevice* Device, std::unique_ptr<RayReconstructionProvider> (*Create)(IRenderDevice*), const char* Name)
    {
        try
        {
            ProviderInfo Entry;
            Entry.Provider = Create(Device);
            if (!Entry.Provider) return;
            Entry.Provider->EnumerateVariants(Entry.Variants);
            if (Entry.Variants.empty()) return;
            m_TotalVariants += static_cast<Uint32>(Entry.Variants.size());
            m_Providers.push_back(std::move(Entry));
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to create ray reconstruction provider '", Name, "': ", Error.what());
        }
    }
    RayReconstructionProvider* FindProvider(const INTERFACE_ID& VariantId) const
    {
        for (const auto& Entry : m_Providers)
            for (const auto& Info : Entry.Variants)
                if (Info.VariantId == VariantId) return Entry.Provider.get();
        return nullptr;
    }
    std::vector<ProviderInfo> m_Providers;
    Uint32                    m_TotalVariants = 0;
};
} // namespace
void CreateRayReconstructionFactory(IRenderDevice* Device, IRayReconstructionFactory** Factory)
{
    if (!Factory) return;
    *Factory = nullptr;
    if (!Device) return;
    try
    {
        auto* F = NEW_RC_OBJ(GetRawAllocator(), "RayReconstructionFactory", RayReconstructionFactory)(Device);
        F->QueryInterface(IID_RayReconstructionFactory, reinterpret_cast<IObject**>(Factory));
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE(Error.what());
    }
}
} // namespace Diligent

extern "C" void Diligent_CreateRayReconstructionFactory(Diligent::IRenderDevice* Device, Diligent::IRayReconstructionFactory** Factory)
{
    Diligent::CreateRayReconstructionFactory(Device, Factory);
}
