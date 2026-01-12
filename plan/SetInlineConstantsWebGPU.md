# SetInlineConstants (WebGPU, buffer-emulated) Build Plan

## Goals
- Implement **buffer-emulated** `SetInlineConstants` for the WebGPU backend.
- Match D3D11 / OpenGL behavior 1:1 (signature-owned shared buffer, per-SRB CPU staging, update on commit).
- Keep validation and limits consistent (only constant buffers, `INLINE_CONSTANTS` is exclusive, max `MAX_INLINE_CONSTANTS` = 64 32-bit values).
- Support `DRAW_FLAG_INLINE_CONSTANTS_INTACT` to skip commit-time updates.

## Non-goals
- No public API changes.

## Desired Data Flow (D3D11 parity)
1) `SetInlineConstants()` only writes into SRB (or signature static cache) CPU staging.
2) Draw/dispatch commit checks inline-constant mask.
3) Inline-constant emulation buffers are updated at commit time by copying staging data into the GPU buffer.
4) Emulation buffers are bound as uniform buffers in WebGPU bind groups and used by shaders.

---

## Implementation Steps

### 1) Define inline-constant metadata for WebGPU signatures

**Files**
- `Graphics/GraphicsEngineWebGPU/include/PipelineResourceSignatureWebGPUImpl.hpp`
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/include/PipelineResourceSignatureD3D11Impl.hpp` (inline-constant buffer attribs and array)
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp` (create shared buffer, commit updates)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/include/PipelineResourceSignatureVkImpl.hpp` (inline-constant buffer attribs and array)
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (init SRB cache, commit inline constants)

**Reference (OpenGL)**
- `plan/SetInlineConstantsGL.md` (structure, staging, commit timing)
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp` (inline-constant metadata)
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (init SRB cache, update inline constant buffers)

**What to add (recommended semantics)**
- `InlineConstantBufferAttribsWebGPU`:
  - `Uint32 ResIndex`: index into `m_Desc.Resources[]`
  - `Uint32 NumConstants`: number of 32-bit constants (= `ResDesc.ArraySize`)
  - `Uint32 BufferSize`: actual byte size written into the emulation buffer (align to WebGPU uniform buffer requirements)
  - `Uint32 SRBCacheOffset` / `Uint32 StaticCacheOffset`: cache slot offsets
  - `Uint32 StagingOffset`: start index into staging `Uint32[]`
  - `RefCntAutoPtr<BufferWebGPUImpl> pBuffer`: signature-owned shared emulation buffer
- `Uint16 m_NumInlineConstantBuffers`
- `Uint16 m_TotalInlineConstants`
- `std::unique_ptr<InlineConstantBufferAttribsWebGPU[]> m_InlineConstantBuffers`

---

### 2) Fix ArraySize semantics for inline constants in implicit signatures

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineStateWebGPUImpl.cpp` (`PipelineStateWebGPUImpl::GetDefaultResourceSignatureDesc`)

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineStateD3D11Impl.cpp` (default signature generation / inline constant count semantics)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp` (default signature generation / SPIR-V reflection path)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/PipelineStateGLImpl.cpp` (GetDefaultResourceSignatureDesc + inline-constant ArraySize fix)

**Problem**
The WebGPU default signature generation currently uses `WGSLShaderResourceAttribs::ArraySize` as `PipelineResourceDesc::ArraySize`.
For inline constants, `PipelineResourceDesc::ArraySize` must be the **number of 32-bit constants**, not the uniform buffer array size (typically 1).

**Strategy**
- When `VarDesc.Flags` contains `SHADER_VARIABLE_FLAG_INLINE_CONSTANTS`:
  - Require `Attribs.BufferStaticSize != 0` to derive the constant count; otherwise, raise a clear error telling the user to enable `ShaderCreateInfo.LoadConstantBufferReflection = true`.
  - Compute `InlineConstantCount = Attribs.BufferStaticSize / sizeof(Uint32)` and validate:
    - `Attribs.BufferStaticSize % 4 == 0`
    - `InlineConstantCount <= MAX_INLINE_CONSTANTS`
  - Use `InlineConstantCount` instead of `Attribs.ArraySize` when calling `SignDesc.AddResource(...)` for `ArraySize`.

**Reference**
- `Graphics/ShaderTools/include/WGSLShaderResources.hpp` (`WGSLShaderResourceAttribs::BufferStaticSize`)

---

### 3) Fix CreateBindGroupLayouts: use GetArraySize() for bindings/cache slots, and create shared buffers

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp` (CreateLayout + shared inline constant buffer creation)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (CreateLayout + inline-constant bindings)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (CreateLayout + GetArraySize handling for inline constants)

**Problem**
`CreateBindGroupLayouts()` currently uses `ResDesc.ArraySize` in several places. For inline constants, this is the constant count,
which incorrectly inflates bind group layout entries and cache slots.

**Fix**
- Replace all usages that represent **descriptor array size / binding count / cache slot count** with `ResDesc.GetArraySize()` (should be 1 for inline constants).
- Store `ResDesc.GetArraySize()` in `PipelineResourceAttribsWebGPU::ArraySize` and update deserialization consistency checks accordingly.
- Create a signature-owned shared emulation buffer for each inline-constant resource during layout creation (via `PipelineResourceSignatureBase::CreateInlineConstantBuffer()` or WebGPU-specific logic).

---

### 4) Extend ShaderResourceCacheWebGPU: allocate inline-constant staging and support HasInlineConstants()

**Files**
- `Graphics/GraphicsEngineWebGPU/include/ShaderResourceCacheWebGPU.hpp`
- `Graphics/GraphicsEngineWebGPU/src/ShaderResourceCacheWebGPU.cpp`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp` (cached CB holds `pInlineConstantData`)
- `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp` (staging tail allocation and initialization)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/include/ShaderResourceCacheVk.hpp` (resource holds `pInlineConstantData`)
- `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp` (inline constants staging init / SetInlineConstants write)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp` (UB cache holds `pInlineConstantData`)
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourceCacheGL.cpp` (staging tail allocation and initialization)

**What to add**
- Make `HasInlineConstants()` return the real state (not hardcoded `false`) so `CommittedShaderResources::InlineConstantsSRBMask` works as intended.
- Append `Uint32` staging storage to the end of the cache memory layout:
  - Size: `m_TotalInlineConstants * sizeof(Uint32)`
  - Needed for both SRB and static caches
- Store `pInlineConstantData` directly on the cache resource for inline-constant buffers:
  - Initialize it to point into the staging tail allocation.
  - `SetInlineConstants(...)` writes directly into `pInlineConstantData` (write staging only; do not update GPU immediately).
  - `CommitInlineConstants(...)` reads directly from `pInlineConstantData` (no extra getter is required).

---

### 5) Fix SRB cache memory sizing to include inline-constant staging

**Files/Entrypoints**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp` (cache size calculation for signature creation/deserialization)
- `Graphics/GraphicsEngineWebGPU/include/ShaderResourceCacheWebGPU.hpp` / `.cpp`

**Goal**
Ensure every `ShaderResourceCacheWebGPU` allocation path includes the extra inline-constant staging space.

---

### 6) InitSRBResourceCache: bind the shared emulation buffers and initialize staging offsets

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp` (`InitSRBResourceCache`)

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp` (InitInlineConstantBuffer binds shared buffer into cache)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (InitSRBResourceCache binds emulation buffers / sets staging pointers)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (InitSRBResourceCache binds UBOs / sets staging pointers)

**Goal**
- For each inline-constant resource:
  - Bind the signature-owned shared emulation buffer into the SRB cache slot (`ResourceCache.SetResource(...)`)
  - Set/record the offset into SRB staging (used by `SetInlineConstants` writes and `CommitInlineConstants` reads)

**Design note (recommended to match D3D11/GL semantics)**
- Contract: `DRAW_FLAG_INLINE_CONSTANTS_INTACT` (and `DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT`) is intended to be used only between adjacent commands within the same frame. Using these flags across frames is undefined behavior.
- With this contract, `USAGE_DYNAMIC` is appropriate for inline-constant emulation (D3D11/GL-style discard + copy). Note that intact flags never suppress updates for stale SRBs, so the first draw/dispatch using a newly committed SRB will still process dynamic buffers/inline constants even if the flags are set (consistent across backends).
- WebGPU detail: `USAGE_DYNAMIC` uniform buffers are typically suballocated from the global dynamic buffer; commit should map with discard, write the data, and rely on dynamic offsets when binding.

---

### 7) Bug fix: static inline-constant initialization boundaries

**Goal**
- Signature static variable `SetInlineConstants()` only writes into the signature static cache staging.
- Only when SRB is created with `InitStaticResources=true`, copy static staging into SRB staging; later updates to signature static data must not propagate to existing SRBs (as documented in `doc/SetInlineConstants.md`).

---

### 8) Implement ShaderVariableManagerWebGPU::SetInlineConstants (staging write)

**Files**
- `Graphics/GraphicsEngineWebGPU/src/ShaderVariableManagerWebGPU.cpp`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/ShaderVariableManagerD3D11.cpp` (SetConstants / SetInlineConstants writes staging)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp` (SetInlineConstants writes staging)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/ShaderVariableManagerGL.cpp` (SetConstants / SetInlineConstants writes staging)

**Current state**
- `SetInlineConstants(...)` is currently `UNSUPPORTED("Not implemented yet")`.

**Behavior**
- DEV validation (match D3D11/GL):
  - Resource type must be `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER`
  - Must have `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`
  - `FirstConstant + NumConstants <= ResDesc.ArraySize`
- Only write into SRB/static cache staging; do not update GPU immediately.

---

### 9) Commit path: update emulation buffers before Draw/Dispatch

**Files**
- `Graphics/GraphicsEngineWebGPU/src/DeviceContextWebGPUImpl.cpp`
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/DeviceContextD3D11Impl.cpp` (BindShaderResources triggers inline-constant buffer updates)
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp` (UpdateInlineConstantBuffers)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp` (commit inline constants before binding descriptor sets)
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (CommitInlineConstants)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp` (commit path and flags)
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp` (UpdateInlineConstantBuffers)

**Goal**
- Commit inline constants before binding bind groups:
  - Graphics: in the `PrepareForDraw()` path, use `m_BindInfo.GetInlineConstantSRBCommitMask(Flags & DRAW_FLAG_INLINE_CONSTANTS_INTACT)` to select SRBs to update.
  - Compute: commit in `PrepareForDispatchCompute()` (no flags; default to processing inline constants every dispatch).
- Add `PipelineResourceSignatureWebGPUImpl::CommitInlineConstants(...)`:
  - Iterate `m_InlineConstantBuffers[]`
  - Read data from cache staging
  - Map the emulation buffer with `MAP_WRITE | MAP_FLAG_DISCARD`, write the first `NumConstants*4` bytes (respect `BufferSize` / alignment policy), and rely on dynamic offsets when binding

---

### 10) Static inline constants: copy signature static staging into SRB staging

**Files**
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`
- (if needed) `Graphics/GraphicsEngineWebGPU/src/ShaderResourceBindingWebGPUImpl.cpp`

**Reference (D3D11)**
- `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp` (copy static inline constants into SRB cache on creation)

**Reference (Vulkan)**
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (CopyStaticResources / SRB initialization behavior)

**Reference (OpenGL)**
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourceCacheGL.cpp` (static-to-SRB copy for inline constants)

**Goal**
Implement the same "static initialization copy" semantics as D3D11/GL:
- Copy signature static staging into SRB staging when SRB is created.

---

### 11) Tests & Acceptance Criteria

**Relevant tests**
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp`

**Acceptance Criteria**
- `SetInlineConstants()` works in WebGPU and matches D3D11/GL semantics (staging writes, commit updates, partial updates).
- The implicit signature path (`ShaderVariableFlag INLINE_CONSTANTS`) works:
  - If the constant count can't be derived (UBO reflection disabled), the error message is actionable.
- `DRAW_FLAG_INLINE_CONSTANTS_INTACT` works: when unchanged, updates can be skipped and rendering/compute results remain correct.

---

## Files to Touch (expected)
- `Graphics/GraphicsEngineWebGPU/include/PipelineResourceSignatureWebGPUImpl.hpp`
- `Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp`
- `Graphics/GraphicsEngineWebGPU/include/ShaderResourceCacheWebGPU.hpp`
- `Graphics/GraphicsEngineWebGPU/src/ShaderResourceCacheWebGPU.cpp`
- `Graphics/GraphicsEngineWebGPU/src/ShaderVariableManagerWebGPU.cpp`
- `Graphics/GraphicsEngineWebGPU/src/DeviceContextWebGPUImpl.cpp`
- `Graphics/GraphicsEngineWebGPU/src/PipelineStateWebGPUImpl.cpp`
- `Tests/DiligentCoreAPITest/src/InlineConstantsTest.cpp` (if WebGPU-specific tweaks or cases are needed)
