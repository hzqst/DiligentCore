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

### 1.5) Fix ArraySize in GetDefaultSignatureDesc for inline constants

**Files**
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourcesGL.hpp`
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourcesGL.cpp`
- `Graphics/GraphicsEngineOpenGL/src/PipelineStateGLImpl.cpp`

**Problem**
When using `CreateGraphicsPipelineState` without an explicit `PipelineResourceSignatureDesc`, the engine creates a default signature via `GetDefaultSignatureDesc()`. For inline constants, the `PipelineResourceDesc::ArraySize` must contain the **number of 32-bit constants** (e.g., 24 for 6 float4s), not the UBO array size (which is always 1).

The original code used `Attribs.ArraySize` directly for all uniform buffers, which is correct for regular UBOs but wrong for inline constants. This caused validation failures like:
```
Inline constant range (0 .. 23) is out of bounds for variable 'cbInlinePositions' of size 1 constants.
```

**Root Cause**
- `UniformBufferInfo::ArraySize` is always 1 for non-arrayed UBOs
- For inline constants, we need `BufferSize / sizeof(Uint32)` to get the constant count
- D3D11 handles this via `GetInlineConstantCountOrThrow()` which uses HLSL reflection data

**Changes**
1. **Add `BufferSize` to `UniformBufferInfo`** (`ShaderResourcesGL.hpp`):
   - Add `Uint32 BufferSize` member to store the uniform block data size
   - Add `Uint32 GetInlineConstantCount() const` method that returns `BufferSize / sizeof(Uint32)`

2. **Populate `BufferSize` during shader loading** (`ShaderResourcesGL.cpp`):
   - In `LoadUniforms()`, call `glGetActiveUniformBlockiv(GLProgram, UniformBlockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &BufferSize)`
   - Pass the buffer size to the `UniformBufferInfo` constructor

3. **Use constant count for inline constants** (`PipelineStateGLImpl.cpp`):
   - Create specialized `HandleUniformBuffer` handler in `GetDefaultSignatureDesc()`
   - When `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` is set:
     - Use `UB.GetInlineConstantCount()` instead of `UB.ArraySize`
     - Validate that the count is non-zero and does not exceed `MAX_INLINE_CONSTANTS`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineStateD3D11Impl.cpp:212` (`HandleCB` with `GetInlineConstantCountOrThrow()`)
- `Graphics/GraphicsEngineD3D11/include/ShaderD3DBase.hpp:89` (`D3DShaderResourceAttribs::GetInlineConstantCountOrThrow()`)

**Status: COMPLETED**

**Changes Made**
1. **Added `BufferSize` field to `UniformBufferInfo`** (`ShaderResourcesGL.hpp:76`):
   - Added `const Uint32 BufferSize` member
   - Updated both constructors to accept and initialize `BufferSize`
   - Added `GetInlineConstantCount()` method

2. **Populated `BufferSize` during loading** (`ShaderResourcesGL.cpp:230-232`):
   - Added `glGetActiveUniformBlockiv` call with `GL_UNIFORM_BLOCK_DATA_SIZE`
   - Passed buffer size to `UniformBufferInfo` constructor

3. **Created specialized handler** (`PipelineStateGLImpl.cpp:337-374`):
   - Added `HandleUniformBuffer` lambda that checks for `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`
   - For inline constants: uses `GetInlineConstantCount()` for `ArraySize`
   - Updated `ProcessConstResources` calls to use the specialized handler for UBs

4. **Updated copyright years** in all modified files

---

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
   - `SetInlineConstants()`: Writes inline constant data to staging buffer (**without** updating revision - see Step 8.5)
   - `CopyInlineConstants()`: Copies inline constant data between caches

8. **Fixed type conversion warning** (`PipelineResourceSignatureGLImpl.cpp:226`):
   - Added explicit cast: `m_TotalInlineConstants += static_cast<Uint16>(ResDesc.ArraySize)` to fix C4244 warning

### 3.5) Fix SRB memory size estimate to include inline constants

**Files**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`

**Problem**
When creating a pipeline resource signature, the `Initialize()` and `Deserialize()` functions use a lambda to estimate SRB cache memory size for the `FixedBlockMemoryAllocator`. If this estimate doesn't include inline constant storage, but `InitSRBResourceCache()` later allocates space for inline constants, the allocator will fail with:
```
Requested size (X) does not match the block size (Y)
```

**Root Cause**
The constructor lambdas were calling `GetRequiredMemorySize(m_BindingCount)` without passing `m_TotalInlineConstants`:
```cpp
// WRONG - defaults TotalInlineConstants to 0:
[this]() { return ShaderResourceCacheGL::GetRequiredMemorySize(m_BindingCount); }

// CORRECT - includes inline constant storage:
[this]() { return ShaderResourceCacheGL::GetRequiredMemorySize(m_BindingCount, m_TotalInlineConstants); }
```

Even though `CreateLayout()` populates `m_TotalInlineConstants` before the lambda is called, the lambda must explicitly pass it to `GetRequiredMemorySize()`.

**Changes**
1. **Regular constructor** (`PipelineResourceSignatureGLImpl.cpp:97`):
   - Changed `GetRequiredMemorySize(m_BindingCount)` to `GetRequiredMemorySize(m_BindingCount, m_TotalInlineConstants)`

2. **Deserialization constructor** (`PipelineResourceSignatureGLImpl.cpp:782`):
   - Same fix applied

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp:172` (uses `m_TotalInlineConstants` in size estimate)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp:156` (uses `m_TotalInlineConstants` in size estimate)

**Status: COMPLETED**

**IMPORTANT for future backend implementations:**
When adding inline constants support to a new backend, ensure the SRB memory size estimate lambda in both constructors includes `m_TotalInlineConstants`. This is easy to miss because:
1. The code compiles without it (parameter has default value of 0)
2. Tests pass until inline constants are actually used
3. The error message doesn't directly point to the root cause

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

### 4.5) Bug Fix: Only initialize static inline constants in static cache

**Problem**
When testing with mixed variable types (e.g., `Pos static, Col mutable`), the test failed with:
```
Debug assertion failed in GetConstUB(), file ShaderResourceCacheGL.hpp, line 310:
Uniform buffer index (1) is out of range
```

**Root Cause**
The static cache was being initialized with ALL inline constant buffers (`m_TotalInlineConstants`), including mutable and dynamic ones. However, the static cache only has space for static resources (`StaticResCounter`). When the code tried to call `InitInlineConstantBuffer` for a mutable inline constant buffer, it accessed a CacheOffset that was out of range for the static cache.

**Fix** (`PipelineResourceSignatureGLImpl.cpp:244-288`)
1. Calculate `StaticInlineConstants` by only counting inline constant buffers where `CacheOffset < StaticResCounter[BINDING_RANGE_UNIFORM_BUFFER]`
2. Pass `StaticInlineConstants` instead of `m_TotalInlineConstants` to `m_pStaticResCache->Initialize()`
3. Only call `InitInlineConstantBuffer()` for inline constant buffers that are within the static cache range

This mirrors the D3D11 approach which uses `ProcessInlineCBs` to filter inline constant buffers based on whether their binding is within the cache's resource count.

**Status: COMPLETED**

* This need to be carefully and properly handled with future backend implementations.

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

### 8.4) Bug Fix: HLSL2GLSLConverter not assigning explicit layout(binding=N) qualifiers to uniform blocks

**Files**
- `Graphics/HLSL2GLSLConverterLib/include/HLSL2GLSLConverterImpl.hpp`
- `Graphics/HLSL2GLSLConverterLib/src/HLSL2GLSLConverterImpl.cpp`

**Problem**
The `InlineConstants.RenderStateCache` test failed on OpenGL with:
```
error: buffer block with binding `0' has mismatching definitions
Failed to link program 0 for pipeline state 'Render State Cache Test'
```

**Root Cause**
The HLSL2GLSL converter was not assigning explicit `layout(binding=N)` qualifiers to uniform blocks (cbuffers), while it was already doing this for SSBOs and images. This caused OpenGL linking failures when:
- VS has `cbInlinePositions` (binding 0 by default) and `cbInlineColors` (binding 1 by default)
- PS has only `cbInlineColors` (binding 0 by default, since it's the first UBO in PS)

When linked together, binding 0 had conflicting definitions because:
- VS: `uniform cbInlinePositions { ... };` gets binding 0
- PS: `uniform cbInlineColors { ... };` also gets binding 0 (first UBO in shader)

The linker sees two different UBO definitions at binding point 0.

**Fix**
Modified `ProcessConstantBuffer()` in the HLSL2GLSL converter to add explicit binding qualifiers:
- Before: `cbuffer` → `uniform` or `layout(row_major) uniform`
- After: `cbuffer` → `layout(binding=N) uniform` or `layout(row_major, binding=N) uniform`

**Changes Made**
1. **Updated function signature** (`HLSL2GLSLConverterImpl.hpp:293`):
   - Changed `void ProcessConstantBuffer(TokenListType::iterator& Token);`
   - To `void ProcessConstantBuffer(TokenListType::iterator& Token, Uint32& UniformBlockBinding);`

2. **Updated implementation** (`HLSL2GLSLConverterImpl.cpp:915-928`):
   - Added `std::stringstream` to generate `layout(binding=N)` or `layout(row_major, binding=N)` prefix
   - Increment `UniformBlockBinding` counter after each cbuffer

3. **Updated call site** (`HLSL2GLSLConverterImpl.cpp:4563-4575`):
   - Added `Uint32 UniformBlockBinding = 0;` counter alongside existing `ShaderStorageBlockBinding` and `ImageBinding`
   - Pass counter to `ProcessConstantBuffer(Token, UniformBlockBinding)`

4. **Updated copyright years**: 2019-2025 → 2019-2026

**Status: COMPLETED**

**CRITICAL for HLSL-to-GLSL conversion:**
When multiple shaders with different uniform block sets are linked into a non-separable GL program, each shader stage independently assigns default binding points to its UBOs. Without explicit `layout(binding=N)` qualifiers, the linker will fail if different stages have different UBOs at the same default binding point. The fix ensures each cbuffer gets a unique, explicit binding in the order they appear in the source.

### 8.5) Bug Fix: SetInlineConstants must NOT call UpdateRevision

**Files**
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp`

**Problem**
After enabling tests, the `InlineConstants.ResourceLayout` test failed with:
```
Revision of the shader resource cache at index 0 does not match the revision recorded when the SRB was committed.
This indicates that resources have been changed since that time, but the SRB has not been committed with CommitShaderResources().
This usage is invalid.
```

**Root Cause**
The `ShaderResourceCacheGL::SetInlineConstants()` method was calling `UpdateRevision()` after writing inline constant data. This caused `DvpVerifyCacheRevisions()` to fail when inline constants were modified after `CommitShaderResources()` but before `Draw()`.

However, inline constants are **designed** to be modified after SRB commit - that's the entire purpose of the `InlineConstantsIntact` flag and `InlineConstantsSRBMask` mechanism. Both D3D11 and Vulkan implementations of `SetInlineConstants()` do NOT call `UpdateRevision()`.

**Test Flow That Exposed the Bug**:
1. `CommitShaderResources(pSRB)` - records current cache revision
2. `pColVarPS->SetInlineConstants(...)` - updates inline constants (incorrectly called `UpdateRevision()`)
3. `Draw()` → `PrepareForDraw()` → `GetCommitMask()` → `DvpVerifyCacheRevisions()` - detects revision mismatch and fails

**Fix**
Removed `UpdateRevision()` call from `ShaderResourceCacheGL::SetInlineConstants()` and added explanatory comment.

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp:776` - no `UpdateRevision()` call

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp:988` - no `UpdateRevision()` call

**Status: COMPLETED**

**CRITICAL for future backend implementations:**
When implementing inline constants for a new backend, the `SetInlineConstants()` method in the shader resource cache **MUST NOT** call `UpdateRevision()`. Inline constants are special resources that are allowed to change between SRB commits. The update mechanism is handled by:
- `InlineConstantsSRBMask` - tracks which SRBs have inline constants
- `InlineConstantsIntact` flag - indicates if inline constants have changed since last draw
- `UpdateInlineConstantBuffers()` - uploads staging data to GPU buffers during commit

## Files to Touch (expected)
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp`
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourceCacheGL.cpp`
- `Graphics/GraphicsEngineOpenGL/src/ShaderVariableManagerGL.cpp`
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourcesGL.hpp` (Step 1.5: add BufferSize for inline constants)
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourcesGL.cpp` (Step 1.5: populate BufferSize via GL_UNIFORM_BLOCK_DATA_SIZE)
- `Graphics/GraphicsEngineOpenGL/src/PipelineStateGLImpl.cpp` (Step 1.5: GetDefaultSignatureDesc inline constants handling)
- `Graphics/HLSL2GLSLConverterLib/include/HLSL2GLSLConverterImpl.hpp` (Step 8.4: add binding qualifiers to cbuffers)
- `Graphics/HLSL2GLSLConverterLib/src/HLSL2GLSLConverterImpl.cpp` (Step 8.4: add binding qualifiers to cbuffers)

## Acceptance Criteria
- `SetInlineConstants()` works for GL and mirrors D3D11 behavior.
- No binding count inflation from `ArraySize` when `INLINE_CONSTANTS` is set.
- Inline constants update only when SRB is stale or `DRAW_FLAG_INLINE_CONSTANTS_INTACT` is not set.
- Static inline constants propagate into SRB caches on creation.


### 8.6) Bug Fix: Re-bind inline constant buffers after update when using compatible SRB

**Files**
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`

**Problem**
In `TEST_F(InlineConstants, RenderStateCache)`, the second call to `VerifyPSOFromCache(pPSO, pRefSRB)` draws nothing because inline constants are all zeros. RenderDoc inspection shows that the GL binding slots have different UBO objects bound compared to the first draw.

**Root Cause**
When an SRB created from one signature (`pRefPSO`) is used with a different but compatible signature (`pPSO`):
1. `BindResources()` binds UBOs from the SRB's cache, which stores pointers to `pRefPSO`'s signature's shared inline constant buffers
2. `UpdateInlineConstantBuffers()` uploads staging data to `pPSO`'s signature's shared inline constant buffers
3. The GL binding slots have `pRefPSO`'s buffers bound, but the data was uploaded to `pPSO`'s buffers
4. Shaders read from the bound buffers (`pRefPSO`'s), which contain zeros/stale data

**Fix**
Modified `UpdateInlineConstantBuffers()` to:
1. Accept a `const TBindings& BaseBindings` parameter to know the GL binding points
2. After uploading data to each inline constant buffer, re-bind the signature's shared buffer to the correct GL slot

This ensures that even when an SRB from a compatible but different signature is used, the current signature's shared buffers (which now contain the updated data) are properly bound.

**Changes Made**
1. **Updated function signature** (`PipelineResourceSignatureGLImpl.hpp:149-151`):
   - Added `const TBindings& BaseBindings` parameter

2. **Updated implementation** (`PipelineResourceSignatureGLImpl.cpp:608-642`):
   - After `Unmap()`, call `BufferMemoryBarrier()` and `BindUniformBuffer()` to re-bind the buffer
   - Binding point is calculated as `BaseBindings[BINDING_RANGE_UNIFORM_BUFFER] + CacheOffset`

3. **Updated call site** (`DeviceContextGLImpl.cpp:759`):
   - Pass `BaseBindings` to `UpdateInlineConstantBuffers()`

**Status: COMPLETED**

**CRITICAL for future backend implementations:**
In OpenGL, each signature creates its own shared inline constant buffers. When an SRB from a compatible but different signature is used, the SRB's cache contains buffer pointers to the original signature's buffers. After updating inline constants, the current signature's buffers must be explicitly bound to override the previously bound buffers.

---

## Recommended Implementation Order

1. **Step 1**: Define `InlineConstantBufferAttribsGL` structure
2. **Step 1.5**: Fix ArraySize in `GetDefaultSignatureDesc` for inline constants (add `BufferSize` to `UniformBufferInfo`)
3. **Step 2**: Fix binding count calculation in `CreateLayout` (use `GetArraySize()`) and create shared buffer
4. **Step 3**: Extend `ShaderResourceCacheGL` for inline constant staging
5. **Step 3.5**: Fix SRB memory size estimate to include `m_TotalInlineConstants` in constructor lambdas
6. **Step 4**: Initialize SRB cache and bind shared buffer
7. **Step 4.5**: Bug fix - only initialize static inline constants in static cache
8. **Step 5**: Implement `ShaderVariableManagerGL::SetConstants`
9. **Step 6**: Add commit path in `DeviceContextGLImpl` (graphics + compute)
10. **Step 7**: Implement static inline constants copy
11. **Step 8**: Enable tests
12. **Step 8.4**: Bug fix - HLSL2GLSLConverter not assigning explicit layout(binding=N) qualifiers to uniform blocks (cbuffers)
13. **Step 8.5**: Bug fix - `SetInlineConstants` must NOT call `UpdateRevision()`
14. **Step 8.6**: Bug fix - Re-bind inline constant buffers after update when using compatible SRB
