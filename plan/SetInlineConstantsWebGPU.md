# SetInlineConstants (WebGPU, buffer-emulated) Build Plan

## Upstream Sync
- Git commit: `7d4a53f47d7a93914e9efc6aab6a3c3c9c23a149` (`Enable SetInlineConstants for WebGPU`)

## Goals
- Implement **buffer-emulated** `SetInlineConstants` for the WebGPU backend.
- Match D3D11 / OpenGL behavior 1:1 (signature-owned shared buffer, per-SRB CPU staging, update on commit).
- Keep validation and limits consistent (only constant buffers, `INLINE_CONSTANTS` is exclusive, max `MAX_INLINE_CONSTANTS` = 64 32-bit values).
- Support `DRAW_FLAG_INLINE_CONSTANTS_INTACT` to skip commit-time updates.

## Non-goals
- No public API changes.
- No dynamic offset usage for inline constant buffers (bind as regular uniform buffers).

## Core Design Decisions

| Decision Point | Choice |
|----------------|--------|
| Buffer allocation strategy | Signature-level shared dynamic UBO |
| Cache memory layout | Tail-append `Uint32[]` staging |
| Dynamic offset | Not used, bind as regular uniform buffer |
| Implicit signature error handling | Throw error when BufferStaticSize == 0 |
| Cross-signature SRB issue | Get buffer from SRB cache for update (D3D11 parity) |

## Desired Data Flow (D3D11 parity)
1) `SetInlineConstants()` only writes into SRB (or signature static cache) CPU staging.
2) Draw/dispatch commit checks inline-constant mask.
3) Inline-constant emulation buffers are updated at commit time by copying staging data into the GPU buffer.
4) Emulation buffers are bound as uniform buffers in WebGPU bind groups and used by shaders.

---

## Data Structures

### InlineConstantBufferAttribsWebGPU

```cpp
struct InlineConstantBufferAttribsWebGPU
{
    Uint32 BindGroup;     // Bind group index (similar to Vulkan's DescrSet)
    Uint32 CacheOffset;   // Offset within bind group (similar to Vulkan's SRBCacheOffset)
    Uint32 NumConstants;  // Number of 32-bit constants (= ResDesc.ArraySize)
    RefCntAutoPtr<BufferWebGPUImpl> pBuffer; // Signature-owned shared emulation buffer
};
```

### Signature Class Members

```cpp
Uint16 m_NumInlineConstantBuffers = 0;
Uint16 m_TotalInlineConstants = 0;
std::unique_ptr<InlineConstantBufferAttribsWebGPU[]> m_InlineConstantBuffers;
```

### ShaderResourceCacheWebGPU Memory Layout

```
| BindGroup[0..N] | Resource[0..M] | WGPUBindGroupEntry[0..M] | InlineConstantData[Uint32...] |
```

### Resource Structure Extension

```cpp
struct Resource
{
    // ... existing fields ...
    void* pInlineConstantData = nullptr;  // Points to staging data in tail

    void SetInlineConstants(const void* pData, Uint32 FirstConstant, Uint32 NumConstants);
};
```

---

## Implementation Steps

### 1) Define inline-constant metadata for WebGPU signatures

**Files**
- `Graphics/GraphicsEngineWebGPU/include/PipelineResourceSignatureWebGPUImpl.hpp`
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Changes**
- Add `InlineConstantBufferAttribsWebGPU` structure (see Data Structures section above)
- Add class members: `m_NumInlineConstantBuffers`, `m_TotalInlineConstants`, `m_InlineConstantBuffers`

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp` (`InlineConstantBufferAttribsGL`)

---

### 2) Fix ArraySize semantics for inline constants in implicit signatures

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineStateWebGPUImpl.cpp` (`GetDefaultResourceSignatureDesc`)

**Problem**
For inline constants, `PipelineResourceDesc::ArraySize` must be the **number of 32-bit constants**, not the uniform buffer array size (typically 1).

**Implementation**
```cpp
if (VarDesc.Flags & SHADER_VARIABLE_FLAG_INLINE_CONSTANTS)
{
    if (Attribs.BufferStaticSize == 0)
    {
        LOG_ERROR_AND_THROW("Unable to determine inline constant count for uniform buffer '",
                            Attribs.Name, "'. Please enable ShaderCreateInfo.LoadConstantBufferReflection.");
    }

    VERIFY(Attribs.BufferStaticSize % sizeof(Uint32) == 0,
           "Buffer size must be a multiple of 4 bytes");

    const Uint32 InlineConstantCount = Attribs.BufferStaticSize / sizeof(Uint32);

    if (InlineConstantCount > MAX_INLINE_CONSTANTS)
    {
        LOG_ERROR_AND_THROW("Inline constant count (", InlineConstantCount,
                            ") exceeds maximum allowed (", MAX_INLINE_CONSTANTS, ")");
    }

    // Use InlineConstantCount instead of Attribs.ArraySize
    SignDesc.AddResource(..., InlineConstantCount, ...);
}
```

**Reference**
- `Graphics/ShaderTools/include/WGSLShaderResources.hpp` (`WGSLShaderResourceAttribs::BufferStaticSize`)

---

### 3) Fix CreateBindGroupLayouts: use GetArraySize() for bindings/cache slots, and create shared buffers

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Problem**
`CreateBindGroupLayouts()` currently uses `ResDesc.ArraySize` for binding count and cache slots. For inline constants, this is the constant count, which incorrectly inflates bind group layout entries.

**Changes**
1. First pass to count inline constant buffers:
```cpp
for (const auto& ResDesc : m_Desc.Resources)
{
    if (ResDesc.Flags & PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS)
        ++m_NumInlineConstantBuffers;
}
if (m_NumInlineConstantBuffers > 0)
    m_InlineConstantBuffers = std::make_unique<InlineConstantBufferAttribsWebGPU[]>(m_NumInlineConstantBuffers);
```

2. Use `ResDesc.GetArraySize()` instead of `ResDesc.ArraySize` for:
   - Bind group entry count
   - Cache slot calculation
   - `PipelineResourceAttribsWebGPU::ArraySize` assignment

3. For each inline constant resource, create shared buffer:
```cpp
if (ResDesc.Flags & PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS)
{
    InlineConstantBufferAttribsWebGPU& InlineAttrib = m_InlineConstantBuffers[InlineCBIdx++];
    InlineAttrib.BindGroup    = BindGroupIdx;
    InlineAttrib.CacheOffset  = CacheOffset;
    InlineAttrib.NumConstants = ResDesc.ArraySize;
    InlineAttrib.pBuffer      = CreateInlineConstantBuffer(ResDesc.Name, ResDesc.ArraySize);
    m_TotalInlineConstants += static_cast<Uint16>(ResDesc.ArraySize);
}
```

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (`CreateLayout`)

---

### 4) Extend ShaderResourceCacheWebGPU: allocate inline-constant staging

**Files**
- `Graphics/GraphicsEngineWebGPU/include/ShaderResourceCacheWebGPU.hpp`
- `Graphics/GraphicsEngineWebGPU/src/ShaderResourceCacheWebGPU.cpp`

**Changes**
1. Add `void* pInlineConstantData = nullptr` to `Resource` struct
2. Add `bool m_HasInlineConstants = false` member
3. Update `HasInlineConstants()` to return `m_HasInlineConstants` (currently hardcoded `false`)
4. Update function signatures:
   - `GetRequiredMemorySize()` - add `Uint32 TotalInlineConstants` parameter
   - `InitializeGroups()` - add `Uint32 TotalInlineConstants` parameter
5. Update memory allocation to include staging tail: `MemSize += TotalInlineConstants * sizeof(Uint32)`
6. Extend `InitializeResources()` to accept inline-constant offsets and initialize `Resource::pInlineConstantData` to point into the staging tail

**New Methods**
```cpp
void InitInlineConstantBuffer(Uint32 BindGroupIdx, Uint32 CacheOffset,
                              IDeviceObject* pBuffer, Uint32 NumConstants,
                              Uint32 InlineConstantOffset);

void SetInlineConstants(Uint32 BindGroupIdx, Uint32 CacheOffset,
                        const void* pConstants, Uint32 FirstConstant, Uint32 NumConstants);

void CopyInlineConstants(const ShaderResourceCacheWebGPU& SrcCache,
                         Uint32 BindGroupIdx,
                         Uint32 SrcCacheOffset,
                         Uint32 DstCacheOffset,
                         Uint32 NumConstants);
```

**CRITICAL: SetInlineConstants must NOT call UpdateRevision()**

Inline constants are designed to be modified after SRB commit. Both D3D11 and OpenGL implementations do NOT call `UpdateRevision()` in their `SetInlineConstants()` methods.

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp` (memory layout, `CachedUB::pInlineConstantData`)
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourceCacheGL.cpp` (staging tail allocation)

---

### 5) Fix SRB cache memory sizing to include inline-constant staging

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Problem**
The constructor lambdas for `FixedBlockMemoryAllocator` must include `m_TotalInlineConstants` in the size estimate.

**Fix**
```cpp
// In both regular constructor and deserialization constructor:
[this]() {
    return ShaderResourceCacheWebGPU::GetRequiredMemorySize(..., m_TotalInlineConstants);
}
```

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (Step 3.5 in GL plan)

---

### 6) InitSRBResourceCache: bind shared emulation buffers and initialize staging

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Implementation**
```cpp
void PipelineResourceSignatureWebGPUImpl::InitSRBResourceCache(ShaderResourceCacheWebGPU& ResourceCache)
{
    ResourceCache.InitializeGroups(..., m_TotalInlineConstants);

    Uint32 InlineConstantOffset = 0;
    for (Uint32 i = 0; i < m_NumInlineConstantBuffers; ++i)
    {
        const InlineConstantBufferAttribsWebGPU& InlineAttrib = m_InlineConstantBuffers[i];
        ResourceCache.InitInlineConstantBuffer(
            InlineAttrib.BindGroup,
            InlineAttrib.CacheOffset,
            InlineAttrib.pBuffer,
            InlineAttrib.NumConstants,
            InlineConstantOffset);
        InlineConstantOffset += InlineAttrib.NumConstants;
    }
    VERIFY_EXPR(InlineConstantOffset == m_TotalInlineConstants);
}
```

**Static Cache Initialization**
- Only allocate staging space for **static variable type** inline constants
- Use `TotalStaticInlineConstants` (not `m_TotalInlineConstants`) to avoid "cache offset out of range" errors

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (`InitSRBResourceCache`, Step 4.5 in GL plan)

---

### 7) Implement ShaderVariableManagerWebGPU::SetInlineConstants

**Files**
- `Graphics/GraphicsEngineWebGPU/src/ShaderVariableManagerWebGPU.cpp`

**Current State**
```cpp
void ShaderVariableManagerWebGPU::SetInlineConstants(...)
{
    UNSUPPORTED("Not implemented yet");
}
```

**Implementation**
```cpp
void ShaderVariableManagerWebGPU::SetInlineConstants(Uint32      ResIndex,
                                                     const void* pConstants,
                                                     Uint32      FirstConstant,
                                                     Uint32      NumConstants)
{
    const PipelineResourceDesc& ResDesc = GetResourceDesc(ResIndex);
    const PipelineResourceAttribsWebGPU& Attr = GetResourceAttribs(ResIndex);

    VERIFY_EXPR(ResDesc.ResourceType == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER);

#ifdef DILIGENT_DEVELOPMENT
    VerifyInlineConstants(ResDesc, pConstants, FirstConstant, NumConstants);
#endif

    m_ResourceCache.SetInlineConstants(Attr.BindGroup, Attr.SRBCacheOffset,
                                       pConstants, FirstConstant, NumConstants);
}
```

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/ShaderVariableManagerGL.cpp` (`UniformBuffBindInfo::SetConstants`)

---

### 8) Commit path: update emulation buffers before Draw/Dispatch

**Files**
- `Graphics/GraphicsEngineWebGPU/src/DeviceContextWebGPUImpl.cpp`
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Add UpdateInlineConstantBuffers method**

**CRITICAL: Get buffer from SRB cache, not from signature's InlineAttrib.pBuffer**

This is essential for cross-signature SRB compatibility. See `plan/SetInlineConstants_InconsistencyCommit.md` for details.

```cpp
void PipelineResourceSignatureWebGPUImpl::UpdateInlineConstantBuffers(
    const ShaderResourceCacheWebGPU& ResourceCache,
    DeviceContextWebGPUImpl*         pCtx) const
{
    for (Uint32 i = 0; i < m_NumInlineConstantBuffers; ++i)
    {
        const InlineConstantBufferAttribsWebGPU& InlineAttrib = m_InlineConstantBuffers[i];

        // Get resource from SRB cache using BindGroup + CacheOffset
        const ShaderResourceCacheWebGPU::BindGroup& Group =
            ResourceCache.GetBindGroup(InlineAttrib.BindGroup);
        const ShaderResourceCacheWebGPU::Resource& CachedRes =
            Group.GetResource(InlineAttrib.CacheOffset);

        // Get buffer from SRB cache (same buffer that was bound by InitInlineConstantBuffer)
        BufferWebGPUImpl* pBuffer = CachedRes.pObject.RawPtr<BufferWebGPUImpl>();
        VERIFY(pBuffer != nullptr, "Inline constant buffer is null in SRB cache");

        const Uint32 DataSize = InlineAttrib.NumConstants * sizeof(Uint32);

        // Update the buffer from SRB cache staging data - no re-binding needed
        PVoid pMappedData = nullptr;
        pCtx->MapBuffer(pBuffer, MAP_WRITE, MAP_FLAG_DISCARD, pMappedData);
        memcpy(pMappedData, CachedRes.pInlineConstantData, DataSize);
        pCtx->UnmapBuffer(pBuffer, MAP_WRITE);
    }
}
```

**Design Principle**: The buffer that is bound should be the same buffer that is updated.
- `InitInlineConstantBuffer()` binds buffers into SRB cache
- `UpdateInlineConstantBuffers()` updates buffers from SRB cache
- The signature's `InlineAttrib.pBuffer` is only used during SRB initialization to populate the cache

**DeviceContextWebGPUImpl Call Sites**

Graphics path (in `PrepareForDraw()`):
```cpp
if (InlineConstantsSRBMask & SignBit)
{
    if (SRBStale || !InlineConstantsIntact)
        pSignature->UpdateInlineConstantBuffers(ResourceCache, this);
}
```

Compute path (in `PrepareForDispatchCompute()`):
- Always update (no intact flag available for compute)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp` (`BindProgramResources`)
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (`UpdateInlineConstantBuffers`)

---

### 9) Static inline constants: copy signature static staging into SRB staging

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Implementation in CopyStaticResources**
```cpp
// In CopyStaticResources(), for UNIFORM_BUFFER with INLINE_CONSTANTS flag:
if (ResDesc.Flags & PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS)
{
    // Copy inline constant staging data from signature cache to SRB cache
    DstResourceCache.CopyInlineConstants(
        SrcResourceCache,
        StaticGroupIdx,
        Attr.CacheOffset(SrcCacheType),
        Attr.CacheOffset(DstCacheType),
        ResDesc.ArraySize);
}
else
{
    // Regular uniform buffer copy logic
    // ...
}
```

**Behavior**
- Only copy when `InitStaticResources = true`
- After copy, signature static data modifications do not propagate to existing SRBs

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (`CopyStaticResources`)

---

### 10) Enable tests

**Files**
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp`

**Change**
```cpp
// Current:
if (!pDevice->GetDeviceInfo().IsD3DDevice() &&
    !pDevice->GetDeviceInfo().IsVulkanDevice() &&
    !pDevice->GetDeviceInfo().IsGLDevice())
{
    GTEST_SKIP() << "Inline constants are not supported by this device";
}

// Updated:
if (!pDevice->GetDeviceInfo().IsD3DDevice() &&
    !pDevice->GetDeviceInfo().IsVulkanDevice() &&
    !pDevice->GetDeviceInfo().IsGLDevice() &&
    !pDevice->GetDeviceInfo().IsWebGPUDevice())
{
    GTEST_SKIP() << "Inline constants are not supported by this device";
}
```

**Other test updates in this commit**
- Add `#include "MapHelper.hpp"` to allocate dynamic space in Vulkan (ensures dynamic offset is non-zero in `CrossSignatureSRB`)
- `CrossSignatureSRB`:
  - Use distinct signature names (`"Cross-Signature Test 1"` / `"Cross-Signature Test 2"`) and verify `pSign1 != pSign2`
  - Move `CommitShaderResources()` before any `SetInlineConstants()` calls to validate the “set-after-commit” semantics
  - Split position constants update into two partial updates + two draws to validate partial update path
- `RenderStateCache`: treat WebGPU like Vulkan for hash consistency (mark `PresentInCache = true` for WebGPU)

**Test Coverage**
- `InlineConstants.ResourceLayout` - Basic functionality
- `InlineConstants.ComputeResourceLayout` - Compute shader
- `InlineConstants.ResourceSignature` - Explicit signature
- `InlineConstants.TwoResourceSignatures` - Multiple signatures
- `InlineConstants.RenderStateCache` - Serialization/deserialization (cross-signature SRB)

---

## Files to Touch (Summary)

| File | Changes |
|------|---------|
| `PipelineResourceSignatureWebGPUImpl.hpp` | Add struct, members |
| `PipelineResourceSignatureWebGPUImpl.cpp` | CreateBindGroupLayouts, InitSRBResourceCache, UpdateInlineConstantBuffers, CopyStaticResources |
| `ShaderResourceCacheWebGPU.hpp` | Resource extension, memory layout, new methods |
| `ShaderResourceCacheWebGPU.cpp` | InitializeGroups, SetInlineConstants, CopyInlineConstants |
| `ShaderVariableManagerWebGPU.cpp` | SetInlineConstants implementation |
| `DeviceContextWebGPUImpl.cpp` | Commit path calls |
| `PipelineStateWebGPUImpl.cpp` | Implicit signature ArraySize fix |
| `InlineConstantsTest.cpp` | Enable WebGPU test |

---

## Acceptance Criteria

- `SetInlineConstants()` works in WebGPU and matches D3D11/GL semantics (staging writes, commit updates, partial updates).
- The implicit signature path (`ShaderVariableFlag INLINE_CONSTANTS`) works:
  - If the constant count can't be derived (UBO reflection disabled), the error message is actionable.
- `DRAW_FLAG_INLINE_CONSTANTS_INTACT` works: when unchanged, updates can be skipped and rendering/compute results remain correct.
- Cross-signature SRB scenario works correctly (RenderStateCache test passes).
