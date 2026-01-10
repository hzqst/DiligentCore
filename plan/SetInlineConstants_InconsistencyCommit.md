# Inline Constants Cross-Signature Commit Inconsistency

## Problem Summary

When an SRB (Shader Resource Binding) created from one signature is used with a different but compatible signature, there's a potential inconsistency in how inline constant data is committed to GPU buffers.

## Affected Backends

- **Vulkan** (existing issue)
- **OpenGL** (fixed in this changeset)

## Root Cause Analysis

### The Cross-Signature SRB Scenario

1. `pRefPSO` is created with signature A, and `pRefSRB` is created from signature A
2. `pPSO` is created (e.g., via serialization/deserialization) with signature B (compatible but different instance)
3. `pPSO` + `pRefSRB` is used for drawing

### How Each Backend Handles This

#### D3D11 (Correct Design)

```cpp
// UpdateInlineConstantBuffers - D3D11
ID3D11Buffer* pd3d11CB = nullptr;
const CachedCB& InlineCB = ResourceCache.GetResource<D3D11_RESOURCE_RANGE_CBV>(
    InlineCBAttr.BindPoints, &pd3d11CB);  // Get buffer from SRB cache
pd3d11Ctx->Map(pd3d11CB, ...);  // Update the buffer from cache
```

D3D11 retrieves the buffer pointer **from the SRB cache** and updates it. Since `BindResources` also binds the buffer from the SRB cache, the **update and binding use the same buffer**. This design is consistent.

#### Vulkan (Has This Issue)

```cpp
// CommitInlineConstants - Vulkan
const void* pInlineConstantData = ResourceCache.GetInlineConstantData(
    InlineCBAttr.DescrSet, InlineCBAttr.SRBCacheOffset);  // Staging from SRB cache
Attribs.Ctx.MapBuffer(InlineCBAttr.pBuffer, ...);  // Update CURRENT signature's buffer!
```

Vulkan retrieves staging data from the SRB cache, but updates the **current signature's** `InlineCBAttr.pBuffer`. However, the descriptor set was written during SRB initialization with the **original signature's** buffer. This means:

- **Descriptor set binding**: Original signature's buffer (from SRB creation)
- **Data update**: Current signature's buffer (from PSO's signature)
- **Result**: Data is written to wrong buffer when cross-signature SRB is used

#### OpenGL (Fixed)

**Before fix:**
```cpp
// UpdateInlineConstantBuffers - OpenGL (BEFORE)
InlineCBAttr.pBuffer->Map(...);  // Update CURRENT signature's buffer
// Then re-bind the current signature's buffer (workaround)
CtxState.BindUniformBuffer(..., InlineCBAttr.pBuffer->GetGLHandle(), ...);
```

The previous implementation updated the current signature's buffer and had to re-bind it to override what `BindResources()` had bound. This was a workaround for the design inconsistency.

**After fix:**
```cpp
// UpdateInlineConstantBuffers - OpenGL (AFTER)
BufferGLImpl* pBuffer = InlineCB.pBuffer;  // Get buffer from SRB cache
pBuffer->Map(...);  // Update the buffer from cache
// No re-binding needed - same buffer was bound by BindResources()
```

Now OpenGL follows D3D11's design: get the buffer from SRB cache and update it. No re-binding is needed because `BindResources()` already bound this same buffer.

## Impact

### When the Issue Manifests (Vulkan)

The issue only manifests when:
1. An SRB is created from signature A
2. The same SRB is used with a compatible but different signature B
3. Inline constants are modified after `CommitShaderResources()` but before `Draw()`

Common scenario: `RenderStateCache` test where PSOs are serialized/deserialized, creating new signature instances.

### Symptoms

- Inline constant values appear as zeros or stale data
- Shader reads from old buffer while new data is written to different buffer
- Intermittent rendering artifacts when using cached/serialized PSOs with shared SRBs

## Recommended Fix for Vulkan

Modify `PipelineResourceSignatureVkImpl::CommitInlineConstants()` to update the buffer stored in the SRB cache instead of the signature's `InlineCBAttr.pBuffer`:

```cpp
// Get buffer from SRB cache
const ShaderResourceCacheVk::Resource& CachedRes = 
    ResourceCache.GetDescriptorSet(InlineCBAttr.DescrSet)
                 .GetResource(InlineCBAttr.SRBCacheOffset);
BufferVkImpl* pBuffer = CachedRes.pObject.RawPtr<BufferVkImpl>();

// Update the buffer from cache
Attribs.Ctx.MapBuffer(pBuffer, MAP_WRITE, MAP_FLAG_DISCARD, pMappedData);
memcpy(pMappedData, pInlineConstantData, DataSize);
Attribs.Ctx.UnmapBuffer(pBuffer, MAP_WRITE);
```

This ensures the same buffer that was written to the descriptor set during SRB initialization is updated.

## Design Principle

**The buffer that is bound should be the same buffer that is updated.**

- `BindResources()` / descriptor set binding uses buffers from SRB cache
- `UpdateInlineConstantBuffers()` / `CommitInlineConstants()` should update buffers from SRB cache
- The signature's `InlineCBAttr.pBuffer` is only used during SRB initialization to populate the cache

## Files Changed (OpenGL Fix)

- `Graphics/GraphicsEngineOpenGL/include/PipelineResourceSignatureGLImpl.hpp`
  - Removed `BaseBindings` parameter from `UpdateInlineConstantBuffers()`
- `Graphics/GraphicsEngineOpenGL/src/PipelineResourceSignatureGLImpl.cpp`
  - Modified `UpdateInlineConstantBuffers()` to get buffer from SRB cache
  - Removed re-binding code
- `Graphics/GraphicsEngineOpenGL/src/DeviceContextGLImpl.cpp`
  - Updated call site to match new signature

## Test Coverage

The `RenderStateCache` test in `InlineConstantsTest.cpp` exercises this scenario by:
1. Creating a reference PSO and SRB
2. Serializing and deserializing the PSO (creates new signature instances)
3. Using the deserialized PSO with the original SRB
4. Verifying inline constants work correctly
