# OpenGL Inline Constants: CPU -> Staging -> GPU Submission Flow

On the OpenGL backend, inline constants are implemented by **emulating** them with internally-managed uniform buffers (UBOs).

Unlike Vulkan/D3D12, OpenGL has no push-constants mechanism, so all inline constants follow a single path:
1. CPU writes via `SetInlineConstants()` into per-SRB staging memory
2. Before draw/dispatch, staged data is uploaded into a shared dynamic UBO via `Map(DISCARD) -> memcpy -> Unmap`
3. The UBO is bound to the program's uniform block binding point, so the draw/dispatch sees the updated constants

For the API usage perspective, see `doc/SetInlineConstants.md`.

---

## 0. Key semantics and constraints

### 0.1 `ArraySize` means "number of 32-bit values"

For inline constants:
- `PipelineResourceDesc::ArraySize` is the number of 4-byte (32-bit) values.
- `PipelineResourceDesc::GetArraySize()` returns `1` (the resource occupies one UBO slot), but the payload size is still `ArraySize * sizeof(Uint32)`.

Interface reference:
- `Graphics/GraphicsEngine/interface/PipelineResourceSignature.h`

### 0.2 Inline constants are constant buffers with a special flag

Rules:
- Only `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER` may use `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`.
- `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` cannot be combined with other flags.
- Size must satisfy `ArraySize <= MAX_INLINE_CONSTANTS` (currently 64, i.e. 256 bytes).

---

## 1. What lives where (architecture)

OpenGL inline constants use the same "staging + commit" model as D3D11:

1) **Per-SRB CPU staging**
- Each SRB stores staging storage for every inline-constant resource.
- `SetInlineConstants()` only writes into this staging memory.

2) **Signature-owned GPU-side UBOs**
- For each inline-constant resource, the signature creates an internal UBO (`BufferGLImpl`).
- All SRBs created from the same signature share the same UBO object to reduce memory usage.

At draw/dispatch time, the backend uploads the currently bound SRB's staging data into the shared UBO and then uses the already-established UBO binding.

---

## 2. Resource creation: internal UBO per inline-constant resource

During signature layout creation, each resource flagged with `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`:
- occupies exactly one uniform-buffer cache slot (`CacheOffset`)
- gets an internal buffer created by `CreateInlineConstantBuffer(ResDesc.Name, ResDesc.ArraySize)`
- contributes `ResDesc.ArraySize` to `m_TotalInlineConstants` (total staging storage size)

Implementation:
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
  - `PipelineResourceSignatureGLImpl::CreateLayout(...)`
- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`
  - `InlineConstantBufferAttribsGL`
- `Graphics/GraphicsEngine/include/PipelineResourceSignatureBase.hpp`
  - `CreateInlineConstantBuffer(...)`

Note: OpenGL has no push constants, so every inline-constant resource is a UBO-backed emulation.

---

## 3. SRB initialization: staging memory allocation and association

When initializing an SRB cache, the signature:
- allocates resource-cache memory sized to include an "inline constant data" tail (`m_TotalInlineConstants`)
- binds each inline-constant resource to:
  - the signature-owned shared UBO (`CachedUB::pBuffer`)
  - a per-SRB staging pointer (`CachedUB::pInlineConstantData`) pointing into the cache tail

Implementation:
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
  - `PipelineResourceSignatureGLImpl::InitSRBResourceCache(...)`
- `Graphics/GraphicsEngineOpenGL/src/ShaderResourceCacheGL.cpp`
  - `ShaderResourceCacheGL::InitInlineConstantBuffer(...)` sets:
    - `RangeSize = NumConstants * sizeof(Uint32)`
    - `pInlineConstantData` to the cache tail storage

---

## 4. CPU write path: `SetInlineConstants()` updates staging only

Call chain (OpenGL):
1. User calls `IShaderResourceVariable::SetInlineConstants(...)`
2. `ShaderVariableManagerGL::UniformBuffBindInfo::SetConstants(...)`
   - `Graphics/GraphicsEngineOpenGL/src/ShaderVariableManagerGL.cpp`
3. `ShaderResourceCacheGL::SetInlineConstants(...)`
   - `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp`
4. `ShaderResourceCacheGL::CachedUB::SetInlineConstants(...)`
   - copies into `pInlineConstantData`

Important behavior:
- No OpenGL buffer is mapped here.
- No GL binding calls happen here.
- `SetInlineConstants()` intentionally does not update SRB revision; inline constants are allowed to change after SRB commit without re-committing.

Reference note in code:
- `Graphics/GraphicsEngineOpenGL/include/ShaderResourceCacheGL.hpp` (`NOTE: Do NOT call UpdateRevision() here.`)

---

## 5. Commit stage: staging -> UBO upload before draw/dispatch

Inline constants are uploaded during program resource binding for draw/dispatch.

### 5.1 Device context decision: when uploads happen

`DeviceContextGLImpl::PrepareForDraw(...)` computes:
- `DynamicBuffersIntact` from `DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT`
- `InlineConstantsIntact` from `DRAW_FLAG_INLINE_CONSTANTS_INTACT`

If resources need committing, it calls:
- `BindProgramResources(BindSRBMask, DynamicBuffersIntact, InlineConstantsIntact)`

Implementation:
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`
  - `DeviceContextGLImpl::PrepareForDraw(...)`
  - `DeviceContextGLImpl::BindProgramResources(...)`

### 5.2 Actual upload: map/discard the shared UBO and memcpy staged data

Inside `BindProgramResources(...)`, for each signature that has inline constants:
- if the SRB is stale, or `InlineConstantsIntact == false`,
  it calls `PipelineResourceSignatureGLImpl::UpdateInlineConstantBuffers(...)`.

`UpdateInlineConstantBuffers(...)` loops all inline-constant resources and does:
1) Read staged data from `CachedUB::pInlineConstantData`
2) Map the UBO (discard) and copy:
   - `Map(MAP_WRITE, MAP_FLAG_DISCARD)`
   - `memcpy(mapped, staged, NumConstants * sizeof(Uint32))`
   - `Unmap()`

Implementation:
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
  - `PipelineResourceSignatureGLImpl::UpdateInlineConstantBuffers(...)`

Binding note:
- UBO binding is handled by the SRB cache binding code (`ShaderResourceCacheGL::BindResources(...)`), which binds UBOs by binding index and range. Inline constants use `BaseOffset = 0`, `DynamicOffset = 0`, `RangeSize = NumConstants * 4`.

---

## 6. STATIC inline constants: signature cache -> SRB cache copy

To support `STATIC` variables:
- the signature creates a "signature cache" (`m_pStaticResCache`) with staging storage for static inline constants
- when an SRB is created with static initialization, `CopyStaticResources(...)` copies the staging bytes from the signature cache into the SRB cache

Implementation:
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
  - `PipelineResourceSignatureGLImpl::CopyStaticResources(...)` calls
    - `ShaderResourceCacheGL::CopyInlineConstants(...)`

---

## 7. What `DRAW_FLAG_INLINE_CONSTANTS_INTACT` does on OpenGL

If you know that inline constants used by the draw/dispatch have not changed since the previous submission, pass `DRAW_FLAG_INLINE_CONSTANTS_INTACT` to skip the UBO upload work (the map/discard + memcpy).

Decision point:
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`
  - `DeviceContextGLImpl::BindProgramResources(...)` updates inline constant buffers only when `SRBStale || !InlineConstantsIntact`.
