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

#include "TextureBaseGL.hpp"

#include "RenderDeviceGLImpl.hpp"
#include "DeviceContextGLImpl.hpp"
#include "TextureViewGLImpl.hpp"

#include "GLTypeConversions.hpp"
#include "EngineMemory.h"
#include "GraphicsAccessories.hpp"
#include "Align.hpp"

namespace Diligent
{

TextureBaseGL::TextureBaseGL(IReferenceCounters*        pRefCounters,
                             FixedBlockMemoryAllocator& TexViewObjAllocator,
                             RenderDeviceGLImpl*        pDeviceGL,
                             const TextureDesc&         TexDesc,
                             GLenum                     BindTarget,
                             const TextureData*         pInitData /*= nullptr*/,
                             bool                       bIsDeviceInternal /*= false*/) :
    // clang-format off
    TTextureBase
    {
        pRefCounters,
        TexViewObjAllocator,
        pDeviceGL,
        TexDesc,
        bIsDeviceInternal
    },
    m_GlTexture     {TexDesc.Usage != USAGE_STAGING},
    m_BindTarget    {BindTarget },
    m_GLTexFormat   {TexFormatToGLInternalTexFormat(m_Desc.Format, m_Desc.BindFlags)}
    //m_uiMapTarget(0)
// clang-format on
{
    VERIFY(m_GLTexFormat != 0, "Unsupported texture format");
    if (TexDesc.Usage == USAGE_IMMUTABLE && pInitData == nullptr)
        LOG_ERROR_AND_THROW("Immutable textures must be initialized with data at creation time");

    if (TexDesc.Usage == USAGE_STAGING)
    {
        BufferDesc  StagingBufferDesc;
        std::string StagingBuffName = "Internal staging buffer of texture '";
        StagingBuffName += m_Desc.Name;
        StagingBuffName += '\'';
        StagingBufferDesc.Name = StagingBuffName.c_str();

        StagingBufferDesc.Size           = GetStagingTextureDataSize(m_Desc, PBOOffsetAlignment);
        StagingBufferDesc.Usage          = USAGE_STAGING;
        StagingBufferDesc.CPUAccessFlags = TexDesc.CPUAccessFlags;

        pDeviceGL->CreateBuffer(StagingBufferDesc, nullptr, &m_pPBO);
        VERIFY_EXPR(m_pPBO);
    }
}

static GLenum GetTextureInternalFormat(const RenderDeviceInfo&               DeviceInfo,
                                       GLContextState&                       GLState,
                                       GLenum                                BindTarget,
                                       const GLObjectWrappers::GLTextureObj& GLTex,
                                       TEXTURE_FORMAT                        TexFmtFromDesc)
{
    GLState.BindTexture(-1, BindTarget, GLTex);

    GLint  GlFormat        = 0;
    GLenum QueryBindTarget = BindTarget;
    if (BindTarget == GL_TEXTURE_CUBE_MAP || BindTarget == GL_TEXTURE_CUBE_MAP_ARRAY)
        QueryBindTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X;

#if GL_TEXTURE_INTERNAL_FORMAT
    if (DeviceInfo.Type == RENDER_DEVICE_TYPE_GL || (DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= Version{3, 1}))
    {
        glGetTexLevelParameteriv(QueryBindTarget, 0, GL_TEXTURE_INTERNAL_FORMAT, &GlFormat);
        DEV_CHECK_GL_ERROR("glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT) failed");
    }
    if (GlFormat != 0)
    {
        if (GlFormat == GL_RGBA)
        {
            // Note: GL_RGBA is not a valid internal format (GL_RGBA8 is).
            // However, Android returns this as an internal format of the external camera
            // texture (which is incorrect), so we have to handle it.
            GlFormat = GL_RGBA8;
        }

        VERIFY(TexFmtFromDesc == TEX_FORMAT_UNKNOWN || static_cast<GLenum>(GlFormat) == TexFormatToGLInternalTexFormat(TexFmtFromDesc), "Texture format does not match the format specified by the texture description");
    }
    else
    {
        if (TexFmtFromDesc != TEX_FORMAT_UNKNOWN)
        {
            GlFormat = TexFormatToGLInternalTexFormat(TexFmtFromDesc);
        }
        else
        {
            LOG_WARNING_MESSAGE("Unable to query internal texture format while the format specified by texture description is TEX_FORMAT_UNKNOWN.");
        }
    }
#else
    (void)QueryBindTarget;

    if (TexFmtFromDesc != TEX_FORMAT_UNKNOWN)
    {
        GlFormat = TexFormatToGLInternalTexFormat(TexFmtFromDesc);
    }
    else
    {
        LOG_WARNING_MESSAGE("Texture format query is not supported while the format specified by texture description is TEX_FORMAT_UNKNOWN.");
    }
#endif

    GLState.BindTexture(-1, BindTarget, GLObjectWrappers::GLTextureObj::Null());

    return GlFormat;
}

static TextureDesc GetTextureDescFromGLHandle(const RenderDeviceInfo& DeviceInfo,
                                              GLContextState&         GLState,
                                              TextureDesc             TexDesc,
                                              GLuint                  GLHandle,
                                              GLenum                  BindTarget)
{
    VERIFY(BindTarget != GL_TEXTURE_CUBE_MAP_ARRAY, "Cubemap arrays are not currently supported");

    GLObjectWrappers::GLTextureObj TmpGLTexWrapper(true, GLObjectWrappers::GLTextureCreateReleaseHelper(GLHandle));
    GLState.BindTexture(-1, BindTarget, TmpGLTexWrapper);

    GLenum QueryBindTarget = BindTarget;
    if (BindTarget == GL_TEXTURE_CUBE_MAP)
        QueryBindTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X;

#if GL_TEXTURE_WIDTH
    GLint TexWidth = 0;
    if (DeviceInfo.Type == RENDER_DEVICE_TYPE_GL || (DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= Version{3, 1}))
    {
        glGetTexLevelParameteriv(QueryBindTarget, 0, GL_TEXTURE_WIDTH, &TexWidth);
        DEV_CHECK_GL_ERROR("glGetTexLevelParameteriv(GL_TEXTURE_WIDTH) failed");
    }
    if (TexWidth > 0)
    {
        if (TexDesc.Width != 0 && TexDesc.Width != static_cast<Uint32>(TexWidth))
        {
            LOG_WARNING_MESSAGE("The width (", TexDesc.Width, ") of texture '", TexDesc.Name,
                                "' specified by TextureDesc struct does not match the actual width (", TexWidth, ")");
        }
        TexDesc.Width = static_cast<Uint32>(TexWidth);
    }
    else
    {
        if (TexDesc.Width == 0)
        {
            LOG_WARNING_MESSAGE("Unable to query the width of texture '", TexDesc.Name,
                                "' while the Width member of TextureDesc struct is 0.");
        }
    }
#else
    (void)QueryBindTarget;

    if (TexDesc.Width == 0)
    {
        LOG_WARNING_MESSAGE("Texture width query is not supported while the Width member of TextureDesc struct of texture '",
                            TexDesc.Name, "' is 0.");
    }
#endif

    if (TexDesc.Type >= RESOURCE_DIM_TEX_2D)
    {
#if GL_TEXTURE_HEIGHT
        GLint TexHeight = 0;
        if (DeviceInfo.Type == RENDER_DEVICE_TYPE_GL || (DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= Version{3, 1}))
        {
            glGetTexLevelParameteriv(QueryBindTarget, 0, GL_TEXTURE_HEIGHT, &TexHeight);
            DEV_CHECK_GL_ERROR("glGetTexLevelParameteriv(GL_TEXTURE_HEIGHT) failed");
        }
        if (TexHeight > 0)
        {
            if (TexDesc.Height != 0 && TexDesc.Height != static_cast<Uint32>(TexHeight))
            {
                LOG_WARNING_MESSAGE("The height (", TexDesc.Height, ") of texture '", TexDesc.Name,
                                    "' specified by TextureDesc struct does not match the actual height (", TexHeight, ")");
            }
            TexDesc.Height = static_cast<Uint32>(TexHeight);
        }
        else
        {
            if (TexDesc.Height == 0)
            {
                LOG_WARNING_MESSAGE("Unable to query the height of texture '", TexDesc.Name,
                                    "' while the Height member of TextureDesc struct is 0.");
            }
        }
#else
        if (TexDesc.Height == 0)
        {
            LOG_WARNING_MESSAGE("Texture height query is not supported while the Height member of TextureDesc struct of texture '",
                                TexDesc.Name, "' is 0.");
        }
#endif
    }
    else
    {
        TexDesc.Height = 1;
    }

    if (TexDesc.Type == RESOURCE_DIM_TEX_3D)
    {
#if GL_TEXTURE_DEPTH
        GLint TexDepth = 0;
        if (DeviceInfo.Type == RENDER_DEVICE_TYPE_GL || (DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= Version{3, 1}))
        {
            glGetTexLevelParameteriv(QueryBindTarget, 0, GL_TEXTURE_DEPTH, &TexDepth);
            DEV_CHECK_GL_ERROR("glGetTexLevelParameteriv(GL_TEXTURE_DEPTH) failed");
        }
        if (TexDepth > 0)
        {
            if (TexDesc.Depth != 0 && TexDesc.Depth != static_cast<Uint32>(TexDepth))
            {
                LOG_WARNING_MESSAGE("The depth (", TexDesc.Depth, ") of texture '", TexDesc.Name,
                                    "' specified by TextureDesc struct does not match the actual depth (", TexDepth, ")");
            }
            TexDesc.Depth = static_cast<Uint32>(TexDepth);
        }
        else
        {
            if (TexDesc.Depth == 0)
            {
                LOG_WARNING_MESSAGE("Unable to query the depth of texture '", TexDesc.Name,
                                    "' while the Depth member of TextureDesc struct is 0.");
            }
        }
#else
        if (TexDesc.Depth == 0)
        {
            LOG_WARNING_MESSAGE("Texture depth query is not supported while the Depth member of TextureDesc struct of texture '",
                                TexDesc.Name, "' is 0.");
        }
#endif
    }

    if (TexDesc.Type == RESOURCE_DIM_TEX_1D || TexDesc.Type == RESOURCE_DIM_TEX_2D)
        TexDesc.ArraySize = 1; // TexDesc.Depth also

#if GL_TEXTURE_INTERNAL_FORMAT
    GLint GlFormat = 0;
    if (DeviceInfo.Type == RENDER_DEVICE_TYPE_GL || (DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES && DeviceInfo.APIVersion >= Version{3, 1}))
    {
        glGetTexLevelParameteriv(QueryBindTarget, 0, GL_TEXTURE_INTERNAL_FORMAT, &GlFormat);
        DEV_CHECK_GL_ERROR("glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT) failed");
    }
    if (GlFormat != 0)
    {
        if (TexDesc.Format != TEX_FORMAT_UNKNOWN && static_cast<GLenum>(GlFormat) != TexFormatToGLInternalTexFormat(TexDesc.Format))
        {
            LOG_WARNING_MESSAGE("The format (", GetTextureFormatAttribs(TexDesc.Format).Name, ") of texture '", TexDesc.Name,
                                "' specified by TextureDesc struct does not match GL texture internal format (", GlFormat, ")");
        }

        TexDesc.Format = GLInternalTexFormatToTexFormat(GlFormat);
    }
    else
    {
        if (TexDesc.Format == TEX_FORMAT_UNKNOWN)
        {
            LOG_WARNING_MESSAGE("Unable to query the format of texture '", TexDesc.Name,
                                "' while the Format member of TextureDesc struct is TEX_FORMAT_UNKNOWN.");
        }
    }
#else
    if (TexDesc.Format == TEX_FORMAT_UNKNOWN)
    {
        LOG_WARNING_MESSAGE("Texture format query is not supported while the Format member of TextureDesc struct of texture '",
                            TexDesc.Name, "' is TEX_FORMAT_UNKNOWN.");
    }
#endif

    GLint MipLevels = 0;
    // GL_TEXTURE_IMMUTABLE_LEVELS is supported in GL4.3+ and GLES3.0+.
    if ((DeviceInfo.Type == RENDER_DEVICE_TYPE_GL && DeviceInfo.APIVersion >= Version{4, 3}) || DeviceInfo.Type == RENDER_DEVICE_TYPE_GLES)
    {
        glGetTexParameteriv(BindTarget, GL_TEXTURE_IMMUTABLE_LEVELS, &MipLevels);
        DEV_CHECK_GL_ERROR("glGetTexParameteriv(GL_TEXTURE_IMMUTABLE_LEVELS) failed");
    }
    if (MipLevels > 0)
    {
        if (TexDesc.MipLevels != 0 && TexDesc.MipLevels != static_cast<Uint32>(MipLevels))
        {
            LOG_WARNING_MESSAGE("The number of mip levels (", TexDesc.MipLevels, ") of texture '", TexDesc.Name,
                                "' specified by TextureDesc struct does not match the actual number of mip levels (", MipLevels, ")");
        }

        TexDesc.MipLevels = static_cast<Uint32>(MipLevels);
    }
    else
    {
        if (TexDesc.MipLevels == 0)
        {
            LOG_WARNING_MESSAGE("Unable to query the mip level count of texture '", TexDesc.Name,
                                "' while the MipLevels member of TextureDesc struct is 0.");
        }
    }

    GLState.BindTexture(-1, BindTarget, GLObjectWrappers::GLTextureObj::Null());
    return TexDesc;
}

TextureBaseGL::TextureBaseGL(IReferenceCounters*        pRefCounters,
                             FixedBlockMemoryAllocator& TexViewObjAllocator,
                             RenderDeviceGLImpl*        pDeviceGL,
                             GLContextState&            GLState,
                             const TextureDesc&         TexDesc,
                             GLuint                     GLTextureHandle,
                             GLenum                     BindTarget,
                             bool                       bIsDeviceInternal /* = false*/) :
    // clang-format off
    TTextureBase
    {
        pRefCounters,
        TexViewObjAllocator,
        pDeviceGL,
        GetTextureDescFromGLHandle(pDeviceGL->GetDeviceInfo(), GLState, TexDesc, GLTextureHandle, BindTarget),
        bIsDeviceInternal
    },
    // Create texture object wrapper, but use external texture handle
    m_GlTexture     {true, GLObjectWrappers::GLTextureCreateReleaseHelper(GLTextureHandle)},
    m_BindTarget    {BindTarget},
    m_GLTexFormat   {GetTextureInternalFormat(pDeviceGL->GetDeviceInfo(), GLState, BindTarget, m_GlTexture, TexDesc.Format)}
// clang-format on
{
}

TextureBaseGL::TextureBaseGL(IReferenceCounters*        pRefCounters,
                             FixedBlockMemoryAllocator& TexViewObjAllocator,
                             RenderDeviceGLImpl*        pDeviceGL,
                             const TextureDesc&         TexDesc,
                             bool                       bIsDeviceInternal) :
    // clang-format off
    TTextureBase
    {
        pRefCounters,
        TexViewObjAllocator,
        pDeviceGL,
        TexDesc,
        bIsDeviceInternal
    },
    m_GlTexture  {false},
    m_BindTarget {0    },
    m_GLTexFormat{0    }
// clang-format on
{
}

TextureBaseGL::~TextureBaseGL()
{
    // Release all FBOs that contain current texture
    // NOTE: we cannot check if BIND_RENDER_TARGET
    // flag is set, because CopyData() can bind
    // texture as render target even when no flag
    // is set
    GetDevice()->OnReleaseTexture(this);
}

IMPLEMENT_QUERY_INTERFACE(TextureBaseGL, IID_TextureGL, TTextureBase)

void TextureBaseGL::CreateViewInternal(const TextureViewDesc& OrigViewDesc, ITextureView** ppView, bool bIsDefaultView)
{
    VERIFY(ppView != nullptr, "Null pointer provided");
    if (!ppView) return;
    VERIFY(*ppView == nullptr, "Overwriting reference to existing object may cause memory leaks");

    *ppView = nullptr;

    try
    {
        TextureViewDesc ViewDesc = OrigViewDesc;
        ValidatedAndCorrectTextureViewDesc(m_Desc, ViewDesc);

        RenderDeviceGLImpl*        pDeviceGLImpl    = GetDevice();
        FixedBlockMemoryAllocator& TexViewAllocator = pDeviceGLImpl->GetTexViewObjAllocator();
        VERIFY(&TexViewAllocator == &m_dbgTexViewObjAllocator, "Texture view allocator does not match allocator provided during texture initialization");

        // http://www.opengl.org/wiki/Texture_Storage#Texture_views

        GLenum GLViewFormat = TexFormatToGLInternalTexFormat(ViewDesc.Format, m_Desc.BindFlags);
        VERIFY(GLViewFormat != 0, "Unsupported texture format");

        TextureViewGLImpl* pViewOGL = nullptr;
        if (ViewDesc.ViewType == TEXTURE_VIEW_SHADER_RESOURCE)
        {
            // clang-format off
            bool bIsFullTextureView =
                ViewDesc.TextureDim               == m_Desc.Type &&
                ViewDesc.Format                   == GetDefaultTextureViewFormat(m_Desc.Format, ViewDesc.ViewType, m_Desc.BindFlags) &&
                ViewDesc.MostDetailedMip          == 0 &&
                ViewDesc.NumMipLevels             == m_Desc.MipLevels &&
                ViewDesc.FirstArrayOrDepthSlice() == 0 &&
                ViewDesc.NumArrayOrDepthSlices()  == m_Desc.ArraySizeOrDepth() &&
                IsIdentityComponentMapping(ViewDesc.Swizzle);
            // clang-format on

            pViewOGL = NEW_RC_OBJ(TexViewAllocator, "TextureViewGLImpl instance", TextureViewGLImpl, bIsDefaultView ? this : nullptr)(
                pDeviceGLImpl, ViewDesc, this,
                !bIsFullTextureView, // Create OpenGL texture view object if view
                                     // does not address the whole texture
                bIsDefaultView);
            if (!bIsFullTextureView)
            {
                GLenum GLViewTarget = 0;
                GLuint NumLayers    = ViewDesc.NumArraySlices;
                switch (ViewDesc.TextureDim)
                {
                    case RESOURCE_DIM_TEX_1D:
                        GLViewTarget            = GL_TEXTURE_1D;
                        ViewDesc.NumArraySlices = NumLayers = 1;
                        break;

                    case RESOURCE_DIM_TEX_1D_ARRAY:
                        GLViewTarget = GL_TEXTURE_1D_ARRAY;
                        break;

                    case RESOURCE_DIM_TEX_2D:
                        GLViewTarget            = m_Desc.SampleCount > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
                        ViewDesc.NumArraySlices = NumLayers = 1;
                        break;

                    case RESOURCE_DIM_TEX_2D_ARRAY:
                        GLViewTarget = m_Desc.SampleCount > 1 ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_ARRAY;
                        break;

                    case RESOURCE_DIM_TEX_3D:
                    {
                        GLViewTarget = GL_TEXTURE_3D;
                        // If target is GL_TEXTURE_3D, NumLayers must equal 1.
                        Uint32 MipDepth = std::max(m_Desc.Depth >> ViewDesc.MostDetailedMip, 1U);
                        if (ViewDesc.FirstDepthSlice != 0 || ViewDesc.NumDepthSlices != MipDepth)
                        {
                            LOG_ERROR("3D texture view '", (ViewDesc.Name ? ViewDesc.Name : ""), "' (most detailed mip: ", ViewDesc.MostDetailedMip,
                                      "; mip levels: ", ViewDesc.NumMipLevels, "; first slice: ", ViewDesc.FirstDepthSlice,
                                      "; num depth slices: ", ViewDesc.NumDepthSlices, ") of texture '", m_Desc.Name, "' does not references"
                                                                                                                      " all depth slices. 3D texture views in OpenGL must address all depth slices.");
                            ViewDesc.NumDepthSlices  = MipDepth;
                            ViewDesc.FirstDepthSlice = 0;
                        }
                        NumLayers = 1;
                        break;
                    }

                    case RESOURCE_DIM_TEX_CUBE:
                        GLViewTarget = GL_TEXTURE_CUBE_MAP;
                        break;

                    case RESOURCE_DIM_TEX_CUBE_ARRAY:
                        GLViewTarget = GL_TEXTURE_CUBE_MAP_ARRAY;
                        break;

                    default: UNEXPECTED("Unsupported texture view type");
                }

                // In OpenGL ES this function is allowed as an extension and may not be supported
                if (glTextureView == nullptr)
                    LOG_ERROR_AND_THROW("glTextureView is not supported");

                glTextureView(pViewOGL->GetHandle(), GLViewTarget, m_GlTexture, GLViewFormat, ViewDesc.MostDetailedMip, ViewDesc.NumMipLevels, ViewDesc.FirstArraySlice, NumLayers);
                DEV_CHECK_GL_ERROR_AND_THROW("Failed to create texture view");
                pViewOGL->SetBindTarget(GLViewTarget);

                if (ViewDesc.Format == TEX_FORMAT_X24_TYPELESS_G8_UINT || ViewDesc.Format == TEX_FORMAT_X32_TYPELESS_G8X24_UINT)
                {
                    const TextureFormatInfo& FmtInfo = pDeviceGLImpl->GetTextureFormatInfo(ViewDesc.Format);

                    if (FmtInfo.Supported)
                    {
                        RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = pDeviceGLImpl->GetImmediateContext(0);
                        VERIFY(pDeviceContext, "Immediate device context has been destroyed");
                        GLContextState& GLState = pDeviceContext->GetContextState();

                        GLState.BindTexture(-1, GLViewTarget, pViewOGL->GetHandle());
                        glTexParameteri(GLViewTarget, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX);
                        DEV_CHECK_GL_ERROR("Failed to set GL_DEPTH_STENCIL_TEXTURE_MODE texture parameter");
                        GLState.BindTexture(-1, GLViewTarget, GLObjectWrappers::GLTextureObj::Null());
                    }
                    else
                    {
                        // Throw an error if the format is not supported
                        LOG_ERROR_AND_THROW("Format ", GetTextureFormatAttribs(ViewDesc.Format).Name, " is not supported");
                    }
                }

                if (!IsIdentityComponentMapping(ViewDesc.Swizzle))
                {
                    RefCntAutoPtr<DeviceContextGLImpl> pDeviceContext = pDeviceGLImpl->GetImmediateContext(0);
                    VERIFY(pDeviceContext, "Immediate device context has been destroyed");
                    GLContextState& GLState = pDeviceContext->GetContextState();

                    GLState.BindTexture(-1, GLViewTarget, pViewOGL->GetHandle());
                    glTexParameteri(GLViewTarget, GL_TEXTURE_SWIZZLE_R, TextureComponentSwizzleToGLTextureSwizzle(ViewDesc.Swizzle.R, GL_RED));
                    DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_R texture parameter");
                    glTexParameteri(GLViewTarget, GL_TEXTURE_SWIZZLE_G, TextureComponentSwizzleToGLTextureSwizzle(ViewDesc.Swizzle.G, GL_GREEN));
                    DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_G texture parameter");
                    glTexParameteri(GLViewTarget, GL_TEXTURE_SWIZZLE_B, TextureComponentSwizzleToGLTextureSwizzle(ViewDesc.Swizzle.B, GL_BLUE));
                    DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_B texture parameter");
                    glTexParameteri(GLViewTarget, GL_TEXTURE_SWIZZLE_A, TextureComponentSwizzleToGLTextureSwizzle(ViewDesc.Swizzle.A, GL_ALPHA));
                    DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_A texture parameter");
                    GLState.BindTexture(-1, GLViewTarget, GLObjectWrappers::GLTextureObj::Null());
                }
            }
        }
        else if (ViewDesc.ViewType == TEXTURE_VIEW_UNORDERED_ACCESS)
        {
            // clang-format off
            VERIFY(ViewDesc.NumArrayOrDepthSlices() == 1 ||
                   (m_Desc.Type == RESOURCE_DIM_TEX_3D && ViewDesc.NumDepthSlices == std::max(m_Desc.Depth >> ViewDesc.MostDetailedMip, 1U)) ||
                   ViewDesc.NumArraySlices == m_Desc.ArraySize,
                   "Only single array/depth slice or the whole texture can be bound as UAV in OpenGL.");
            // clang-format on
            VERIFY(ViewDesc.AccessFlags != 0, "At least one access flag must be specified");
            pViewOGL = NEW_RC_OBJ(TexViewAllocator, "TextureViewGLImpl instance", TextureViewGLImpl, bIsDefaultView ? this : nullptr)(
                pDeviceGLImpl, ViewDesc, this,
                false, // Do NOT create texture view OpenGL object
                bIsDefaultView);
        }
        else if (ViewDesc.ViewType == TEXTURE_VIEW_RENDER_TARGET)
        {
            VERIFY(ViewDesc.NumMipLevels == 1, "Only a single mip level can be bound as RTV");
            pViewOGL = NEW_RC_OBJ(TexViewAllocator, "TextureViewGLImpl instance", TextureViewGLImpl, bIsDefaultView ? this : nullptr)(
                pDeviceGLImpl, ViewDesc, this,
                false, // Do NOT create texture view OpenGL object
                bIsDefaultView);
        }
        else if (ViewDesc.ViewType == TEXTURE_VIEW_DEPTH_STENCIL || ViewDesc.ViewType == TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL)
        {
            VERIFY(ViewDesc.NumMipLevels == 1, "Only a single mip level can be bound as DSV");
            pViewOGL = NEW_RC_OBJ(TexViewAllocator, "TextureViewGLImpl instance", TextureViewGLImpl, bIsDefaultView ? this : nullptr)(
                pDeviceGLImpl, ViewDesc, this,
                false, // Do NOT create texture view OpenGL object
                bIsDefaultView);
        }

        if (bIsDefaultView)
            *ppView = pViewOGL;
        else
        {
            if (pViewOGL)
            {
                pViewOGL->QueryInterface(IID_TextureView, reinterpret_cast<IObject**>(ppView));
            }
        }
    }
    catch (const std::runtime_error&)
    {
        const char* ViewTypeName = GetTexViewTypeLiteralName(OrigViewDesc.ViewType);
        LOG_ERROR("Failed to create view '", (OrigViewDesc.Name ? OrigViewDesc.Name : ""), "' (", ViewTypeName, ") for texture '", (m_Desc.Name ? m_Desc.Name : ""), "'");
    }
}


void TextureBaseGL::UpdateData(GLContextState& CtxState, Uint32 MipLevel, Uint32 Slice, const Box& DstBox, const TextureSubResData& SubresData)
{
    // GL_TEXTURE_UPDATE_BARRIER_BIT:
    //      Writes to a texture via glTex( Sub )Image*, glCopyTex( Sub )Image*, glClearTex*Image,
    //      glCompressedTex( Sub )Image*, and reads via glTexImage() after the barrier will reflect
    //      data written by shaders prior to the barrier. Additionally, texture writes from these
    //      commands issued after the barrier will not execute until all shader writes initiated prior
    //      to the barrier complete
    TextureMemoryBarrier(MEMORY_BARRIER_TEXTURE_UPDATE, CtxState);
}

//void TextureBaseGL::UpdateData(Uint32 Offset, Uint32 Size, const void* pData)
//{
//    CTexture::UpdateData(Offset, Size, pData);
//
//    glBindTexture(GL_ARRAY_Texture, m_GlTexture);
//    glTextureSubData(GL_ARRAY_Texture, Offset, Size, pData);
//    glBindTexture(GL_ARRAY_Texture, 0);
//}
//

inline GLbitfield GetFramebufferCopyMask(TEXTURE_FORMAT Format)
{
    const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(Format);
    switch (FmtAttribs.ComponentType)
    {
        case COMPONENT_TYPE_DEPTH:
            return GL_DEPTH_BUFFER_BIT;
        case COMPONENT_TYPE_DEPTH_STENCIL:
            return GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
        default:
            return GL_COLOR_BUFFER_BIT;
    }
}

void TextureBaseGL::CopyData(DeviceContextGLImpl* pDeviceCtxGL,
                             TextureBaseGL*       pSrcTextureGL,
                             Uint32               SrcMipLevel,
                             Uint32               SrcSlice,
                             const Box*           pSrcBox,
                             Uint32               DstMipLevel,
                             Uint32               DstSlice,
                             Uint32               DstX,
                             Uint32               DstY,
                             Uint32               DstZ)
{
    const TextureDesc& SrcTexDesc = pSrcTextureGL->GetDesc();

    Box SrcBox;
    if (pSrcBox == nullptr)
    {
        SrcBox.MaxX = std::max(SrcTexDesc.Width >> SrcMipLevel, 1u);
        if (SrcTexDesc.Type == RESOURCE_DIM_TEX_1D ||
            SrcTexDesc.Type == RESOURCE_DIM_TEX_1D_ARRAY)
            SrcBox.MaxY = 1;
        else
            SrcBox.MaxY = std::max(SrcTexDesc.Height >> SrcMipLevel, 1u);

        if (SrcTexDesc.Type == RESOURCE_DIM_TEX_3D)
            SrcBox.MaxZ = std::max(SrcTexDesc.Depth >> SrcMipLevel, 1u);
        else
            SrcBox.MaxZ = 1;
        pSrcBox = &SrcBox;
    }

    const bool IsDefaultBackBuffer = GetGLHandle() == 0;
#if GL_ARB_copy_image
    // We can't use glCopyImageSubData with the proxy texture of a default framebuffer
    // because we don't have the texture handle.
    if (glCopyImageSubData && !IsDefaultBackBuffer && pSrcTextureGL->GetGLHandle() != 0)
    {
        GLint SrcSliceY = (SrcTexDesc.Type == RESOURCE_DIM_TEX_1D_ARRAY) ? SrcSlice : 0;
        GLint SrcSliceZ = (SrcTexDesc.Type == RESOURCE_DIM_TEX_2D_ARRAY) ? SrcSlice : 0;
        GLint DstSliceY = (m_Desc.Type == RESOURCE_DIM_TEX_1D_ARRAY) ? DstSlice : 0;
        GLint DstSliceZ = (m_Desc.Type == RESOURCE_DIM_TEX_2D_ARRAY) ? DstSlice : 0;
        glCopyImageSubData(
            pSrcTextureGL->GetGLHandle(),
            pSrcTextureGL->GetBindTarget(),
            SrcMipLevel,
            pSrcBox->MinX,
            pSrcBox->MinY + SrcSliceY,
            pSrcBox->MinZ + SrcSliceZ, // Slice must be zero for 3D texture
            GetGLHandle(),
            GetBindTarget(),
            DstMipLevel,
            DstX,
            DstY + DstSliceY,
            DstZ + DstSliceZ, // Slice must be zero for 3D texture
            pSrcBox->Width(),
            pSrcBox->Height(),
            pSrcBox->Depth());
        DEV_CHECK_GL_ERROR("glCopyImageSubData() failed");
    }
    else
#endif
    {
#if PLATFORM_WEB
        // Always use BlitFramebuffer on WebGL as CopyTexSubimage has
        // a very high performance penalty.
        bool UseBlitFramebuffer = true;
#else
        bool UseBlitFramebuffer = IsDefaultBackBuffer;
        if (!UseBlitFramebuffer && m_pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_GLES)
        {
            const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(m_Desc.Format);
            if (FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH ||
                FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH_STENCIL)
            {
                // glCopyTexSubImage* does not support depth formats in GLES
                UseBlitFramebuffer = true;
            }
        }
#endif

        GLContextState& GLState = pDeviceCtxGL->GetContextState();

        // Copy operations (glCopyTexSubImage* and glBindFramebuffer) are affected by scissor test!
        bool ScissorEnabled = GLState.GetScissorTestEnabled();
        if (ScissorEnabled)
            GLState.EnableScissorTest(false);

        for (Uint32 DepthSlice = 0; DepthSlice < pSrcBox->Depth(); ++DepthSlice)
        {
            GLuint SrcFboHandle = 0;
            if (pSrcTextureGL->GetGLHandle() != 0)
            {
                // Get read framebuffer for the source subimage

                FBOCache& FboCache = m_pDevice->GetFBOCache(GLState.GetCurrentGLContext());
                VERIFY_EXPR(SrcSlice == 0 || SrcTexDesc.IsArray());
                VERIFY_EXPR((pSrcBox->MinZ == 0 && DepthSlice == 0) || SrcTexDesc.Is3D());
                const Uint32 SrcFramebufferSlice = SrcSlice + pSrcBox->MinZ + DepthSlice;
                // NOTE: GetFBO may bind a framebuffer, so we need to invalidate it in the GL context state.
                const GLObjectWrappers::GLFrameBufferObj& ReadFBO = FboCache.GetFBO(pSrcTextureGL, SrcFramebufferSlice, SrcMipLevel, TextureBaseGL::FRAMEBUFFER_TARGET_FLAG_READ);

                SrcFboHandle = ReadFBO;
            }
            else
            {
                SrcFboHandle = pDeviceCtxGL->GetDefaultFBO();
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, SrcFboHandle);
            DEV_CHECK_GL_ERROR("Failed to bind read framebuffer");
            DEV_CHECK_ERR(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                          "Read framebuffer is incomplete: ", GetFramebufferStatusString(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)));

            if (!UseBlitFramebuffer)
            {
                CopyTexSubimageAttribs CopyAttribs{*pSrcBox};
                CopyAttribs.DstMip   = DstMipLevel;
                CopyAttribs.DstLayer = DstSlice;
                CopyAttribs.DstX     = DstX;
                CopyAttribs.DstY     = DstY;
                CopyAttribs.DstZ     = DstZ + DepthSlice;
                CopyTexSubimage(GLState, CopyAttribs);
            }
            else
            {
                GLuint DstFboHandle = 0;
                if (IsDefaultBackBuffer)
                {
                    DstFboHandle = pDeviceCtxGL->GetDefaultFBO();
                }
                else
                {
                    // Get draw framebuffer for the destination subimage

                    FBOCache& FboCache = m_pDevice->GetFBOCache(GLState.GetCurrentGLContext());
                    VERIFY_EXPR(DstSlice == 0 || m_Desc.IsArray());
                    VERIFY_EXPR((DstZ == 0 && DepthSlice == 0) || m_Desc.Is3D());
                    const Uint32 DstFramebufferSlice = DstSlice + DstZ + DepthSlice;
                    // NOTE: GetFBO may bind a framebuffer, so we need to invalidate it in the GL context state.
                    const GLObjectWrappers::GLFrameBufferObj& DrawFBO = FboCache.GetFBO(this, DstFramebufferSlice, DstMipLevel, TextureBaseGL::FRAMEBUFFER_TARGET_FLAG_DRAW);

                    DstFboHandle = DrawFBO;
                }

                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, DstFboHandle);
                DEV_CHECK_GL_ERROR("Failed to bind draw framebuffer");
                DEV_CHECK_ERR(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                              "Draw framebuffer is incomplete: ", GetFramebufferStatusString(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)));

                const GLbitfield CopyMask = GetFramebufferCopyMask(SrcTexDesc.Format);
                DEV_CHECK_ERR(CopyMask == GetFramebufferCopyMask(m_Desc.Format),
                              "Src and dst framebuffer copy masks must be the same");
                glBlitFramebuffer(pSrcBox->MinX,
                                  pSrcBox->MinY,
                                  pSrcBox->MaxX,
                                  pSrcBox->MaxY,
                                  DstX,
                                  DstY,
                                  DstX + pSrcBox->Width(),
                                  DstY + pSrcBox->Height(),
                                  CopyMask,
                                  GL_NEAREST);
                DEV_CHECK_GL_ERROR("Failed to blit framebuffer");
            }
        }

        if (ScissorEnabled)
            GLState.EnableScissorTest(true);

        // Invalidate FBO as we used glBindFramebuffer directly
        GLState.InvalidateFBO();

        if (!UseBlitFramebuffer)
            GLState.BindTexture(-1, GetBindTarget(), GLObjectWrappers::GLTextureObj::Null());

        pDeviceCtxGL->CommitRenderTargets();
    }
}

void TextureBaseGL::SetDefaultGLParameters()
{
#ifdef DILIGENT_DEBUG
    {
        GLint BoundTex;
        GLint TextureBinding = 0;
        switch (m_BindTarget)
        {
                // clang-format off
            case GL_TEXTURE_1D:                     TextureBinding = GL_TEXTURE_BINDING_1D;                   break;
            case GL_TEXTURE_1D_ARRAY:               TextureBinding = GL_TEXTURE_BINDING_1D_ARRAY;             break;
            case GL_TEXTURE_2D:                     TextureBinding = GL_TEXTURE_BINDING_2D;                   break;
            case GL_TEXTURE_2D_ARRAY:               TextureBinding = GL_TEXTURE_BINDING_2D_ARRAY;             break;
            case GL_TEXTURE_2D_MULTISAMPLE:         TextureBinding = GL_TEXTURE_BINDING_2D_MULTISAMPLE;       break;
            case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:   TextureBinding = GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY; break;
            case GL_TEXTURE_3D:                     TextureBinding = GL_TEXTURE_BINDING_3D;                   break;
            case GL_TEXTURE_CUBE_MAP:               TextureBinding = GL_TEXTURE_BINDING_CUBE_MAP;             break;
            case GL_TEXTURE_CUBE_MAP_ARRAY:         TextureBinding = GL_TEXTURE_BINDING_CUBE_MAP_ARRAY;       break;
            default: UNEXPECTED("Unknown bind target");
                // clang-format on
        }
        glGetIntegerv(TextureBinding, &BoundTex);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_MIN_FILTER texture parameter");
        VERIFY(static_cast<GLuint>(BoundTex) == m_GlTexture, "Current texture is not bound to GL context");
    }
#endif

    if (m_Desc.Format == TEX_FORMAT_A8_UNORM)
    {
        // We need to do channel swizzling since TEX_FORMAT_A8_UNORM
        // is actually implemented using GL_RED
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_R, GL_ZERO);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_R texture parameter");
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_G, GL_ZERO);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_G texture parameter");
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_B, GL_ZERO);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_B texture parameter");
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_A, GL_RED);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_A texture parameter");
    }
    else if (m_Desc.Format == TEX_FORMAT_BGRA8_UNORM)
    {
        // We need to do channel swizzling since TEX_FORMAT_BGRA8_UNORM
        // is actually implemented using GL_RGBA
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_R texture parameter");
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_G texture parameter");
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_B, GL_RED);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_B texture parameter");
        glTexParameteri(m_BindTarget, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_SWIZZLE_A texture parameter");
    }

    if (m_BindTarget != GL_TEXTURE_2D_MULTISAMPLE &&
        m_BindTarget != GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
    {
        // Note that texture bound to image unit must be complete.
        // That means that if an integer texture is being bound, its
        // GL_TEXTURE_MIN_FILTER and GL_TEXTURE_MAG_FILTER must be NEAREST,
        // otherwise it will be incomplete

        // The default value of GL_TEXTURE_MIN_FILTER is GL_NEAREST_MIPMAP_LINEAR
        // Reset it to GL_NEAREST to avoid incompleteness issues with integer textures
        glTexParameteri(m_BindTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_MIN_FILTER texture parameter");

        // The default value of GL_TEXTURE_MAG_FILTER is GL_LINEAR
        glTexParameteri(m_BindTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        DEV_CHECK_GL_ERROR("Failed to set GL_TEXTURE_MAG_FILTER texture parameter");
    }
}

} // namespace Diligent
