# D3D11 Inline Constants: CPU -> Staging -> GPU Submission Flow

On the D3D11 backend, inline constants are implemented by **emulating** them with `USAGE_DYNAMIC` constant buffers (CBs). This document describes the complete submission pipeline:

1. CPU writes via `SetInlineConstants()` into per-SRB staging memory
2. At draw/dispatch time, staging data is uploaded into a shared dynamic CB via `Map(WRITE_DISCARD) -> memcpy -> Unmap`
3. The dynamic CB is bound to the correct shader stages/slots, so the draw/dispatch sees the updated constants

For the API usage perspective, see `doc/SetInlineConstants.md`.

---

## 0. Key semantics and constraints

### 0.1 `ArraySize` means "number of 32-bit values"

For inline constants, `PipelineResourceDesc::ArraySize` is the number of 4-byte values, not bytes.

Interface reference:
- `Graphics/GraphicsEngine/interface/PipelineResourceSignature.h` (`PipelineResourceDesc::ArraySize`, `GetArraySize()`)

### 0.2 Inline constants are constant buffers with a special flag

Rules:
- Only `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER` may use `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`.
- `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` cannot be combined with other flags.
- Size must satisfy `ArraySize <= MAX_INLINE_CONSTANTS` (currently 64).

---

## 1. What lives where (architecture)

D3D11 uses the following model:

1) **Per-SRB CPU staging**
- Each SRB stores a staging array for each inline-constant resource.
- `SetInlineConstants()` writes into this staging array only.

2) **Signature-owned GPU-side dynamic constant buffers**
- For each inline-constant resource, the signature creates an internal `USAGE_DYNAMIC` constant buffer.
- All SRBs created from the same signature share this same buffer object.

At draw/dispatch time, the backend uploads the currently bound SRB’s staging data into the shared dynamic buffer and then binds it.

---

## 2. Resource creation: internal dynamic CB per inline-constant resource

The buffer is created during pipeline resource signature layout creation.

Implementation:
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp`
  - `PipelineResourceSignatureD3D11Impl::CreateLayout(...)` (creates inline-constant buffers via `CreateInlineConstantBuffer(...)`)
- `Graphics/GraphicsEngine/include/PipelineResourceSignatureBase.hpp`
  - `CreateInlineConstantBuffer(...)` creates a `USAGE_DYNAMIC + CPU_ACCESS_WRITE` uniform buffer of size `NumConstants * sizeof(Uint32)`

Important detail:
- The buffer object is **shared** by all SRBs created from the same signature.

---

## 3. SRB initialization: staging memory allocation and association

When an SRB resource cache is initialized, the cache allocates storage for all resources, including inline constants.

Inline-constant initialization binds:
- the signature-owned dynamic CB object, and
- a per-SRB staging pointer (`pInlineConstantData`)

Implementation:
- `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp`
  - `ShaderResourceCacheD3D11::Initialize(...)` allocates memory for inline constants and calls `InitInlineConstantBuffer(...)`
  - `ShaderResourceCacheD3D11::InitInlineConstantBuffer(...)` assigns:
    - `CachedCB::pBuff` (the shared `BufferD3D11Impl`)
    - `CachedCB::pInlineConstantData` (the SRB’s staging pointer)
    - and uses the same staging pointer for all active shader stages of that resource

---

## 4. CPU write path: `SetInlineConstants()` updates staging only

Call chain (D3D11):

1. User calls `IShaderResourceVariable::SetInlineConstants(...)`
2. `ShaderVariableManagerD3D11` forwards to resource cache:
   - `Graphics/GraphicsEngineD3D11/src/ShaderVariableManagerD3D11.cpp`
3. `ShaderResourceCacheD3D11::SetInlineConstants(...)` locates the correct cached CB and calls:
   - `Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp`
     - `CachedCB::SetInlineConstants(...)`

Behavior:
- The implementation copies the provided 32-bit values into `CachedCB::pInlineConstantData` at offset `FirstConstant * sizeof(Uint32)`.
- No D3D11 `Map/Unmap` happens here.
- No D3D11 binding calls happen here.

---

## 5. Commit stage: uploading staging -> GPU buffer via `Map(WRITE_DISCARD)`

Inline constants are uploaded during resource binding for draw/dispatch.

The device context decides whether inline constants need updating:
- If the SRB is stale (first use or resources changed), the update happens.
- Otherwise, if you did not pass `DRAW_FLAG_INLINE_CONSTANTS_INTACT`, the update happens.

Implementation (decision point):
- `Graphics/GraphicsEngineD3D11/src/DeviceContextD3D11Impl.cpp`
  - When `(InlineConstantsSRBMask & SignBit) != 0` and `(SRBStale || !InlineConstantsIntact)`, it calls:
    - `PipelineResourceSignatureD3D11Impl::UpdateInlineConstantBuffers(...)`

Implementation (actual upload):
- `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp`
  - `PipelineResourceSignatureD3D11Impl::UpdateInlineConstantBuffers(...)` does:
    1) Fetch the D3D11 buffer and staging pointer from the SRB cache
    2) `Map(D3D11_MAP_WRITE_DISCARD)`
    3) `memcpy(mapped, pInlineConstantData, NumConstants * 4)`
    4) `Unmap`

At this point the shared dynamic CB contains the correct values for the currently bound SRB, and subsequent draw/dispatch will observe them.

---

## 6. What `DRAW_FLAG_INLINE_CONSTANTS_INTACT` does on D3D11

If you know that inline constants used by the draw/dispatch have not changed since the previous submission, pass `DRAW_FLAG_INLINE_CONSTANTS_INTACT` to skip the `Map/Unmap` upload.

This is especially valuable when:
- the same SRB is rebound many times with unchanged inline constants, or
- you want to avoid redundant discard-map updates.

---

## 7. Implications of "signature-owned shared buffer"

Because the dynamic constant buffer object is shared by all SRBs created from the same signature:
- Correctness is ensured by updating the buffer right before the draw/dispatch that uses an SRB.
- If you rapidly switch SRBs (each with different inline constants), the backend will likely upload on each switch (unless you can use the intact flag).

If you need to minimize updates:
- Batch draw calls by SRB when possible, and/or
- use `DRAW_FLAG_INLINE_CONSTANTS_INTACT` when values are unchanged.
