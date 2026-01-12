# Vulkan Inline Constants: CPU -> Staging -> GPU Submission Flow

This document explains how inline constants (`PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`) are actually submitted on the Vulkan backend, end-to-end:

1. CPU writes via `SetInlineConstants()` (staging only)
2. Vulkan backend commits staged data before draw/dispatch
3. Data reaches GPU either via `vkCmdPushConstants` (one selected resource) or via a dynamic UBO emulation path

This is intentionally implementation-focused. For the API usage perspective, see `doc/SetInlineConstants.md`.

---

## 0. Key constraints to keep in mind

### 0.1 `ArraySize` means "number of 32-bit values"

For inline constants, `PipelineResourceDesc::ArraySize` is the number of 4-byte values.

Also note:
- `PipelineResourceDesc::GetArraySize()` returns `1` for inline constants (descriptor array size is always 1), but the payload size is still `ArraySize * 4`.

Interface reference:
- `Graphics/GraphicsEngine/interface/PipelineResourceSignature.h` (`PipelineResourceDesc::ArraySize`, `GetArraySize()`)

### 0.2 Vulkan pipeline layouts allow only one push constant range

Vulkan permits only a single push-constant range per pipeline layout. Diligent therefore promotes only **one** inline-constant resource to the push-constant path; all other inline constants use a buffer emulation path.

---

## 1. High-level architecture (what lives where)

Vulkan inline constants are implemented with a two-level cache + commit model:

1) **Per-SRB CPU staging**
- Each SRB has staging storage for every inline-constant resource.
- `SetInlineConstants()` only writes into this staging memory.

2) **Signature-owned GPU-side resources for the emulation path**
- For every inline-constant resource, the signature creates a shared `USAGE_DYNAMIC` uniform buffer.
- All SRBs created from that signature share the same Vulkan buffer object(s).

At draw/dispatch time, staged values are committed:
- The selected resource is submitted via `vkCmdPushConstants`.
- All other inline constants are copied into the shared dynamic UBO(s) via `Map(DISCARD) -> memcpy -> Unmap`, and then consumed via dynamic offsets when binding descriptor sets.

---

## 2. Selecting which resource uses `vkCmdPushConstants`

Selection happens when building the pipeline layout:

- The backend scans pipeline resource signatures in **binding index order** (as arranged by `PipelineStateBase::CopyResourceSignatures`).
- It picks the **first** resource that has `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`.
- The push constant range is created as:
  - `size = ArraySize * sizeof(Uint32)`
  - `offset = 0`
  - `stageFlags = ShaderStages of that resource`

Implementation:
- `Graphics/GraphicsEngineVulkan/src/PipelineLayoutVk.cpp`
  - `PipelineLayoutVk::GetPushConstantInfo(...)`
  - `PipelineLayoutVk::Create(...)` validates against `VkPhysicalDeviceLimits::maxPushConstantsSize`

Important detail:
- Even if a resource is promoted to push constants, the descriptor for that resource still exists in the descriptor set layout, but the pipeline will not use it.

---

## 3. CPU write path: `SetInlineConstants()` only updates staging memory

Call chain (Vulkan):

1. User calls `IShaderResourceVariable::SetInlineConstants(...)`
2. `ShaderVariableManagerVk::SetInlineConstants(...)`
   - `Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp`
3. `ShaderResourceCacheVk::SetInlineConstants(...)`
   - `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp`

Behavior:
- The implementation copies the provided 32-bit values into per-SRB staging storage (typically via `memcpy` into `pInlineConstantData`).
- No Vulkan commands are recorded here.
- No Vulkan buffers are mapped here.
- Descriptor sets are not touched here.

This separation is crucial: the backend batches actual GPU submission to the moment it knows which SRBs are bound for a draw/dispatch.

---

## 4. Commit timeline: why Vulkan commits inline constants before binding descriptor sets

Inline constants are committed during draw/dispatch preparation.

Key ordering constraint in Vulkan:
- The emulation path uses **dynamic uniform buffers**.
- Updating a `USAGE_DYNAMIC` buffer (discard-map) changes the dynamic allocation backing that buffer.
- The correct **dynamic offsets** must be bound together with descriptor sets *after* the buffer update.

Implementation:
- `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp`
  - `DeviceContextVkImpl::PrepareForDraw(...)` contains the ordering comment and calls
    - `CommitInlineConstants(...)` first
    - `CommitDescriptorSets(...)` second

This is the end-to-end timeline per draw:

```
CPU:
  SetInlineConstants(...)               // staging only
GPU submission (recorded to command buffer):
  PrepareForDraw():
    CommitInlineConstants(...)          // push constants or Map/Unmap dynamic UBOs
    CommitDescriptorSets(...)           // bind sets + dynamic offsets referencing the committed UBO data
  vkCmdDraw / vkCmdDispatch
```

---

## 5. Commit stage (per signature): push constants vs dynamic UBO emulation

### 5.1 Signature-level dispatch from the device context

`DeviceContextVkImpl::CommitInlineConstants(...)`:
- Retrieves `PipelineLayoutVk::PushConstantInfo` (selected signature index + resource index + `VkPushConstantRange`).
- Iterates signatures that need committing.
- For the signature that owns the selected push-constant resource:
  - passes `PushConstantResIndex = PCInfo.ResourceIndex`
- For all other signatures:
  - passes `PushConstantResIndex = ~0u` (meaning: do not use push constants in this signature)

Implementation:
- `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp`
  - `DeviceContextVkImpl::CommitInlineConstants(...)`

### 5.2 Actual commit loop inside the signature

`PipelineResourceSignatureVkImpl::CommitInlineConstants(...)` iterates all inline-constant resources in the signature and does:

1) Read staged data:
- Locates the per-SRB staging pointer for the current inline-constant resource (staging was previously written by `SetInlineConstants()`).
- `DataSize = NumConstants * sizeof(Uint32)`.

2) If this resource is the selected push-constant resource:
- Records a push-constant update:
  - `vkCmdPushConstants(vkPipelineLayout, stageFlags, 0, DataSize, pStagedData)`

3) Otherwise (emulation path):
- Updates the signature-owned dynamic uniform buffer:
  - `Map(MAP_WRITE | MAP_FLAG_DISCARD)`
  - `memcpy(mapped, pStagedData, DataSize)`
  - `Unmap()`

Implementation:
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp`
  - `PipelineResourceSignatureVkImpl::CommitInlineConstants(...)`

Result:
- For push constants, the data lives in the command buffer stream.
- For emulated inline constants, the data lives in a dynamic UBO allocation referenced via the dynamic offset(s) later bound in `CommitDescriptorSets(...)`.

---

## 6. What `DRAW_FLAG_INLINE_CONSTANTS_INTACT` does on Vulkan

When you pass `DRAW_FLAG_INLINE_CONSTANTS_INTACT`, the device context may skip `CommitInlineConstants(...)` for SRBs where inline constants are known to be unchanged since the last commit. This avoids:
- redundant `vkCmdPushConstants` recording, and/or
- redundant discard-map updates of dynamic UBOs

The exact mask logic is in:
- `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp`
  - `BindInfo.GetInlineConstantSRBCommitMask(...)`

---

## 7. Practical guidance: influencing which block becomes push constants

Since selection is "first inline-constant resource across signatures and resources":
- If you have multiple inline-constant resources, put the one you want as push constants earliest in:
  1) signature binding order, then
  2) resource order within that signature

Also ensure:
- `ArraySize * 4 <= maxPushConstantsSize` on your target device(s).
