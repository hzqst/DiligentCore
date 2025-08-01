/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
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

#include "pch.h"

#include "RenderDeviceGLImpl.hpp"

#include "BufferGLImpl.hpp"
#include "ShaderGLImpl.hpp"
#include "Texture1D_GL.hpp"
#include "Texture1DArray_GL.hpp"
#include "Texture2D_GL.hpp"
#include "Texture2DArray_GL.hpp"
#include "Texture3D_GL.hpp"
#include "TextureCube_GL.hpp"
#include "TextureCubeArray_GL.hpp"
#include "SamplerGLImpl.hpp"
#include "DeviceContextGLImpl.hpp"
#include "PipelineStateGLImpl.hpp"
#include "ShaderResourceBindingGLImpl.hpp"
#include "FenceGLImpl.hpp"
#include "QueryGLImpl.hpp"
#include "RenderPassGLImpl.hpp"
#include "FramebufferGLImpl.hpp"
#include "PipelineResourceSignatureGLImpl.hpp"

#include "GLTypeConversions.hpp"
#include "VAOCache.hpp"
#include "EngineMemory.h"
#include "StringTools.hpp"

namespace Diligent
{

#if GL_KHR_debug
static void GLAPIENTRY openglCallbackFunction(GLenum        source,
                                              GLenum        type,
                                              GLuint        id,
                                              GLenum        severity,
                                              GLsizei       length,
                                              const GLchar* message,
                                              const void*   userParam)
{
    const int* ShowDebugOutput = reinterpret_cast<const int*>(userParam);
    if (*ShowDebugOutput == 0)
        return;

    // Note: disabling flood of notifications through glDebugMessageControl() has no effect,
    // so we have to filter them out here
    if (id == 131185 || // Buffer detailed info: Buffer object <X> (bound to GL_XXXX ... , usage hint is GL_DYNAMIC_DRAW)
                        // will use VIDEO memory as the source for buffer object operations.
        id == 131186    // Buffer object <X> (bound to GL_XXXX, usage hint is GL_DYNAMIC_DRAW) is being copied/moved from VIDEO memory to HOST memory.
    )
        return;

    std::stringstream MessageSS;

    MessageSS << "OpenGL debug message " << id << " (";
    switch (source)
    {
        // clang-format off
        case GL_DEBUG_SOURCE_API:             MessageSS << "Source: API.";             break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   MessageSS << "Source: Window System.";   break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: MessageSS << "Source: Shader Compiler."; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     MessageSS << "Source: Third Party.";     break;
        case GL_DEBUG_SOURCE_APPLICATION:     MessageSS << "Source: Application.";     break;
        case GL_DEBUG_SOURCE_OTHER:           MessageSS << "Source: Other.";           break;
        default:                              MessageSS << "Source: Unknown (" << source << ").";
            // clang-format on
    }

    switch (type)
    {
        // clang-format off
        case GL_DEBUG_TYPE_ERROR:               MessageSS << " Type: ERROR.";                break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: MessageSS << " Type: Deprecated Behaviour."; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  MessageSS << " Type: UNDEFINED BEHAVIOUR.";  break;
        case GL_DEBUG_TYPE_PORTABILITY:         MessageSS << " Type: Portability.";          break;
        case GL_DEBUG_TYPE_PERFORMANCE:         MessageSS << " Type: PERFORMANCE.";          break;
        case GL_DEBUG_TYPE_MARKER:              MessageSS << " Type: Marker.";               break;
        case GL_DEBUG_TYPE_PUSH_GROUP:          MessageSS << " Type: Push Group.";           break;
        case GL_DEBUG_TYPE_POP_GROUP:           MessageSS << " Type: Pop Group.";            break;
        case GL_DEBUG_TYPE_OTHER:               MessageSS << " Type: Other.";                break;
        default:                                MessageSS << " Type: Unknown (" << type << ").";
            // clang-format on
    }

    switch (severity)
    {
        // clang-format off
        case GL_DEBUG_SEVERITY_HIGH:         MessageSS << " Severity: HIGH";         break;
        case GL_DEBUG_SEVERITY_MEDIUM:       MessageSS << " Severity: Medium";       break;
        case GL_DEBUG_SEVERITY_LOW:          MessageSS << " Severity: Low";          break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: MessageSS << " Severity: Notification"; break;
        default:                             MessageSS << " Severity: Unknown (" << severity << ")"; break;
            // clang-format on
    }

    MessageSS << "): " << message;

    LOG_INFO_MESSAGE(MessageSS.str().c_str());
}
#endif // GL_KHR_debug

class BottomLevelASGLImpl
{};
class TopLevelASGLImpl
{};
class ShaderBindingTableGLImpl
{};
class DeviceMemoryGLImpl
{};

static void VerifyEngineGLCreateInfo(const EngineGLCreateInfo& EngineCI) noexcept(false)
{
    if (EngineCI.Features.ShaderResourceQueries == DEVICE_FEATURE_STATE_ENABLED &&
        EngineCI.Features.SeparablePrograms == DEVICE_FEATURE_STATE_DISABLED)
    {
        LOG_ERROR_AND_THROW("Requested state for ShaderResourceQueries feature is ENABLED, while requested state for SeparablePrograms feature is DISABLED. "
                            "ShaderResourceQueries may only be enabled when SeparablePrograms feature is also enabled.");
    }

    if (EngineCI.Features.GeometryShaders == DEVICE_FEATURE_STATE_ENABLED &&
        EngineCI.Features.SeparablePrograms == DEVICE_FEATURE_STATE_DISABLED)
    {
        LOG_ERROR_AND_THROW("Requested state for GeometryShaders feature is ENABLED, while requested state for SeparablePrograms feature is DISABLED. "
                            "GeometryShaders may only be enabled when SeparablePrograms feature is also enabled.");
    }

    if (EngineCI.Features.Tessellation == DEVICE_FEATURE_STATE_ENABLED &&
        EngineCI.Features.SeparablePrograms == DEVICE_FEATURE_STATE_DISABLED)
    {
        LOG_ERROR_AND_THROW("Requested state for Tessellation feature is ENABLED, while requested state for SeparablePrograms feature is DISABLED. "
                            "Tessellation may only be enabled when SeparablePrograms feature is also enabled.");
    }
}

RenderDeviceGLImpl::RenderDeviceGLImpl(IReferenceCounters*       pRefCounters,
                                       IMemoryAllocator&         RawMemAllocator,
                                       IEngineFactory*           pEngineFactory,
                                       const EngineGLCreateInfo& EngineCI,
                                       const SwapChainDesc*      pSCDesc) :
    // clang-format off
    TRenderDeviceBase
    {
        pRefCounters,
        RawMemAllocator,
        pEngineFactory,
        EngineCI,
        GraphicsAdapterInfo{} // Adapter properties can only be queried after GL context is initialized
    },
    // Device caps must be filled in before the constructor of Pipeline Cache is called!
    m_GLContext{EngineCI, m_DeviceInfo.Type, m_DeviceInfo.APIVersion, pSCDesc}
// clang-format on
{
    VerifyEngineGLCreateInfo(EngineCI);

    VERIFY(EngineCI.NumDeferredContexts == 0, "EngineCI.NumDeferredContexts > 0 should've been caught by CreateDeviceAndSwapChainGL() or AttachToActiveGLContext()");

    GLint NumExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &NumExtensions);
    CHECK_GL_ERROR("Failed to get the number of extensions");
    m_ExtensionStrings.reserve(NumExtensions);
    for (int Ext = 0; Ext < NumExtensions; ++Ext)
    {
        const GLubyte* CurrExtension = glGetStringi(GL_EXTENSIONS, Ext);
        CHECK_GL_ERROR("Failed to get extension string #", Ext);
        m_ExtensionStrings.emplace(reinterpret_cast<const Char*>(CurrExtension));
    }

#if GL_KHR_debug
    if (EngineCI.EnableValidation && glDebugMessageCallback != nullptr)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(openglCallbackFunction, &m_ShowDebugGLOutput);
        if (glDebugMessageControl != nullptr)
        {
            glDebugMessageControl(
                GL_DONT_CARE, // Source of debug messages to enable or disable
                GL_DONT_CARE, // Type of debug messages to enable or disable
                GL_DONT_CARE, // Severity of debug messages to enable or disable
                0,            // The length of the array ids
                nullptr,      // Array of unsigned integers containing the ids of the messages to enable or disable
                GL_TRUE       // Flag determining whether the selected messages should be enabled or disabled
            );

            // Disable messages from glPushDebugGroup and glDebugMessageInsert
            glDebugMessageControl(
                GL_DEBUG_SOURCE_APPLICATION, // Source of debug messages to enable or disable
                GL_DONT_CARE,                // Type of debug messages to enable or disable
                GL_DONT_CARE,                // Severity of debug messages to enable or disable
                0,                           // The length of the array ids
                nullptr,                     // Array of unsigned integers containing the ids of the messages to enable or disable
                GL_FALSE                     // Flag determining whether the selected messages should be enabled or disabled
            );
        }
        if (glGetError() != GL_NO_ERROR)
            LOG_ERROR_MESSAGE("Failed to enable debug messages");
    }
#endif

#if PLATFORM_WIN32 || PLATFORM_LINUX || PLATFORM_MACOS
    if (m_DeviceInfo.APIVersion >= Version{4, 6} || CheckExtension("GL_ARB_ES3_compatibility"))
    {
        glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
        if (glGetError() != GL_NO_ERROR)
            LOG_ERROR_MESSAGE("Failed to enable primitive restart fixed index");
    }
    else
    {
        glEnable(GL_PRIMITIVE_RESTART);
        if (glGetError() == GL_NO_ERROR)
        {
            glPrimitiveRestartIndex(0xFFFFFFFFu);
            if (glGetError() != GL_NO_ERROR)
                LOG_ERROR_MESSAGE("Failed to set the primitive restart index");
        }
        else
        {
            LOG_ERROR_MESSAGE("Failed to enable primitive restart");
        }
    }

    {
        // In all APIs except for OpenGL, the first primitive vertex is the provoking vertex
        // for flat shading. In OpenGL, the last vertex is the provoking vertex by default.
        // Make the behavior consistent across all APIs
        glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
        if (glGetError() != GL_NO_ERROR)
            LOG_ERROR_MESSAGE("Failed to set provoking vertex convention to GL_FIRST_VERTEX_CONVENTION");
    }
#endif

    InitAdapterInfo();

    // Enable requested device features
    m_DeviceInfo.Features = EnableDeviceFeatures(m_AdapterInfo.Features, EngineCI.Features);
    if (m_AdapterInfo.Features.SeparablePrograms && !EngineCI.Features.SeparablePrograms)
    {
        VERIFY_EXPR(!m_DeviceInfo.Features.SeparablePrograms);
        LOG_INFO_MESSAGE("Disabling separable shader programs");
    }
    m_DeviceInfo.Features.ShaderResourceQueries = m_DeviceInfo.Features.SeparablePrograms;
    m_DeviceInfo.Features.GeometryShaders       = std::min(m_DeviceInfo.Features.SeparablePrograms, m_DeviceInfo.Features.GeometryShaders);
    m_DeviceInfo.Features.Tessellation          = std::min(m_DeviceInfo.Features.SeparablePrograms, m_DeviceInfo.Features.Tessellation);

    FlagSupportedTexFormats();

    if (EngineCI.ZeroToOneNDZ && (CheckExtension("GL_ARB_clip_control") || CheckExtension("GL_EXT_clip_control")))
    {
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        m_DeviceInfo.NDC = NDCAttribs{0.0f, 1.0f, 0.5f};
    }
    else
    {
        m_DeviceInfo.NDC = NDCAttribs{-1.0f, 0.5f, 0.5f};
    }

    if (m_GLCaps.FramebufferSRGB)
    {
        // When GL_FRAMEBUFFER_SRGB is enabled, and if the destination image is in the sRGB colorspace
        // then OpenGL will assume the shader's output is in the linear RGB colorspace. It will therefore
        // convert the output from linear RGB to sRGB.
        // Any writes to images that are not in the sRGB format should not be affected.
        // Thus this setting should be just set once and left that way
        glEnable(GL_FRAMEBUFFER_SRGB);
        if (glGetError() != GL_NO_ERROR)
        {
            LOG_ERROR_MESSAGE("Failed to enable SRGB framebuffers");
            m_GLCaps.FramebufferSRGB = false;
        }
    }

#if PLATFORM_WIN32 || PLATFORM_LINUX || PLATFORM_MACOS
    if (m_GLCaps.SemalessCubemaps)
    {
        // Under the standard filtering rules for cubemaps, filtering does not work across faces of the cubemap.
        // This results in a seam across the faces of a cubemap. This was a hardware limitation in the past, but
        // modern hardware is capable of interpolating across a cube face boundary.
        // GL_TEXTURE_CUBE_MAP_SEAMLESS is not defined in OpenGLES
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        if (glGetError() != GL_NO_ERROR)
        {
            LOG_ERROR_MESSAGE("Failed to enable seamless cubemap filtering");
            m_GLCaps.SemalessCubemaps = false;
        }
    }
#endif

    // get device limits
    {
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &m_DeviceLimits.MaxUniformBlocks);
        CHECK_GL_ERROR("glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS) failed");

        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &m_DeviceLimits.MaxTextureUnits);
        CHECK_GL_ERROR("glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS) failed");

        if (m_AdapterInfo.Features.ComputeShaders)
        {
#if GL_ARB_shader_storage_buffer_object
            glGetIntegerv(GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS, &m_DeviceLimits.MaxStorageBlock);
            CHECK_GL_ERROR("glGetIntegerv(GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS) failed");
#endif
#if GL_ARB_shader_image_load_store
            glGetIntegerv(GL_MAX_IMAGE_UNITS, &m_DeviceLimits.MaxImagesUnits);
            CHECK_GL_ERROR("glGetIntegerv(GL_MAX_IMAGE_UNITS) failed");
#endif
        }
    }

    if (m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GL)
        m_DeviceInfo.MaxShaderVersion.GLSL = m_DeviceInfo.APIVersion;
    else
        m_DeviceInfo.MaxShaderVersion.GLESSL = m_DeviceInfo.APIVersion;

#if !DILIGENT_NO_HLSL
    m_DeviceInfo.MaxShaderVersion.HLSL = {5, 0};
#endif

#if GL_KHR_parallel_shader_compile
    if (m_DeviceInfo.Features.AsyncShaderCompilation)
    {
        glMaxShaderCompilerThreadsKHR(EngineCI.NumAsyncShaderCompilationThreads);
    }
#endif
}

RenderDeviceGLImpl::~RenderDeviceGLImpl()
{
}

IMPLEMENT_QUERY_INTERFACE(RenderDeviceGLImpl, IID_RenderDeviceGL, TRenderDeviceBase)

void RenderDeviceGLImpl::CreateBuffer(const BufferDesc& BuffDesc, const BufferData* pBuffData, IBuffer** ppBuffer, bool bIsDeviceInternal)
{
    RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = GetImmediateContext(0);
    VERIFY(pDeviceContext, "Immediate device context has been destroyed");
    CreateBufferImpl(ppBuffer, BuffDesc, std::ref(pDeviceContext->GetContextState()), pBuffData, bIsDeviceInternal);
}

void RenderDeviceGLImpl::CreateBuffer(const BufferDesc& BuffDesc, const BufferData* BuffData, IBuffer** ppBuffer)
{
    CreateBuffer(BuffDesc, BuffData, ppBuffer, false);
}

void RenderDeviceGLImpl::CreateBufferFromGLHandle(Uint32 GLHandle, const BufferDesc& BuffDesc, RESOURCE_STATE InitialState, IBuffer** ppBuffer)
{
    DEV_CHECK_ERR(GLHandle != 0, "GL buffer handle must not be null");

    RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = GetImmediateContext(0);
    VERIFY(pDeviceContext, "Immediate device context has been destroyed");
    CreateBufferImpl(ppBuffer, BuffDesc, std::ref(pDeviceContext->GetContextState()), GLHandle, /*bIsDeviceInternal =*/false);
}

void RenderDeviceGLImpl::CreateShader(const ShaderCreateInfo& ShaderCreateInfo,
                                      IShader**               ppShader,
                                      IDataBlob**             ppCompilerOutput,
                                      bool                    bIsDeviceInternal)
{
    const ShaderGLImpl::CreateInfo GLShaderCI{
        GetDeviceInfo(),
        GetAdapterInfo(),
        ppCompilerOutput,
    };
    CreateShaderImpl(ppShader, ShaderCreateInfo, GLShaderCI, bIsDeviceInternal);
}

void RenderDeviceGLImpl::CreateShader(const ShaderCreateInfo& ShaderCreateInfo,
                                      IShader**               ppShader,
                                      IDataBlob**             ppCompilerOutput)
{
    CreateShader(ShaderCreateInfo, ppShader, ppCompilerOutput, false);
}

void RenderDeviceGLImpl::CreateTexture(const TextureDesc& TexDesc, const TextureData* pData, ITexture** ppTexture, bool bIsDeviceInternal)
{
    CreateDeviceObject(
        "texture", TexDesc, ppTexture,
        [&]() //
        {
            RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = GetImmediateContext(0);
            VERIFY(pDeviceContext, "Immediate device context has been destroyed");
            GLContextState& GLState = pDeviceContext->GetContextState();

            const TextureFormatInfo& FmtInfo = GetTextureFormatInfo(TexDesc.Format);
            if (!FmtInfo.Supported)
            {
                LOG_ERROR_AND_THROW(FmtInfo.Name, " is not supported texture format");
            }

            TextureBaseGL* pTextureOGL = nullptr;
            switch (TexDesc.Type)
            {
                case RESOURCE_DIM_TEX_1D:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture1D_GL instance", Texture1D_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, pData, bIsDeviceInternal);
                    break;

                case RESOURCE_DIM_TEX_1D_ARRAY:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture1DArray_GL instance", Texture1DArray_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, pData, bIsDeviceInternal);
                    break;

                case RESOURCE_DIM_TEX_2D:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture2D_GL instance", Texture2D_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, pData, bIsDeviceInternal);
                    break;

                case RESOURCE_DIM_TEX_2D_ARRAY:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture2DArray_GL instance", Texture2DArray_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, pData, bIsDeviceInternal);
                    break;

                case RESOURCE_DIM_TEX_3D:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture3D_GL instance", Texture3D_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, pData, bIsDeviceInternal);
                    break;

                case RESOURCE_DIM_TEX_CUBE:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "TextureCube_GL instance", TextureCube_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, pData, bIsDeviceInternal);
                    break;

                case RESOURCE_DIM_TEX_CUBE_ARRAY:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "TextureCubeArray_GL instance", TextureCubeArray_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, pData, bIsDeviceInternal);
                    break;

                default: LOG_ERROR_AND_THROW("Unknown texture type. (Did you forget to initialize the Type member of TextureDesc structure?)");
            }

            pTextureOGL->QueryInterface(IID_Texture, reinterpret_cast<IObject**>(ppTexture));
            pTextureOGL->CreateDefaultViews();
        } //
    );
}

void RenderDeviceGLImpl::CreateTexture(const TextureDesc& TexDesc, const TextureData* Data, ITexture** ppTexture)
{
    CreateTexture(TexDesc, Data, ppTexture, false);
}

void RenderDeviceGLImpl::CreateTextureFromGLHandle(Uint32             GLHandle,
                                                   Uint32             GLBindTarget,
                                                   const TextureDesc& TexDesc,
                                                   RESOURCE_STATE     InitialState,
                                                   ITexture**         ppTexture)
{
    VERIFY(GLHandle, "GL texture handle must not be null");
    CreateDeviceObject(
        "texture", TexDesc, ppTexture,
        [&]() //
        {
            RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = GetImmediateContext(0);
            VERIFY(pDeviceContext, "Immediate device context has been destroyed");
            GLContextState& GLState = pDeviceContext->GetContextState();

            TextureBaseGL* pTextureOGL = nullptr;
            switch (TexDesc.Type)
            {
                case RESOURCE_DIM_TEX_1D:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture1D_GL instance", Texture1D_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, GLHandle, GLBindTarget);
                    break;

                case RESOURCE_DIM_TEX_1D_ARRAY:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture1DArray_GL instance", Texture1DArray_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, GLHandle, GLBindTarget);
                    break;

                case RESOURCE_DIM_TEX_2D:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture2D_GL instance", Texture2D_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, GLHandle, GLBindTarget);
                    break;

                case RESOURCE_DIM_TEX_2D_ARRAY:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture2DArray_GL instance", Texture2DArray_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, GLHandle, GLBindTarget);
                    break;

                case RESOURCE_DIM_TEX_3D:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Texture3D_GL instance", Texture3D_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, GLHandle, GLBindTarget);
                    break;

                case RESOURCE_DIM_TEX_CUBE:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "TextureCube_GL instance", TextureCube_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, GLHandle, GLBindTarget);
                    break;

                case RESOURCE_DIM_TEX_CUBE_ARRAY:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "TextureCubeArray_GL instance", TextureCubeArray_GL)(m_TexViewObjAllocator, this, GLState, TexDesc, GLHandle, GLBindTarget);
                    break;

                default: LOG_ERROR_AND_THROW("Unknown texture type. (Did you forget to initialize the Type member of TextureDesc structure?)");
            }

            pTextureOGL->QueryInterface(IID_Texture, reinterpret_cast<IObject**>(ppTexture));
            pTextureOGL->CreateDefaultViews();
        } //
    );
}

void RenderDeviceGLImpl::CreateDummyTexture(const TextureDesc& TexDesc, RESOURCE_STATE InitialState, ITexture** ppTexture)
{
    CreateDeviceObject(
        "texture", TexDesc, ppTexture,
        [&]() //
        {
            TextureBaseGL* pTextureOGL = nullptr;
            switch (TexDesc.Type)
            {
                case RESOURCE_DIM_TEX_2D:
                    pTextureOGL = NEW_RC_OBJ(m_TexObjAllocator, "Dummy Texture2D_GL instance", Texture2D_GL)(m_TexViewObjAllocator, this, TexDesc);
                    break;

                default: LOG_ERROR_AND_THROW("Unsupported texture type.");
            }

            pTextureOGL->QueryInterface(IID_Texture, reinterpret_cast<IObject**>(ppTexture));
            pTextureOGL->CreateDefaultViews();
        } //
    );
}

void RenderDeviceGLImpl::CreateSampler(const SamplerDesc& SamplerDesc, ISampler** ppSampler, bool bIsDeviceInternal)
{
    CreateSamplerImpl(ppSampler, SamplerDesc, bIsDeviceInternal);
}

void RenderDeviceGLImpl::CreateSampler(const SamplerDesc& SamplerDesc, ISampler** ppSampler)
{
    CreateSampler(SamplerDesc, ppSampler, false);
}

void RenderDeviceGLImpl::CreateGraphicsPipelineState(const GraphicsPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState, bool bIsDeviceInternal)
{
    CreatePipelineStateImpl(ppPipelineState, PSOCreateInfo, bIsDeviceInternal);
}

void RenderDeviceGLImpl::CreateComputePipelineState(const ComputePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState, bool bIsDeviceInternal)
{
    CreatePipelineStateImpl(ppPipelineState, PSOCreateInfo, bIsDeviceInternal);
}

void RenderDeviceGLImpl::CreateGraphicsPipelineState(const GraphicsPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState)
{
    CreatePipelineStateImpl(ppPipelineState, PSOCreateInfo, false);
}

void RenderDeviceGLImpl::CreateComputePipelineState(const ComputePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState)
{
    CreatePipelineStateImpl(ppPipelineState, PSOCreateInfo, false);
}

void RenderDeviceGLImpl::CreateRayTracingPipelineState(const RayTracingPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipelineState)
{
    UNSUPPORTED("Ray tracing is not supported in OpenGL");
    *ppPipelineState = nullptr;
}

void RenderDeviceGLImpl::CreateFence(const FenceDesc& Desc, IFence** ppFence)
{
    CreateFenceImpl(ppFence, Desc);
}

void RenderDeviceGLImpl::CreateQuery(const QueryDesc& Desc, IQuery** ppQuery)
{
    CreateQueryImpl(ppQuery, Desc);
}

void RenderDeviceGLImpl::CreateRenderPass(const RenderPassDesc& Desc, IRenderPass** ppRenderPass)
{
    CreateRenderPassImpl(ppRenderPass, Desc);
}

void RenderDeviceGLImpl::CreateFramebuffer(const FramebufferDesc& Desc, IFramebuffer** ppFramebuffer)
{
    RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = GetImmediateContext(0);
    VERIFY(pDeviceContext, "Immediate device context has been destroyed");
    GLContextState& GLState = pDeviceContext->GetContextState();

    CreateFramebufferImpl(ppFramebuffer, Desc, std::ref(GLState));
}

void RenderDeviceGLImpl::CreatePipelineResourceSignature(const PipelineResourceSignatureDesc& Desc,
                                                         IPipelineResourceSignature**         ppSignature)
{
    CreatePipelineResourceSignature(Desc, ppSignature, SHADER_TYPE_UNKNOWN, false);
}

void RenderDeviceGLImpl::CreatePipelineResourceSignature(const PipelineResourceSignatureDesc& Desc,
                                                         IPipelineResourceSignature**         ppSignature,
                                                         SHADER_TYPE                          ShaderStages,
                                                         bool                                 IsDeviceInternal)
{
    CreatePipelineResourceSignatureImpl(ppSignature, Desc, ShaderStages, IsDeviceInternal);
}

void RenderDeviceGLImpl::CreatePipelineResourceSignature(const PipelineResourceSignatureDesc&           Desc,
                                                         const PipelineResourceSignatureInternalDataGL& InternalData,
                                                         IPipelineResourceSignature**                   ppSignature)
{
    CreatePipelineResourceSignatureImpl(ppSignature, Desc, InternalData);
}

void RenderDeviceGLImpl::CreateBLAS(const BottomLevelASDesc& Desc,
                                    IBottomLevelAS**         ppBLAS)
{
    UNSUPPORTED("CreateBLAS is not supported in OpenGL");
    *ppBLAS = nullptr;
}

void RenderDeviceGLImpl::CreateTLAS(const TopLevelASDesc& Desc,
                                    ITopLevelAS**         ppTLAS)
{
    UNSUPPORTED("CreateTLAS is not supported in OpenGL");
    *ppTLAS = nullptr;
}

void RenderDeviceGLImpl::CreateSBT(const ShaderBindingTableDesc& Desc,
                                   IShaderBindingTable**         ppSBT)
{
    UNSUPPORTED("CreateSBT is not supported in OpenGL");
    *ppSBT = nullptr;
}

void RenderDeviceGLImpl::CreateDeviceMemory(const DeviceMemoryCreateInfo& CreateInfo, IDeviceMemory** ppMemory)
{
    UNSUPPORTED("CreateDeviceMemory is not supported in OpenGL");
    *ppMemory = nullptr;
}

void RenderDeviceGLImpl::CreatePipelineStateCache(const PipelineStateCacheCreateInfo& CreateInfo,
                                                  IPipelineStateCache**               ppPSOCache)
{
    *ppPSOCache = nullptr;
}

void RenderDeviceGLImpl::CreateDeferredContext(IDeviceContext** ppContext)
{
    LOG_ERROR_MESSAGE("Deferred contexts are not supported in OpenGL backend.");
    *ppContext = nullptr;
}

SparseTextureFormatInfo RenderDeviceGLImpl::GetSparseTextureFormatInfo(TEXTURE_FORMAT     TexFormat,
                                                                       RESOURCE_DIMENSION Dimension,
                                                                       Uint32             SampleCount) const
{
    UNSUPPORTED("GetSparseTextureFormatInfo is not supported in OpenGL");
    return {};
}

bool RenderDeviceGLImpl::CheckExtension(const Char* ExtensionString) const
{
    return m_ExtensionStrings.find(ExtensionString) != m_ExtensionStrings.end();
}

void RenderDeviceGLImpl::InitAdapterInfo()
{
    const Version GLVersion = m_DeviceInfo.APIVersion;

    // Set graphics adapter properties
    {
        const std::string glstrVendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const std::string glstrRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const std::string Vendor        = StrToLower(glstrVendor);
        LOG_INFO_MESSAGE("GPU Vendor: ", Vendor);
        LOG_INFO_MESSAGE("GPU Renderer: ", glstrRenderer);

        for (size_t i = 0; i < _countof(m_AdapterInfo.Description) - 1 && i < glstrRenderer.length(); ++i)
            m_AdapterInfo.Description[i] = glstrRenderer[i];

        m_AdapterInfo.Type       = ADAPTER_TYPE_UNKNOWN;
        m_AdapterInfo.VendorId   = 0;
        m_AdapterInfo.DeviceId   = 0;
        m_AdapterInfo.NumOutputs = 0;

        if (Vendor.find("intel") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_INTEL;
        else if (Vendor.find("nvidia") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_NVIDIA;
        else if (Vendor.find("ati") != std::string::npos ||
                 Vendor.find("amd") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_AMD;
        else if (Vendor.find("qualcomm") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_QUALCOMM;
        else if (Vendor.find("arm") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_ARM;
        else if (Vendor.find("microsoft") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_MSFT;
        else if (Vendor.find("apple") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_APPLE;
        else if (Vendor.find("mesa") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_MESA;
        else if (Vendor.find("broadcom") != std::string::npos)
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_BROADCOM;
        else
            m_AdapterInfo.Vendor = ADAPTER_VENDOR_UNKNOWN;
    }

    // Set memory properties
    {
        AdapterMemoryInfo& Mem = m_AdapterInfo.Memory;

        switch (m_AdapterInfo.Vendor)
        {
            case ADAPTER_VENDOR_NVIDIA:
            {
#ifndef GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX
                static constexpr GLenum GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX = 0x9048;
#endif

                GLint AvailableMemoryKb = 0;
                glGetIntegerv(GL_GPU_MEM_INFO_TOTAL_AVAILABLE_MEM_NVX, &AvailableMemoryKb);
                if (glGetError() == GL_NO_ERROR)
                {
                    Mem.LocalMemory = static_cast<Uint64>(AvailableMemoryKb) * Uint64{1024};
                }
                else
                {
                    LOG_WARNING_MESSAGE("Unable to read available memory size for NVidia GPU");
                }
            }
            break;

            case ADAPTER_VENDOR_AMD:
            {
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
                static constexpr GLenum GL_TEXTURE_FREE_MEMORY_ATI = 0x87FC;
#endif
                // https://www.khronos.org/registry/OpenGL/extensions/ATI/ATI_meminfo.txt
                // param[0] - total memory free in the pool
                // param[1] - largest available free block in the pool
                // param[2] - total auxiliary memory free
                // param[3] - largest auxiliary free block
                GLint MemoryParamsKb[4] = {};

                glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, MemoryParamsKb);
                if (glGetError() == GL_NO_ERROR)
                {
                    Mem.LocalMemory = static_cast<Uint64>(MemoryParamsKb[0]) * Uint64{1024};
                }
                else
                {
                    LOG_WARNING_MESSAGE("Unable to read free memory size for AMD GPU");
                }
            }
            break;

            default:
                // No way to get memory info
                break;
        }
    }

    // Enable features and set properties
    {
#define ENABLE_FEATURE(FeatureName, Supported) \
    Features.FeatureName = (Supported) ? DEVICE_FEATURE_STATE_ENABLED : DEVICE_FEATURE_STATE_DISABLED;

        DeviceFeatures& Features = m_AdapterInfo.Features;

        GLint MaxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &MaxTextureSize);
        CHECK_GL_ERROR("Failed to get maximum texture size");

        GLint Max3DTextureSize = 0;
        glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &Max3DTextureSize);
        CHECK_GL_ERROR("Failed to get maximum 3d texture size");

        GLint MaxCubeTextureSize = 0;
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &MaxCubeTextureSize);
        CHECK_GL_ERROR("Failed to get maximum cubemap texture size");

        GLint MaxLayers = 0;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &MaxLayers);
        CHECK_GL_ERROR("Failed to get maximum number of texture array layers");

        Features.MeshShaders                 = DEVICE_FEATURE_STATE_DISABLED;
        Features.RayTracing                  = DEVICE_FEATURE_STATE_DISABLED;
        Features.ShaderResourceStaticArrays  = DEVICE_FEATURE_STATE_ENABLED;
        Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_DISABLED;
        Features.InstanceDataStepRate        = DEVICE_FEATURE_STATE_ENABLED;
        Features.NativeFence                 = DEVICE_FEATURE_STATE_DISABLED;
        Features.TileShaders                 = DEVICE_FEATURE_STATE_DISABLED;
        Features.SubpassFramebufferFetch     = DEVICE_FEATURE_STATE_DISABLED;
        Features.TextureComponentSwizzle     = DEVICE_FEATURE_STATE_DISABLED;

        {
            bool WireframeFillSupported = (glPolygonMode != nullptr);
            if (WireframeFillSupported)
            {
                // Test glPolygonMode() function to check if it fails
                // (It does fail on NVidia Shield tablet, but works fine
                // on Intel hw)
                VERIFY(glGetError() == GL_NO_ERROR, "Unhandled gl error encountered");
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                if (glGetError() != GL_NO_ERROR)
                    WireframeFillSupported = false;
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                if (glGetError() != GL_NO_ERROR)
                    WireframeFillSupported = false;
            }
            ENABLE_FEATURE(WireframeFill, WireframeFillSupported);
        }

        {
            GLint MaxVertexSSBOs = 0;
#if GL_ARB_shader_storage_buffer_object
            bool IsGL43OrAbove   = (m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GL) && (GLVersion >= Version{4, 3});
            bool IsGLES31OrAbove = (m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES) && (GLVersion >= Version{3, 1});
            if (IsGL43OrAbove || IsGLES31OrAbove)
            {
                glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &MaxVertexSSBOs);
                CHECK_GL_ERROR("glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS)");
            }
#endif
            ENABLE_FEATURE(VertexPipelineUAVWritesAndAtomics, MaxVertexSSBOs);
        }

        TextureProperties& TexProps = m_AdapterInfo.Texture;
        SamplerProperties& SamProps = m_AdapterInfo.Sampler;
        if (m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GL)
        {
            const bool IsGL46OrAbove = GLVersion >= Version{4, 6};
            const bool IsGL43OrAbove = GLVersion >= Version{4, 3};
            const bool IsGL42OrAbove = GLVersion >= Version{4, 2};
            const bool IsGL41OrAbove = GLVersion >= Version{4, 1};
            const bool IsGL40OrAbove = GLVersion >= Version{4, 0};

            // Separable programs may be disabled
            Features.SeparablePrograms = DEVICE_FEATURE_STATE_OPTIONAL;

            // clang-format off
            ENABLE_FEATURE(WireframeFill,                 true);
            ENABLE_FEATURE(MultithreadedResourceCreation, false);
            ENABLE_FEATURE(ComputeShaders,                IsGL43OrAbove || CheckExtension("GL_ARB_compute_shader"));
            ENABLE_FEATURE(GeometryShaders,               IsGL40OrAbove || CheckExtension("GL_ARB_geometry_shader4"));
            ENABLE_FEATURE(Tessellation,                  IsGL40OrAbove || CheckExtension("GL_ARB_tessellation_shader"));
            ENABLE_FEATURE(BindlessResources,             false);
            ENABLE_FEATURE(OcclusionQueries,              true);    // Present since 3.3
            ENABLE_FEATURE(BinaryOcclusionQueries,        true);    // Present since 3.3
            ENABLE_FEATURE(TimestampQueries,              true);    // Present since 3.3
            ENABLE_FEATURE(PipelineStatisticsQueries,     true);    // Present since 3.3
            ENABLE_FEATURE(DurationQueries,               true);    // Present since 3.3
            ENABLE_FEATURE(DepthBiasClamp,                false);   // There is no depth bias clamp in OpenGL
            ENABLE_FEATURE(DepthClamp,                    IsGL40OrAbove || CheckExtension("GL_ARB_depth_clamp"));
            ENABLE_FEATURE(IndependentBlend,              true);
            ENABLE_FEATURE(DualSourceBlend,               IsGL41OrAbove || CheckExtension("GL_ARB_blend_func_extended"));
            ENABLE_FEATURE(MultiViewport,                 IsGL41OrAbove || CheckExtension("GL_ARB_viewport_array"));
            ENABLE_FEATURE(PixelUAVWritesAndAtomics,      IsGL42OrAbove || CheckExtension("GL_ARB_shader_image_load_store"));
            ENABLE_FEATURE(TextureUAVExtendedFormats,     false);
            ENABLE_FEATURE(ShaderFloat16,                 CheckExtension("GL_EXT_shader_explicit_arithmetic_types_float16"));
            ENABLE_FEATURE(ResourceBuffer16BitAccess,     CheckExtension("GL_EXT_shader_16bit_storage"));
            ENABLE_FEATURE(UniformBuffer16BitAccess,      CheckExtension("GL_EXT_shader_16bit_storage"));
            ENABLE_FEATURE(ShaderInputOutput16,           false);
            ENABLE_FEATURE(ShaderInt8,                    CheckExtension("GL_EXT_shader_explicit_arithmetic_types_int8"));
            ENABLE_FEATURE(ResourceBuffer8BitAccess,      CheckExtension("GL_EXT_shader_8bit_storage"));
            ENABLE_FEATURE(UniformBuffer8BitAccess,       CheckExtension("GL_EXT_shader_8bit_storage"));
            ENABLE_FEATURE(TextureComponentSwizzle,       IsGL46OrAbove || CheckExtension("GL_ARB_texture_swizzle"));
            ENABLE_FEATURE(TextureSubresourceViews,       IsGL43OrAbove || CheckExtension("GL_ARB_texture_view"));
            ENABLE_FEATURE(NativeMultiDraw,               IsGL46OrAbove || CheckExtension("GL_ARB_shader_draw_parameters")); // Requirements for gl_DrawID
            ENABLE_FEATURE(AsyncShaderCompilation,        CheckExtension("GL_KHR_parallel_shader_compile"));
            ENABLE_FEATURE(FormattedBuffers,              IsGL40OrAbove);
            // clang-format on

            TexProps.MaxTexture1DDimension      = MaxTextureSize;
            TexProps.MaxTexture1DArraySlices    = MaxLayers;
            TexProps.MaxTexture2DDimension      = MaxTextureSize;
            TexProps.MaxTexture2DArraySlices    = MaxLayers;
            TexProps.MaxTexture3DDimension      = Max3DTextureSize;
            TexProps.MaxTextureCubeDimension    = MaxCubeTextureSize;
            TexProps.Texture2DMSSupported       = IsGL43OrAbove || CheckExtension("GL_ARB_texture_storage_multisample");
            TexProps.Texture2DMSArraySupported  = IsGL43OrAbove || CheckExtension("GL_ARB_texture_storage_multisample");
            TexProps.TextureViewSupported       = IsGL43OrAbove || CheckExtension("GL_ARB_texture_view");
            TexProps.CubemapArraysSupported     = IsGL43OrAbove || CheckExtension("GL_ARB_texture_cube_map_array");
            TexProps.TextureView2DOn3DSupported = TexProps.TextureViewSupported;
            ASSERT_SIZEOF(TexProps, 32, "Did you add a new member to TextureProperites? Please initialize it here.");

            SamProps.BorderSamplingModeSupported = True;
            if (IsGL46OrAbove || CheckExtension("GL_ARB_texture_filter_anisotropic"))
            {
                GLint MaxAnisotropy = 0;
                glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &MaxAnisotropy);
                CHECK_GL_ERROR("glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY)");
                SamProps.MaxAnisotropy = static_cast<Uint8>(MaxAnisotropy);
            }

            SamProps.LODBiasSupported = True;
            ASSERT_SIZEOF(SamProps, 3, "Did you add a new member to SamplerProperites? Please initialize it here.");

            m_GLCaps.FramebufferSRGB  = IsGL40OrAbove || CheckExtension("GL_ARB_framebuffer_sRGB");
            m_GLCaps.SemalessCubemaps = IsGL40OrAbove || CheckExtension("GL_ARB_seamless_cube_map");
        }
        else
        {
            VERIFY(m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES, "Unexpected device type: OpenGLES expected");

            const char* Extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            LOG_INFO_MESSAGE("Supported extensions: \n", Extensions);

            const bool IsGLES31OrAbove = GLVersion >= Version{3, 1};
            const bool IsGLES32OrAbove = GLVersion >= Version{3, 2};

            // Separable programs may be disabled
            Features.SeparablePrograms = (IsGLES31OrAbove || strstr(Extensions, "separate_shader_objects")) ? DEVICE_FEATURE_STATE_OPTIONAL : DEVICE_FEATURE_STATE_DISABLED;

            // clang-format off
            ENABLE_FEATURE(WireframeFill,                 false);
            ENABLE_FEATURE(MultithreadedResourceCreation, false);
            ENABLE_FEATURE(ComputeShaders,                IsGLES31OrAbove || strstr(Extensions, "compute_shader"));
            ENABLE_FEATURE(GeometryShaders,               IsGLES32OrAbove || strstr(Extensions, "geometry_shader"));
            ENABLE_FEATURE(Tessellation,                  IsGLES32OrAbove || strstr(Extensions, "tessellation_shader"));
            ENABLE_FEATURE(BindlessResources,             false);
            ENABLE_FEATURE(OcclusionQueries,              false);
            ENABLE_FEATURE(BinaryOcclusionQueries,        true); // Supported in GLES3.0
#if GL_TIMESTAMP
            const bool DisjointTimerQueriesSupported = strstr(Extensions, "disjoint_timer_query");
            ENABLE_FEATURE(TimestampQueries,          DisjointTimerQueriesSupported);
            ENABLE_FEATURE(DurationQueries,           DisjointTimerQueriesSupported);
#else
            ENABLE_FEATURE(TimestampQueries,          false);
            ENABLE_FEATURE(DurationQueries,           false);
#endif
            ENABLE_FEATURE(PipelineStatisticsQueries, false);
            ENABLE_FEATURE(DepthBiasClamp,            false); // There is no depth bias clamp in OpenGL
            ENABLE_FEATURE(DepthClamp,                strstr(Extensions, "depth_clamp"));
            ENABLE_FEATURE(IndependentBlend,          IsGLES32OrAbove);
            ENABLE_FEATURE(DualSourceBlend,           strstr(Extensions, "blend_func_extended"));
            ENABLE_FEATURE(MultiViewport,             strstr(Extensions, "viewport_array"));
            ENABLE_FEATURE(PixelUAVWritesAndAtomics,  IsGLES31OrAbove || strstr(Extensions, "shader_image_load_store"));
            ENABLE_FEATURE(TextureUAVExtendedFormats, false);

            ENABLE_FEATURE(ShaderFloat16,             strstr(Extensions, "shader_explicit_arithmetic_types_float16"));
            ENABLE_FEATURE(ResourceBuffer16BitAccess, strstr(Extensions, "shader_16bit_storage"));
            ENABLE_FEATURE(UniformBuffer16BitAccess,  strstr(Extensions, "shader_16bit_storage"));
            ENABLE_FEATURE(ShaderInputOutput16,       false);
            ENABLE_FEATURE(ShaderInt8,                strstr(Extensions, "shader_explicit_arithmetic_types_int8"));
            ENABLE_FEATURE(ResourceBuffer8BitAccess,  strstr(Extensions, "shader_8bit_storage"));
            ENABLE_FEATURE(UniformBuffer8BitAccess,   strstr(Extensions, "shader_8bit_storage"));
            ENABLE_FEATURE(TextureComponentSwizzle,   true);
            ENABLE_FEATURE(TextureSubresourceViews,   strstr(Extensions, "texture_view"));
            ENABLE_FEATURE(NativeMultiDraw,           strstr(Extensions, "multi_draw"));
            ENABLE_FEATURE(AsyncShaderCompilation,    strstr(Extensions, "parallel_shader_compile"));
            ENABLE_FEATURE(FormattedBuffers,          IsGLES32OrAbove);
            // clang-format on

            TexProps.MaxTexture1DDimension      = 0; // Not supported in GLES 3.2
            TexProps.MaxTexture1DArraySlices    = 0; // Not supported in GLES 3.2
            TexProps.MaxTexture2DDimension      = MaxTextureSize;
            TexProps.MaxTexture2DArraySlices    = MaxLayers;
            TexProps.MaxTexture3DDimension      = Max3DTextureSize;
            TexProps.MaxTextureCubeDimension    = MaxCubeTextureSize;
            TexProps.Texture2DMSSupported       = IsGLES31OrAbove || strstr(Extensions, "texture_storage_multisample");
            TexProps.Texture2DMSArraySupported  = IsGLES32OrAbove || strstr(Extensions, "texture_storage_multisample_2d_array");
            TexProps.TextureViewSupported       = IsGLES31OrAbove || strstr(Extensions, "texture_view");
            TexProps.CubemapArraysSupported     = IsGLES32OrAbove || strstr(Extensions, "texture_cube_map_array");
            TexProps.TextureView2DOn3DSupported = TexProps.TextureViewSupported;
            ASSERT_SIZEOF(TexProps, 32, "Did you add a new member to TextureProperites? Please initialize it here.");

            SamProps.BorderSamplingModeSupported = GL_TEXTURE_BORDER_COLOR && (IsGLES32OrAbove || strstr(Extensions, "texture_border_clamp"));
            if (strstr(Extensions, "texture_filter_anisotropic"))
            {
                GLint MaxAnisotropy = 0;
                glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &MaxAnisotropy);
                CHECK_GL_ERROR("glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY)");
                SamProps.MaxAnisotropy = static_cast<Uint8>(MaxAnisotropy);
            }
            SamProps.LODBiasSupported = GL_TEXTURE_LOD_BIAS && IsGLES31OrAbove;
            ASSERT_SIZEOF(SamProps, 3, "Did you add a new member to SamplerProperites? Please initialize it here.");

            m_GLCaps.FramebufferSRGB  = strstr(Extensions, "sRGB_write_control");
            m_GLCaps.SemalessCubemaps = false;
        }

#ifdef GL_KHR_shader_subgroup
        if (CheckExtension("GL_KHR_shader_subgroup"))
        {
            GLint SubgroupSize = 0;
            glGetIntegerv(GL_SUBGROUP_SIZE_KHR, &SubgroupSize);
            CHECK_GL_ERROR("glGetIntegerv(GL_SUBGROUP_SIZE_KHR)");

            GLint SubgroupStages = 0;
            glGetIntegerv(GL_SUBGROUP_SUPPORTED_STAGES_KHR, &SubgroupStages);
            CHECK_GL_ERROR("glGetIntegerv(GL_SUBGROUP_SUPPORTED_STAGES_KHR)");

            GLint SubgroupFeatures = 0;
            glGetIntegerv(GL_SUBGROUP_SUPPORTED_FEATURES_KHR, &SubgroupFeatures);
            CHECK_GL_ERROR("glGetIntegerv(GL_SUBGROUP_SUPPORTED_FEATURES_KHR)");

            {
                WaveOpProperties& WaveOpProps{m_AdapterInfo.WaveOp};
                WaveOpProps.MinSize         = static_cast<Uint32>(SubgroupSize);
                WaveOpProps.MaxSize         = static_cast<Uint32>(SubgroupSize);
                WaveOpProps.SupportedStages = GLShaderBitsToShaderTypes(SubgroupStages);
                WaveOpProps.Features        = GLSubgroupFeatureBitsToWaveFeatures(SubgroupFeatures);
                ASSERT_SIZEOF(WaveOpProps, 16, "Did you add a new member to WaveOpProperties? Please initialize it here.");
            }

            ENABLE_FEATURE(WaveOp, true);
        }
        else
#endif
        {
            ENABLE_FEATURE(WaveOp, false);
        }

        Features.ShaderResourceQueries = Features.SeparablePrograms;

        const bool bRGTC = CheckExtension("GL_EXT_texture_compression_rgtc") || CheckExtension("GL_ARB_texture_compression_rgtc");
        const bool bBPTC = CheckExtension("GL_EXT_texture_compression_bptc") || CheckExtension("GL_ARB_texture_compression_bptc");
        const bool bS3TC = CheckExtension("GL_EXT_texture_compression_s3tc") || CheckExtension("GL_WEBGL_compressed_texture_s3tc");
        ENABLE_FEATURE(TextureCompressionBC, bRGTC && bBPTC && bS3TC);

#if PLATFORM_WEB
        const bool bETC2 = CheckExtension("GL_WEBGL_compressed_texture_etc");
#else
        const bool bETC2 = m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES || CheckExtension("GL_ARB_ES3_compatibility");
#endif
        ENABLE_FEATURE(TextureCompressionETC2, bETC2);

        // Buffer properties
        {
            BufferProperties& BufferProps{m_AdapterInfo.Buffer};
            BufferProps.ConstantBufferOffsetAlignment   = 256;
            BufferProps.StructuredBufferOffsetAlignment = 16;
            ASSERT_SIZEOF(BufferProps, 8, "Did you add a new member to BufferProperites? Please initialize it here.");
        }
#undef ENABLE_FEATURE
    }

    // Compute shader properties
#if GL_ARB_compute_shader
    if (m_AdapterInfo.Features.ComputeShaders)
    {
        ComputeShaderProperties& CompProps{m_AdapterInfo.ComputeShader};
        glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, reinterpret_cast<GLint*>(&CompProps.SharedMemorySize));
        CHECK_GL_ERROR("glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE)");
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, reinterpret_cast<GLint*>(&CompProps.MaxThreadGroupInvocations));
        CHECK_GL_ERROR("glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS)");

        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, reinterpret_cast<GLint*>(&CompProps.MaxThreadGroupSizeX));
        CHECK_GL_ERROR("glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0)");
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, reinterpret_cast<GLint*>(&CompProps.MaxThreadGroupSizeY));
        CHECK_GL_ERROR("glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1)");
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, reinterpret_cast<GLint*>(&CompProps.MaxThreadGroupSizeZ));
        CHECK_GL_ERROR("glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2)");

        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, reinterpret_cast<GLint*>(&CompProps.MaxThreadGroupCountX));
        CHECK_GL_ERROR("glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0)");
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, reinterpret_cast<GLint*>(&CompProps.MaxThreadGroupCountY));
        CHECK_GL_ERROR("glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1)");
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, reinterpret_cast<GLint*>(&CompProps.MaxThreadGroupCountZ));
        CHECK_GL_ERROR("glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2)");

        ASSERT_SIZEOF(CompProps, 32, "Did you add a new member to ComputeShaderProperties? Please initialize it here.");
    }
#endif

    // Draw command properties
    {
        DrawCommandProperties& DrawCommandProps{m_AdapterInfo.DrawCommand};
        DrawCommandProps.MaxDrawIndirectCount = ~0u; // no limits
        DrawCommandProps.CapFlags             = DRAW_COMMAND_CAP_FLAG_NONE;
        if (m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GL)
        {
            DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT | DRAW_COMMAND_CAP_FLAG_BASE_VERTEX;

            // The baseInstance member of the DrawElementsIndirectCommand structure is defined only if the GL version is 4.2 or greater.
            if (GLVersion >= Version{4, 2})
                DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT_FIRST_INSTANCE;

            if (GLVersion >= Version{4, 3} || CheckExtension("GL_ARB_multi_draw_indirect"))
                DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_NATIVE_MULTI_DRAW_INDIRECT;

            if (GLVersion >= Version{4, 6} || CheckExtension("GL_ARB_indirect_parameters"))
                DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT_COUNTER_BUFFER;

            // Always 2^32-1 on desktop
            DrawCommandProps.MaxIndexValue = ~Uint32{0};
        }
        else if (m_DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES)
        {
            const char* Extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            if (GLVersion >= Version{3, 1} || strstr(Extensions, "draw_indirect"))
                DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT;

            if (GLVersion >= Version{3, 2} || strstr(Extensions, "draw_elements_base_vertex"))
                DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_BASE_VERTEX;

            if (strstr(Extensions, "base_instance"))
                DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT_FIRST_INSTANCE;

            if (strstr(Extensions, "multi_draw_indirect"))
                DrawCommandProps.CapFlags |= DRAW_COMMAND_CAP_FLAG_NATIVE_MULTI_DRAW_INDIRECT | DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT_COUNTER_BUFFER;

            DrawCommandProps.MaxIndexValue = 0;
            glGetIntegerv(GL_MAX_ELEMENT_INDEX, reinterpret_cast<GLint*>(&DrawCommandProps.MaxIndexValue));
            if (glGetError() != GL_NO_ERROR)
            {
                // Note that on desktop, GL_MAX_ELEMENT_INDEX was added only in 4.3 and always returns 2^32-1
                LOG_ERROR_MESSAGE("glGetIntegerv(GL_MAX_ELEMENT_INDEX) failed");
                DrawCommandProps.MaxIndexValue = (1u << 24) - 1; // Guaranteed by the spec
            }
        }

        ASSERT_SIZEOF(DrawCommandProps, 12, "Did you add a new member to DrawCommandProperties? Please initialize it here.");
    }

    // Set queue info
    {
        m_AdapterInfo.NumQueues = 1;

        m_AdapterInfo.Queues[0].QueueType                 = COMMAND_QUEUE_TYPE_GRAPHICS;
        m_AdapterInfo.Queues[0].MaxDeviceContexts         = 1;
        m_AdapterInfo.Queues[0].TextureCopyGranularity[0] = 1;
        m_AdapterInfo.Queues[0].TextureCopyGranularity[1] = 1;
        m_AdapterInfo.Queues[0].TextureCopyGranularity[2] = 1;
    }

    ASSERT_SIZEOF(DeviceFeatures, 47, "Did you add a new feature to DeviceFeatures? Please handle its status here.");
}

void RenderDeviceGLImpl::FlagSupportedTexFormats()
{
    const RenderDeviceInfo& DeviceInfo     = GetDeviceInfo();
    const bool              bDekstopGL     = DeviceInfo.Type == RENDER_DEVICE_TYPE_GL;
    const bool              bGL430OrAbove  = DeviceInfo.Type == RENDER_DEVICE_TYPE_GL && DeviceInfo.APIVersion >= Version{4, 3};
    const bool              bGLES30OrAbove = DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= Version{3, 0};
    const bool              bGLES31OrAbove = DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= Version{3, 1};

    const bool bRGTC       = CheckExtension("GL_EXT_texture_compression_rgtc") || CheckExtension("GL_ARB_texture_compression_rgtc");
    const bool bBPTC       = CheckExtension("GL_EXT_texture_compression_bptc") || CheckExtension("GL_ARB_texture_compression_bptc");
    const bool bS3TC       = CheckExtension("GL_EXT_texture_compression_s3tc") || CheckExtension("GL_WEBGL_compressed_texture_s3tc");
    const bool bTexNorm16  = bDekstopGL || CheckExtension("GL_EXT_texture_norm16"); // Only for ES3.1+
    const bool bTexSwizzle = bDekstopGL || bGLES30OrAbove || CheckExtension("GL_ARB_texture_swizzle");
    const bool bStencilTex = bGL430OrAbove || bGLES31OrAbove || CheckExtension("GL_ARB_stencil_texturing");

#if PLATFORM_WEB
    const bool bETC2 = CheckExtension("GL_WEBGL_compressed_texture_etc");
#else
    const bool bETC2 = bGLES30OrAbove || CheckExtension("GL_ARB_ES3_compatibility");
#endif

    //              ||   GLES3.0   ||            GLES3.1              ||            GLES3.2              ||
    // |   Format   ||  CR  |  TF  ||  CR  |  TF  | Req RB | Req. Tex ||  CR  |  TF  | Req RB | Req. Tex ||
    // |------------||------|------||------|------|--------|----------||------|------|--------|----------||
    // |     U8     ||  V   |  V   ||  V   |  V   |   V    |    V     ||  V   |  V   |   V    |    V     ||
    // |     S8     ||      |  V   ||      |  V   |        |    V     ||      |  V   |        |    V     ||
    // |  SRGBA8    ||  V   |  V   ||  V   |  V   |   V    |    V     ||  V   |  V   |   V    |    V     ||
    // |    UI8     ||  V   |      ||  V   |      |   V    |    V     ||  V   |      |   V    |    V     ||
    // |    SI8     ||  V   |      ||  V   |      |   V    |    V     ||  V   |      |   V    |    V     ||
    // |    U16     ||  -   |  -   ||  -   |  -   |   -    |    -     ||  -   |  -   |   -    |    -     ||
    // |    S16     ||  -   |  -   ||  -   |  -   |   -    |    -     ||  -   |  -   |   -    |    -     ||
    // |   UI16     ||  V   |      ||  V   |      |   V    |    V     ||  V   |      |   V    |    V     ||
    // |   SI16     ||  V   |      ||  V   |      |   V    |    V     ||  V   |      |   V    |    V     ||
    // |   UI32     ||  V   |      ||  V   |      |   V    |    V     ||  V   |      |   V    |    V     ||
    // |   SI32     ||  V   |      ||  V   |      |   V    |    V     ||  V   |      |   V    |    V     ||
    // |    F16     ||      |  V   ||      |  V   |        |    V     || +V   |  V   |  +V    |    V     ||
    // |    F32     ||      |      ||      |      |        |    V     || +V   |      |  +V    |    V     ||
    // |  RGB10A2   ||  V   |  V   ||  V   |  V   |   V    |    V     ||  V   |  V   |   V    |    V     ||
    // | RGB10A2UI  ||  V   |      ||  V   |      |   V    |    V     ||  V   |      |   V    |    V     ||
    // | R11G11B10F ||      |  V   ||      |  V   |        |    V     || +V   |  V   |   V    |    V     ||
    // |  RGB9_E5   ||      |      ||      |  V   |        |    V     ||      |  V   |        |    V     ||

    // CR (Color Renderable)          - texture can be used as color attachment
    // TF (Texture Filterable)        - texture can be filtered (mipmapping and minification/magnification filtering)
    // Req RB (Required Renderbuffer) - texture supports renderbuffer usage
    // Req. Tex (Required Texture)    - texture usage is supported

    static constexpr Version NotAvaiable = Version{~0u, ~0u};

    auto CheckBindFlagSupport = [&](BIND_FLAGS                     BindFlag,
                                    const Version&                 MinGLVersion,
                                    const Version&                 MinGLESVersion = Version{~0u, ~0u},
                                    const std::vector<const char*> Extensions     = {}) {
        if (DeviceInfo.Type == RENDER_DEVICE_TYPE_GL && DeviceInfo.APIVersion >= MinGLVersion)
            return BindFlag;

        if (DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= MinGLESVersion)
            return BindFlag;

        for (const char* Ext : Extensions)
        {
            if (CheckExtension(Ext))
                return BindFlag;
        }

        return BIND_NONE;
    };

    const BIND_FLAGS TexBindFlags =
        BIND_SHADER_RESOURCE |
        (m_DeviceInfo.Features.PixelUAVWritesAndAtomics ? BIND_UNORDERED_ACCESS : BIND_NONE);

    BIND_FLAGS U8BindFlags         = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS SRGBA8BindFlags     = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS S8BindFlags         = TexBindFlags | CheckBindFlagSupport(BIND_RENDER_TARGET, {4, 4}, NotAvaiable, {"GL_EXT_render_snorm"});
    BIND_FLAGS UI8BindFlags        = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS SI8BindFlags        = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS U16BindFlags        = TexBindFlags | CheckBindFlagSupport(BIND_RENDER_TARGET, {4, 0}, NotAvaiable, {"GL_EXT_texture_norm16"});
    BIND_FLAGS S16BindFlags        = TexBindFlags | CheckBindFlagSupport(BIND_RENDER_TARGET, {4, 4}, NotAvaiable, {"GL_EXT_render_snorm"});
    BIND_FLAGS UI16BindFlags       = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS SI16BindFlags       = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS UI32BindFlags       = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS SI32BindFlags       = TexBindFlags | BIND_RENDER_TARGET;
    BIND_FLAGS F16BindFlags        = TexBindFlags | CheckBindFlagSupport(BIND_RENDER_TARGET, {4, 0}, {3, 2}, {"GL_EXT_color_buffer_half_float"});
    BIND_FLAGS F32BindFlags        = TexBindFlags | CheckBindFlagSupport(BIND_RENDER_TARGET, {4, 0}, {3, 2}, {"GL_EXT_color_buffer_float"});
    BIND_FLAGS R11G11B10FBindFlags = TexBindFlags | CheckBindFlagSupport(BIND_RENDER_TARGET, {4, 0}, {3, 2});
    BIND_FLAGS BindSrvRtvUav       = TexBindFlags | BIND_RENDER_TARGET;

    auto FlagFormat = [this](TEXTURE_FORMAT Fmt, bool Supported, BIND_FLAGS BindFlags = BIND_NONE, bool Filterable = false) {
        TextureFormatInfoExt& FmtInfo = m_TextureFormatsInfo[Fmt];

        FmtInfo.Supported  = Supported;
        FmtInfo.BindFlags  = Supported ? BindFlags : BIND_NONE;
        FmtInfo.Filterable = Supported && Filterable;
    };

    // The formats marked by true below are required in GL 3.3+ and GLES 3.0+
    // Note that GLES2.0 does not specify any required formats

    // clang-format off
    //               Format                           Supported     BindFlags       Filterable
    FlagFormat(TEX_FORMAT_RGBA32_TYPELESS,            true                                      );
    FlagFormat(TEX_FORMAT_RGBA32_FLOAT,               true,         F32BindFlags,     bDekstopGL);
    FlagFormat(TEX_FORMAT_RGBA32_UINT,                true,         UI32BindFlags               );
    FlagFormat(TEX_FORMAT_RGBA32_SINT,                true,         SI32BindFlags               );
    FlagFormat(TEX_FORMAT_RGB32_TYPELESS,             true                                      );
    FlagFormat(TEX_FORMAT_RGB32_FLOAT,                true,         F32BindFlags,     bDekstopGL);
    FlagFormat(TEX_FORMAT_RGB32_SINT,                 true,         SI32BindFlags               );
    FlagFormat(TEX_FORMAT_RGB32_UINT,                 true,         UI32BindFlags               );
    FlagFormat(TEX_FORMAT_RGBA16_TYPELESS,            true                                      );
    FlagFormat(TEX_FORMAT_RGBA16_FLOAT,               true,         F16BindFlags,           true);
    FlagFormat(TEX_FORMAT_RGBA16_UNORM,               bTexNorm16,   U16BindFlags,           true);
    FlagFormat(TEX_FORMAT_RGBA16_UINT,                true,         UI16BindFlags               );
    FlagFormat(TEX_FORMAT_RGBA16_SNORM,               bTexNorm16,   S16BindFlags,           true);
    FlagFormat(TEX_FORMAT_RGBA16_SINT,                true,         SI16BindFlags               );
    FlagFormat(TEX_FORMAT_RG32_TYPELESS,              true                                      );
    FlagFormat(TEX_FORMAT_RG32_FLOAT,                 true,         F32BindFlags,     bDekstopGL);
    FlagFormat(TEX_FORMAT_RG32_SINT,                  true,         SI32BindFlags               );
    FlagFormat(TEX_FORMAT_RG32_UINT,                  true,         UI32BindFlags               );
    FlagFormat(TEX_FORMAT_R32G8X24_TYPELESS,          true                                      );
    FlagFormat(TEX_FORMAT_D32_FLOAT_S8X24_UINT,       true,         BIND_DEPTH_STENCIL          );
    FlagFormat(TEX_FORMAT_R32_FLOAT_X8X24_TYPELESS,   true,         TexBindFlags,     bDekstopGL);
    FlagFormat(TEX_FORMAT_X32_TYPELESS_G8X24_UINT,    bStencilTex,  BIND_SHADER_RESOURCE,  false);
    FlagFormat(TEX_FORMAT_RGB10A2_TYPELESS,           true                                      );
    FlagFormat(TEX_FORMAT_RGB10A2_UNORM,              true,         BindSrvRtvUav,          true);
    FlagFormat(TEX_FORMAT_RGB10A2_UINT,               true,         BindSrvRtvUav               );
    FlagFormat(TEX_FORMAT_R11G11B10_FLOAT,            true,         R11G11B10FBindFlags,    true);
    FlagFormat(TEX_FORMAT_RGBA8_TYPELESS,             true                                      );
    FlagFormat(TEX_FORMAT_RGBA8_UNORM,                true,         U8BindFlags,            true);
    FlagFormat(TEX_FORMAT_RGBA8_UNORM_SRGB,           true,         SRGBA8BindFlags,        true);
    FlagFormat(TEX_FORMAT_RGBA8_UINT,                 true,         UI8BindFlags                );
    FlagFormat(TEX_FORMAT_RGBA8_SNORM,                true,         S8BindFlags,            true);
    FlagFormat(TEX_FORMAT_RGBA8_SINT,                 true,         SI8BindFlags                );
    FlagFormat(TEX_FORMAT_RG16_TYPELESS,              true                                      );
    FlagFormat(TEX_FORMAT_RG16_FLOAT,                 true,         F16BindFlags,           true);
    FlagFormat(TEX_FORMAT_RG16_UNORM,                 bTexNorm16,   U16BindFlags,           true);
    FlagFormat(TEX_FORMAT_RG16_UINT,                  true,         UI16BindFlags               );
    FlagFormat(TEX_FORMAT_RG16_SNORM,                 bTexNorm16,   S16BindFlags,           true);
    FlagFormat(TEX_FORMAT_RG16_SINT,                  true,         SI16BindFlags               );
    FlagFormat(TEX_FORMAT_R32_TYPELESS,               true                                      );
    FlagFormat(TEX_FORMAT_D32_FLOAT,                  true,         BIND_DEPTH_STENCIL          );
    FlagFormat(TEX_FORMAT_R32_FLOAT,                  true,         F32BindFlags,     bDekstopGL);
    FlagFormat(TEX_FORMAT_R32_UINT,                   true,         UI32BindFlags               );
    FlagFormat(TEX_FORMAT_R32_SINT,                   true,         SI32BindFlags               );
    FlagFormat(TEX_FORMAT_R24G8_TYPELESS,             true                                      );
    FlagFormat(TEX_FORMAT_D24_UNORM_S8_UINT,          true,         BIND_DEPTH_STENCIL          );
    FlagFormat(TEX_FORMAT_R24_UNORM_X8_TYPELESS,      true,         TexBindFlags,           true);
    FlagFormat(TEX_FORMAT_X24_TYPELESS_G8_UINT,       bStencilTex,  BIND_SHADER_RESOURCE,  false);
    FlagFormat(TEX_FORMAT_RG8_TYPELESS,               true                                      );
    FlagFormat(TEX_FORMAT_RG8_UNORM,                  true,         U8BindFlags,            true);
    FlagFormat(TEX_FORMAT_RG8_UINT,                   true,         UI8BindFlags                );
    FlagFormat(TEX_FORMAT_RG8_SNORM,                  true,         S8BindFlags,            true);
    FlagFormat(TEX_FORMAT_RG8_SINT,                   true,         SI8BindFlags                );
    FlagFormat(TEX_FORMAT_R16_TYPELESS,               true                                      );
    FlagFormat(TEX_FORMAT_R16_FLOAT,                  true,         F16BindFlags,           true);
    FlagFormat(TEX_FORMAT_D16_UNORM,                  true,         BIND_DEPTH_STENCIL          );
    FlagFormat(TEX_FORMAT_R16_UNORM,                  bTexNorm16,   U16BindFlags,           true);
    FlagFormat(TEX_FORMAT_R16_UINT,                   true,         UI16BindFlags               );
    FlagFormat(TEX_FORMAT_R16_SNORM,                  bTexNorm16,   S16BindFlags,           true);
    FlagFormat(TEX_FORMAT_R16_SINT,                   true,         SI16BindFlags               );
    FlagFormat(TEX_FORMAT_R8_TYPELESS,                true                                      );
    FlagFormat(TEX_FORMAT_R8_UNORM,                   true,         U8BindFlags,            true);
    FlagFormat(TEX_FORMAT_R8_UINT,                    true,         UI8BindFlags                );
    FlagFormat(TEX_FORMAT_R8_SNORM,                   true,         S8BindFlags,            true);
    FlagFormat(TEX_FORMAT_R8_SINT,                    true,         SI8BindFlags                );
    FlagFormat(TEX_FORMAT_A8_UNORM,                   bTexSwizzle,  U8BindFlags,            true);
    FlagFormat(TEX_FORMAT_R1_UNORM,                   false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_RGB9E5_SHAREDEXP,           true,         BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_RG8_B8G8_UNORM,             false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_G8R8_G8B8_UNORM,            false                                     ); // Not supported in OpenGL

    FlagFormat(TEX_FORMAT_BC1_TYPELESS,               bS3TC                                     );
    FlagFormat(TEX_FORMAT_BC1_UNORM,                  bS3TC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC1_UNORM_SRGB,             bS3TC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC2_TYPELESS,               bS3TC                                     );
    FlagFormat(TEX_FORMAT_BC2_UNORM,                  bS3TC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC2_UNORM_SRGB,             bS3TC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC3_TYPELESS,               bS3TC                                     );
    FlagFormat(TEX_FORMAT_BC3_UNORM,                  bS3TC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC3_UNORM_SRGB,             bS3TC,        BIND_SHADER_RESOURCE,   true);

    FlagFormat(TEX_FORMAT_BC4_TYPELESS,               bRGTC                                     );
    FlagFormat(TEX_FORMAT_BC4_UNORM,                  bRGTC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC4_SNORM,                  bRGTC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC5_TYPELESS,               bRGTC                                     );
    FlagFormat(TEX_FORMAT_BC5_UNORM,                  bRGTC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC5_SNORM,                  bRGTC,        BIND_SHADER_RESOURCE,   true);

    FlagFormat(TEX_FORMAT_B5G6R5_UNORM,               false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_B5G5R5A1_UNORM,             false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_BGRA8_UNORM,                bTexSwizzle,  BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BGRX8_UNORM,                false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_BGRA8_TYPELESS,             false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_BGRA8_UNORM_SRGB,           false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_BGRX8_TYPELESS,             false                                     ); // Not supported in OpenGL
    FlagFormat(TEX_FORMAT_BGRX8_UNORM_SRGB,           false                                     ); // Not supported in OpenGL

    FlagFormat(TEX_FORMAT_BC6H_TYPELESS,              bBPTC);
    FlagFormat(TEX_FORMAT_BC6H_UF16,                  bBPTC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC6H_SF16,                  bBPTC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC7_TYPELESS,               bBPTC);
    FlagFormat(TEX_FORMAT_BC7_UNORM,                  bBPTC,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_BC7_UNORM_SRGB,             bBPTC,        BIND_SHADER_RESOURCE,   true);

    FlagFormat(TEX_FORMAT_ETC2_RGB8_UNORM,            bETC2,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_ETC2_RGB8_UNORM_SRGB,       bETC2,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_ETC2_RGB8A1_UNORM,          bETC2,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_ETC2_RGB8A1_UNORM_SRGB,     bETC2,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_ETC2_RGBA8_UNORM,           bETC2,        BIND_SHADER_RESOURCE,   true);
    FlagFormat(TEX_FORMAT_ETC2_RGBA8_UNORM_SRGB,      bETC2,        BIND_SHADER_RESOURCE,   true);
    // clang-format on

#ifdef DILIGENT_DEVELOPMENT
    const bool bGL43OrAbove = DeviceInfo.Type == RENDER_DEVICE_TYPE_GL && DeviceInfo.APIVersion >= Version{4, 3};

    constexpr int      TestTextureDim = 8;
    constexpr int      MaxTexelSize   = 16;
    std::vector<Uint8> ZeroData(TestTextureDim * TestTextureDim * MaxTexelSize);

    // Go through all formats and try to create small 2D texture to check if the format is supported
    for (TextureFormatInfoExt& FmtInfo : m_TextureFormatsInfo)
    {
        if (FmtInfo.Format == TEX_FORMAT_UNKNOWN)
            continue;

        GLenum GLFmt = TexFormatToGLInternalTexFormat(FmtInfo.Format);
        if (GLFmt == 0)
        {
            VERIFY(!FmtInfo.Supported, "Format should be marked as unsupported");
            continue;
        }

#    if GL_ARB_internalformat_query2
        // Only works on GL4.3+
        if (bGL43OrAbove)
        {
            GLint params = 0;
            glGetInternalformativ(GL_TEXTURE_2D, GLFmt, GL_INTERNALFORMAT_SUPPORTED, 1, &params);
            CHECK_GL_ERROR("glGetInternalformativ() failed");
            VERIFY(FmtInfo.Supported == (params == GL_TRUE), "This internal format should be supported");
        }
#    else
        (void)bGL43OrAbove; // To suppress warning
#    endif

        // Check that the format is indeed supported
        if (FmtInfo.Supported && !FmtInfo.IsDepthStencil() && !FmtInfo.IsTypeless)
        {
            GLObjectWrappers::GLTextureObj TestGLTex{true};
            // Immediate context has not been created yet, so use raw GL functions
            glBindTexture(GL_TEXTURE_2D, TestGLTex);
            CHECK_GL_ERROR("Failed to bind texture");
            glTexStorage2D(GL_TEXTURE_2D, 1, GLFmt, TestTextureDim, TestTextureDim);
            if (glGetError() == GL_NO_ERROR)
            {
                // It turned out it is not enough to only allocate texture storage
                // For some reason glTexStorage2D() may succeed, but upload operation
                // will later fail. So we need to additionally try to upload some
                // data to the texture
                const NativePixelAttribs& TransferAttribs = GetNativePixelTransferAttribs(FmtInfo.Format);
                if (TransferAttribs.IsCompressed)
                {
                    const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(FmtInfo.Format);
                    static_assert((TestTextureDim & (TestTextureDim - 1)) == 0, "Test texture dim must be power of two!");
                    int BlockBytesInRow = (TestTextureDim / int{FmtAttribs.BlockWidth}) * int{FmtAttribs.ComponentSize};
                    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, // mip level
                                              0, 0, TestTextureDim, TestTextureDim,
                                              GLFmt,
                                              (TestTextureDim / int{FmtAttribs.BlockHeight}) * BlockBytesInRow,
                                              ZeroData.data());
                }
                else
                {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, // mip level
                                    0, 0, TestTextureDim, TestTextureDim,
                                    TransferAttribs.PixelFormat, TransferAttribs.DataType,
                                    ZeroData.data());
                }

                if (glGetError() != GL_NO_ERROR)
                {
                    LOG_WARNING_MESSAGE("Failed to upload data to a test ", TestTextureDim, "x", TestTextureDim, " ", FmtInfo.Name,
                                        " texture. This likely indicates that the format is not supported despite being reported so by the device.");
                    FmtInfo.Supported = false;
                }
            }
            else
            {
                LOG_WARNING_MESSAGE("Failed to allocate storage for a test ", TestTextureDim, "x", TestTextureDim, " ", FmtInfo.Name,
                                    " texture. This likely indicates that the format is not supported despite being reported so by the device.");
                FmtInfo.Supported = false;
            }
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
#endif
}

template <typename CreateFuncType>
bool CreateTestGLTexture(GLContextState& GlCtxState, GLenum BindTarget, const GLObjectWrappers::GLTextureObj& GLTexObj, CreateFuncType CreateFunc)
{
    GlCtxState.BindTexture(-1, BindTarget, GLTexObj);
    CreateFunc();
    bool bSuccess = glGetError() == GL_NO_ERROR;
    GlCtxState.BindTexture(-1, BindTarget, GLObjectWrappers::GLTextureObj{false});
    return bSuccess;
}

template <typename CreateFuncType>
bool CreateTestGLTexture(GLContextState& GlCtxState, GLenum BindTarget, CreateFuncType CreateFunc)
{
    GLObjectWrappers::GLTextureObj GLTexObj{true};
    return CreateTestGLTexture(GlCtxState, BindTarget, GLTexObj, CreateFunc);
}

void RenderDeviceGLImpl::TestTextureFormat(TEXTURE_FORMAT TexFormat)
{
    TextureFormatInfoExt& TexFormatInfo = m_TextureFormatsInfo[TexFormat];
    VERIFY(TexFormatInfo.Supported, "Texture format is not supported");

    GLenum GLFmt = TexFormatToGLInternalTexFormat(TexFormat);
    VERIFY(GLFmt != 0, "Incorrect internal GL format");

    RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = GetImmediateContext(0);
    VERIFY(pDeviceContext, "Immediate device context has been destroyed");
    GLContextState& ContextState = pDeviceContext->GetContextState();

    const int TestTextureDim   = 32;
    const int TestArraySlices  = 8;
    const int TestTextureDepth = 8;

    TexFormatInfo.Dimensions = RESOURCE_DIMENSION_SUPPORT_NONE;

    // Disable debug messages - errors are expected
    m_ShowDebugGLOutput = 0;

    // Clear error code
    glGetError();

    const TextureProperties& TexProps = GetAdapterInfo().Texture;
    // Create test texture 1D
    if (TexProps.MaxTexture1DDimension != 0 && TexFormatInfo.ComponentType != COMPONENT_TYPE_COMPRESSED)
    {
        if (CreateTestGLTexture(ContextState, GL_TEXTURE_1D,
                                [&]() //
                                {
                                    glTexStorage1D(GL_TEXTURE_1D, 1, GLFmt, TestTextureDim);
                                }))

        {
            TexFormatInfo.Dimensions |= RESOURCE_DIMENSION_SUPPORT_TEX_1D;

            if (CreateTestGLTexture(ContextState, GL_TEXTURE_1D_ARRAY,
                                    [&]() //
                                    {
                                        glTexStorage2D(GL_TEXTURE_1D_ARRAY, 1, GLFmt, TestTextureDim, TestArraySlices);
                                    }))

            {
                TexFormatInfo.Dimensions |= RESOURCE_DIMENSION_SUPPORT_TEX_1D_ARRAY;
            }
        }
    }

    // Create test texture 2D
    {
        GLObjectWrappers::GLTextureObj TestGLTex2D{true};
        if (CreateTestGLTexture(ContextState, GL_TEXTURE_2D, TestGLTex2D,
                                [&]() //
                                {
                                    glTexStorage2D(GL_TEXTURE_2D, 1, GLFmt, TestTextureDim, TestTextureDim);
                                }))
        {
            TexFormatInfo.Dimensions |= RESOURCE_DIMENSION_SUPPORT_TEX_2D;

            if (CreateTestGLTexture(ContextState, GL_TEXTURE_2D_ARRAY,
                                    [&]() //
                                    {
                                        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GLFmt, TestTextureDim, TestTextureDim, TestArraySlices);
                                    }))
            {
                TexFormatInfo.Dimensions |= RESOURCE_DIMENSION_SUPPORT_TEX_2D_ARRAY;
            }
        }

        if (TexFormatInfo.Dimensions & RESOURCE_DIMENSION_SUPPORT_TEX_2D)
        {
            if (CreateTestGLTexture(
                    ContextState, GL_TEXTURE_CUBE_MAP,
                    [&]() //
                    {
                        glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, GLFmt, TestTextureDim, TestTextureDim);
                    }))
            {
                TexFormatInfo.Dimensions |= RESOURCE_DIMENSION_SUPPORT_TEX_CUBE;

                if (TexProps.CubemapArraysSupported)
                {
                    if (CreateTestGLTexture(
                            ContextState, GL_TEXTURE_CUBE_MAP_ARRAY,
                            [&]() //
                            {
                                glTexStorage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 1, GLFmt, TestTextureDim, TestTextureDim, 6);
                            }))
                    {
                        TexFormatInfo.Dimensions |= RESOURCE_DIMENSION_SUPPORT_TEX_CUBE_ARRAY;
                    }
                }
            }

            bool bTestDepthAttachment = (TexFormatInfo.BindFlags & BIND_DEPTH_STENCIL) != 0;
            VERIFY_EXPR(!bTestDepthAttachment ||
                        TexFormatInfo.ComponentType == COMPONENT_TYPE_DEPTH ||
                        TexFormatInfo.ComponentType == COMPONENT_TYPE_DEPTH_STENCIL);
            bool bTestColorAttachment = (TexFormatInfo.BindFlags & BIND_RENDER_TARGET) != 0;
            VERIFY_EXPR(!bTestColorAttachment || (!bTestDepthAttachment && TexFormatInfo.ComponentType != COMPONENT_TYPE_COMPRESSED));

            GLObjectWrappers::GLFrameBufferObj NewFBO{false};

            GLint CurrentFramebuffer = -1;
            if (bTestColorAttachment || bTestDepthAttachment)
            {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &CurrentFramebuffer);
                CHECK_GL_ERROR("Failed to get current framebuffer");

                NewFBO.Create();
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, NewFBO);
                CHECK_GL_ERROR("Failed to bind the framebuffer");
            }

            if (bTestDepthAttachment)
            {
                GLenum Attachment = TexFormatInfo.ComponentType == COMPONENT_TYPE_DEPTH ? GL_DEPTH_ATTACHMENT : GL_DEPTH_STENCIL_ATTACHMENT;
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, Attachment, GL_TEXTURE_2D, TestGLTex2D, 0);
                if (glGetError() == GL_NO_ERROR)
                {
                    // Create dummy texture2D since some older version do not allow depth only
                    // attachments
                    GLObjectWrappers::GLTextureObj ColorTex(true);

                    bool Success = CreateTestGLTexture(
                        ContextState, GL_TEXTURE_2D, ColorTex,
                        [&]() //
                        {
                            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, TestTextureDim, TestTextureDim);
                        } //
                    );
                    VERIFY(Success, "Failed to create dummy render target texture");
                    (void)Success;
                    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ColorTex, 0);
                    CHECK_GL_ERROR("Failed to bind dummy render target to framebuffer");

                    static constexpr GLenum DrawBuffers[] = {GL_COLOR_ATTACHMENT0};
                    glDrawBuffers(_countof(DrawBuffers), DrawBuffers);
                    CHECK_GL_ERROR("Failed to set draw buffers via glDrawBuffers()");

                    GLenum Status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    if ((glGetError() != GL_NO_ERROR) || (Status != GL_FRAMEBUFFER_COMPLETE))
                        TexFormatInfo.BindFlags &= ~BIND_DEPTH_STENCIL;
                }
                else
                {
                    TexFormatInfo.BindFlags &= ~BIND_DEPTH_STENCIL;
                }
            }
            else if (bTestColorAttachment)
            {
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, TestGLTex2D, 0);
                if (glGetError() == GL_NO_ERROR)
                {
                    static constexpr GLenum DrawBuffers[] = {GL_COLOR_ATTACHMENT0};
                    glDrawBuffers(_countof(DrawBuffers), DrawBuffers);
                    CHECK_GL_ERROR("Failed to set draw buffers via glDrawBuffers()");

                    GLenum Status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    if ((glGetError() != GL_NO_ERROR) || (Status != GL_FRAMEBUFFER_COMPLETE))
                        TexFormatInfo.BindFlags &= ~BIND_RENDER_TARGET;
                }
                else
                {
                    TexFormatInfo.BindFlags &= ~BIND_RENDER_TARGET;
                }
            }

            if (bTestColorAttachment || bTestDepthAttachment)
            {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, CurrentFramebuffer);
                CHECK_GL_ERROR("Failed to bind the framebuffer");
            }
        }

#if GL_ARB_shader_image_load_store
        if (TexFormatInfo.BindFlags & BIND_UNORDERED_ACCESS)
        {
            GLuint    CurrentImg     = 0;
            GLint     CurrentLevel   = 0;
            GLboolean CurrentLayered = 0;
            GLint     CurrentLayer   = 0;
            GLenum    CurrenAccess   = 0;
            GLenum    CurrenFormat   = 0;
            ContextState.GetBoundImage(0, CurrentImg, CurrentLevel, CurrentLayered, CurrentLayer, CurrenAccess, CurrenFormat);

            glBindImageTexture(0, TestGLTex2D, 0, GL_FALSE, 0, GL_READ_WRITE, GLFmt);
            if (glGetError() != GL_NO_ERROR)
                TexFormatInfo.BindFlags &= ~BIND_UNORDERED_ACCESS;

            glBindImageTexture(0, CurrentImg, CurrentLevel, CurrentLayered, CurrentLayer, CurrenAccess, CurrenFormat);
            CHECK_GL_ERROR("Failed to restore original image");
        }
#endif
    }

    TexFormatInfo.SampleCounts = SAMPLE_COUNT_1;
    if (TexFormatInfo.ComponentType != COMPONENT_TYPE_COMPRESSED && TexProps.Texture2DMSSupported)
    {
#if GL_ARB_texture_storage_multisample
        for (GLsizei SampleCount = 2; SampleCount <= 8; SampleCount *= 2)
        {
            GLObjectWrappers::GLTextureObj TestGLTex(true);

            auto SampleCountSupported = CreateTestGLTexture(
                ContextState, GL_TEXTURE_2D_MULTISAMPLE, TestGLTex,
                [&]() //
                {
                    glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, SampleCount, GLFmt, TestTextureDim, TestTextureDim, GL_TRUE);
                } //
            );
            if (SampleCountSupported)
                TexFormatInfo.SampleCounts |= static_cast<SAMPLE_COUNT>(SampleCount);
        }
#endif
    }

    // Create test texture 3D.
    // 3D textures do not support depth formats.
    if (!(TexFormatInfo.ComponentType == COMPONENT_TYPE_DEPTH ||
          TexFormatInfo.ComponentType == COMPONENT_TYPE_DEPTH_STENCIL))
    {
        if (CreateTestGLTexture(
                ContextState, GL_TEXTURE_3D,
                [&]() //
                {
                    glTexStorage3D(GL_TEXTURE_3D, 1, GLFmt, TestTextureDim, TestTextureDim, TestTextureDepth);
                }))
        {
            TexFormatInfo.Dimensions |= RESOURCE_DIMENSION_SUPPORT_TEX_3D;
        }
    }

    // Enable debug messages
    m_ShowDebugGLOutput = 1;
}

FBOCache& RenderDeviceGLImpl::GetFBOCache(GLContext::NativeGLContextType Context)
{
    Threading::SpinLockGuard FBOCacheGuard{m_FBOCacheLock};
    return m_FBOCache[Context];
}

void RenderDeviceGLImpl::OnReleaseTexture(ITexture* pTexture)
{
    Threading::SpinLockGuard FBOCacheGuard{m_FBOCacheLock};
    for (auto& FBOCacheIt : m_FBOCache)
        FBOCacheIt.second.OnReleaseTexture(pTexture);
}

VAOCache& RenderDeviceGLImpl::GetVAOCache(GLContext::NativeGLContextType Context)
{
    Threading::SpinLockGuard VAOCacheGuard{m_VAOCacheLock};
    return m_VAOCache[Context];
}

void RenderDeviceGLImpl::OnDestroyPSO(PipelineStateGLImpl& PSO)
{
    Threading::SpinLockGuard VAOCacheGuard{m_VAOCacheLock};
    for (auto& VAOCacheIt : m_VAOCache)
        VAOCacheIt.second.OnDestroyPSO(PSO);
}

void RenderDeviceGLImpl::OnDestroyBuffer(BufferGLImpl& Buffer)
{
    Threading::SpinLockGuard VAOCacheGuard{m_VAOCacheLock};
    for (auto& VAOCacheIt : m_VAOCache)
        VAOCacheIt.second.OnDestroyBuffer(Buffer);
}


void RenderDeviceGLImpl::PurgeContextCaches(GLContext::NativeGLContextType Context)
{
    {
        Threading::SpinLockGuard FBOCacheGuard{m_FBOCacheLock};

        auto it = m_FBOCache.find(Context);
        if (it != m_FBOCache.end())
        {
            it->second.Clear();
            m_FBOCache.erase(it);
        }
    }
    {
        Threading::SpinLockGuard VAOCacheGuard{m_VAOCacheLock};

        auto it = m_VAOCache.find(Context);
        if (it != m_VAOCache.end())
        {
            it->second.Clear();
            m_VAOCache.erase(it);
        }
    }
}

void RenderDeviceGLImpl::IdleGPU()
{
    glFinish();
}

#if PLATFORM_WIN32
NativeGLContextAttribs RenderDeviceGLImpl::GetNativeGLContextAttribs() const
{
    NativeGLContextAttribs Attribs;
    Attribs.hDC   = m_GLContext.GetWindowHandleToDeviceContext();
    Attribs.hGLRC = m_GLContext.GetHandle();
    return Attribs;
}
#elif PLATFORM_ANDROID
NativeGLContextAttribs RenderDeviceGLImpl::GetNativeGLContextAttribs() const
{
    NativeGLContextAttribs Attribs;
    Attribs.Display = m_GLContext.GetDisplay();
    Attribs.Surface = m_GLContext.GetSurface();
    Attribs.Context = m_GLContext.GetEGLCtx();
    Attribs.Config = m_GLContext.GetConfig();
    return Attribs;
}
#endif

} // namespace Diligent
