/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "GPUTestingEnvironment.hpp"
#include "RayReconstructionFactoryLoader.h"
#include "gtest/gtest.h"
#include "BasicMath.hpp"
#include "LoadEngineDll.h"
#include <vector>
#if SUPER_RESOLUTION_SUPPORTED
#    include "SuperResolutionFactoryLoader.h"
#endif

using namespace Diligent;
using namespace Diligent::Testing;
extern "C" int TestRayReconstructionCInterface(IRayReconstruction*, IRayReconstructionFactory*);

TEST(RayReconstructionTest, InvalidSettingsDoNotReturnStaleResults)
{
    RefCntAutoPtr<IRayReconstructionFactory> Factory;
    LoadAndCreateRayReconstructionFactory(GPUTestingEnvironment::GetInstance()->GetDevice(), &Factory);
    ASSERT_NE(nullptr, Factory);
    RayReconstructionSourceSettingsAttribs Query;
    RayReconstructionSourceSettings        Settings;
    Settings.OptimalInputWidth  = 1280;
    Settings.OptimalInputHeight = 720;
    EXPECT_FALSE(Factory->GetSourceSettings(Query, Settings));
    EXPECT_EQ(0u, Settings.OptimalInputWidth);
    EXPECT_EQ(0u, Settings.OptimalInputHeight);
    Query.VariantId    = IID_Unknown;
    Query.OutputWidth  = 1280;
    Query.OutputHeight = 720;
    EXPECT_FALSE(Factory->GetSourceSettings(Query, Settings));
    RayReconstructionDesc Desc;
    Desc.InputWidth   = 1280;
    Desc.InputHeight  = 720;
    Desc.OutputWidth  = 640;
    Desc.OutputHeight = 360;
    RefCntAutoPtr<IRayReconstruction> RR;
    Factory->CreateRayReconstruction(Desc, &RR);
    EXPECT_EQ(nullptr, RR);
}

TEST(RayReconstructionTest, ProviderDispatchAndEnumerationBounds)
{
    auto*                                    Device = GPUTestingEnvironment::GetInstance()->GetDevice();
    RefCntAutoPtr<IRayReconstructionFactory> Factory;
    LoadAndCreateRayReconstructionFactory(Device, &Factory);
    ASSERT_NE(nullptr, Factory);
    Uint32 Available = 0;
    Factory->EnumerateVariants(Available, nullptr);
    // FSR is only an extension point; DLSS is the sole implemented provider.
    EXPECT_LE(Available, 1u);
    if (Device->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11)
        EXPECT_EQ(0u, Available);
    RayReconstructionInfo Infos[2]{};
    Infos[0].VariantId = Infos[1].VariantId = IID_Unknown;
    Uint32 Count                            = 0;
    Factory->EnumerateVariants(Count, Infos);
    EXPECT_EQ(0u, Count);
    EXPECT_EQ(IID_Unknown, Infos[0].VariantId);
    Count = 2;
    Factory->EnumerateVariants(Count, Infos);
    EXPECT_EQ(Available, Count);
    EXPECT_EQ(IID_Unknown, Infos[1].VariantId);
    if (Available)
        EXPECT_EQ(RayReconstructionVariant_DLSS, Infos[0].VariantId);
    else
        EXPECT_EQ(IID_Unknown, Infos[0].VariantId);
    RayReconstructionDesc Desc;
    Desc.VariantId  = IID_Unknown;
    Desc.InputWidth = Desc.OutputWidth = 640;
    Desc.InputHeight = Desc.OutputHeight = 360;
    RefCntAutoPtr<IRayReconstruction> RR;
    Factory->CreateRayReconstruction(Desc, &RR);
    EXPECT_EQ(nullptr, RR);
    RayReconstructionSourceSettingsAttribs Query;
    Query.VariantId    = IID_Unknown;
    Query.OutputWidth  = 640;
    Query.OutputHeight = 360;
    RayReconstructionSourceSettings Settings;
    Settings.OptimalInputWidth = 640;
    EXPECT_FALSE(Factory->GetSourceSettings(Query, Settings));
    EXPECT_EQ(0u, Settings.OptimalInputWidth);
}

TEST(RayReconstructionTest, SharedAndStaticFactories)
{
    auto CreateShared = reinterpret_cast<CreateRayReconstructionFactoryType>(LoadEngineDll("RayReconstruction", "CreateRayReconstructionFactory"));
    ASSERT_NE(nullptr, CreateShared);
    auto*                                    Device = GPUTestingEnvironment::GetInstance()->GetDevice();
    RefCntAutoPtr<IRayReconstructionFactory> Shared, Static;
    CreateShared(Device, &Shared);
    LoadAndCreateRayReconstructionFactory(Device, &Static);
    ASSERT_NE(nullptr, Shared);
    ASSERT_NE(nullptr, Static);
    Uint32 SharedCount = 0, StaticCount = 0;
    Shared->EnumerateVariants(SharedCount, nullptr);
    Static->EnumerateVariants(StaticCount, nullptr);
    EXPECT_EQ(StaticCount, SharedCount);
    if (!StaticCount) GTEST_SKIP() << "NGX Ray Reconstruction unavailable";
    RayReconstructionSourceSettingsAttribs Query;
    Query.OutputWidth  = 1280;
    Query.OutputHeight = 720;
    RayReconstructionSourceSettings Settings;
    ASSERT_TRUE(Shared->GetSourceSettings(Query, Settings));
    Shared.Release();
    ASSERT_TRUE(Static->GetSourceSettings(Query, Settings));
    CreateShared(Device, &Shared);
    ASSERT_NE(nullptr, Shared);
    Static.Release();
    ASSERT_TRUE(Shared->GetSourceSettings(Query, Settings));
}

TEST(RayReconstructionTest, FactoryAndLifetime)
{
    auto*                                    Device = GPUTestingEnvironment::GetInstance()->GetDevice();
    RefCntAutoPtr<IRayReconstructionFactory> Factory;
    LoadAndCreateRayReconstructionFactory(Device, &Factory);
    ASSERT_NE(nullptr, Factory);
    Uint32 Count = 0;
    Factory->EnumerateVariants(Count, nullptr);
    if (!Count)
        GTEST_SKIP() << "NGX Ray Reconstruction unavailable";
    RayReconstructionInfo Info;
    Count = 1;
    Factory->EnumerateVariants(Count, &Info);
    ASSERT_EQ(1u, Count);
    EXPECT_EQ(RayReconstructionVariant_DLSS, Info.VariantId);

    RayReconstructionSourceSettingsAttribs Query;
    Query.OutputWidth  = 1280;
    Query.OutputHeight = 720;
    RayReconstructionSourceSettings Settings;
    ASSERT_TRUE(Factory->GetSourceSettings(Query, Settings));
    ASSERT_GT(Settings.OptimalInputWidth, 0u);
    EXPECT_LE(Settings.MinInputWidth, Settings.OptimalInputWidth);
    EXPECT_LE(Settings.OptimalInputWidth, Settings.MaxInputWidth);
    RayReconstructionDesc Desc;
    Desc.InputWidth   = Settings.OptimalInputWidth;
    Desc.InputHeight  = Settings.OptimalInputHeight;
    Desc.OutputWidth  = Query.OutputWidth;
    Desc.OutputHeight = Query.OutputHeight;
    RefCntAutoPtr<IRayReconstruction> RR;
    Factory->CreateRayReconstruction(Desc, &RR);
    ASSERT_NE(nullptr, RR);
    EXPECT_EQ(1, TestRayReconstructionCInterface(RR, Factory));
    Factory.Release();
    EXPECT_EQ(1280u, RR->GetDesc().OutputWidth);
    float X = 0, Y = 0;
    RR->GetJitterOffset(1, X, Y);
    EXPECT_GE(X, -0.5f);
    EXPECT_LE(X, 0.5f);
    EXPECT_GE(Y, -0.5f);
    EXPECT_LE(Y, 0.5f);
    RR.Release();
    LoadAndCreateRayReconstructionFactory(Device, &Factory);
    ASSERT_NE(nullptr, Factory);
    ASSERT_TRUE(Factory->GetSourceSettings(Query, Settings));
}

TEST(RayReconstructionTest, EvaluateAndReadback)
{
    GPUTestingEnvironment::ScopedReset       Reset;
    auto*                                    Env     = GPUTestingEnvironment::GetInstance();
    auto*                                    Device  = Env->GetDevice();
    auto*                                    Context = Env->GetDeviceContext();
    RefCntAutoPtr<IRayReconstructionFactory> Factory;
    LoadAndCreateRayReconstructionFactory(Device, &Factory);
    ASSERT_NE(nullptr, Factory);
    Uint32 Count = 0;
    Factory->EnumerateVariants(Count, nullptr);
    if (!Count) GTEST_SKIP() << "NGX Ray Reconstruction unavailable";

    // Exercise native and upscaled output, both roughness representations, and recreation.
    for (auto Quality : {RAY_RECONSTRUCTION_QUALITY_QUALITY, RAY_RECONSTRUCTION_QUALITY_NATIVE})
    {
        RayReconstructionSourceSettingsAttribs Query;
        Query.OutputWidth  = Quality == RAY_RECONSTRUCTION_QUALITY_NATIVE ? 640 : 1280;
        Query.OutputHeight = Quality == RAY_RECONSTRUCTION_QUALITY_NATIVE ? 360 : 720;
        Query.Quality      = Quality;
        RayReconstructionSourceSettings Settings;
        ASSERT_TRUE(Factory->GetSourceSettings(Query, Settings));
        RayReconstructionDesc Desc;
        Desc.InputWidth         = Settings.OptimalInputWidth;
        Desc.InputHeight        = Settings.OptimalInputHeight;
        Desc.OutputWidth        = Query.OutputWidth;
        Desc.OutputHeight       = Query.OutputHeight;
        Desc.Quality            = Quality;
        Desc.AutoExposure       = Quality != RAY_RECONSTRUCTION_QUALITY_NATIVE;
        char PresetOverride[16] = {};
        if (GetEnvironmentVariableA("DILIGENT_TEST_RR_PRESET", PresetOverride, sizeof(PresetOverride)) != 0)
            Desc.DLSSPreset = static_cast<RAY_RECONSTRUCTION_DLSS_PRESET>(std::strtoul(PresetOverride, nullptr, 10));
        Desc.RoughnessPacked = Quality == RAY_RECONSTRUCTION_QUALITY_NATIVE;
        RefCntAutoPtr<IRayReconstruction> RR, OtherRR;
        Factory->CreateRayReconstruction(Desc, &RR);
        auto CreateShared = reinterpret_cast<CreateRayReconstructionFactoryType>(LoadEngineDll("RayReconstruction", "CreateRayReconstructionFactory"));
        ASSERT_NE(nullptr, CreateShared);
        RefCntAutoPtr<IRayReconstructionFactory> SharedFactory;
        CreateShared(Device, &SharedFactory);
        ASSERT_NE(nullptr, SharedFactory);
        SharedFactory->CreateRayReconstruction(Desc, &OtherRR);
        SharedFactory.Release();
        ASSERT_NE(nullptr, RR);
        ASSERT_NE(nullptr, OtherRR);
        Factory.Release();

        const auto MakeTexture = [&](const char* Name, TEXTURE_FORMAT Format, Uint32 Components, const float* Value) {
            TextureDesc T;
            T.Name      = Name;
            T.Type      = RESOURCE_DIM_TEX_2D;
            T.Width     = Desc.InputWidth;
            T.Height    = Desc.InputHeight;
            T.Format    = Format;
            T.BindFlags = BIND_SHADER_RESOURCE;
            std::vector<float> Pixels(size_t{T.Width} * T.Height * Components);
            for (size_t i = 0; i < Pixels.size(); i += Components)
                std::copy(Value, Value + Components, Pixels.begin() + i);
            TextureSubResData Sub;
            Sub.pData  = Pixels.data();
            Sub.Stride = Uint64{T.Width} * Components * sizeof(float);
            TextureData             Data{&Sub, 1};
            RefCntAutoPtr<ITexture> Texture;
            Device->CreateTexture(T, &Data, &Texture);
            return Texture;
        };
        const float ColorValue[]     = {0.3f, 0.2f, 0.1f, 1};
        const float NormalValue[]    = {0, 0, -1, 0.5f};
        const float DepthValue[]     = {10};
        const float RoughnessValue[] = {0.5f};
        const float MotionValue[]    = {0, 0};
        auto        Color            = MakeTexture("RR radiance", TEX_FORMAT_RGBA32_FLOAT, 4, ColorValue);
        auto        Normal           = MakeTexture("RR normals", TEX_FORMAT_RGBA32_FLOAT, 4, NormalValue);
        auto        Depth            = MakeTexture("RR depth", TEX_FORMAT_R32_FLOAT, 1, DepthValue);
        auto        Roughness        = MakeTexture("RR roughness", TEX_FORMAT_R32_FLOAT, 1, RoughnessValue);
        auto        Motion           = MakeTexture("RR motion", TEX_FORMAT_RG32_FLOAT, 2, MotionValue);
        ASSERT_NE(nullptr, Color);
        ASSERT_NE(nullptr, Normal);
        ASSERT_NE(nullptr, Depth);
        ASSERT_NE(nullptr, Roughness);
        ASSERT_NE(nullptr, Motion);
        TextureDesc OutputDesc;
        OutputDesc.Name      = "RR output";
        OutputDesc.Type      = RESOURCE_DIM_TEX_2D;
        OutputDesc.Width     = Desc.OutputWidth;
        OutputDesc.Height    = Desc.OutputHeight;
        OutputDesc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        OutputDesc.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        RefCntAutoPtr<ITexture> Output;
        Device->CreateTexture(OutputDesc, nullptr, &Output);
        ASSERT_NE(nullptr, Output);
        ExecuteRayReconstructionAttribs A;
        A.pContext          = Context;
        A.pColorSRV         = Color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        A.pDiffuseAlbedoSRV = A.pSpecularAlbedoSRV = A.pColorSRV;
        A.pNormalsSRV                              = Normal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        A.pDepthSRV                                = Depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        A.pMotionVectorsSRV                        = Motion->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        A.pRoughnessSRV                            = Desc.RoughnessPacked ? nullptr : Roughness->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        A.pOutputUAV                               = Output->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS);
        const auto Projection                      = float4x4::Projection(PI_F / 3, float(Desc.InputWidth) / Desc.InputHeight, 0.1f, 1000.0f, false);
        std::copy(&Projection.m00, &Projection.m00 + 16, A.ViewToClip);
        {
            auto Invalid      = A;
            Invalid.pColorSRV = nullptr;
            Env->SetErrorAllowance(1);
            EXPECT_FALSE(RR->Execute(Invalid));
            Env->SetErrorAllowance(0);
            Invalid            = A;
            Invalid.pOutputUAV = Output->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            Env->SetErrorAllowance(1);
            EXPECT_FALSE(RR->Execute(Invalid));
            Env->SetErrorAllowance(0);
            Invalid          = A;
            Invalid.pContext = nullptr;
            Env->SetErrorAllowance(1);
            EXPECT_FALSE(RR->Execute(Invalid));
            Env->SetErrorAllowance(0);
            if (Env->GetNumDeferredContexts() != 0)
            {
                Invalid.pContext = Env->GetDeferredContext(0);
                Env->SetErrorAllowance(1);
                EXPECT_FALSE(RR->Execute(Invalid));
                Env->SetErrorAllowance(0);
            }
        }
#if SUPER_RESOLUTION_SUPPORTED
        RefCntAutoPtr<ISuperResolutionFactory> SRFactory;
        LoadAndCreateSuperResolutionFactory(Device, &SRFactory);
        ASSERT_NE(nullptr, SRFactory);
        Uint32 SRCount = 0;
        SRFactory->EnumerateVariants(SRCount, nullptr);
        std::vector<SuperResolutionInfo> SRVariants(SRCount);
        SRFactory->EnumerateVariants(SRCount, SRVariants.data());
        RefCntAutoPtr<ISuperResolution> SR;
        for (const auto& Info : SRVariants)
        {
            if (std::string{Info.Name}.find("DLSS") == std::string::npos) continue;
            SuperResolutionDesc SD;
            SD.VariantId    = Info.VariantId;
            SD.InputWidth   = Desc.InputWidth;
            SD.InputHeight  = Desc.InputHeight;
            SD.OutputWidth  = Desc.OutputWidth;
            SD.OutputHeight = Desc.OutputHeight;
            SD.ColorFormat  = TEX_FORMAT_RGBA32_FLOAT;
            SD.DepthFormat  = TEX_FORMAT_R32_FLOAT;
            SD.MotionFormat = TEX_FORMAT_RG32_FLOAT;
            SD.OutputFormat = TEX_FORMAT_RGBA16_FLOAT;
            SD.Flags        = SUPER_RESOLUTION_FLAG_AUTO_EXPOSURE;
            SRFactory->CreateSuperResolution(SD, &SR);
            ASSERT_NE(nullptr, SR);
            break;
        }
        SRFactory.Release();
        if (SR)
        {
            ExecuteSuperResolutionAttribs SA;
            SA.pContext           = Context;
            SA.pColorTextureSRV   = A.pColorSRV;
            SA.pDepthTextureSRV   = A.pDepthSRV;
            SA.pMotionVectorsSRV  = A.pMotionVectorsSRV;
            SA.pOutputTextureView = A.pOutputUAV;
            SA.ResetHistory       = True;
            SR->Execute(SA);
            Context->Flush();
        }
#endif
        for (Uint32 Frame = 0; Frame < 4; ++Frame)
        {
            const float   Sentinel[] = {0, 0, 0, 0};
            ITextureView* RTV        = Output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            Context->SetRenderTargets(1, &RTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Context->ClearRenderTarget(RTV, Sentinel, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            Context->Flush(); // A dropped RR command buffer must leave a detectable zero output.
            // Leave a real barrier queued, as compute-produced RR guides do.
            StateTransitionDesc PendingBarrier{Output, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE};
            Context->TransitionResourceStates(1, &PendingBarrier);
            RR->GetJitterOffset(Frame, A.JitterX, A.JitterY);
            A.ResetHistory              = Frame == 2;
            A.pSpecularHitDistanceSRV   = Frame == 0 ? A.pDepthSRV : nullptr;
            A.pDiffuseHitDistanceSRV    = Frame == 0 ? A.pDepthSRV : nullptr;
            A.pSpecularMotionVectorsSRV = Frame == 1 ? A.pMotionVectorsSRV : nullptr;
            ASSERT_TRUE(RR->Execute(A));
            Context->Flush();
        }
        // The shared RR module must flush through the owning engine's virtual
        // context API, not an inline helper using another module's Vulkan loader.
        StateTransitionDesc PendingBarriers[] = {
            {Output, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_COPY_SOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE},
            {Output, RESOURCE_STATE_COPY_SOURCE, RESOURCE_STATE_UNORDERED_ACCESS, STATE_TRANSITION_FLAG_UPDATE_STATE}};
        Context->TransitionResourceStates(2, PendingBarriers);
        ASSERT_TRUE(OtherRR->Execute(A));
        Context->Flush();
        OtherRR.Release();
#if SUPER_RESOLUTION_SUPPORTED
        SR.Release();
        ASSERT_TRUE(RR->Execute(A));
#endif

        OutputDesc.Name           = "RR readback";
        OutputDesc.BindFlags      = BIND_NONE;
        OutputDesc.Usage          = USAGE_STAGING;
        OutputDesc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> Readback;
        Device->CreateTexture(OutputDesc, nullptr, &Readback);
        ASSERT_NE(nullptr, Readback);
        CopyTextureAttribs Copy{Output, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Readback, RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
        Context->CopyTexture(Copy);
        Context->Flush();
        Context->WaitForIdle();
        MappedTextureSubresource Mapped;
        Context->MapTextureSubresource(Readback, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, Mapped);
        ASSERT_NE(nullptr, Mapped.pData);
        const auto* Pixel = reinterpret_cast<const Uint16*>(static_cast<const Uint8*>(Mapped.pData) + (Desc.OutputHeight / 2) * Mapped.Stride) + (Desc.OutputWidth / 2) * 4;
        for (Uint32 Channel = 0; Channel < 3; ++Channel)
        {
            EXPECT_NE(0x7c00u, Pixel[Channel] & 0x7c00u) << "Non-finite RR output";
            EXPECT_NE(0u, Pixel[Channel] & 0x7fffu) << "RR output was not written";
        }
        Context->UnmapTextureSubresource(Readback, 0, 0);
        RR.Release();
        LoadAndCreateRayReconstructionFactory(Device, &Factory);
        ASSERT_NE(nullptr, Factory);
    }
}
