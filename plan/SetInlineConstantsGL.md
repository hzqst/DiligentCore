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

### 0) Add inline constants validation in `CreateLayout`
**Files**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`

**Changes**
- At the beginning of resource processing in `CreateLayout()`, add validation for inline constants:
```cpp
if (ResDesc.Flags & PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS)
{
    DEV_CHECK_ERR(ResDesc.ResourceType == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER,
                  "Only constant buffers can have INLINE_CONSTANTS flag");
    DEV_CHECK_ERR((ResDesc.Flags & ~PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS) == 0,
                  "INLINE_CONSTANTS flag cannot be combined with other flags");
    DEV_CHECK_ERR(ResDesc.ArraySize <= MAX_INLINE_CONSTANTS,
                  "ArraySize exceeds MAX_INLINE_CONSTANTS");
}
```

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:157` (inline constants only for CBs)
- `Graphics/GraphicsEngineD3D11/src/PipelineStateD3D11Impl.cpp:96` (flag exclusivity)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:200` (inline constants only for CBs)
- `Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp:702` (flag exclusivity)

**Status: COMPLETED** - No changes required.
Step 0 is already complete. No additional validation code needs to be added in GL's `CreateLayout`. The `DEV_CHECK_ERR` validations suggested in the original plan would be redundant because the base class constructor's `ValidatePipelineResourceSignatureDesc` (in `Graphics/GraphicsEngine/src/PipelineResourceSignatureBase.cpp`) executes all these checks before `CreateLayout` is called:

1. **Only constant buffers can have INLINE_CONSTANTS flag** - Validated via `GetValidPipelineResourceFlags()` at line 106-111
2. **INLINE_CONSTANTS flag cannot be combined with other flags** - Validated at lines 115-119
3. **ArraySize cannot exceed MAX_INLINE_CONSTANTS** - Validated at lines 122-126

The GL signature implementation inherits from `TPipelineResourceSignatureBase`, whose constructor calls `ValidatePipelineResourceSignatureDesc` which performs all necessary inline constants validation.

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

**Existing Bugs to Fix**

The current code has bugs that will cause incorrect behavior for inline constants:

1. **CacheOffset calculation** (`PipelineResourceSignatureGLImpl.cpp:188`):
   ```cpp
   // Current (wrong):
   CacheOffset += static_cast<TBindings::value_type>(ResDesc.ArraySize);
   // Should be:
   CacheOffset += static_cast<TBindings::value_type>(ResDesc.GetArraySize());
   ```
   This causes inline constants' `ArraySize` (number of 32-bit constants) to be incorrectly treated as UBO array length.

2. **Dynamic UBO mask calculation** (`PipelineResourceSignatureGLImpl.cpp:174-177`):
   ```cpp
   // Current (wrong):
   for (Uint64 elem = 0; elem < ResDesc.ArraySize; ++elem)
       m_DynamicUBOMask |= Uint64{1} << (Uint64{CacheOffset} + elem);
   // Should use ResDesc.GetArraySize() instead
   ```
   For inline constants, this sets too many bits in the dynamic UBO mask.

3. **Static cache counter** (`PipelineResourceSignatureGLImpl.cpp:191-195`):
   Since `CacheOffset` is incorrectly incremented, the static resource counter will also be wrong.

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
- Add a `m_HasInlineConstants` flag.
  - **Important**: Set this flag in `Initialize()` based on `TotalInlineConstants > 0`.
  - Update existing stub `HasInlineConstants()` to return `m_HasInlineConstants`.
- Update `Initialize()` signature to add `Uint32 TotalInlineConstants` parameter:
  ```cpp
  // Current:
  void Initialize(const GLResourceCounters& ResCount, IMemoryAllocator& Allocator, 
                  Uint64 DynamicUBOMask, Uint64 DynamicSSBOMask);
  // Change to:
  void Initialize(const GLResourceCounters& ResCount, IMemoryAllocator& Allocator, 
                  Uint64 DynamicUBOMask, Uint64 DynamicSSBOMask, Uint32 TotalInlineConstants);
  ```
- Update memory layout to include `InlineConstantData` tail:
  - `GetRequiredMemorySize(ResCount, TotalInlineConstants)`
- Add helpers:
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
  - **Important**: Call `UpdateInlineConstantBuffers()` at the **beginning** of `BindProgramResources` (before binding resources).
  - If SRB has inline constants and (SRB stale or not intact), call `UpdateInlineConstantBuffers()`.
  - Mirror D3D11 logic:
    - use `m_BindInfo.InlineConstantsSRBMask`
    - verify `ResourceCache.HasInlineConstants()`
    - respect `DRAW_FLAG_INLINE_CONSTANTS_INTACT` in graphics draw paths
  - Compute paths: Both compute dispatch paths call `BindProgramResources`, so inline constant updates will be handled automatically if the check is placed correctly within that function.

**Compute Shader Path References**
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp:1367` (compute dispatch)
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp:1397` (compute indirect dispatch)

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

### 8) Enable tests
- Reuse the existing multi-backend test in `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp` (graphics + compute coverage is already present).
- Enable OpenGL by removing/relaxing the backend guard in `InlineConstants::SetUpTestSuite()` so GL is not skipped once the impl is ready.
- No new test code is required; run the existing suite to validate inline constant updates and SRB behavior.

**Reference (Tests)**
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:211` (`InlineConstants::SetUpTestSuite`)
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:216` (backend skip guard)
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:281` (`TEST_F(InlineConstants, ResourceLayout)`)
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:400` (`TEST_F(InlineConstants, ComputeResourceLayout)`)

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

## Optional (Not MVP)
- **Serialization/deserialization support**: If PSO cache/archive support is needed, `InlineConstantBufferAttribsGL` serialization and deserialization must also be handled. This can be deferred.

---

## Recommended Implementation Order

1. **Step 0**: Add inline constants validation in `CreateLayout`
2. **Step 1**: Define `InlineConstantBufferAttribsGL` structure
3. **Step 2**: Fix binding count calculation in `CreateLayout` (use `GetArraySize()`) and create shared buffer
4. **Step 3**: Extend `ShaderResourceCacheGL` for inline constant staging
5. **Step 4**: Initialize SRB cache and bind shared buffer
6. **Step 5**: Implement `ShaderVariableManagerGL::SetConstants`
7. **Step 6**: Add commit path in `DeviceContextGLImpl` (graphics + compute)
8. **Step 7**: Implement static inline constants copy
9. **Step 8**: Enable tests
