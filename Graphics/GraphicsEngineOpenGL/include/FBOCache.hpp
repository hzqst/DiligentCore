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

#pragma once

#include "GraphicsTypes.h"
#include "TextureView.h"
#include "SpinLock.hpp"
#include "HashUtils.hpp"
#include "GLObjectWrapper.hpp"
#include "TextureBaseGL.hpp"

namespace Diligent
{

class TextureViewGLImpl;
class GLContextState;

class FBOCache
{
public:
    FBOCache();
    ~FBOCache();

    // clang-format off
    FBOCache             (const FBOCache&)  = delete;
    FBOCache             (      FBOCache&&) = delete;
    FBOCache& operator = (const FBOCache&)  = delete;
    FBOCache& operator = (      FBOCache&&) = delete;
    // clang-format on

    static GLObjectWrappers::GLFrameBufferObj CreateFBO(GLContextState&    ContextState,
                                                        Uint32             NumRenderTargets,
                                                        TextureViewGLImpl* ppRTVs[],
                                                        TextureViewGLImpl* pDSV,
                                                        Uint32             DefaultWidth  = 0,
                                                        Uint32             DefaultHeight = 0);

    GLObjectWrappers::GLFrameBufferObj& GetFBO(Uint32             NumRenderTargets,
                                               TextureViewGLImpl* ppRTVs[],
                                               TextureViewGLImpl* pDSV,
                                               GLContextState&    ContextState);

    const GLObjectWrappers::GLFrameBufferObj& GetFBO(Uint32 Width, Uint32 Height, GLContextState& ContextState);

    // NOTE: the function may bind a framebuffer, so the FBO in the GL context state must be invalidated.
    const GLObjectWrappers::GLFrameBufferObj& GetFBO(TextureBaseGL* pTex, Uint32 ArraySlice, Uint32 MipLevel, TextureBaseGL::FRAMEBUFFER_TARGET_FLAGS Targets);

    void OnReleaseTexture(ITexture* pTexture);

    void Clear();

private:
    // This structure is used as the key to find FBO
    struct FBOCacheKey
    {
        // Using pointers is not reliable!

        Uint32 NumRenderTargets = 0;

        // Unique IDs of textures bound as render targets
        UniqueIdentifier RTIds[MAX_RENDER_TARGETS] = {};
        TextureViewDesc  RTVDescs[MAX_RENDER_TARGETS];

        // Unique IDs of texture bound as depth stencil
        UniqueIdentifier DSId    = 0;
        TextureViewDesc  DSVDesc = {};

        Uint32 Width  = 0;
        Uint32 Height = 0;

        mutable size_t Hash = 0;

        bool operator==(const FBOCacheKey& Key) const noexcept;
    };

    struct FBOCacheKeyHashFunc
    {
        std::size_t operator()(const FBOCacheKey& Key) const noexcept;
    };


    friend class RenderDeviceGLImpl;
    Threading::SpinLock                                                                      m_CacheLock;
    std::unordered_map<FBOCacheKey, GLObjectWrappers::GLFrameBufferObj, FBOCacheKeyHashFunc> m_Cache;

    // Multimap that sets up correspondence between unique texture id and all
    // FBOs it is used in
    std::unordered_multimap<UniqueIdentifier, FBOCacheKey> m_TexIdToKey;
};

} // namespace Diligent
