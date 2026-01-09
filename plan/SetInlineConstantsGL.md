# SetInlineConstants (OpenGL, UBO-emulated) Build Plan

## Goals
- Implement `SetInlineConstants` for OpenGL using uniform-buffer emulation.
- Match D3D11 behavior 1:1 (shared per-signature UBO, per-SRB CPU staging, update on commit).
- Keep validation and limits consistent (only CBs, `INLINE_CONSTANTS` is exclusive, max 64 constants).

## Non-goals
- No push-constant path for OpenGL.
- No API surface changes.

## Desired Data Flow (D3D11 parity)
1) `SetInlineConstants()` writes into SRB (or signature) cache staging.
2) Draw/dispatch commit checks inline-constant mask.
3) Inline constant buffers are updated via map/copy/unmap of shared UBO(s).
4) UBOs are bound to GL binding points and used by shaders.

## Implementation Steps

### 1) Define inline-constant metadata for GL signatures
**Files**
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`

**Changes**
- Add `InlineConstantBufferAttribsGL`:
  - `Uint32 CacheOffset` (UBO cache slot for this resource)
  - `Uint32 NumConstants` (ResDesc.ArraySize)
  - `RefCntAutoPtr<BufferGLImpl> pBuffer` (shared dynamic UBO)
- Add members:
  - `std::vector<InlineConstantBufferAttribsGL> m_InlineConstantBuffers`
  - `Uint16 m_TotalInlineConstants`
- Add `HasInlineConstants()` to PRS (useful for commit path).

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/include/PipelineResourceSignatureD3D11Impl.hpp:63` (`InlineConstantBufferAttribsD3D11`)
- `Graphics/GraphicsEngineD3D11/include/PipelineResourceSignatureD3D11Impl.hpp:142` (`m_InlineConstantBuffers`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/include/PipelineResourceSignatureVkImpl.hpp:69` (`InlineConstantBufferAttribsVk`)
- `Graphics/GraphicsEngineVulkan/include/PipelineResourceSignatureVkImpl.hpp:242` (`m_InlineConstantBuffers`)

### 2) Fix binding counts for inline constants in GL layout
**Files**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`

**Changes**
- In `CreateLayout()`:
  - Use `ResDesc.GetArraySize()` (not `ResDesc.ArraySize`) when:
    - advancing `CacheOffset` and `StaticResCounter`
    - building dynamic UBO/SSBO masks
  - When `ResDesc.Flags & PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`:
    - allocate a shared dynamic UBO via `CreateInlineConstantBuffer(ResDesc.Name, ResDesc.ArraySize)`
    - push `InlineConstantBufferAttribsGL{CacheOffset, ResDesc.ArraySize, pBuffer}`
    - increment `m_TotalInlineConstants += ResDesc.ArraySize`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:217` (`ResDesc.GetArraySize()` for binding count)
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:248` (inline-constant buffer creation)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:184` (`ResDesc.GetArraySize()` for descriptor count)
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:301` (`ResArraySize` for bindings)

### 3) Extend ShaderResourceCacheGL for inline-constant staging
**Files**
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp`
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourceCacheGL.cpp`

**Changes**
- Extend `CachedUB` with:
  - `void* pInlineConstantData = nullptr`
  - `void SetInlineConstants(const void* pSrc, Uint32 First, Uint32 Num)`
- Add a `m_HasInlineConstants` flag and `HasInlineConstants()` implementation.
- Update memory layout to include `InlineConstantData` tail:
  - `GetRequiredMemorySize(ResCount, TotalInlineConstants)`
  - `Initialize(ResCount, MemAllocator, DynamicUBOMask, DynamicSSBOMask, TotalInlineConstants)`
- Add helper:
  - `InitInlineConstantBuffer(CacheOffset, pBuffer, NumConstants, pInlineData)`
  - `SetInlineConstants(CacheOffset, pConstants, First, Num)`
  - `CopyInlineConstants(SrcCache, CacheOffset, NumConstants)`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp:95` (`CachedCB::pInlineConstantData`)
- `Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp:146` (`CachedCB::SetInlineConstants`)
- `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp:118` (inline-constant storage sizing)
- `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp:270` (`InitInlineConstantBuffer`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/include/ShaderResourceCacheVk.hpp:141` (`Resource::pInlineConstantData`)
- `Graphics/GraphicsEngineVulkan/include/ShaderResourceCacheVk.hpp:280` (`ShaderResourceCacheVk::SetInlineConstants`)
- `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp:52` (inline-constant storage sizing)
- `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp:988` (`ShaderResourceCacheVk::SetInlineConstants`)

### 4) Initialize SRB cache with inline constant buffers
**Files**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`

**Changes**
- `InitSRBResourceCache()`:
  - call `ResourceCache.Initialize(..., m_TotalInlineConstants)`
  - for each `InlineConstantBufferAttribsGL`:
    - bind shared buffer via `ResourceCache.SetUniformBuffer(CacheOffset, pBuffer, 0, NumConstants * 4)`
    - set staging pointer via `InitInlineConstantBuffer(...)`
- Static cache initialization:
  - allocate inline-constant staging for the signature cache as well
  - ensure static inline constants are stored even before SRB creation

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:400` (`InitSRBResourceCache`)
- `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp:270` (`InitInlineConstantBuffer`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:574` (`InitSRBResourceCache`)
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:583` (`InitializeSets` with inline constants)
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:628` (bind shared UBOs into cache)

### 5) Implement SetInlineConstants in ShaderVariableManagerGL
**Files**
- `Graphics/GraphicsEngineOpenGL/src/ShaderVariableManagerGL.cpp`

**Changes**
- Implement `UniformBuffBindInfo::SetConstants()`:
  - DEV build: `VerifyInlineConstants(Desc, pConstants, First, Num)`
  - `m_ParentManager.m_ResourceCache.SetInlineConstants(Attr.CacheOffset, pConstants, First, Num)`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/ShaderVariableManagerD3D11.cpp:304` (`ConstBuffBindInfo::SetConstants`)
- `Graphics/GraphicsEngineD3D11/src/ShaderVariableManagerD3D11.cpp:313` (calls `SetInlineConstants`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp:654` (`ShaderVariableManagerVk::SetInlineConstants`)
- `Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp:673` (calls `ResourceCache.SetInlineConstants`)

### 6) Commit path: update UBOs before binding
**Files**
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`

**Changes**
- Add `PipelineResourceSignatureGLImpl::UpdateInlineConstantBuffers(...)`:
  - For each `InlineConstantBufferAttribsGL`:
    - read `pInlineConstantData` from cache
    - map shared UBO (MAP_WRITE | MAP_FLAG_DISCARD)
    - memcpy `NumConstants * 4`
    - unmap
- In `DeviceContextGLImpl::BindProgramResources()`:
  - If SRB has inline constants and (SRB stale or not intact), call `UpdateInlineConstantBuffers()` before binding.
  - Mirror D3D11 logic:
    - use `m_BindInfo.InlineConstantsSRBMask`
    - verify `ResourceCache.HasInlineConstants()`
    - respect `DRAW_FLAG_INLINE_CONSTANTS_INTACT` in graphics draw paths
  - Compute paths keep default behavior (always update when present), matching D3D11.

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/DeviceContextD3D11Impl.cpp:489` (inline-constant commit decision)
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:496` (`UpdateInlineConstantBuffers`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp:418` (`CommitInlineConstants`)
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:1125` (`CommitInlineConstants`)

### 7) Copy static inline constants into SRB cache
**Files**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`

**Changes**
- In `CopyStaticResources()`:
  - For inline-constant resources:
    - copy staging data from signature cache to SRB cache using `CopyInlineConstants(...)`
  - Ensure loops use `ResDesc.GetArraySize()` for binding arrays.

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:330` (inline-constant branch in `CopyStaticResources`)
- `Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp:863` (`CopyInlineConstants`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:685` (copy inline-constant staging data)

### 8) Validation and edge cases
- Ensure inline constants:
  - only constant buffers
  - flag is exclusive
  - `ArraySize <= MAX_INLINE_CONSTANTS`
- Ensure GL binding counts do not treat `ArraySize` as UBO array length when `INLINE_CONSTANTS` is set.
- Confirm shared UBO per signature (not per SRB), per D3D11.

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:157` (inline constants only for CBs)
- `Graphics/GraphicsEngineD3D11/src/PipelineStateD3D11Impl.cpp:96` (flag exclusivity)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:200` (inline constants only for CBs)
- `Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp:702` (flag exclusivity)

## Testing Plan
- Add or reuse a small GL test that:
  - defines inline constants in PRS (static + mutable/dynamic)
  - calls `SetInlineConstants()` with partial updates
  - verifies data reaches shader (draw & dispatch)
  - uses `DRAW_FLAG_INLINE_CONSTANTS_INTACT` to skip updates on repeated draws
- Validate SRB stale vs intact behavior:
  - change constants between draws -> update happens
  - no change + intact flag -> no map/unmap

## Files to Touch (expected)
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp`
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourceCacheGL.cpp`
- `Graphics/GraphicsEngineOpenGL/src/ShaderVariableManagerGL.cpp`
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`

## Acceptance Criteria
- `SetInlineConstants()` works for GL and mirrors D3D11 behavior.
- No binding count inflation from `ArraySize` when `INLINE_CONSTANTS` is set.
- Inline constants update only when SRB is stale or `DRAW_FLAG_INLINE_CONSTANTS_INTACT` is not set.
- Static inline constants propagate into SRB caches on creation.
