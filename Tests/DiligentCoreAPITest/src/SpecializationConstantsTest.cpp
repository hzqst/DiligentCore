/*
 *  Copyright 2019-2026 Diligent Graphics LLC
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

// Positive / functional tests for specialization constants.
// Verifies that specialization constant values affect shader output.
//
// PSO creation failure tests (validation of invalid SpecializationConstant entries)
// live in ObjectCreationFailure/PSOCreationFailureTest.cpp.

#include "GPUTestingEnvironment.hpp"
#include "TestingSwapChainBase.hpp"
#include "GraphicsAccessories.hpp"

#include "gtest/gtest.h"

namespace Diligent
{
namespace Testing
{
void RenderDrawCommandReference(ISwapChain* pSwapChain, const float* pClearColor = nullptr);
void ComputeShaderReference(ISwapChain* pSwapChain);
} // namespace Testing
} // namespace Diligent

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

// ---------------------------------------------------------------------------
// Compute path: fill the swap chain back buffer via specialization constants.
// Uses ComputeShaderReference for the reference snapshot, just like
// InlineConstantsTest::ComputeResourceLayout.
// ---------------------------------------------------------------------------

// Spec-const shader: same gradient as FillTextureCS, but channel multipliers
// come from specialization constants.
// Reference output: vec4(vec2(xy % 256) / 256.0, 0.0, 1.0)
// Base color has non-zero B so that sc_MulB is not optimized away.
// To match: sc_MulR=1.0, sc_MulG=1.0, sc_MulB=0.0
static constexpr char g_SpecConstComputeCS_GLSL[] = R"(
    #version 450
    layout(constant_id = 0) const float sc_MulR = 0.0;
    layout(constant_id = 1) const float sc_MulG = 0.0;
    layout(constant_id = 2) const float sc_MulB = 0.0;

    layout(rgba8, binding = 0) writeonly uniform image2D g_tex2DUAV;

    layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
    void main()
    {
        ivec2 dim   = imageSize(g_tex2DUAV);
        ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
        if (coord.x >= dim.x || coord.y >= dim.y)
            return;
        vec2 uv = vec2(gl_GlobalInvocationID.xy % 256u) / 256.0;
        // Base color has non-zero B channel so the compiler cannot
        // eliminate sc_MulB as a dead specialization constant.
        vec4 Color = vec4(uv.x * sc_MulR,
                          uv.y * sc_MulG,
                          uv.x * sc_MulB,
                          1.0);
        imageStore(g_tex2DUAV, coord, Color);
    }
)";

TEST(SpecializationConstantsTest, ComputePath)
{
    auto* const pEnv       = GPUTestingEnvironment::GetInstance();
    auto* const pDevice    = pEnv->GetDevice();
    auto* const pContext   = pEnv->GetDeviceContext();
    auto* const pSwapChain = pEnv->GetSwapChain();
    const auto& DeviceInfo = pDevice->GetDeviceInfo();

    if (!DeviceInfo.IsVulkanDevice())
        GTEST_SKIP() << "Specialization constants are currently Vulkan-only";
    if (DeviceInfo.Features.SpecializationConstants != DEVICE_FEATURE_STATE_ENABLED)
        GTEST_SKIP() << "Specialization constants are not supported by this device";
    if (!DeviceInfo.Features.ComputeShaders)
        GTEST_SKIP() << "Compute shaders are not supported by this device";

    GPUTestingEnvironment::ScopedReset EnvironmentAutoReset;

    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{pSwapChain, IID_TestingSwapChain};
    if (!pTestingSwapChain)
    {
        GTEST_SKIP() << "SpecializationConstants compute test requires testing swap chain";
    }

    const SwapChainDesc& SCDesc = pSwapChain->GetDesc();

    // --- Reference pass: native-API compute dispatch + TakeSnapshot ---
    ComputeShaderReference(pSwapChain);

    // --- Spec-const pass: same gradient, channel multipliers via specialization constants ---
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
        ShaderCI.Desc                       = {"SpecConst Compute CS", SHADER_TYPE_COMPUTE, true};
        ShaderCI.EntryPoint                 = "main";
        ShaderCI.Source                     = g_SpecConstComputeCS_GLSL;
        ShaderCI.LoadSpecializationConstants = true;

        RefCntAutoPtr<IShader> pCS;
        pDevice->CreateShader(ShaderCI, &pCS);
        ASSERT_NE(pCS, nullptr);

        // Multipliers that reproduce the reference output:
        //   R channel = x-gradient * 1.0
        //   G channel = y-gradient * 1.0
        //   B channel = 0.0 * 0.0 = 0.0
        const float            MulR         = 1.0f;
        const float            MulG         = 1.0f;
        const float            MulB         = 0.0f;
        SpecializationConstant SpecConsts[] = {
            {"sc_MulR", SHADER_TYPE_COMPUTE, sizeof(float), &MulR},
            {"sc_MulG", SHADER_TYPE_COMPUTE, sizeof(float), &MulG},
            {"sc_MulB", SHADER_TYPE_COMPUTE, sizeof(float), &MulB},
        };

        ComputePipelineStateCreateInfo PsoCI;
        PsoCI.PSODesc.Name               = "SpecConst Compute Test";
        PsoCI.PSODesc.PipelineType       = PIPELINE_TYPE_COMPUTE;
        PsoCI.pCS                        = pCS;
        PsoCI.NumSpecializationConstants = _countof(SpecConsts);
        PsoCI.pSpecializationConstants   = SpecConsts;

        RefCntAutoPtr<IPipelineState> pPSO;
        pDevice->CreateComputePipelineState(PsoCI, &pPSO);
        ASSERT_NE(pPSO, nullptr);

        pPSO->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "g_tex2DUAV")
            ->Set(pTestingSwapChain->GetCurrentBackBufferUAV());

        RefCntAutoPtr<IShaderResourceBinding> pSRB;
        pPSO->CreateShaderResourceBinding(&pSRB, true);
        ASSERT_NE(pSRB, nullptr);

        pContext->SetPipelineState(pPSO);
        pContext->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DispatchComputeAttribs DispatchAttribs;
        DispatchAttribs.ThreadGroupCountX = (SCDesc.Width + 15) / 16;
        DispatchAttribs.ThreadGroupCountY = (SCDesc.Height + 15) / 16;
        pContext->DispatchCompute(DispatchAttribs);
    }

    pSwapChain->Present();
}


// ---------------------------------------------------------------------------
// Graphics path: draw two triangles with per-vertex colors from spec constants.
// Uses RenderDrawCommandReference for the reference snapshot, just like
// DrawCommandTest and InlineConstantsTest.
// ---------------------------------------------------------------------------

// Vertex shader: hardcoded positions (same as DrawTest_ProceduralTriangleVS),
// per-vertex colors supplied via specialization constants (9 floats).
static constexpr char g_SpecConstGraphicsVS_GLSL[] = R"(
    #version 450

    #ifndef GL_ES
    out gl_PerVertex { vec4 gl_Position; };
    #endif

    // Per-vertex colors as specialization constants (3 colors x RGB).
    layout(constant_id = 0) const float sc_Col0_R = 1.0;
    layout(constant_id = 1) const float sc_Col0_G = 0.0;
    layout(constant_id = 2) const float sc_Col0_B = 0.0;

    layout(constant_id = 3) const float sc_Col1_R = 0.0;
    layout(constant_id = 4) const float sc_Col1_G = 1.0;
    layout(constant_id = 5) const float sc_Col1_B = 0.0;

    layout(constant_id = 6) const float sc_Col2_R = 0.0;
    layout(constant_id = 7) const float sc_Col2_G = 0.0;
    layout(constant_id = 8) const float sc_Col2_B = 1.0;

    layout(location = 0) out vec3 out_Color;

    void main()
    {
        vec4 Pos[6];
        Pos[0] = vec4(-1.0, -0.5, 0.0, 1.0);
        Pos[1] = vec4(-0.5, +0.5, 0.0, 1.0);
        Pos[2] = vec4( 0.0, -0.5, 0.0, 1.0);

        Pos[3] = vec4(+0.0, -0.5, 0.0, 1.0);
        Pos[4] = vec4(+0.5, +0.5, 0.0, 1.0);
        Pos[5] = vec4(+1.0, -0.5, 0.0, 1.0);

        vec3 Col[3];
        Col[0] = vec3(sc_Col0_R, sc_Col0_G, sc_Col0_B);
        Col[1] = vec3(sc_Col1_R, sc_Col1_G, sc_Col1_B);
        Col[2] = vec3(sc_Col2_R, sc_Col2_G, sc_Col2_B);

        gl_Position = Pos[gl_VertexIndex];
        out_Color   = Col[gl_VertexIndex % 3];
    }
)";

// Fragment shader: pass-through interpolated color.
static constexpr char g_SpecConstGraphicsPS_GLSL[] = R"(
    #version 450

    layout(location = 0) in  vec3 in_Color;
    layout(location = 0) out vec4 out_Color;

    void main()
    {
        out_Color = vec4(in_Color, 1.0);
    }
)";

TEST(SpecializationConstantsTest, GraphicsPath)
{
    auto* const pEnv       = GPUTestingEnvironment::GetInstance();
    auto* const pDevice    = pEnv->GetDevice();
    auto* const pContext   = pEnv->GetDeviceContext();
    auto* const pSwapChain = pEnv->GetSwapChain();
    const auto& DeviceInfo = pDevice->GetDeviceInfo();

    if (!DeviceInfo.IsVulkanDevice())
        GTEST_SKIP() << "Specialization constants are currently Vulkan-only";
    if (DeviceInfo.Features.SpecializationConstants != DEVICE_FEATURE_STATE_ENABLED)
        GTEST_SKIP() << "Specialization constants are not supported by this device";

    GPUTestingEnvironment::ScopedReset EnvironmentAutoReset;

    RefCntAutoPtr<ITestingSwapChain> pTestingSwapChain{pSwapChain, IID_TestingSwapChain};
    if (!pTestingSwapChain)
    {
        GTEST_SKIP() << "SpecializationConstants graphics test requires testing swap chain";
    }

    const SwapChainDesc& SCDesc = pSwapChain->GetDesc();

    // --- Reference pass: native-API two-triangle draw + TakeSnapshot ---
    const float ClearColor[] = {0.25f, 0.25f, 0.25f, 1.0f};
    RenderDrawCommandReference(pSwapChain, ClearColor);

    // --- Spec-const pass: same two triangles, colors via specialization constants ---
    {
        ITextureView* pRTVs[] = {pSwapChain->GetCurrentBackBufferRTV()};
        pContext->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pContext->ClearRenderTarget(pRTVs[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage              = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
        ShaderCI.LoadSpecializationConstants = true;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc   = {"SpecConst Graphics VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.Source = g_SpecConstGraphicsVS_GLSL;
            pDevice->CreateShader(ShaderCI, &pVS);
            ASSERT_NE(pVS, nullptr);
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc   = {"SpecConst Graphics PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.Source = g_SpecConstGraphicsPS_GLSL;
            pDevice->CreateShader(ShaderCI, &pPS);
            ASSERT_NE(pPS, nullptr);
        }

        // Same per-vertex colors as DrawTest_ProceduralTriangleVS:
        //   Col[0] = (1, 0, 0)  Col[1] = (0, 1, 0)  Col[2] = (0, 0, 1)
        const float Col0_R = 1.0f, Col0_G = 0.0f, Col0_B = 0.0f;
        const float Col1_R = 0.0f, Col1_G = 1.0f, Col1_B = 0.0f;
        const float Col2_R = 0.0f, Col2_G = 0.0f, Col2_B = 1.0f;

        SpecializationConstant SpecConsts[] = {
            {"sc_Col0_R", SHADER_TYPE_VERTEX, sizeof(float), &Col0_R},
            {"sc_Col0_G", SHADER_TYPE_VERTEX, sizeof(float), &Col0_G},
            {"sc_Col0_B", SHADER_TYPE_VERTEX, sizeof(float), &Col0_B},
            {"sc_Col1_R", SHADER_TYPE_VERTEX, sizeof(float), &Col1_R},
            {"sc_Col1_G", SHADER_TYPE_VERTEX, sizeof(float), &Col1_G},
            {"sc_Col1_B", SHADER_TYPE_VERTEX, sizeof(float), &Col1_B},
            {"sc_Col2_R", SHADER_TYPE_VERTEX, sizeof(float), &Col2_R},
            {"sc_Col2_G", SHADER_TYPE_VERTEX, sizeof(float), &Col2_G},
            {"sc_Col2_B", SHADER_TYPE_VERTEX, sizeof(float), &Col2_B},
        };

        GraphicsPipelineStateCreateInfo PsoCI;
        PsoCI.PSODesc.Name                                  = "SpecConst Graphics Test";
        PsoCI.pVS                                           = pVS;
        PsoCI.pPS                                           = pPS;
        PsoCI.GraphicsPipeline.NumRenderTargets             = 1;
        PsoCI.GraphicsPipeline.RTVFormats[0]                = SCDesc.ColorBufferFormat;
        PsoCI.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PsoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
        PsoCI.NumSpecializationConstants                    = _countof(SpecConsts);
        PsoCI.pSpecializationConstants                      = SpecConsts;

        RefCntAutoPtr<IPipelineState> pPSO;
        pDevice->CreateGraphicsPipelineState(PsoCI, &pPSO);
        ASSERT_NE(pPSO, nullptr);

        RefCntAutoPtr<IShaderResourceBinding> pSRB;
        pPSO->CreateShaderResourceBinding(&pSRB, true);
        ASSERT_NE(pSRB, nullptr);

        pContext->SetPipelineState(pPSO);
        pContext->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs drawAttribs;
        drawAttribs.NumVertices = 6;
        pContext->Draw(drawAttribs);
    }

    pSwapChain->Present();
}


} // namespace
