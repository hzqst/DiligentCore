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

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/include/PipelineResourceSignatureD3D11Impl.hpp:63` (`InlineConstantBufferAttribsD3D11`)
- `Graphics/GraphicsEngineD3D11/include/PipelineResourceSignatureD3D11Impl.hpp:142` (`m_InlineConstantBuffers`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/include/PipelineResourceSignatureVkImpl.hpp:69` (`InlineConstantBufferAttribsVk`)
- `Graphics/GraphicsEngineVulkan/include/PipelineResourceSignatureVkImpl.hpp:242` (`m_InlineConstantBuffers`)

**Status: COMPLETED**

**Changes Made**
- Added `InlineConstantBufferAttribsGL` structure with:
  - `Uint32 CacheOffset` - UBO cache slot offset for this resource
  - `Uint32 NumConstants` - Number of 32-bit constants (from ResDesc.ArraySize)
  - `RefCntAutoPtr<BufferGLImpl> pBuffer` - Shared dynamic UBO
- Added class members:
  - `Uint32 m_NumInlineConstantBuffers` - Number of inline constant buffers
  - `Uint16 m_TotalInlineConstants` - Total number of 32-bit constants
  - `std::unique_ptr<InlineConstantBufferAttribsGL[]> m_InlineConstantBuffers` - Inline constant buffer attributes array
- Added public methods:
  - `bool HasInlineConstants() const` - Returns true if signature has inline constants
  - `Uint32 GetNumInlineConstantBuffers() const` - Returns number of inline constant buffers
  - `Uint16 GetTotalInlineConstants() const` - Returns total inline constants count
  - `const InlineConstantBufferAttribsGL& GetInlineConstantBuffer(Uint32 Index) const` - Accessor for inline constant buffer attributes

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

**Status: COMPLETED**

**Changes Made**
1. **Added first pass to count inline constant buffers** (`PipelineResourceSignatureGLImpl.cpp:109-123`):
   - Added loop to count resources with `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`
   - Allocate `m_InlineConstantBuffers` array if count > 0
   - Initialize `InlineConstantBufferIdx` counter for second pass

2. **Fixed `GetArraySize()` usage for binding counts** (`PipelineResourceSignatureGLImpl.cpp:193`):
   - Changed from `ResDesc.ArraySize` to `ResDesc.GetArraySize()` for `ArraySize` variable
   - This correctly treats inline constants as single UBO slot instead of using constant count as array size

3. **Fixed dynamic UBO mask to exclude inline constants** (`PipelineResourceSignatureGLImpl.cpp:195-198`):
   - Changed condition from `PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS` check only
   - To `(PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS | PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS) == 0`
   - Inline constant buffers are internally managed and don't participate in dynamic buffer offset logic
   - Updated loop to use `ArraySize` (which is `GetArraySize()`) instead of `ResDesc.ArraySize`

4. **Created shared buffer for inline constants** (`PipelineResourceSignatureGLImpl.cpp:208-227`):
   - Added inline constant buffer handling block when `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` flag is set
   - Store `CacheOffset` and `NumConstants` in `InlineConstantBufferAttribsGL`
   - Create shared dynamic UBO via `CreateInlineConstantBuffer()` (inherited from base class)
   - Increment `m_TotalInlineConstants` counter with proper type cast to avoid C4244 warning
   - All SRBs share the same buffer to reduce memory usage

5. **Added validation assertion** (`PipelineResourceSignatureGLImpl.cpp:241`):
   - Added `VERIFY_EXPR(InlineConstantBufferIdx == m_NumInlineConstantBuffers)` after loop
   - Ensures all inline constant buffers were properly initialized

6. **Updated copyright year** (`PipelineResourceSignatureGLImpl.cpp:1`):
   - Updated from 2019-2025 to 2019-2026

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

**Status: COMPLETED**

**Changes Made**
1. **Updated memory layout documentation** (`ShaderResourceCacheGL.hpp:41-46`):
   - Updated the memory layout comment to include `Inline Constant Data | Uint32[] (tail)` section
   
2. **Extended `CachedUB` structure** (`ShaderResourceCacheGL.hpp:79-103`):
   - Added `void* pInlineConstantData = nullptr` member for CPU-side staging buffer pointer
   - Added `SetInlineConstants()` method with validation and memcpy logic

3. **Added `m_HasInlineConstants` flag** (`ShaderResourceCacheGL.hpp:233`):
   - Added `bool m_HasInlineConstants = false` member variable
   - Updated `HasInlineConstants()` method to return `m_HasInlineConstants` instead of hardcoded `false`

4. **Updated function signatures** (`ShaderResourceCacheGL.hpp`):
   - `GetRequiredMemorySize()`: Added `Uint32 TotalInlineConstants = 0` parameter
   - `Initialize()`: Added `Uint32 TotalInlineConstants = 0` parameter

5. **Updated `GetRequiredMemorySize()` implementation** (`ShaderResourceCacheGL.cpp:42-57`):
   - Added inline constant data tail allocation: `MemSize += TotalInlineConstants * sizeof(Uint32)`

6. **Updated `Initialize()` implementation** (`ShaderResourceCacheGL.cpp:59-82`):
   - Set `m_HasInlineConstants = (TotalInlineConstants > 0)`
   - Updated buffer size calculation to include inline constant data: `BufferSize = m_MemoryEndOffset + TotalInlineConstants * sizeof(Uint32)`

7. **Added helper methods** (`ShaderResourceCacheGL.hpp`):
   - `InitInlineConstantBuffer()`: Binds shared UBO and sets staging pointer
   - `SetInlineConstants()`: Writes inline constant data to staging buffer and updates revision
   - `CopyInlineConstants()`: Copies inline constant data between caches

8. **Fixed type conversion warning** (`PipelineResourceSignatureGLImpl.cpp:226`):
   - Added explicit cast: `m_TotalInlineConstants += static_cast<Uint16>(ResDesc.ArraySize)` to fix C4244 warning

### 4) Initialize SRB cache with inline constant buffers
**Files**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp`

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

**Status: COMPLETED**

**Changes Made**
1. **Added `GetInlineConstantDataPtr()` method** (`ShaderResourceCacheGL.hpp:367-374`):
   - Returns pointer to inline constant data at given offset (in number of 32-bit constants)
   - Data is stored at tail of resource cache memory, after `m_MemoryEndOffset`

2. **Updated `InitSRBResourceCache()`** (`PipelineResourceSignatureGLImpl.cpp:555-605`):
   - Pass `m_TotalInlineConstants` to `ResourceCache.Initialize()`
   - For each `InlineConstantBufferAttribsGL`:
     - Calculate pointer to inline constant data in the cache memory tail
     - Call `ResourceCache.InitInlineConstantBuffer()` to bind shared UBO and set staging pointer
   - Added verification that total inline constants match expected count

3. **Updated static cache initialization in `CreateLayout()`** (`PipelineResourceSignatureGLImpl.cpp:242-269`):
   - Pass `m_TotalInlineConstants` to `m_pStaticResCache->Initialize()`
   - For each inline constant buffer, call `InitInlineConstantBuffer()` on static cache
   - This allows static inline constants to be set on the signature and later copied to SRBs

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

**Status: COMPLETED**

**Changes Made**
1. **Implemented `UniformBuffBindInfo::SetConstants()`** (`ShaderVariableManagerGL.cpp:226-237`):
   - Added validation `VERIFY_EXPR(Desc.ResourceType == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)`
   - Added DEV build validation via `VerifyInlineConstants(Desc, pConstants, FirstConstant, NumConstants)`
   - Calls `m_ParentManager.m_ResourceCache.SetInlineConstants(Attr.CacheOffset, pConstants, FirstConstant, NumConstants)`
   - Mirrors D3D11 implementation pattern exactly

2. **Updated copyright year** (`ShaderVariableManagerGL.cpp:2`):
   - Changed from 2019-2025 to 2019-2026

### 6) Commit path: update UBOs before binding
**Files**
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/include/DeviceContextGLImpl.hpp`
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`

**Changes**
- Add `PipelineResourceSignatureGLImpl::UpdateInlineConstantBuffers(...)`
- In `DeviceContextGLImpl::BindProgramResources()`:
  - Update inline constants when SRB is stale or inline constants are not intact
  - Mirror D3D11 logic with `InlineConstantsSRBMask`
  - Respect `DRAW_FLAG_INLINE_CONSTANTS_INTACT` in graphics draw paths

**Compute Shader Path References**
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp:1406` (compute dispatch)
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp:1436` (compute indirect dispatch)

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/DeviceContextD3D11Impl.cpp:489` (inline-constant commit decision)
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:496` (`UpdateInlineConstantBuffers`)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp:418` (`CommitInlineConstants`)
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:1125` (`CommitInlineConstants`)

**Status: COMPLETED**

**Changes Made**
1. **Added `UpdateInlineConstantBuffers` method declaration** (`PipelineResourceSignatureGLImpl.hpp:145`):
   - Added method signature: `void UpdateInlineConstantBuffers(const ShaderResourceCacheGL& ResourceCache, class GLContextState& CtxState) const;`

2. **Implemented `UpdateInlineConstantBuffers`** (`PipelineResourceSignatureGLImpl.cpp:578-601`):
   - Loops through all inline constant buffers
   - For each buffer: reads staging data from cache, maps shared UBO with `MAP_WRITE | MAP_FLAG_DISCARD`, copies data, unmaps

3. **Added includes for buffer mapping** (`PipelineResourceSignatureGLImpl.cpp:35-36`):
   - Added `#include "BufferGLImpl.hpp"`
   - Added `#include "GLContextState.hpp"`

4. **Updated `BindProgramResources` signature** (`DeviceContextGLImpl.hpp:328`):
   - Changed from `void BindProgramResources(Uint32 BindSRBMask);`
   - To `void BindProgramResources(Uint32 BindSRBMask, bool DynamicBuffersIntact = false, bool InlineConstantsIntact = false);`

5. **Updated `BindProgramResources` implementation** (`DeviceContextGLImpl.cpp:701-776`):
   - Added `SRBStale` flag tracking
   - Updated VERIFY check to include `InlineConstantsSRBMask`
   - Added inline constant update block after resource binding:
     - Checks `InlineConstantsSRBMask & SignBit`
     - Verifies cache `HasInlineConstants()` consistency
     - Calls `UpdateInlineConstantBuffers` when `SRBStale || !InlineConstantsIntact`
   - Mirrors D3D11 implementation pattern exactly

6. **Updated `PrepareForDraw` call site** (`DeviceContextGLImpl.cpp:838-844`):
   - Extracts `DynamicBuffersIntact` and `InlineConstantsIntact` flags
   - Passes both flags to `BindProgramResources`

7. **Compute dispatch paths** (`DeviceContextGLImpl.cpp:1406, 1436`):
   - Use default parameter values `false` for both intact flags
   - Inline constants always updated for compute shaders (no intact flag available)

8. **Updated copyright years**:
   - `DeviceContextGLImpl.cpp`: 2019-2025 → 2019-2026
   - `DeviceContextGLImpl.hpp`: 2019-2025 → 2019-2026

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

**Status: COMPLETED**

**Changes Made**
1. **Added inline constants handling in `CopyStaticResources()`** (`PipelineResourceSignatureGLImpl.cpp:474-499`):
   - In the `BINDING_RANGE_UNIFORM_BUFFER` case, added check for `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` flag
   - For inline constants: call `DstResourceCache.CopyInlineConstants(SrcResourceCache, ResAttr.CacheOffset, ResDesc.ArraySize)`
   - Added verification that `INLINE_CONSTANTS` flag is exclusive (cannot be combined with other flags)
   - For regular uniform buffers: kept existing loop-based copy logic
   - Mirrors D3D11 implementation pattern exactly

### 8) Enable tests
- Reuse the existing multi-backend test in `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp` (graphics + compute coverage is already present).
- Enable OpenGL by removing/relaxing the backend guard in `InlineConstants::SetUpTestSuite()` so GL is not skipped once the impl is ready.
- No new test code is required; run the existing suite to validate inline constant updates and SRB behavior.

**Reference (Tests)**
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:211` (`InlineConstants::SetUpTestSuite`)
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:216` (backend skip guard)
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:281` (`TEST_F(InlineConstants, ResourceLayout)`)
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp:400` (`TEST_F(InlineConstants, ComputeResourceLayout)`)

**Status: COMPLETED**

**Changes Made**
1. **Updated backend skip guard** (`InlineConstantsTest.cpp:216`):
   - Changed from: `if (!pDevice->GetDeviceInfo().IsD3DDevice() && !pDevice->GetDeviceInfo().IsVulkanDevice())`
   - To: `if (!pDevice->GetDeviceInfo().IsD3DDevice() && !pDevice->GetDeviceInfo().IsVulkanDevice() && !pDevice->GetDeviceInfo().IsGLDevice())`
   - This enables the inline constants test suite for OpenGL backend in addition to D3D and Vulkan
   - All existing tests (ResourceLayout, ComputeResourceLayout, ResourceSignature, TwoResourceSignatures, RenderStateCache) will now run for OpenGL

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

2. **Step 1**: Define `InlineConstantBufferAttribsGL` structure
3. **Step 2**: Fix binding count calculation in `CreateLayout` (use `GetArraySize()`) and create shared buffer
4. **Step 3**: Extend `ShaderResourceCacheGL` for inline constant staging
5. **Step 4**: Initialize SRB cache and bind shared buffer
6. **Step 5**: Implement `ShaderVariableManagerGL::SetConstants`
7. **Step 6**: Add commit path in `DeviceContextGLImpl` (graphics + compute)
8. **Step 7**: Implement static inline constants copy
9. **Step 8**: Enable tests
